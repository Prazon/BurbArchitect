// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildDoorTool.h"
#include "Actors/LotManager.h"
#include "Actors/PortalBase.h"
#include "Actors/DoorBase.h"
#include "Components/PortalBoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WallComponent.h"
#include "Subsystems/BuildServer.h"
#include "Engine/World.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"

ABuildDoorTool::ABuildDoorTool()
{
	PrimaryActorTick.bCanEverTick = true;

	PreviewDoor = nullptr;

	// Doors snap to the floor
	bSnapsToFloor = true;
}

void ABuildDoorTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate preview door so all clients can see it
	DOREPLIFETIME(ABuildDoorTool, PreviewDoor);
}

void ABuildDoorTool::BeginPlay()
{
	Super::BeginPlay();

	// Eagerly load the class to ensure it's available (fixes Blueprint compilation issues)
	if (ClassToSpawn.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: ClassToSpawn is not set! Please configure a portal class in the blueprint or data table."));
	}
	else
	{
		// Load the class synchronously if not already loaded
		ClassToPlace = ClassToSpawn.LoadSynchronous();

		if (!ClassToPlace)
		{
			UE_LOG(LogTemp, Error, TEXT("BuildDoorTool: Failed to load ClassToSpawn! Make sure the Blueprint class is compiled."));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Successfully loaded door class: %s"), *ClassToPlace->GetName());
		}
	}
}

void ABuildDoorTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up preview door when tool ends
	DestroyPreviewDoor();
	Super::EndPlay(EndPlayReason);
}

void ABuildDoorTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	// SERVER AUTHORITATIVE: Only server calculates snapping, clients use replicated values
	// This prevents desyncs in listen server where each machine would calculate differently
	if (HasAuthority())
	{
		// Call parent to handle wall detection and location snapping
		Super::Move_Implementation(MoveLocation, SelectPressed, CursorWorldHitResult, TracedLevel);

		// Additional validation: Check if portal extends beyond wall bounds (WallGraph-based)
		// Only check if parent validation passed and we have a valid wall
		if (bValidPlacementLocation && HitMeshSection >= 0 && CurrentLot && CurrentLot->WallComponent && CurrentLot->WallGraph)
		{
			const FWallSegmentData& HitWall = CurrentLot->WallComponent->WallDataArray[HitMeshSection];
			int32 EdgeID = HitWall.WallEdgeID;

			// Check bounds if we have a preview door with a box component
			if (PreviewDoor && PreviewDoor->Box && EdgeID != -1)
			{
				FVector BoxExtent = PreviewDoor->Box->GetScaledBoxExtent();
				FQuat RotationQuat = TargetRotation.Quaternion();

				if (!CurrentLot->WallGraph->IsPortalWithinWallBounds(EdgeID, TargetLocation, BoxExtent, RotationQuat))
				{
					bValidPlacementLocation = false;
				}

				// Precise vertical bounds clamping using actual portal box extent
				// Note: Doors snap to floor (bSnapsToFloor=true), so Z is fixed at HitWall.StartLoc.Z
				// We only need to validate the door doesn't extend above the wall top
				float WallBaseZ = HitWall.StartLoc.Z;
				float WallHeight = HitWall.Height;
				float WallTopZ = WallBaseZ + WallHeight;
				float PortalHalfHeight = BoxExtent.Z; // Z extent is the portal's half-height

				// For doors, only clamp the top bound (bottom is handled by bSnapsToFloor)
				// Ensure door doesn't extend above wall
				float MaxZ = WallTopZ - PortalHalfHeight;
				if (TargetLocation.Z > MaxZ)
				{
					TargetLocation.Z = MaxZ;
				}

				// Update tool actor location with clamped position
				SetActorLocation(TargetLocation);
			}
		}
	}
	else
	{
		// CLIENT: Use server's replicated TargetLocation/TargetRotation without recalculating
		// Just update our actor location to match the replicated values
		SetActorLocation(TargetLocation);
		SetActorRotation(TargetRotation);
	}

	// Both server and client update preview (server spawns it, client receives via replication)
	UpdatePreviewDoor();
}

void ABuildDoorTool::Click_Implementation()
{
	Super::Click_Implementation();

	// Only place door if preview door is valid (meaning we're on a valid wall)
	if (!PreviewDoor)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: Cannot place door - no preview door"));
		return;
	}

	if (!bValidPlacementLocation)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: Invalid placement location"));
		return;
	}

	if (!CurrentLot || !CurrentLot->WallComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: Cannot place door - no lot or wall component"));
		return;
	}

	if (!ClassToPlace)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: ClassToPlace is null"));
		return;
	}

	// Get the BuildServer subsystem for proper undo/redo support
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildDoorTool: World is null"));
		return;
	}

	UBuildServer* BuildServer = World->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildDoorTool: BuildServer subsystem not found"));
		return;
	}

	// Calculate the affected wall sections by checking portal bounds against all wall sections
	TArray<int32> AffectedWallSections;

	// Get the box extents from the preview door to determine which wall sections to check
	if (PreviewDoor && PreviewDoor->Box)
	{
		FVector BoxExtent = PreviewDoor->Box->GetScaledBoxExtent();
		FQuat RotationQuat = UKismetMathLibrary::Conv_RotatorToQuaternion(TargetRotation);

		// Query which wall sections the door intersects
		AffectedWallSections = CurrentLot->WallComponent->GetMultiSectionIDFromHitResult(TargetLocation, BoxExtent, RotationQuat);

		UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Door affects %d wall section(s)"), AffectedWallSections.Num());
	}

	// Fallback: if we couldn't determine affected sections, use the hit section
	if (AffectedWallSections.Num() == 0)
	{
		AffectedWallSections.Add(HitMeshSection);
		UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: Using fallback - single hit section %d"), HitMeshSection);
	}

	// Use the BuildServer to create the portal via command pattern (provides undo/redo)
	BuildServer->BuildPortal(
		ClassToPlace,
		TargetLocation,
		TargetRotation,
		AffectedWallSections,
		PortalSize,
		PortalOffset,
		nullptr,              // WindowMesh (not used for doors)
		DoorStaticMesh,       // Door panel mesh
		DoorFrameMesh         // Door frame mesh
	);

	UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Placed door affecting %d wall section(s)"), AffectedWallSections.Num());
}

void ABuildDoorTool::BroadcastRelease_Implementation()
{
	Super::BroadcastRelease_Implementation();

	// Clean up preview door after successful placement
	DestroyPreviewDoor();
}

void ABuildDoorTool::Delete_Implementation()
{
	Super::Delete_Implementation();

	// When in deletion mode, if we're hovering over a valid wall location with a portal
	if (bValidPlacementLocation && HitMeshSection >= 0 && CurrentLot && CurrentLot->WallComponent)
	{
		// Find the portal at this wall section
		UWallComponent* WallComp = CurrentLot->WallComponent;

		// Check if there's a portal in the wall data at this section
		if (WallComp->WallDataArray.IsValidIndex(HitMeshSection))
		{
			FWallSegmentData& WallData = WallComp->WallDataArray[HitMeshSection];

			// Look through portals attached to this wall
			for (int32 i = WallData.PortalArray.Num() - 1; i >= 0; i--)
			{
				APortalBase* Portal = WallData.PortalArray[i];
				if (Portal && IsValid(Portal))
				{
					// Check if portal is at roughly the same location as our target
					float Distance = FVector::Dist(Portal->GetActorLocation(), TargetLocation);
					if (Distance < 100.0f) // Within 1 meter
					{
						// Remove from array first
						WallData.PortalArray.RemoveAt(i);

						// Destroy the portal
						Portal->Destroy();

						// Regenerate the wall to remove the portal cutout
						WallComp->RegenerateWallSection(WallData, true);

						UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Deleted portal at wall section %d"), HitMeshSection);
						break;
					}
				}
			}
		}
	}
}

void ABuildDoorTool::Destroyed()
{
	// Clean up preview door when tool is destroyed
	DestroyPreviewDoor();
	Super::Destroyed();
}

void ABuildDoorTool::UpdatePreviewDoor()
{
	// If validity changed from invalid to valid, recreate preview to restore original materials
	// Only server should destroy/recreate - clients get the reference via replication
	if (HasAuthority() && !bPreviousValidPlacement && bValidPlacementLocation && PreviewDoor)
	{
		DestroyPreviewDoor();
	}

	// Update previous state for next frame
	bPreviousValidPlacement = bValidPlacementLocation;

	// If we don't have a preview door, create one (server only - clients get it via replication)
	if (!PreviewDoor && ClassToSpawn.IsValid() && HasAuthority())
	{
		// Load the class if not already loaded
		if (!ClassToPlace)
		{
			ClassToPlace = ClassToSpawn.LoadSynchronous();
		}

		if (ClassToPlace)
		{
			// Spawn the preview door on server - it will replicate to clients
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			PreviewDoor = GetWorld()->SpawnActor<APortalBase>(ClassToPlace, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

			if (PreviewDoor)
			{
				// Attach to this tool actor
				PreviewDoor->AttachToActor(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

				// Disable collision on preview so it doesn't interfere with placement detection
				PreviewDoor->SetActorEnableCollision(false);

				// Apply portal size from tool configuration
				if (PortalSize.X > 0.0f && PortalSize.Y > 0.0f)
				{
					PreviewDoor->PortalSize = PortalSize;

					// Update box component to match portal size
					if (PreviewDoor->Box)
					{
						PreviewDoor->Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
					}
				}

				// Apply portal offset to the box component (cutaway) for visual preview
				// Also store in portal actor for wall cutout rendering
				PreviewDoor->PortalOffset = PortalOffset;

				if (PreviewDoor->Box)
				{
					// Offset Box visually for preview (not used for cutout calculation)
					PreviewDoor->Box->SetRelativeLocation(FVector(PortalOffset.X, 0.0f, PortalOffset.Y));
					PreviewDoor->Box->UpdateComponentToWorld();
				}

				// Apply door meshes if this is a DoorBase actor
				// Use replicated properties so clients also see the meshes
				ADoorBase* DoorActor = Cast<ADoorBase>(PreviewDoor);
				if (DoorActor)
				{
					UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Successfully cast preview door to DoorBase"));

					// Apply door panel mesh via replicated property
					if (!DoorStaticMesh.IsNull())
					{
						// Set the replicated property - this will replicate to clients and trigger OnRep
						DoorActor->DoorMeshAsset = DoorStaticMesh;
						DoorActor->ApplyDoorMesh();  // Apply locally on server
						UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Set DoorMeshAsset for preview replication"));
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: DoorStaticMesh soft pointer is null (not set in data asset)"));
					}

					// Apply door frame mesh via replicated property
					if (!DoorFrameMesh.IsNull())
					{
						// Set the replicated property - this will replicate to clients and trigger OnRep
						DoorActor->DoorFrameMeshAsset = DoorFrameMesh;
						DoorActor->ApplyDoorFrameMesh();  // Apply locally on server
						UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Set DoorFrameMeshAsset for preview replication"));
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: DoorFrameMesh soft pointer is null (not set in data asset)"));
					}

				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("BuildDoorTool: Preview door is NOT a DoorBase actor (ClassToPlace might be wrong type). Class: %s"),
						PreviewDoor ? *PreviewDoor->GetClass()->GetName() : TEXT("NULL"));
				}

				// Force network update to ensure ALL replicated properties are sent to clients
				// This includes PortalSize, PortalOffset, and mesh assets
				PreviewDoor->ForceNetUpdate();

				UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Created preview door with size (%.1f, %.1f) and offset (%.1f, %.1f)"),
					PortalSize.X, PortalSize.Y, PortalOffset.X, PortalOffset.Y);
			}
		}
	}

	// Update preview door location and material
	if (PreviewDoor)
	{
		PreviewDoor->SetActorLocation(TargetLocation);
		PreviewDoor->SetActorRotation(TargetRotation);

		// Only apply invalid material when placement is not valid
		// When valid, leave the portal's default materials so we can see it properly
		if (!bValidPlacementLocation && InvalidMaterial)
		{
			// Get all mesh components from the preview door and apply invalid material
			TArray<UMeshComponent*> MeshComponents;
			PreviewDoor->GetComponents<UMeshComponent>(MeshComponents);

			for (UMeshComponent* MeshComp : MeshComponents)
			{
				if (MeshComp)
				{
					// Apply invalid material to all material slots
					const int32 NumMaterials = MeshComp->GetNumMaterials();
					for (int32 i = 0; i < NumMaterials; i++)
					{
						MeshComp->SetMaterial(i, InvalidMaterial);
					}
				}
			}
		}

		// Preview stays visible at all times - material provides visual feedback
		// No SetActorHiddenInGame() call - we want to see invalid material

		// Register preview with walls to show cutouts if valid placement
		if (bValidPlacementLocation)
		{
			RegisterPreviewWithWalls();
		}
		else
		{
			UnregisterPreviewFromWalls();
		}
	}
}

void ABuildDoorTool::DestroyPreviewDoor()
{
	// Unregister from walls on ALL machines - each machine needs to clean up its local PortalArray
	// and re-render walls to remove the cutout
	UnregisterPreviewFromWalls();

	// Only server destroys the preview actor - clients will have their reference cleared via replication
	if (HasAuthority() && PreviewDoor && IsValid(PreviewDoor))
	{
		PreviewDoor->Destroy();
		PreviewDoor = nullptr;
		UE_LOG(LogTemp, Log, TEXT("BuildDoorTool: Destroyed preview door"));
	}
}

void ABuildDoorTool::RegisterPreviewWithWalls()
{
	if (!PreviewDoor || !CurrentLot || !CurrentLot->WallComponent || !PreviewDoor->Box)
	{
		return;
	}

	// Calculate which wall sections this preview portal affects
	FVector BoxExtent = PreviewDoor->Box->GetScaledBoxExtent();
	FQuat RotationQuat = PreviewDoor->GetActorQuat();
	TArray<int32> AffectedWallSections = CurrentLot->WallComponent->GetMultiSectionIDFromHitResult(
		TargetLocation, BoxExtent, RotationQuat);

	// Check if the affected sections changed
	bool bSectionsChanged = (AffectedWallSections != PreviewRegisteredWallIndices);

	if (bSectionsChanged)
	{
		// Unregister from previous walls (this re-renders them without the portal, clearing old cutouts)
		UnregisterPreviewFromWalls();

		// Register with new walls
		for (int32 WallIndex : AffectedWallSections)
		{
			if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIndex))
			{
				CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Add(PreviewDoor);
			}
		}

		// Store which walls we registered with
		PreviewRegisteredWallIndices = AffectedWallSections;

		// Render the cutouts at the new position
		if (AffectedWallSections.Num() > 0)
		{
			CurrentLot->WallComponent->RenderPortalsForWalls(AffectedWallSections);
		}
	}
	else if (AffectedWallSections.Num() > 0)
	{
		// Same wall sections, but portal may have moved within them
		// Need to clear and re-render to update cutout position
		// First remove portal, render to clear, then re-add and render again
		for (int32 WallIndex : AffectedWallSections)
		{
			if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIndex))
			{
				CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Remove(PreviewDoor);
			}
		}

		// Render without portal to clear old cutout
		CurrentLot->WallComponent->RenderPortalsForWalls(AffectedWallSections);

		// Re-add portal and render again at new position
		for (int32 WallIndex : AffectedWallSections)
		{
			if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIndex))
			{
				CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Add(PreviewDoor);
			}
		}

		CurrentLot->WallComponent->RenderPortalsForWalls(AffectedWallSections);
	}
}

void ABuildDoorTool::UnregisterPreviewFromWalls()
{
	if (!PreviewDoor || !CurrentLot || !CurrentLot->WallComponent)
	{
		PreviewRegisteredWallIndices.Empty();
		return;
	}

	// Remove preview portal from all walls it was registered with
	for (int32 WallIndex : PreviewRegisteredWallIndices)
	{
		if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIndex))
		{
			CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Remove(PreviewDoor);
		}
	}

	// Re-render portals to remove preview cutouts
	// OPTIMIZED: Only re-render the specific walls we just modified (not all walls on the lot!)
	if (PreviewRegisteredWallIndices.Num() > 0)
	{
		CurrentLot->WallComponent->RenderPortalsForWalls(PreviewRegisteredWallIndices);
	}

	PreviewRegisteredWallIndices.Empty();
}

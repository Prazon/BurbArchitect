// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildWindowTool.h"
#include "Actors/LotManager.h"
#include "Actors/PortalBase.h"
#include "Actors/WindowBase.h"
#include "Components/PortalBoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WallComponent.h"
#include "Subsystems/BuildServer.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

ABuildWindowTool::ABuildWindowTool()
{
	PrimaryActorTick.bCanEverTick = true;

	PreviewWindow = nullptr;
}

void ABuildWindowTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate preview window so all clients can see it
	DOREPLIFETIME(ABuildWindowTool, PreviewWindow);
}

void ABuildWindowTool::BeginPlay()
{
	Super::BeginPlay();

	// Eagerly load the class to ensure it's available (fixes Blueprint compilation issues)
	if (ClassToSpawn.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildWindowTool: ClassToSpawn is not set! Please configure a portal class in the blueprint or data table."));
	}
	else
	{
		// Load the class synchronously if not already loaded
		ClassToPlace = ClassToSpawn.LoadSynchronous();

		if (!ClassToPlace)
		{
			UE_LOG(LogTemp, Error, TEXT("BuildWindowTool: Failed to load ClassToSpawn! Make sure the Blueprint class is compiled."));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("BuildWindowTool: Successfully loaded window class: %s"), *ClassToPlace->GetName());
		}
	}
}

void ABuildWindowTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyPreviewWindow();
	Super::EndPlay(EndPlayReason);
}

void ABuildWindowTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	// All machines calculate snapping using the same replicated parameters
	// This ensures consistent results across server and clients

	// Call parent to handle wall detection and location snapping
	Super::Move_Implementation(MoveLocation, SelectPressed, CursorWorldHitResult, TracedLevel);

	// Additional validation: Check if portal extends beyond wall bounds (WallGraph-based)
	// Only check if parent validation passed and we have a valid wall
	if (bValidPlacementLocation && HitMeshSection >= 0 && CurrentLot && CurrentLot->WallComponent && CurrentLot->WallGraph)
	{
		const FWallSegmentData& HitWall = CurrentLot->WallComponent->WallDataArray[HitMeshSection];
		int32 EdgeID = HitWall.WallEdgeID;

		// Use replicated PortalSize instead of preview actor's box
		// This ensures all machines use the same values even if preview hasn't replicated yet
		if (PortalSize.X > 0.0f && PortalSize.Y > 0.0f && EdgeID != -1)
		{
			// Calculate box extent from replicated PortalSize (same as how preview box is sized)
			FVector BoxExtent = FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f);
			FQuat RotationQuat = TargetRotation.Quaternion();

			if (!CurrentLot->WallGraph->IsPortalWithinWallBounds(EdgeID, TargetLocation, BoxExtent, RotationQuat))
			{
				bValidPlacementLocation = false;
			}

			// Precise vertical bounds clamping using portal size
			float WallBaseZ = HitWall.StartLoc.Z;
			float WallHeight = HitWall.Height;
			float WallTopZ = WallBaseZ + WallHeight;
			float PortalHalfHeight = BoxExtent.Z; // Z extent is the portal's half-height

			// Clamp portal center to keep full portal within wall vertical bounds
			float MinZ = WallBaseZ + PortalHalfHeight;
			float MaxZ = WallTopZ - PortalHalfHeight;

			// FIX: Use cursor impact point Z instead of the parent's grid-snapped value
			// The parent class snaps CursorWorldHitResult.Location.Z which may be at ground level
			// We need to snap the actual wall impact point Z instead
			float RawZ = CursorWorldHitResult.ImpactPoint.Z;

			// Apply vertical snapping with bounds awareness
			float SnappedZ = FMath::GridSnap(RawZ, VerticalSnap);

			// Clamp the snapped value to keep portal within wall bounds
			TargetLocation.Z = FMath::Clamp(SnappedZ, MinZ, MaxZ);

			// Update tool actor location with clamped position
			SetActorLocation(TargetLocation);
		}
	}

	// Both server and client update preview (server spawns it, client receives via replication)
	UpdatePreviewWindow();
}

void ABuildWindowTool::Click_Implementation()
{
	Super::Click_Implementation();

	if (!CurrentLot || !CurrentLot->WallComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildWindowTool: Cannot place window - no lot or wall component"));
		return;
	}

	if (!bValidPlacementLocation)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildWindowTool: Invalid placement location"));
		return;
	}

	if (!ClassToPlace)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildWindowTool: ClassToPlace is null"));
		return;
	}

	// Get the BuildServer subsystem
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildWindowTool: World is null"));
		return;
	}

	UBuildServer* BuildServer = World->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildWindowTool: BuildServer subsystem not found"));
		return;
	}

	// Calculate the affected wall sections by checking portal bounds against all wall sections
	TArray<int32> AffectedWallSections;

	// Create a temporary portal instance to get its bounding box
	FActorSpawnParameters TempSpawnParams;
	TempSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APortalBase* TempPortal = World->SpawnActor<APortalBase>(ClassToPlace, TargetLocation, TargetRotation, TempSpawnParams);

	if (TempPortal && TempPortal->Box)
	{
		// Get the portal's bounding box in world space
		FVector BoxExtent = TempPortal->Box->GetScaledBoxExtent();
		FVector BoxLocation = TempPortal->Box->GetComponentLocation();
		FQuat BoxRotation = TempPortal->Box->GetComponentQuat();

		// Query which wall sections the portal intersects
		AffectedWallSections = CurrentLot->WallComponent->GetMultiSectionIDFromHitResult(BoxLocation, BoxExtent, BoxRotation);

		// Clean up temp portal
		TempPortal->Destroy();

		UE_LOG(LogTemp, Log, TEXT("BuildWindowTool: Portal affects %d wall section(s)"), AffectedWallSections.Num());
	}

	// Fallback: if we couldn't determine affected sections, use the hit section
	if (AffectedWallSections.Num() == 0)
	{
		AffectedWallSections.Add(HitMeshSection);
		UE_LOG(LogTemp, Warning, TEXT("BuildWindowTool: Using fallback - single hit section %d"), HitMeshSection);
	}

	// Use the BuildServer to create the portal via command pattern
	BuildServer->BuildPortal(
		ClassToPlace,
		TargetLocation,
		TargetRotation,
		AffectedWallSections,
		PortalSize,
		PortalOffset,
		WindowMesh,         // Window mesh
		nullptr,            // DoorStaticMesh (not used for windows)
		nullptr             // DoorFrameMesh (not used for windows)
	);

	UE_LOG(LogTemp, Log, TEXT("BuildWindowTool: Placed window affecting %d wall section(s)"), AffectedWallSections.Num());
}

void ABuildWindowTool::UpdatePreviewWindow()
{
	// If validity changed from invalid to valid, recreate preview to restore original materials
	// Only server should destroy/recreate - clients get the reference via replication
	if (HasAuthority() && !bPreviousValidPlacement && bValidPlacementLocation && PreviewWindow)
	{
		DestroyPreviewWindow();
	}

	// Update previous state for next frame
	bPreviousValidPlacement = bValidPlacementLocation;

	// If we don't have a preview window, create one (server only - clients get it via replication)
	if (!PreviewWindow && ClassToSpawn.IsValid() && HasAuthority())
	{
		// Load the class if not already loaded
		if (!ClassToPlace)
		{
			ClassToPlace = ClassToSpawn.LoadSynchronous();
		}

		if (ClassToPlace)
		{
			// Spawn the preview window on server - it will replicate to clients
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			PreviewWindow = GetWorld()->SpawnActor<APortalBase>(ClassToPlace, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

			if (PreviewWindow)
			{
				// Attach to this tool actor
				PreviewWindow->AttachToActor(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

				// Disable collision on preview so it doesn't interfere with placement detection
				PreviewWindow->SetActorEnableCollision(false);

				// Apply portal size from tool configuration
				if (PortalSize.X > 0.0f && PortalSize.Y > 0.0f)
				{
					PreviewWindow->PortalSize = PortalSize;

					// Update box component to match portal size
					if (PreviewWindow->Box)
					{
						PreviewWindow->Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
					}
				}

				// Apply portal offset to the box component (cutaway) for visual preview
				// Also store in portal actor for wall cutout rendering
				PreviewWindow->PortalOffset = PortalOffset;

				if (PreviewWindow->Box)
				{
					// Offset Box visually for preview (not used for cutout calculation)
					PreviewWindow->Box->SetRelativeLocation(FVector(PortalOffset.X, 0.0f, PortalOffset.Y));
					PreviewWindow->Box->UpdateComponentToWorld();
				}

				// Apply window mesh if this is a WindowBase actor
				// Use replicated property so clients also see the mesh
				if (AWindowBase* WindowActor = Cast<AWindowBase>(PreviewWindow))
				{
					if (!WindowMesh.IsNull())
					{
						// Set the replicated property - this will replicate to clients and trigger OnRep
						WindowActor->WindowMeshAsset = WindowMesh;
						WindowActor->ApplyWindowMesh();  // Apply locally on server
						UE_LOG(LogTemp, Log, TEXT("BuildWindowTool: Set WindowMeshAsset for preview replication"));
					}
				}

				// Force network update to ensure ALL replicated properties are sent to clients
				// This includes PortalSize, PortalOffset, and mesh assets
				PreviewWindow->ForceNetUpdate();

				UE_LOG(LogTemp, Log, TEXT("BuildWindowTool: Created preview window with size (%.1f, %.1f) and offset (%.1f, %.1f)"),
					PortalSize.X, PortalSize.Y, PortalOffset.X, PortalOffset.Y);
			}
		}
	}

	// Update preview window location and material
	if (PreviewWindow)
	{
		PreviewWindow->SetActorLocation(TargetLocation);
		PreviewWindow->SetActorRotation(TargetRotation);

		// Only apply invalid material when placement is not valid
		// When valid, leave the portal's default materials so we can see it properly
		if (!bValidPlacementLocation && InvalidMaterial)
		{
			// Get all mesh components from the preview window and apply invalid material
			TArray<UMeshComponent*> MeshComponents;
			PreviewWindow->GetComponents<UMeshComponent>(MeshComponents);

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

void ABuildWindowTool::BroadcastRelease_Implementation()
{
	Super::BroadcastRelease_Implementation();

	// Clean up preview window after successful placement
	DestroyPreviewWindow();
}

void ABuildWindowTool::Delete_Implementation()
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

						UE_LOG(LogTemp, Log, TEXT("BuildWindowTool: Deleted portal at wall section %d"), HitMeshSection);
						break;
					}
				}
			}
		}
	}
}

void ABuildWindowTool::Destroyed()
{
	// Clean up preview window when tool is destroyed
	DestroyPreviewWindow();
	Super::Destroyed();
}

void ABuildWindowTool::DestroyPreviewWindow()
{
	// Unregister from walls on ALL machines - each machine needs to clean up its local PortalArray
	// and re-render walls to remove the cutout
	UnregisterPreviewFromWalls();

	// Only server destroys the preview actor - clients will have their reference cleared via replication
	if (HasAuthority() && PreviewWindow && IsValid(PreviewWindow))
	{
		PreviewWindow->Destroy();
		PreviewWindow = nullptr;
		UE_LOG(LogTemp, Log, TEXT("BuildWindowTool: Destroyed preview window"));
	}
}

void ABuildWindowTool::RegisterPreviewWithWalls()
{
	if (!PreviewWindow || !CurrentLot || !CurrentLot->WallComponent || !PreviewWindow->Box)
	{
		return;
	}

	// Calculate which wall sections this preview portal affects
	FVector BoxExtent = PreviewWindow->Box->GetScaledBoxExtent();
	FQuat RotationQuat = PreviewWindow->GetActorQuat();
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
				CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Add(PreviewWindow);
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
				CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Remove(PreviewWindow);
			}
		}

		// Render without portal to clear old cutout
		CurrentLot->WallComponent->RenderPortalsForWalls(AffectedWallSections);

		// Re-add portal and render again at new position
		for (int32 WallIndex : AffectedWallSections)
		{
			if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIndex))
			{
				CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Add(PreviewWindow);
			}
		}

		CurrentLot->WallComponent->RenderPortalsForWalls(AffectedWallSections);
	}
}

void ABuildWindowTool::UnregisterPreviewFromWalls()
{
	if (!PreviewWindow || !CurrentLot || !CurrentLot->WallComponent)
	{
		PreviewRegisteredWallIndices.Empty();
		return;
	}

	// Remove preview portal from all walls it was registered with
	for (int32 WallIndex : PreviewRegisteredWallIndices)
	{
		if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIndex))
		{
			CurrentLot->WallComponent->WallDataArray[WallIndex].PortalArray.Remove(PreviewWindow);
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

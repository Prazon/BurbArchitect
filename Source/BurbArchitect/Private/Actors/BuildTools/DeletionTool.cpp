// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/DeletionTool.h"
#include "Actors/LotManager.h"
#include "Actors/BurbPawn.h"
#include "Subsystems/BuildServer.h"
#include "Components/WallComponent.h"
#include "Components/FloorComponent.h"
#include "Components/RoofComponent.h"
#include "Actors/StairsBase.h"
#include "Actors/PortalBase.h"
#include "Interfaces/IDeletable.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"

ADeletionTool::ADeletionTool()
{
	PrimaryActorTick.bCanEverTick = true;
	bDeletionMode = true; // Always in deletion mode

	// Set trace channel to Primitives so player controller traces building components (walls, floors, roofs, stairs)
	TraceChannel = ECC_GameTraceChannel1;
}

void ADeletionTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeletionTool, bIsDragging);
	DOREPLIFETIME(ADeletionTool, DragStartLocation);
	DOREPLIFETIME(ADeletionTool, DragLevel);
	DOREPLIFETIME(ADeletionTool, ComponentsToDelete);
}

void ADeletionTool::BeginPlay()
{
	Super::BeginPlay();
}

void ADeletionTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear any highlights on tool destruction
	ClearHighlights();
	Super::EndPlay(EndPlayReason);
}

void ADeletionTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ADeletionTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	if (!CurrentLot)
	{
		return;
	}

	// Clear previous hover highlight
	if (HoveredComponent.ComponentType != EDeletionComponentType::UnknownComponent)
	{
		HighlightComponent(HoveredComponent, false);
		HoveredComponent = FDeletionComponentData();
	}

	// Detect component at cursor
	FDeletionComponentData NewHoveredComponent;
	DetectComponentAtLocation(CursorWorldHitResult, NewHoveredComponent);

	// Update hover state
	if (NewHoveredComponent.ComponentType != EDeletionComponentType::UnknownComponent)
	{
		HoveredComponent = NewHoveredComponent;
		HighlightComponent(HoveredComponent, true);
		OnComponentHovered(HoveredComponent);
	}

	// Update tool location
	if (CursorWorldHitResult.bBlockingHit)
	{
		TargetLocation = CursorWorldHitResult.Location;
		SetActorLocation(TargetLocation);
		UpdateLocation(GetActorLocation());
	}

	// Handle drag selection update
	if (bIsDragging && SelectPressed)
	{
		GatherComponentsInArea(DragStartLocation, MoveLocation, DragLevel);
	}
}

void ADeletionTool::Click_Implementation()
{
	Super::Click_Implementation();

	// Single element deletion on click
	if (HoveredComponent.ComponentType != EDeletionComponentType::UnknownComponent)
	{
		if (HasAuthority())
		{
			DeleteComponent(HoveredComponent);
			BroadcastClick_Deletion(HoveredComponent);
		}
		else
		{
			ServerClick_Deletion(HoveredComponent);
		}
	}
}

void ADeletionTool::Drag_Implementation()
{
	Super::Drag_Implementation();

	// Start drag selection
	bIsDragging = true;
	DragStartLocation = GetActorLocation();
	DragLevel = 0; // TODO: Get from traced level parameter
	ComponentsToDelete.Empty();

	if (HasAuthority())
	{
		BroadcastDrag_Deletion(DragStartLocation, DragLevel);
	}
	else
	{
		ServerDrag_Deletion(DragStartLocation, DragLevel);
	}
}

void ADeletionTool::Release_Implementation()
{
	Super::Release_Implementation();

	if (bIsDragging)
	{
		// Perform batch deletion
		if (ComponentsToDelete.Num() > 0)
		{
			if (HasAuthority())
			{
				BroadcastRelease_Implementation();
			}
			else
			{
				ServerRelease_Deletion(ComponentsToDelete);
			}
		}

		// Reset drag state
		bIsDragging = false;
		ClearHighlights();
		ComponentsToDelete.Empty();
	}
}

void ADeletionTool::BroadcastRelease_Implementation()
{
	if (!CurrentLot || ComponentsToDelete.Num() == 0)
	{
		return;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("DeletionTool: BuildServer subsystem not found"));
		return;
	}

	BuildServer->SetCurrentLot(CurrentLot);

	// Start batch operation for multiple deletions
	bool bNeedsBatch = ComponentsToDelete.Num() > 1;
	if (bNeedsBatch)
	{
		FString BatchDesc = FString::Printf(TEXT("Delete %d Components"), ComponentsToDelete.Num());
		BuildServer->BeginBatch(BatchDesc);
		OnBatchDeletionStarted(ComponentsToDelete.Num());
	}

	// Delete all components in batch
	for (const FDeletionComponentData& ComponentData : ComponentsToDelete)
	{
		DeleteComponent(ComponentData);
	}

	// End batch operation
	if (bNeedsBatch)
	{
		BuildServer->EndBatch();
		OnBatchDeletionCompleted(ComponentsToDelete.Num());
	}

	// Play delete sound
	if (DeleteSound)
	{
		UGameplayStatics::PlaySound2D(this, DeleteSound);
	}

	// Clear highlights and reset state
	ClearHighlights();
	ComponentsToDelete.Empty();
	bIsDragging = false;
}

void ADeletionTool::DetectComponentAtLocation(const FHitResult& HitResult, FDeletionComponentData& OutComponentData)
{
	if (!CurrentLot || !HitResult.bBlockingHit)
	{
		OutComponentData = FDeletionComponentData();
		return;
	}

	// Check for Wall Component
	if (UWallComponent* HitWallComponent = Cast<UWallComponent>(HitResult.Component))
	{
		int32 HitArrayIndex = HitWallComponent->GetSectionIDFromHitResult(HitResult);
		if (HitArrayIndex != -1 && CurrentLot->WallComponent->WallDataArray.IsValidIndex(HitArrayIndex))
		{
			const FWallSegmentData& WallData = CurrentLot->WallComponent->WallDataArray[HitArrayIndex];
			float WallHeight = WallData.Height;
			const FVector WallLocation = ((WallData.StartLoc + WallData.EndLoc) / 2) + (FVector::UpVector * WallHeight);

			OutComponentData.ComponentType = EDeletionComponentType::WallComponent;
			OutComponentData.ArrayIndex = HitArrayIndex;
			OutComponentData.Location = WallLocation;
			return;
		}
	}

	// Check for Floor Component
	if (UFloorComponent* HitFloorComponent = Cast<UFloorComponent>(HitResult.Component))
	{
		int32 HitArrayIndex = HitFloorComponent->GetSectionIDFromHitResult(HitResult);
		if (HitArrayIndex != -1 && CurrentLot->FloorComponent->FloorDataArray.IsValidIndex(HitArrayIndex))
		{
			const FVector Location = CurrentLot->FloorComponent->FloorDataArray[HitArrayIndex].StartLoc;

			OutComponentData.ComponentType = EDeletionComponentType::FloorComponent;
			OutComponentData.ArrayIndex = HitArrayIndex;
			OutComponentData.Location = Location;
			return;
		}
	}

	// Check for Stairs Component
	if (AStairsBase* HitStairsActor = Cast<AStairsBase>(HitResult.GetActor()))
	{
		if (HitStairsActor->bCommitted)
		{
			OutComponentData.ComponentType = EDeletionComponentType::StairsComponent;
			// Use actor's world location since StairsData.StartLoc is in local space
			OutComponentData.Location = HitStairsActor->GetActorLocation();
			OutComponentData.HitStairsActor = HitStairsActor;
			OutComponentData.ArrayIndex = CurrentLot->StairsActors.Find(HitStairsActor);
			return;
		}
	}

	// Check for Roof Component
	if (URoofComponent* HitRoofComponent = Cast<URoofComponent>(HitResult.Component))
	{
		int32 HitArrayIndex = HitRoofComponent->GetSectionIDFromHitResult(HitResult);
		if (HitArrayIndex != -1 && HitRoofComponent->RoofDataArray.IsValidIndex(HitArrayIndex))
		{
			const FVector Location = HitRoofComponent->RoofDataArray[HitArrayIndex].Location;

			OutComponentData.ComponentType = EDeletionComponentType::RoofComponent;
			OutComponentData.ArrayIndex = HitArrayIndex;
			OutComponentData.Location = Location;
			OutComponentData.HitRoofComponent = HitRoofComponent;
			return;
		}
	}

	// Check for Portal Component (doors/windows)
	if (APortalBase* HitPortalActor = Cast<APortalBase>(HitResult.GetActor()))
	{
		// Portals can always be detected (no bCommitted check needed)
		OutComponentData.ComponentType = EDeletionComponentType::PortalComponent;
		// Use actor's world location
		OutComponentData.Location = HitPortalActor->GetActorLocation();
		OutComponentData.HitPortalActor = HitPortalActor;
		// ArrayIndex is not used for portals (they're standalone actors)
		OutComponentData.ArrayIndex = -1;
		return;
	}

	// No component detected
	OutComponentData = FDeletionComponentData();
}

void ADeletionTool::HighlightComponent(const FDeletionComponentData& ComponentData, bool bHighlight)
{
	if (!CurrentLot || !InvalidMaterial)
	{
		return;
	}

	UMaterialInterface* MaterialToApply = bHighlight ? InvalidMaterial : nullptr;

	switch (ComponentData.ComponentType)
	{
	case EDeletionComponentType::WallComponent:
		if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(ComponentData.ArrayIndex))
		{
			int32 SectionIndex = CurrentLot->WallComponent->WallDataArray[ComponentData.ArrayIndex].SectionIndex;

			if (bHighlight)
			{
				// Store original material
				UMaterialInterface* OriginalMaterial = CurrentLot->WallComponent->WallDataArray[ComponentData.ArrayIndex].WallMaterial;
				int32 Key = GenerateHighlightKey(ComponentData);
				HighlightedMaterials.Add(Key, OriginalMaterial);

				// Apply highlight material
				CurrentLot->WallComponent->SetMaterial(SectionIndex, InvalidMaterial);
			}
			else
			{
				// Restore original material
				int32 Key = GenerateHighlightKey(ComponentData);
				UMaterialInterface** OriginalMaterialPtr = HighlightedMaterials.Find(Key);
				if (OriginalMaterialPtr && *OriginalMaterialPtr)
				{
					CurrentLot->WallComponent->SetMaterial(SectionIndex, *OriginalMaterialPtr);
					HighlightedMaterials.Remove(Key);
				}
			}
		}
		break;

	case EDeletionComponentType::FloorComponent:
		if (CurrentLot->FloorComponent->FloorDataArray.IsValidIndex(ComponentData.ArrayIndex))
		{
			int32 SectionIndex = CurrentLot->FloorComponent->FloorDataArray[ComponentData.ArrayIndex].SectionIndex;

			if (bHighlight)
			{
				UMaterialInterface* OriginalMaterial = CurrentLot->FloorComponent->FloorDataArray[ComponentData.ArrayIndex].FloorMaterial;
				int32 Key = GenerateHighlightKey(ComponentData);
				HighlightedMaterials.Add(Key, OriginalMaterial);

				CurrentLot->FloorComponent->SetMaterial(SectionIndex, InvalidMaterial);
			}
			else
			{
				int32 Key = GenerateHighlightKey(ComponentData);
				UMaterialInterface** OriginalMaterialPtr = HighlightedMaterials.Find(Key);
				if (OriginalMaterialPtr && *OriginalMaterialPtr)
				{
					CurrentLot->FloorComponent->SetMaterial(SectionIndex, *OriginalMaterialPtr);
					HighlightedMaterials.Remove(Key);
				}
			}
		}
		break;

	case EDeletionComponentType::StairsComponent:
		if (ComponentData.HitStairsActor && ComponentData.HitStairsActor->StairsMeshComponent)
		{
			if (bHighlight)
			{
				// Apply highlight to all stair modules
				for (UStaticMeshComponent* Module : ComponentData.HitStairsActor->StairsData.StairModules)
				{
					if (Module)
					{
						Module->SetMaterial(0, InvalidMaterial);
					}
				}
			}
			else
			{
				// Restore original materials
				for (int32 i = 0; i < ComponentData.HitStairsActor->StairsData.StairModules.Num(); ++i)
				{
					UStaticMeshComponent* Module = ComponentData.HitStairsActor->StairsData.StairModules[i];
					if (Module && ComponentData.HitStairsActor->StairsData.Structures.IsValidIndex(i))
					{
						UMaterialInterface* OriginalMaterial = (ComponentData.HitStairsActor->StairsData.Structures[i].StairType == EStairModuleType::Tread)
							? ComponentData.HitStairsActor->TreadMaterial
							: ComponentData.HitStairsActor->LandingMaterial;

						if (OriginalMaterial)
						{
							Module->SetMaterial(0, OriginalMaterial);
						}
					}
				}
			}
		}
		break;

	case EDeletionComponentType::RoofComponent:
		if (ComponentData.HitRoofComponent && ComponentData.HitRoofComponent->RoofDataArray.IsValidIndex(ComponentData.ArrayIndex))
		{
			int32 SectionIndex = ComponentData.HitRoofComponent->RoofDataArray[ComponentData.ArrayIndex].SectionIndex;

			if (bHighlight)
			{
				UMaterialInterface* OriginalMaterial = ComponentData.HitRoofComponent->RoofDataArray[ComponentData.ArrayIndex].RoofMaterial;
				int32 Key = GenerateHighlightKey(ComponentData);
				HighlightedMaterials.Add(Key, OriginalMaterial);

				ComponentData.HitRoofComponent->SetMaterial(SectionIndex, InvalidMaterial);
			}
			else
			{
				int32 Key = GenerateHighlightKey(ComponentData);
				UMaterialInterface** OriginalMaterialPtr = HighlightedMaterials.Find(Key);
				if (OriginalMaterialPtr && *OriginalMaterialPtr)
				{
					ComponentData.HitRoofComponent->SetMaterial(SectionIndex, *OriginalMaterialPtr);
					HighlightedMaterials.Remove(Key);
				}
			}
		}
		break;

	case EDeletionComponentType::PortalComponent:
		if (ComponentData.HitPortalActor)
		{
			// Get all primitive components (door/window meshes added by Blueprint children)
			TArray<UPrimitiveComponent*> PrimitiveComponents;
			ComponentData.HitPortalActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

			if (bHighlight)
			{
				// Apply highlight material to all mesh materials
				for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
				{
					if (PrimComp)
					{
						int32 NumMaterials = PrimComp->GetNumMaterials();
						for (int32 MatIndex = 0; MatIndex < NumMaterials; ++MatIndex)
						{
							// Store original material using unique key
							int32 Key = GenerateHighlightKey(ComponentData) + MatIndex;
							UMaterialInterface* OriginalMaterial = PrimComp->GetMaterial(MatIndex);
							HighlightedMaterials.Add(Key, OriginalMaterial);

							// Apply highlight material
							PrimComp->SetMaterial(MatIndex, InvalidMaterial);
						}
					}
				}
			}
			else
			{
				// Restore original materials
				for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
				{
					if (PrimComp)
					{
						int32 NumMaterials = PrimComp->GetNumMaterials();
						for (int32 MatIndex = 0; MatIndex < NumMaterials; ++MatIndex)
						{
							int32 Key = GenerateHighlightKey(ComponentData) + MatIndex;
							UMaterialInterface** OriginalMaterialPtr = HighlightedMaterials.Find(Key);
							if (OriginalMaterialPtr && *OriginalMaterialPtr)
							{
								PrimComp->SetMaterial(MatIndex, *OriginalMaterialPtr);
								HighlightedMaterials.Remove(Key);
							}
						}
					}
				}
			}
		}
		break;

	default:
		break;
	}
}

void ADeletionTool::DeleteComponent(const FDeletionComponentData& ComponentData)
{
	if (!CurrentLot)
	{
		return;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("DeletionTool: BuildServer subsystem not found"));
		return;
	}

	BuildServer->SetCurrentLot(CurrentLot);

	switch (ComponentData.ComponentType)
	{
	case EDeletionComponentType::WallComponent:
		if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(ComponentData.ArrayIndex))
		{
			const FWallSegmentData& WallData = CurrentLot->WallComponent->WallDataArray[ComponentData.ArrayIndex];
			BuildServer->DeleteWall(WallData);
			OnComponentDeleted(ComponentData);
		}
		break;

	case EDeletionComponentType::FloorComponent:
		if (CurrentLot->FloorComponent->FloorDataArray.IsValidIndex(ComponentData.ArrayIndex))
		{
			const FFloorSegmentData& FloorData = CurrentLot->FloorComponent->FloorDataArray[ComponentData.ArrayIndex];
			BuildServer->DeleteFloor(FloorData);
			OnComponentDeleted(ComponentData);
		}
		break;

	case EDeletionComponentType::RoofComponent:
		if (ComponentData.HitRoofComponent && ComponentData.HitRoofComponent->RoofDataArray.IsValidIndex(ComponentData.ArrayIndex))
		{
			const FVector& RoofLocation = ComponentData.HitRoofComponent->RoofDataArray[ComponentData.ArrayIndex].Location;
			BuildServer->DeleteRoof(RoofLocation);
			OnComponentDeleted(ComponentData);
		}
		break;

	case EDeletionComponentType::StairsComponent:
		if (ComponentData.HitStairsActor && ComponentData.HitStairsActor->StairsData.bCommitted)
		{
			// Use actor's world location since StairsData.StartLoc is in local space
			const FVector& StairsLocation = ComponentData.HitStairsActor->GetActorLocation();
			BuildServer->DeleteStairs(StairsLocation);
			OnComponentDeleted(ComponentData);
		}
		break;

	case EDeletionComponentType::PortalComponent:
		if (ComponentData.HitPortalActor)
		{
			// Use IDeletable interface to delete portals (doors/windows)
			// This handles cleanup via OnDeleted() and then destroys the actor
			if (ComponentData.HitPortalActor->GetClass()->ImplementsInterface(UDeletable::StaticClass()))
			{
				if (IDeletable::Execute_RequestDeletion(ComponentData.HitPortalActor))
				{
					OnComponentDeleted(ComponentData);
					UE_LOG(LogTemp, Log, TEXT("DeletionTool: Deleted portal %s"), *ComponentData.HitPortalActor->GetName());
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("DeletionTool: Portal deletion blocked by CanBeDeleted()"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("DeletionTool: Portal does not implement IDeletable interface"));
			}
		}
		break;

	default:
		UE_LOG(LogTemp, Warning, TEXT("DeletionTool: Unknown component type for deletion"));
		break;
	}

	// Play delete sound for single deletions (batch deletions play once at end)
	if (!bIsDragging && DeleteSound)
	{
		UGameplayStatics::PlaySound2D(this, DeleteSound);
	}
}

void ADeletionTool::GatherComponentsInArea(FVector StartLocation, FVector EndLocation, int32 Level)
{
	if (!CurrentLot)
	{
		return;
	}

	// Clear previous selection
	ClearHighlights();
	ComponentsToDelete.Empty();

	// Calculate bounding box for selection area
	FVector MinBounds = FVector(
		FMath::Min(StartLocation.X, EndLocation.X),
		FMath::Min(StartLocation.Y, EndLocation.Y),
		FMath::Min(StartLocation.Z, EndLocation.Z)
	);

	FVector MaxBounds = FVector(
		FMath::Max(StartLocation.X, EndLocation.X),
		FMath::Max(StartLocation.Y, EndLocation.Y),
		FMath::Max(StartLocation.Z, EndLocation.Z)
	);

	// Gather walls within bounds
	for (int32 i = 0; i < CurrentLot->WallComponent->WallDataArray.Num(); ++i)
	{
		const FWallSegmentData& WallData = CurrentLot->WallComponent->WallDataArray[i];
		if (!WallData.bCommitted)
		{
			continue;
		}

		// Check if wall midpoint is within bounds
		FVector WallMidpoint = (WallData.StartLoc + WallData.EndLoc) / 2.0f;
		if (WallMidpoint.X >= MinBounds.X && WallMidpoint.X <= MaxBounds.X &&
			WallMidpoint.Y >= MinBounds.Y && WallMidpoint.Y <= MaxBounds.Y)
		{
			FDeletionComponentData ComponentData;
			ComponentData.ComponentType = EDeletionComponentType::WallComponent;
			ComponentData.ArrayIndex = i;
			ComponentData.Location = WallMidpoint;

			ComponentsToDelete.Add(ComponentData);
			HighlightComponent(ComponentData, true);
		}
	}

	// Gather floors within bounds
	for (int32 i = 0; i < CurrentLot->FloorComponent->FloorDataArray.Num(); ++i)
	{
		const FFloorSegmentData& FloorData = CurrentLot->FloorComponent->FloorDataArray[i];
		FVector FloorLocation = FloorData.StartLoc;

		if (FloorLocation.X >= MinBounds.X && FloorLocation.X <= MaxBounds.X &&
			FloorLocation.Y >= MinBounds.Y && FloorLocation.Y <= MaxBounds.Y)
		{
			FDeletionComponentData ComponentData;
			ComponentData.ComponentType = EDeletionComponentType::FloorComponent;
			ComponentData.ArrayIndex = i;
			ComponentData.Location = FloorLocation;

			ComponentsToDelete.Add(ComponentData);
			HighlightComponent(ComponentData, true);
		}
	}

	// Gather stairs within bounds
	for (int32 i = 0; i < CurrentLot->StairsActors.Num(); ++i)
	{
		AStairsBase* StairsActor = CurrentLot->StairsActors[i];
		if (!StairsActor || !StairsActor->bCommitted)
		{
			continue;
		}

		// Use actor's world location since StairsData.StartLoc is in local space
		FVector StairsLocation = StairsActor->GetActorLocation();
		if (StairsLocation.X >= MinBounds.X && StairsLocation.X <= MaxBounds.X &&
			StairsLocation.Y >= MinBounds.Y && StairsLocation.Y <= MaxBounds.Y)
		{
			FDeletionComponentData ComponentData;
			ComponentData.ComponentType = EDeletionComponentType::StairsComponent;
			ComponentData.ArrayIndex = i;
			ComponentData.Location = StairsLocation;
			ComponentData.HitStairsActor = StairsActor;

			ComponentsToDelete.Add(ComponentData);
			HighlightComponent(ComponentData, true);
		}
	}

	// Gather roofs within bounds
	// Note: Roofs are stored in separate component instances, need to iterate through lot's roof components
	// TODO: Add roof iteration when LotManager has a roof components array

	// Gather portals (doors/windows) within bounds
	// Portals are standalone actors in the world
	TArray<AActor*> AllPortals;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APortalBase::StaticClass(), AllPortals);

	for (AActor* PortalActor : AllPortals)
	{
		APortalBase* Portal = Cast<APortalBase>(PortalActor);
		if (!Portal)
		{
			continue;
		}

		// Check if portal is within bounds
		FVector PortalLocation = Portal->GetActorLocation();
		if (PortalLocation.X >= MinBounds.X && PortalLocation.X <= MaxBounds.X &&
			PortalLocation.Y >= MinBounds.Y && PortalLocation.Y <= MaxBounds.Y)
		{
			FDeletionComponentData ComponentData;
			ComponentData.ComponentType = EDeletionComponentType::PortalComponent;
			ComponentData.ArrayIndex = -1; // Not used for portals
			ComponentData.Location = PortalLocation;
			ComponentData.HitPortalActor = Portal;

			ComponentsToDelete.Add(ComponentData);
			HighlightComponent(ComponentData, true);
		}
	}
}

void ADeletionTool::ClearHighlights()
{
	// Restore all highlighted materials
	for (const FDeletionComponentData& ComponentData : ComponentsToDelete)
	{
		HighlightComponent(ComponentData, false);
	}

	HighlightedMaterials.Empty();
}

int32 ADeletionTool::GenerateHighlightKey(const FDeletionComponentData& ComponentData) const
{
	// Generate unique key based on component type and array index
	return (static_cast<int32>(ComponentData.ComponentType) << 24) | (ComponentData.ArrayIndex & 0x00FFFFFF);
}

void ADeletionTool::RestoreMaterial(const FDeletionComponentData& ComponentData, UMaterialInterface* OriginalMaterial)
{
	HighlightComponent(ComponentData, false);
}

// RPC Implementations

void ADeletionTool::ServerClick_Deletion_Implementation(const FDeletionComponentData& ComponentData)
{
	// Authority check for safety - Server RPCs should only execute server-side operations
	if (!HasAuthority())
	{
		return;
	}

	DeleteComponent(ComponentData);
	BroadcastClick_Deletion(ComponentData);
}

void ADeletionTool::ServerDrag_Deletion_Implementation(FVector DragStart, int32 Level)
{
	bIsDragging = true;
	DragStartLocation = DragStart;
	DragLevel = Level;
	ComponentsToDelete.Empty();

	BroadcastDrag_Deletion(DragStart, Level);
}

void ADeletionTool::ServerRelease_Deletion_Implementation(const TArray<FDeletionComponentData>& InComponentsToDelete)
{
	ComponentsToDelete = InComponentsToDelete;
	BroadcastRelease_Implementation();
	BroadcastRelease_Deletion(InComponentsToDelete);
}

void ADeletionTool::BroadcastClick_Deletion_Implementation(const FDeletionComponentData& ComponentData)
{
	// Visual feedback on clients
	if (DeleteSound)
	{
		UGameplayStatics::PlaySound2D(this, DeleteSound);
	}
}

void ADeletionTool::BroadcastDrag_Deletion_Implementation(FVector DragStart, int32 Level)
{
	bIsDragging = true;
	DragStartLocation = DragStart;
	DragLevel = Level;
}

void ADeletionTool::BroadcastRelease_Deletion_Implementation(const TArray<FDeletionComponentData>& InComponentsToDelete)
{
	// Visual feedback on clients
	ClearHighlights();
	bIsDragging = false;
}

// Blueprint Event Implementations

void ADeletionTool::OnComponentHovered_Implementation(const FDeletionComponentData& ComponentData)
{
	// Blueprint hook - can be overridden
}

void ADeletionTool::OnComponentDeleted_Implementation(const FDeletionComponentData& ComponentData)
{
	// Blueprint hook - can be overridden
}

void ADeletionTool::OnBatchDeletionStarted_Implementation(int32 ComponentCount)
{
	// Blueprint hook - can be overridden
}

void ADeletionTool::OnBatchDeletionCompleted_Implementation(int32 ComponentCount)
{
	// Blueprint hook - can be overridden
}

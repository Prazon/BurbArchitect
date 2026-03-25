// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuildTools/SelectionTool.h"

#include "LandscapeGizmoActiveActor.h"
#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Actors/StairsBase.h"
#include "Actors/Roofs/RoofBase.h"
#include "Actors/PortalBase.h"
#include "Actors/DoorBase.h"
#include "Actors/WindowBase.h"
#include "Interfaces/IDeletable.h"
#include "Components/LineBatchComponent.h"
#include "Components/RoomManagerComponent.h"
#include "Components/WallGraphComponent.h"
#include "Data/WallGraphData.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"


// Sets default values
ASelectionTool::ASelectionTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	// Use Primitives trace channel to hit walls, floors, roofs, etc.
	TraceChannel = ECC_GameTraceChannel1;
}

void ASelectionTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

// Called when the game starts or when spawned
void ASelectionTool::BeginPlay()
{
	Super::BeginPlay();
}

void ASelectionTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up room footprint lines
	ClearRoomFootprintLines();

	if (RoomFootprintLineComponent)
	{
		RoomFootprintLineComponent->DestroyComponent();
		RoomFootprintLineComponent = nullptr;
	}

	// Clean up room control widget
	HideRoomControlWidget();
	if (RoomControlWidgetComponent)
	{
		RoomControlWidgetComponent->DestroyComponent();
		RoomControlWidgetComponent = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ASelectionTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ASelectionTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	// Cache hit result for use in Click
	LastCursorHitResult = CursorWorldHitResult;
	
	// Store traced level for wall filtering in GetRoomFromWallClick
	CurrentTracedLevel = TracedLevel;

	if(Cast<UPrimitiveComponent>(CursorWorldHitResult.Component))
	{
		TargetLocation = CursorWorldHitResult.Location;

		SetActorLocation(TargetLocation);
		UpdateLocation(GetActorLocation());
		//Tell blueprint children we moved successfully
		OnMoved();
		bValidPlacementLocation = true;
		if (SelectPressed)
		{
			Drag_Implementation();
		}
		else
		{
			PreviousLocation = GetActorLocation();
		}

	}
	else if (CurrentPlayerPawn != nullptr && CurrentLot != nullptr)
	{
		TargetLocation = CurrentLot->GetMouseWorldPosition(FVector::Dist(CurrentPlayerPawn->GetPawnViewLocation(), DragCreateVectors.StartOperation));
		SetActorLocation(TargetLocation);
		UpdateLocation(GetActorLocation());
		bValidPlacementLocation = false;
	}

	FComponentData ComponentData = FComponentData();

	if(Cast<UWallComponent>(CursorWorldHitResult.Component))
	{
		UWallComponent* HitComponent = Cast<UWallComponent>(CursorWorldHitResult.Component);
		int HitArrayIndex = HitComponent->GetSectionIDFromHitResult(CursorWorldHitResult);

		if (HitArrayIndex != -1)
		{
			// Use the wall's actual height instead of hardcoded value
			float WallHeight = CurrentLot->WallComponent->WallDataArray[HitArrayIndex].Height;
			const FVector WallLocation = ((CurrentLot->WallComponent->WallDataArray[HitArrayIndex].StartLoc+CurrentLot->WallComponent->WallDataArray[HitArrayIndex].EndLoc)/2) + (FVector::UpVector * WallHeight);

			ComponentData.ComponentType = ESectionComponentType::WallComponent;
			ComponentData.Name = StaticEnum<ESectionComponentType>()->GetNameStringByValue(static_cast<int64>(ComponentData.ComponentType));
			ComponentData.ArrayIndex = HitArrayIndex;
			ComponentData.Location = WallLocation;
			ComponentData.SectionIndex = CurrentLot->WallComponent->WallDataArray[HitArrayIndex].SectionIndex;
		}
	}
	else if(Cast<UFloorComponent>(CursorWorldHitResult.Component))
	{
		UFloorComponent* HitComponent = Cast<UFloorComponent>(CursorWorldHitResult.Component);
		int HitArrayIndex = HitComponent->GetSectionIDFromHitResult(CursorWorldHitResult);

		if (HitArrayIndex != -1)
		{
			const FVector Location = CurrentLot->FloorComponent->FloorDataArray[HitArrayIndex].StartLoc;

			ComponentData.ComponentType = ESectionComponentType::FloorComponent;
			ComponentData.Name = StaticEnum<ESectionComponentType>()->GetNameStringByValue(static_cast<int64>(ComponentData.ComponentType));
			ComponentData.ArrayIndex = HitArrayIndex;
			ComponentData.Location = Location;
			ComponentData.SectionIndex = CurrentLot->FloorComponent->FloorDataArray[HitArrayIndex].SectionIndex;
		}
	}
	else if(AStairsBase* HitStairsActor = Cast<AStairsBase>(CursorWorldHitResult.GetActor()))
	{
		// Check if this is a committed stairs actor
		if (HitStairsActor->bCommitted)
		{
			ComponentData.ComponentType = ESectionComponentType::StairsComponent;
			ComponentData.Name = StaticEnum<ESectionComponentType>()->GetNameStringByValue(static_cast<int64>(ComponentData.ComponentType));
			// Use actor's world location since StairsData.StartLoc is in local space
			ComponentData.Location = HitStairsActor->GetActorLocation();
			ComponentData.HitStairsActor = HitStairsActor;
			// Find the array index in the lot's stairs actors
			ComponentData.ArrayIndex = CurrentLot->StairsActors.Find(HitStairsActor);
		}
	}
	else if(Cast<URoofComponent>(CursorWorldHitResult.Component))
	{
		URoofComponent* HitComponent = Cast<URoofComponent>(CursorWorldHitResult.Component);
		int HitArrayIndex = HitComponent->GetSectionIDFromHitResult(CursorWorldHitResult);

		if (HitArrayIndex != -1)
		{
			// Use the hit component directly since roof components are now separate instances
			const FVector Location = HitComponent->RoofDataArray[HitArrayIndex].Location;

			ComponentData.ComponentType = ESectionComponentType::RoofComponent;
			ComponentData.Name = StaticEnum<ESectionComponentType>()->GetNameStringByValue(static_cast<int64>(ComponentData.ComponentType));
			ComponentData.ArrayIndex = HitArrayIndex;
			ComponentData.Location = Location;
			ComponentData.SectionIndex = HitComponent->RoofDataArray[HitArrayIndex].SectionIndex;
			// Store reference to the specific component that was hit
			ComponentData.HitRoofComponent = HitComponent;
		}
	}

	SelectObject(ComponentData, MoveLocation);
}

void ASelectionTool::Click_Implementation()
{
	Super::Click_Implementation();

	// BURB-46: Room selection disabled — needs more work before re-enabling
	// See BURB-46 for details on what needs to be addressed
#if 0
	// Room selection logic
	if (!LastCursorHitResult.bBlockingHit)
		return;

	UPrimitiveComponent* HitComponent = LastCursorHitResult.GetComponent();

	// Only process wall clicks for room selection
	if (CurrentLot && HitComponent == CurrentLot->WallComponent)
	{
		int32 RoomID = GetRoomFromWallClick(LastCursorHitResult);

		if (RoomID > 0)
		{
			SelectRoom(RoomID);
		}
		else
		{
			// Clicked wall with no rooms -> deselect
			DeselectRoom();
		}
	}
	else
	{
		// Clicked on non-wall (floor, terrain, etc.) -> deselect
		DeselectRoom();
	}
#endif
}

void ASelectionTool::Drag_Implementation()
{
	Super::Drag_Implementation();
}

void ASelectionTool::BroadcastRelease_Implementation()
{
	OnReleasedSection();
}

void ASelectionTool::SelectObject(FComponentData ComponentData, const FVector& MoveLocation)
{
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// Walls and floors no longer show hover highlight in SelectionTool
	// Use DeletionTool for hover highlighting of walls/floors
	if (ComponentData.ComponentType == ESectionComponentType::WallComponent)
	{
		// No hover highlight for walls - handled by DeletionTool
	}
	else if (ComponentData.ComponentType == ESectionComponentType::FloorComponent)
	{
		// No hover highlight for floors - handled by DeletionTool
	}
	else if (ComponentData.ComponentType == ESectionComponentType::StairsComponent)
	{
		if (ComponentData.HitStairsActor != SelectedComponent.HitStairsActor)
		{
			if (SelectedComponent.HitStairsActor)
			{
				ReleaseObject(SelectedComponent);
			}

			// Apply selection material to the stairs actor's mesh component
			if (ComponentData.HitStairsActor && ComponentData.HitStairsActor->StairsMeshComponent)
			{
				// Apply material to all stair modules
				for (UStaticMeshComponent* Module : ComponentData.HitStairsActor->StairsData.StairModules)
				{
					if (Module)
					{
						Module->SetMaterial(0, Cast<UMaterialInstance>(ValidMaterial));
					}
				}

				// Enter edit mode when stairs are selected (Sims-like workflow)
				ComponentData.HitStairsActor->EnterEditMode();
			}

			SelectedComponent = ComponentData;

			OnSelectedSection(ComponentData);
		}
	}
	else if (ComponentData.ComponentType == ESectionComponentType::RoofComponent)
	{
		if (ComponentData.SectionIndex != SelectedComponent.SectionIndex)
		{
			if (SelectedComponent.SectionIndex != -1)
			{
				ReleaseObject(SelectedComponent);
			}

			// Get the roof actor from the component and enter edit mode
			if (ComponentData.HitRoofComponent)
			{
				ARoofBase* RoofActor = Cast<ARoofBase>(ComponentData.HitRoofComponent->GetOwner());
				if (RoofActor && RoofActor->bCommitted)
				{
					// Apply selection material
					ComponentData.HitRoofComponent->SetMaterial(ComponentData.SectionIndex, Cast<UMaterialInstance>(ValidMaterial));

					// Enter edit mode (shows scale tools and sets bInEditMode=true)
					RoofActor->EnterEditMode();

					// Store reference to roof actor for rotation forwarding
					ComponentData.HitRoofActor = RoofActor;
				}
			}

			SelectedComponent = ComponentData;

			OnSelectedSection(ComponentData);
		}
	}
	else if (ComponentData.ComponentType == ESectionComponentType::UnknownComponent)
	{
		// Clicked on empty space - deselect any currently selected component
		if (SelectedComponent.ComponentType != ESectionComponentType::UnknownComponent)
		{
			ReleaseObject(SelectedComponent);
		}
	}
	else if (SelectedComponent.SectionIndex != -1 && CurrentLot->CalculatePointOnLineDistance(CameraLocation, SelectedComponent.Location, MoveLocation) > 200.0f)
	{
		ReleaseObject(SelectedComponent);
	}
}

void ASelectionTool::ReleaseObject(FComponentData ComponentData)
{
	// Walls and floors no longer highlighted in SelectionTool - skip material restoration
	if (ComponentData.ComponentType == ESectionComponentType::WallComponent)
	{
		// No material restoration needed - walls not highlighted
		OnReleasedSection();
	}
	else if (ComponentData.ComponentType == ESectionComponentType::FloorComponent)
	{
		// No material restoration needed - floors not highlighted
		OnReleasedSection();
	}
	else if (ComponentData.ComponentType == ESectionComponentType::StairsComponent)
	{
		// Restore original materials to stairs actor
		if (ComponentData.HitStairsActor)
		{
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

			// Exit edit mode when stairs are deselected
			ComponentData.HitStairsActor->ExitEditMode();
		}
		OnReleasedSection();
	}
	else if (ComponentData.ComponentType == ESectionComponentType::RoofComponent)
	{
		// Restore original material
		if (ComponentData.HitRoofComponent)
		{
			ComponentData.HitRoofComponent->SetMaterial(ComponentData.HitRoofComponent->RoofDataArray[ComponentData.ArrayIndex].SectionIndex, ComponentData.HitRoofComponent->RoofDataArray[ComponentData.ArrayIndex].RoofMaterial);
		}

		// Exit edit mode when roof is deselected
		if (ComponentData.HitRoofActor)
		{
			ComponentData.HitRoofActor->ExitEditMode();
		}
		OnReleasedSection();
	}

	SelectedComponent = FComponentData();
}

void ASelectionTool::ModifySection(const FComponentData& ComponentData)
{
	if (ComponentData.ComponentType == ESectionComponentType::WallComponent)
	{
		CurrentLot->WallComponent->RemoveWallSection(ComponentData.ArrayIndex);
	}
	else if (ComponentData.ComponentType == ESectionComponentType::FloorComponent)
	{
		CurrentLot->FloorComponent->RemoveFloorSection(ComponentData.ArrayIndex);
	}
	else if (ComponentData.ComponentType == ESectionComponentType::StairsComponent)
	{
		// Remove stairs actor from the lot
		if (ComponentData.HitStairsActor)
		{
			CurrentLot->RemoveStairsActor(ComponentData.HitStairsActor);
		}
	}
	else if (ComponentData.ComponentType == ESectionComponentType::RoofComponent)
	{
		// Check if this is an actor-based roof (new system)
		if (ComponentData.HitRoofActor)
		{
			// Use IDeletable interface to delete the roof actor (also deletes rooms, walls, floors)
			IDeletable* DeletableRoof = Cast<IDeletable>(ComponentData.HitRoofActor);
			if (DeletableRoof)
			{
				DeletableRoof->Execute_RequestDeletion(ComponentData.HitRoofActor);
			}
		}
		// Otherwise, use component-based deletion (old system)
		else if (ComponentData.HitRoofComponent)
		{
			ComponentData.HitRoofComponent->RemoveRoofSection(ComponentData.ArrayIndex);
		}
	}

	ReleaseObject(SelectedComponent);
}

void ASelectionTool::AppearanceSection(const FComponentData& ComponentData)
{
	ReleaseObject(SelectedComponent);
}

void ASelectionTool::DeleteSection(const FComponentData& ComponentData)
{
	if (ComponentData.ComponentType == ESectionComponentType::WallComponent)
	{
		CurrentLot->WallComponent->RemoveWallSection(ComponentData.ArrayIndex);
	}
	else if (ComponentData.ComponentType == ESectionComponentType::FloorComponent)
	{
		CurrentLot->FloorComponent->RemoveFloorSection(ComponentData.ArrayIndex);
	}
	else if (ComponentData.ComponentType == ESectionComponentType::StairsComponent)
	{
		// Remove stairs actor from the lot
		if (ComponentData.HitStairsActor)
		{
			CurrentLot->RemoveStairsActor(ComponentData.HitStairsActor);
		}
	}
	else if (ComponentData.ComponentType == ESectionComponentType::RoofComponent)
	{
		// Check if this is an actor-based roof (new system)
		if (ComponentData.HitRoofActor)
		{
			// Use IDeletable interface to delete the roof actor (also deletes rooms, walls, floors)
			IDeletable* DeletableRoof = Cast<IDeletable>(ComponentData.HitRoofActor);
			if (DeletableRoof)
			{
				DeletableRoof->Execute_RequestDeletion(ComponentData.HitRoofActor);
			}
		}
		// Otherwise, use component-based deletion (old system)
		else if (ComponentData.HitRoofComponent)
		{
			ComponentData.HitRoofComponent->RemoveRoofSection(ComponentData.ArrayIndex);
		}
	}

	ReleaseObject(SelectedComponent);
}

// ========================================
// Room Selection Functions
// ========================================

void ASelectionTool::SelectRoom(int32 RoomID)
{
	// Don't reselect the same room
	if (SelectedRoomID == RoomID)
		return;

	// Always deselect current room first (ensures only 1 room selected at a time)
	DeselectRoom();

	SelectedRoomID = RoomID;

	// Update RoomManager selection state
	if (CurrentLot && CurrentLot->RoomManager)
	{
		CurrentLot->RoomManager->SelectRoom(RoomID);
	}

	// Draw footprint lines
	DrawRoomFootprintLines(RoomID);

	// Show room control widget at centroid
	FRoomData RoomData;
	if (CurrentLot && CurrentLot->RoomManager && CurrentLot->RoomManager->GetRoom(RoomID, RoomData))
	{
		FVector WidgetLocation = RoomData.Centroid + FVector(0, 0, RoomControlWidgetZOffset);
		ShowRoomControlWidget(WidgetLocation);
	}

	UE_LOG(LogTemp, Log, TEXT("SelectionTool: Selected room %d"), RoomID);
}

void ASelectionTool::DeselectRoom()
{
	// Always clear visual feedback (even if no room selected, ensures clean state)
	ClearRoomFootprintLines();
	HideRoomControlWidget();

	if (SelectedRoomID == 0)
		return;

	UE_LOG(LogTemp, Log, TEXT("SelectionTool: Deselected room %d"), SelectedRoomID);

	// Update RoomManager
	if (CurrentLot && CurrentLot->RoomManager)
	{
		CurrentLot->RoomManager->DeselectRoom();
	}

	SelectedRoomID = 0;
}

void ASelectionTool::DrawRoomFootprintLines(int32 RoomID)
{
	// Create footprint line component if it doesn't exist
	if (!RoomFootprintLineComponent)
	{
		RoomFootprintLineComponent = NewObject<ULineBatchComponent>(this, TEXT("RoomFootprintLines"));
		if (RoomFootprintLineComponent)
		{
			RoomFootprintLineComponent->SetTranslucentSortPriority(1000);
			RoomFootprintLineComponent->RegisterComponent();
			RoomFootprintLineComponent->SetRenderCustomDepth(false);
		}
	}

	if (!RoomFootprintLineComponent || !CurrentLot || !CurrentLot->RoomManager)
		return;

	// Clear existing lines
	ClearRoomFootprintLines();

	// Get room data from RoomManager
	FRoomData RoomData;
	if (!CurrentLot->RoomManager->GetRoom(RoomID, RoomData) || !RoomData.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("SelectionTool: Failed to get room data for RoomID %d"), RoomID);
		return;
	}

	// Draw closed polygon boundary
	const FColor FloorLineColor = FColor::Green;   // Green for floor outline
	const FColor WallLineColor = FColor::White;    // White for 3D wireframe
	const float WireframeThickness = 6.0f;         // Wireframe line thickness
	const float FootprintThickness = 16.0f;        // Thicker footprint to lay flat on floor
	const float LineLifeTime = -1.0f;              // Persistent
	const float ZOffset = 10.0f;                   // Above floor to prevent z-fighting
	const uint8 DepthPriority = SDPG_Foreground;   // Render on top

	// Get wall height and thickness from the lot's wall component (or use defaults)
	float WallHeight = 300.0f;    // Default wall height
	float WallThickness = 20.0f;  // Default wall thickness
	if (CurrentLot && CurrentLot->WallComponent && CurrentLot->WallComponent->WallDataArray.Num() > 0)
	{
		WallHeight = CurrentLot->WallComponent->WallDataArray[0].Height;
		WallThickness = CurrentLot->WallComponent->WallDataArray[0].Thickness;
	}

	const float HalfThickness = WallThickness * 0.5f;
	const float FootprintInset = HalfThickness + 15.0f;  // Inset floor footprint past wall inner edge

	// Use all boundary vertices for calculations (handles diagonal walls correctly)
	const TArray<FVector>& Vertices = RoomData.BoundaryVertices;

	if (Vertices.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("SelectionTool: Room %d has invalid vertex count: %d"), RoomID, Vertices.Num());
		return;
	}

	// Determine which vertices are corners (non-collinear) for visualization
	// We calculate offsets for ALL vertices, but only draw at corners
	const float CollinearThreshold = 0.9999f; // Dot product threshold for collinearity
	TArray<bool> IsCorner;
	IsCorner.SetNum(Vertices.Num());

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector& Prev = Vertices[(i - 1 + Vertices.Num()) % Vertices.Num()];
		const FVector& Curr = Vertices[i];
		const FVector& Next = Vertices[(i + 1) % Vertices.Num()];

		// Calculate directions (2D, ignore Z)
		FVector DirToPrev = (Prev - Curr).GetSafeNormal2D();
		FVector DirToNext = (Next - Curr).GetSafeNormal2D();

		// Check if this vertex is a corner (not collinear with neighbors)
		float Dot = FMath::Abs(FVector::DotProduct(DirToPrev, DirToNext));
		IsCorner[i] = (Dot < CollinearThreshold);
	}

	// Calculate inner and outer vertices based on wall thickness for ALL vertices
	TArray<FVector> InnerVertices;
	TArray<FVector> OuterVertices;
	TArray<FVector> FootprintVertices;

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector& Prev = Vertices[(i - 1 + Vertices.Num()) % Vertices.Num()];
		const FVector& Curr = Vertices[i];
		const FVector& Next = Vertices[(i + 1) % Vertices.Num()];

		// Edge directions (2D)
		FVector EdgeToPrev = (Prev - Curr).GetSafeNormal2D();
		FVector EdgeToNext = (Next - Curr).GetSafeNormal2D();

		// Calculate inward bisector direction
		// The bisector points into the room (inward)
		FVector Bisector = (EdgeToPrev + EdgeToNext).GetSafeNormal2D();

		// Check if bisector points inward (toward room center) or outward
		FVector ToCenter = (RoomData.Centroid - Curr).GetSafeNormal2D();
		if (FVector::DotProduct(Bisector, ToCenter) < 0)
		{
			Bisector = -Bisector;
		}

		// Calculate the miter length (how far to offset along bisector)
		// This accounts for the angle at the corner
		float HalfAngle = FMath::Acos(FMath::Clamp(FVector::DotProduct(EdgeToPrev, EdgeToNext), -1.0f, 1.0f)) * 0.5f;
		float MiterLength = (HalfAngle > 0.01f) ? (HalfThickness / FMath::Sin(HalfAngle)) : HalfThickness;
		MiterLength = FMath::Min(MiterLength, HalfThickness * 3.0f);  // Cap to prevent extreme values

		float FootprintMiterLength = (HalfAngle > 0.01f) ? (FootprintInset / FMath::Sin(HalfAngle)) : FootprintInset;
		FootprintMiterLength = FMath::Min(FootprintMiterLength, FootprintInset * 3.0f);

		// Calculate offset vertices
		FVector InnerVert = Curr + Bisector * MiterLength;
		FVector OuterVert = Curr - Bisector * MiterLength;
		FVector FootprintVert = Curr + Bisector * FootprintMiterLength;

		InnerVertices.Add(InnerVert);
		OuterVertices.Add(OuterVert);
		FootprintVertices.Add(FootprintVert);
	}

	// Draw the wireframes - only draw lines that connect to corner vertices
	// This handles diagonal walls by computing correct offsets but not cluttering visuals
	int32 CornerCount = 0;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		// Find the next corner vertex (skip collinear vertices)
		int32 NextCornerIdx = (i + 1) % Vertices.Num();
		while (!IsCorner[NextCornerIdx] && NextCornerIdx != i)
		{
			NextCornerIdx = (NextCornerIdx + 1) % Vertices.Num();
		}

		// Only draw from corner vertices
		if (!IsCorner[i])
			continue;

		CornerCount++;

		// --- Floor footprint (green, inset, thick) ---
		FVector FootprintStart = FootprintVertices[i] + FVector(0, 0, ZOffset);
		FVector FootprintEnd = FootprintVertices[NextCornerIdx] + FVector(0, 0, ZOffset);
		RoomFootprintLineComponent->DrawLine(FootprintStart, FootprintEnd, FloorLineColor, DepthPriority, FootprintThickness, LineLifeTime);

		// --- Inner wall boundary (white) ---
		FVector InnerFloorStart = InnerVertices[i] + FVector(0, 0, ZOffset);
		FVector InnerFloorEnd = InnerVertices[NextCornerIdx] + FVector(0, 0, ZOffset);
		FVector InnerTopStart = InnerVertices[i] + FVector(0, 0, WallHeight);
		FVector InnerTopEnd = InnerVertices[NextCornerIdx] + FVector(0, 0, WallHeight);

		// Inner floor edge
		RoomFootprintLineComponent->DrawLine(InnerFloorStart, InnerFloorEnd, WallLineColor, DepthPriority, WireframeThickness, LineLifeTime);
		// Inner top edge
		RoomFootprintLineComponent->DrawLine(InnerTopStart, InnerTopEnd, WallLineColor, DepthPriority, WireframeThickness, LineLifeTime);
		// Inner vertical corner
		RoomFootprintLineComponent->DrawLine(InnerFloorStart, InnerTopStart, WallLineColor, DepthPriority, WireframeThickness, LineLifeTime);

		// --- Outer wall boundary (white) ---
		FVector OuterFloorStart = OuterVertices[i] + FVector(0, 0, ZOffset);
		FVector OuterFloorEnd = OuterVertices[NextCornerIdx] + FVector(0, 0, ZOffset);
		FVector OuterTopStart = OuterVertices[i] + FVector(0, 0, WallHeight);
		FVector OuterTopEnd = OuterVertices[NextCornerIdx] + FVector(0, 0, WallHeight);

		// Outer floor edge
		RoomFootprintLineComponent->DrawLine(OuterFloorStart, OuterFloorEnd, WallLineColor, DepthPriority, WireframeThickness, LineLifeTime);
		// Outer top edge
		RoomFootprintLineComponent->DrawLine(OuterTopStart, OuterTopEnd, WallLineColor, DepthPriority, WireframeThickness, LineLifeTime);
		// Outer vertical corner
		RoomFootprintLineComponent->DrawLine(OuterFloorStart, OuterTopStart, WallLineColor, DepthPriority, WireframeThickness, LineLifeTime);
	}

	// --- Portal cutout outlines (black) ---
	// Find walls that border this room and draw rectangles around any portal cutouts
	const FColor PortalLineColor = FColor::Black;
	const float PortalLineThickness = 4.0f;
	int32 PortalCount = 0;

	if (CurrentLot && CurrentLot->WallGraph && CurrentLot->WallComponent)
	{
		// Iterate through all walls to find those bordering this room
		for (const FWallSegmentData& Wall : CurrentLot->WallComponent->WallDataArray)
		{
			if (!Wall.bCommitted || Wall.WallEdgeID == -1)
				continue;

			// Check if this wall borders the selected room
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(Wall.WallEdgeID);
			if (!Edge || (Edge->Room1 != RoomID && Edge->Room2 != RoomID))
				continue;

		// This wall borders our room - draw outlines for any portals
			for (APortalBase* Portal : Wall.PortalArray)
			{
				if (!Portal)
					continue;

				// Only draw outline when the portal mesh is hidden
				if (!Portal->IsHidden())
					continue;

				// Determine portal type
				bool bIsDoor = Cast<ADoorBase>(Portal) != nullptr;
				bool bIsWindow = Cast<AWindowBase>(Portal) != nullptr;

				// Get portal dimensions, location, and offset
				FVector PortalLocation = Portal->GetActorLocation();
				float PortalWidth = Portal->PortalSize.X;
				float PortalHeight = Portal->PortalSize.Y;
				FVector2D PortalOffset = Portal->PortalOffset;

				// Calculate portal corners in local space then transform to world
				FVector WallDir = (Wall.EndLoc - Wall.StartLoc).GetSafeNormal();
				FVector UpDir = FVector::UpVector;

				// Apply portal offset (X = horizontal along wall, Y = vertical)
				FVector CutoutCenter = PortalLocation + (WallDir * PortalOffset.X) + (UpDir * PortalOffset.Y);

				float HalfWidth = PortalWidth * 0.5f;
				float HalfHeight = PortalHeight * 0.5f;

				// Frame thickness for the outline visualization
				const float FrameThickness = 15.0f;
				float InnerHalfWidth = HalfWidth - FrameThickness;
				float InnerHalfHeight = HalfHeight - FrameThickness;

				// Outer rectangle corners (frame outer edge)
				FVector OuterBottomLeft = CutoutCenter + (-WallDir * HalfWidth) + (-UpDir * HalfHeight);
				FVector OuterBottomRight = CutoutCenter + (WallDir * HalfWidth) + (-UpDir * HalfHeight);
				FVector OuterTopRight = CutoutCenter + (WallDir * HalfWidth) + (UpDir * HalfHeight);
				FVector OuterTopLeft = CutoutCenter + (-WallDir * HalfWidth) + (UpDir * HalfHeight);

				// Inner rectangle corners (frame inner edge / opening)
				FVector InnerBottomLeft = CutoutCenter + (-WallDir * InnerHalfWidth) + (-UpDir * InnerHalfHeight);
				FVector InnerBottomRight = CutoutCenter + (WallDir * InnerHalfWidth) + (-UpDir * InnerHalfHeight);
				FVector InnerTopRight = CutoutCenter + (WallDir * InnerHalfWidth) + (UpDir * InnerHalfHeight);
				FVector InnerTopLeft = CutoutCenter + (-WallDir * InnerHalfWidth) + (UpDir * InnerHalfHeight);

				// Draw outer rectangle (all 4 sides)
				RoomFootprintLineComponent->DrawLine(OuterBottomLeft, OuterBottomRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
				RoomFootprintLineComponent->DrawLine(OuterBottomRight, OuterTopRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
				RoomFootprintLineComponent->DrawLine(OuterTopRight, OuterTopLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
				RoomFootprintLineComponent->DrawLine(OuterTopLeft, OuterBottomLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);

				// Draw inner rectangle and frame connectors based on portal type
				if (bIsDoor)
				{
					// Door: No bottom frame (opens to floor), draw 3 sides of inner
					FVector DoorInnerBottomLeft = CutoutCenter + (-WallDir * InnerHalfWidth) + (-UpDir * HalfHeight);
					FVector DoorInnerBottomRight = CutoutCenter + (WallDir * InnerHalfWidth) + (-UpDir * HalfHeight);

					// Inner frame (3 sides - no bottom)
					RoomFootprintLineComponent->DrawLine(InnerTopLeft, InnerTopRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerTopLeft, DoorInnerBottomLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerTopRight, DoorInnerBottomRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);

					// Frame thickness connectors (top corners only)
					RoomFootprintLineComponent->DrawLine(OuterTopLeft, InnerTopLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(OuterTopRight, InnerTopRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
				}
				else if (bIsWindow)
				{
					// Window: Full inner rectangle + corner connectors + center mullion cross
					// Inner rectangle (all 4 sides)
					RoomFootprintLineComponent->DrawLine(InnerBottomLeft, InnerBottomRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerBottomRight, InnerTopRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerTopRight, InnerTopLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerTopLeft, InnerBottomLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);

					// Frame thickness connectors (all 4 corners)
					RoomFootprintLineComponent->DrawLine(OuterBottomLeft, InnerBottomLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(OuterBottomRight, InnerBottomRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(OuterTopRight, InnerTopRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(OuterTopLeft, InnerTopLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);

					// Center mullion cross (distinguishes window from door)
					FVector MullionLeft = CutoutCenter + (-WallDir * InnerHalfWidth);
					FVector MullionRight = CutoutCenter + (WallDir * InnerHalfWidth);
					FVector MullionBottom = CutoutCenter + (-UpDir * InnerHalfHeight);
					FVector MullionTop = CutoutCenter + (UpDir * InnerHalfHeight);
					RoomFootprintLineComponent->DrawLine(MullionLeft, MullionRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(MullionBottom, MullionTop, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
				}
				else
				{
					// Unknown portal type: Just draw inner rectangle + corner connectors
					RoomFootprintLineComponent->DrawLine(InnerBottomLeft, InnerBottomRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerBottomRight, InnerTopRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerTopRight, InnerTopLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(InnerTopLeft, InnerBottomLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);

					RoomFootprintLineComponent->DrawLine(OuterBottomLeft, InnerBottomLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(OuterBottomRight, InnerBottomRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(OuterTopRight, InnerTopRight, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
					RoomFootprintLineComponent->DrawLine(OuterTopLeft, InnerTopLeft, PortalLineColor, DepthPriority, PortalLineThickness, LineLifeTime);
				}

				PortalCount++;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SelectionTool: Drew 3D wireframe for room %d (%d corners from %d vertices, wall height: %.1f, thickness: %.1f, %d portals)"), RoomID, CornerCount, Vertices.Num(), WallHeight, WallThickness, PortalCount);
}

void ASelectionTool::ClearRoomFootprintLines()
{
	if (RoomFootprintLineComponent)
	{
		RoomFootprintLineComponent->Flush();
	}
}

void ASelectionTool::RefreshRoomSelection()
{
	// Redraw room footprint if a room is selected (updates portal outline visibility)
	if (SelectedRoomID > 0)
	{
		DrawRoomFootprintLines(SelectedRoomID);
	}
}

void ASelectionTool::ShowRoomControlWidget(const FVector& Location)
{
	// Don't show if no widget class is assigned
	if (!RoomControlWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("SelectionTool: RoomControlWidgetClass is not set"));
		return;
	}

	// Create widget component if it doesn't exist
	if (!RoomControlWidgetComponent)
	{
		// Create as a component of the LotManager so it doesn't follow the tool
		AActor* WidgetOwner = CurrentLot ? Cast<AActor>(CurrentLot) : this;
		RoomControlWidgetComponent = NewObject<UWidgetComponent>(WidgetOwner, TEXT("RoomControlWidget"));
		if (RoomControlWidgetComponent)
		{
			RoomControlWidgetComponent->RegisterComponent();

			// Don't attach to anything - use absolute world positioning
			RoomControlWidgetComponent->SetAbsolute(true, true, true);

			// Configure widget component for screen-space UI
			RoomControlWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
			RoomControlWidgetComponent->SetDrawAtDesiredSize(true);
			RoomControlWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
		}
	}

	if (RoomControlWidgetComponent)
	{
		// Set widget class and location
		RoomControlWidgetComponent->SetWidgetClass(RoomControlWidgetClass);
		RoomControlWidgetComponent->SetWorldLocation(Location);
		RoomControlWidgetComponent->SetVisibility(true);

		UE_LOG(LogTemp, Log, TEXT("SelectionTool: Showing room control widget at %s"), *Location.ToString());
	}
}

void ASelectionTool::HideRoomControlWidget()
{
	if (RoomControlWidgetComponent)
	{
		RoomControlWidgetComponent->SetVisibility(false);
		UE_LOG(LogTemp, Log, TEXT("SelectionTool: Hiding room control widget"));
	}
}

int32 ASelectionTool::GetRoomFromWallClick(const FHitResult& HitResult)
{
	if (!CurrentLot || !CurrentLot->WallGraph || !CurrentLot->WallComponent)
		return 0;

	// Find the closest wall using point-to-line-segment distance
	// This correctly handles adjacent walls and shared endpoints by measuring
	// distance to the actual wall segment, not the center or bounding box
	int32 WallArrayIndex = -1;
	float ClosestDistance = FLT_MAX;

	for (int32 i = 0; i < CurrentLot->WallComponent->WallDataArray.Num(); ++i)
	{
		const FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[i];

		// Skip uncommitted walls or walls without edge IDs
		if (!Wall.bCommitted || Wall.WallEdgeID == -1)
		{
			continue;
		}

		// CRITICAL: Filter by current level to avoid selecting walls on other floors!
		// This was the root cause of BURB-35 - walls on other floors could be closer in 3D space
		if (Wall.Level != CurrentTracedLevel)
		{
			continue;
		}

		// Calculate distance from hit point to wall LINE SEGMENT (not center!)
		// Use 2D distance (XY plane) since walls on the same level may have different Z heights
		FVector HitLoc2D = FVector(HitResult.Location.X, HitResult.Location.Y, 0.0f);
		FVector WallStart2D = FVector(Wall.StartLoc.X, Wall.StartLoc.Y, 0.0f);
		FVector WallEnd2D = FVector(Wall.EndLoc.X, Wall.EndLoc.Y, 0.0f);
		
		FVector ClosestPointOnSegment = FMath::ClosestPointOnSegment(HitLoc2D, WallStart2D, WallEnd2D);
		float Distance = FVector::Dist(HitLoc2D, ClosestPointOnSegment);

		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			WallArrayIndex = i;
		}
	}

	if (WallArrayIndex < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SelectionTool: No wall found at click location (Level=%d, HitLoc=%s)"), 
			CurrentTracedLevel, *HitResult.Location.ToString());
		return 0;
	}
	
	UE_LOG(LogTemp, Log, TEXT("SelectionTool: Found closest wall at index %d (distance=%.2f, level=%d)"), 
		WallArrayIndex, ClosestDistance, CurrentTracedLevel);

	// Validate the wall data
	if (!CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallArrayIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SelectionTool: Invalid wall array index %d"), WallArrayIndex);
		return 0;
	}

	const FWallSegmentData& ClickedWall = CurrentLot->WallComponent->WallDataArray[WallArrayIndex];
	if (!ClickedWall.bCommitted || ClickedWall.WallEdgeID == -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("SelectionTool: Wall at index %d is uncommitted or has no EdgeID"), WallArrayIndex);
		return 0;
	}

	// Get the wall edge ID
	int32 EdgeID = ClickedWall.WallEdgeID;

	// Get the edge from wall graph
	const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(EdgeID);
	if (!Edge)
	{
		UE_LOG(LogTemp, Warning, TEXT("SelectionTool: Edge %d not found in wall graph"), EdgeID);
		return 0;
	}

	UE_LOG(LogTemp, Log, TEXT("SelectionTool: Edge %d has Room1=%d, Room2=%d"), EdgeID, Edge->Room1, Edge->Room2);

	// Determine which side of the wall was clicked
	FVector WallDirection = (ClickedWall.EndLoc - ClickedWall.StartLoc).GetSafeNormal();
	FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();
	FVector WallCenter = (ClickedWall.StartLoc + ClickedWall.EndLoc) * 0.5f;
	FVector ToHitPoint = HitResult.Location - WallCenter;
	ToHitPoint.Z = 0.0f; // 2D test

	float DotProduct = FVector::DotProduct(ToHitPoint, WallNormal);
	bool bClickedOnRoom1Side = (DotProduct > 0.0f); // Room1 is on +normal side

	UE_LOG(LogTemp, Log, TEXT("SelectionTool: WallDir=%s, WallNormal=%s, ToHit=%s, Dot=%.4f, ClickedRoom1Side=%s"),
		*WallDirection.ToString(), *WallNormal.ToString(), *ToHitPoint.ToString(), 
		DotProduct, bClickedOnRoom1Side ? TEXT("true") : TEXT("false"));

	// Try clicked side first
	int32 ClickedSideRoom = bClickedOnRoom1Side ? Edge->Room1 : Edge->Room2;
	if (ClickedSideRoom > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SelectionTool: Returning clicked side room %d"), ClickedSideRoom);
		return ClickedSideRoom;
	}

	// Try other side if clicked side is exterior
	int32 OtherSideRoom = bClickedOnRoom1Side ? Edge->Room2 : Edge->Room1;
	if (OtherSideRoom > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SelectionTool: Clicked side is exterior, returning other side room %d"), OtherSideRoom);
		return OtherSideRoom;
	}

	// Both sides are exterior (Room1=0, Room2=0)
	UE_LOG(LogTemp, Warning, TEXT("SelectionTool: Wall edge %d has no rooms on either side (exterior wall)"), EdgeID);
	return 0;
}

void ASelectionTool::OnSelectedSection_Implementation(const FComponentData& ComponentData)
{
}
void ASelectionTool::OnReleasedSection_Implementation()
{
}

void ASelectionTool::ServerDelete_Implementation()
{
	// If we have a selected component in edit mode, delete it directly
	if (SelectedComponent.ComponentType != ESectionComponentType::UnknownComponent)
	{
		// Check if the selected object is in edit mode
		bool bIsInEditMode = false;

		if (SelectedComponent.ComponentType == ESectionComponentType::StairsComponent && SelectedComponent.HitStairsActor)
		{
			bIsInEditMode = SelectedComponent.HitStairsActor->bInEditMode;
		}
		else if (SelectedComponent.ComponentType == ESectionComponentType::RoofComponent && SelectedComponent.HitRoofActor)
		{
			bIsInEditMode = SelectedComponent.HitRoofActor->bInEditMode;
		}

		// If in edit mode, delete the object directly
		if (bIsInEditMode)
		{
			DeleteSection(SelectedComponent);
			UE_LOG(LogTemp, Log, TEXT("SelectionTool: Deleted selected object in edit mode"));
			return;
		}
	}

	// Otherwise, use default behavior (switch to deletion tool)
	Super::ServerDelete_Implementation();
}

void ASelectionTool::RotateLeft_Implementation()
{
	// Use cached reference from BurbPawn instead of iterating all actors
	if (CurrentPlayerPawn && CurrentPlayerPawn->CurrentEditModeActor)
	{
		// Try stairs
		if (AStairsBase* Stairs = Cast<AStairsBase>(CurrentPlayerPawn->CurrentEditModeActor))
		{
			if (Stairs->IsSelected_Implementation())
			{
				Stairs->RotateLeft();
				UE_LOG(LogTemp, Log, TEXT("SelectionTool: Rotated selected stairs left"));
				return;
			}
		}

		// Try roof
		if (ARoofBase* Roof = Cast<ARoofBase>(CurrentPlayerPawn->CurrentEditModeActor))
		{
			if (Roof->IsSelected_Implementation())
			{
				Roof->RotateLeft();
				Roof->DrawFootprintLines();
				UE_LOG(LogTemp, Log, TEXT("SelectionTool: Rotated selected roof left"));
				return;
			}
		}
	}

	// Call base implementation if no object is selected
	Super::RotateLeft_Implementation();
}

void ASelectionTool::RotateRight_Implementation()
{
	// Use cached reference from BurbPawn instead of iterating all actors
	if (CurrentPlayerPawn && CurrentPlayerPawn->CurrentEditModeActor)
	{
		// Try stairs
		if (AStairsBase* Stairs = Cast<AStairsBase>(CurrentPlayerPawn->CurrentEditModeActor))
		{
			if (Stairs->IsSelected_Implementation())
			{
				Stairs->RotateRight();
				UE_LOG(LogTemp, Log, TEXT("SelectionTool: Rotated selected stairs right"));
				return;
			}
		}

		// Try roof
		if (ARoofBase* Roof = Cast<ARoofBase>(CurrentPlayerPawn->CurrentEditModeActor))
		{
			if (Roof->IsSelected_Implementation())
			{
				Roof->RotateRight();
				Roof->DrawFootprintLines();
				UE_LOG(LogTemp, Log, TEXT("SelectionTool: Rotated selected roof right"));
				return;
			}
		}
	}

	// Call base implementation if no object is selected
	Super::RotateRight_Implementation();
}

// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuildTools/BuildFloorTool.h"

#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"
#include "Data/FloorPattern.h"
#include "Data/TileTriangleData.h"
#include "Components/RoomManagerComponent.h"
#include "Components/WallGraphComponent.h"
#include "Net/UnrealNetwork.h"


// Sets default values
ABuildFloorTool::ABuildFloorTool(): PreviewFloorMesh(nullptr)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void ABuildFloorTool::BeginPlay()
{
	Super::BeginPlay();
}

void ABuildFloorTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Log, TEXT("BuildFloorTool::EndPlay - Cleaning up tool"));

	// Clean up DragCreateFloorArray components
	for (UFloorComponent* FloorComp : DragCreateFloorArray)
	{
		if (FloorComp && IsValid(FloorComp))
		{
			FloorComp->DestroyFloor();
		}
	}
	DragCreateFloorArray.Empty();

	// Clean up preview cache when tool is destroyed
	ClearPreviewCache();

	// Clean up room preview when tool is destroyed
	ClearRoomPreview();

	// Clean up existing tile preview when tool is destroyed
	ClearExistingTilePreview();

	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABuildFloorTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildFloorTool::CreateFloorPreview(int32 Level, FVector TileCenter, FTileSectionState TileSectionState, UMaterialInstance* OptionalMaterial = nullptr)
{
	// Create patterned material for preview if pattern is available
	UMaterialInstance* PreviewMaterial = OptionalMaterial ? OptionalMaterial : ValidMaterial;
	if (BaseMaterial && DefaultFloorPattern && !OptionalMaterial)
	{
		PreviewMaterial = CreatePatternedMaterial(BaseMaterial, DefaultFloorPattern);
	}

	// Apply Z-offset to prevent Z-fighting with terrain/floors
	FVector PreviewLocation = TileCenter;
	PreviewLocation.Z += PreviewZOffset;

	// Assuming GenerateFloorSection creates a floor segment based on the state provided
	DragCreateFloorArray.Add(CurrentLot->GenerateFloorSegment(Level, PreviewLocation, PreviewMaterial, TileSectionState));
	bLockToForwardAxis = true;
}

ETriangleSelection ABuildFloorTool::DetermineTriangleFromCursorPosition(const FVector& TileCenter, const FVector& CursorLocation)
{
	// Calculate vector from tile center to cursor (in 2D - ignore Z)
	FVector2D TileCenter2D(TileCenter.X, TileCenter.Y);
	FVector2D CursorLocation2D(CursorLocation.X, CursorLocation.Y);
	FVector2D DeltaVector = CursorLocation2D - TileCenter2D;

	// If cursor is too close to center, return None
	if (DeltaVector.SizeSquared() < 1.0f)
	{
		return ETriangleSelection::None;
	}

	// Calculate angle from tile center to cursor
	// Unreal uses: 0° = +X (right), 90° = +Y (forward/top), 180° = -X (left), 270° = -Y (back/bottom)
	float AngleDegrees = FMath::Atan2(DeltaVector.Y, DeltaVector.X) * (180.0f / PI);

	// Normalize to 0-360 range
	if (AngleDegrees < 0.0f)
	{
		AngleDegrees += 360.0f;
	}

	// Divide into 4 quadrants (45° offset to align with diagonal divisions)
	// Right: -45 to 45 degrees (315-360 and 0-45)
	// Top: 45 to 135 degrees
	// Left: 135 to 225 degrees
	// Bottom: 225 to 315 degrees

	if (AngleDegrees >= 315.0f || AngleDegrees < 45.0f)
	{
		return ETriangleSelection::Right;
	}
	else if (AngleDegrees >= 45.0f && AngleDegrees < 135.0f)
	{
		return ETriangleSelection::Top;
	}
	else if (AngleDegrees >= 135.0f && AngleDegrees < 225.0f)
	{
		return ETriangleSelection::Left;
	}
	else // 225-315 degrees
	{
		return ETriangleSelection::Bottom;
	}
}

FTileSectionState ABuildFloorTool::CreateSingleTriangleState(ETriangleSelection Triangle)
{
	FTileSectionState State;

	// Initialize all to false
	State.Top = false;
	State.Bottom = false;
	State.Right = false;
	State.Left = false;

	// Set only the selected triangle to true
	switch (Triangle)
	{
		case ETriangleSelection::Top:
			State.Top = true;
			break;
		case ETriangleSelection::Right:
			State.Right = true;
			break;
		case ETriangleSelection::Bottom:
			State.Bottom = true;
			break;
		case ETriangleSelection::Left:
			State.Left = true;
			break;
		case ETriangleSelection::None:
			// All remain false
			break;
	}

	return State;
}

TArray<FTileData> ABuildFloorTool::GetTilesInRoom(int32 RoomID, int32 Level)
{
	TArray<FTileData> RoomTiles;

	// Don't return tiles for outside (RoomID 0)
	if (RoomID == 0 || !CurrentLot || !CurrentLot->RoomManager)
	{
		return RoomTiles;
	}

	// Find the room in RoomManager
	const FRoomData* Room = CurrentLot->RoomManager->Rooms.Find(RoomID);
	if (!Room || Room->Level != Level)
	{
		return RoomTiles;
	}

	// Iterate through all grid tiles at this level and check if they're inside the room polygon
	for (const FTileData& Tile : CurrentLot->GridData)
	{
		if (Tile.Level != Level)
		{
			continue;
		}

		// Check if tile center is inside room boundary polygon
		if (URoomManagerComponent::IsPointInPolygon(Tile.Center, Room->BoundaryVertices))
		{
			RoomTiles.Add(Tile);
		}
	}

	return RoomTiles;
}

void ABuildFloorTool::ShowRoomPreview(int32 RoomID, int32 Level)
{
	// Clear any existing room preview
	ClearRoomPreview();

	// Don't show preview for outside
	if (RoomID == 0 || !CurrentLot || !CurrentLot->RoomManager)
	{
		return;
	}

	// Find the room data
	const FRoomData* Room = CurrentLot->RoomManager->Rooms.Find(RoomID);
	if (!Room || Room->Level != Level || Room->BoundaryVertices.Num() < 3)
	{
		return;
	}

	// Create patterned material for room previews
	UMaterialInstance* RoomPreviewMaterial = ValidMaterial;
	if (BaseMaterial && DefaultFloorPattern)
	{
		RoomPreviewMaterial = CreatePatternedMaterial(BaseMaterial, DefaultFloorPattern);
	}

	// Map to collect which triangles belong to this room per tile
	// Key: (Row, Column), Value: TileSectionState with only room-interior triangles enabled
	TMap<FIntVector2, FTileSectionState> TileTriangleStates;

	// Check each tile in the grid at this level
	for (const FTileData& Tile : CurrentLot->GridData)
	{
		if (Tile.Level != Level)
		{
			continue;
		}

		int32 Row = (int32)Tile.TileCoord.X;
		int32 Column = (int32)Tile.TileCoord.Y;

		// Get tile corners for triangle centroid calculation
		TArray<FVector> CornerLocs = CurrentLot->LocationToAllTileCorners(Tile.Center, Level);
		if (CornerLocs.Num() != 4)
		{
			continue;
		}

		// Corners: [0]=BottomLeft, [1]=BottomRight, [2]=TopLeft, [3]=TopRight
		FVector TileCenter = Tile.Center;

		// Calculate centroid for each triangle and check if it's inside the room
		FTileSectionState State;
		State.Top = false;
		State.Right = false;
		State.Bottom = false;
		State.Left = false;

		// Top triangle: TopLeft, TopRight, Center
		FVector TopCentroid = (CornerLocs[2] + CornerLocs[3] + TileCenter) / 3.0f;
		if (URoomManagerComponent::IsPointInPolygon(TopCentroid, Room->BoundaryVertices))
		{
			State.Top = true;
		}

		// Right triangle: TopRight, BottomRight, Center
		FVector RightCentroid = (CornerLocs[3] + CornerLocs[1] + TileCenter) / 3.0f;
		if (URoomManagerComponent::IsPointInPolygon(RightCentroid, Room->BoundaryVertices))
		{
			State.Right = true;
		}

		// Bottom triangle: BottomRight, BottomLeft, Center
		FVector BottomCentroid = (CornerLocs[1] + CornerLocs[0] + TileCenter) / 3.0f;
		if (URoomManagerComponent::IsPointInPolygon(BottomCentroid, Room->BoundaryVertices))
		{
			State.Bottom = true;
		}

		// Left triangle: BottomLeft, TopLeft, Center
		FVector LeftCentroid = (CornerLocs[0] + CornerLocs[2] + TileCenter) / 3.0f;
		if (URoomManagerComponent::IsPointInPolygon(LeftCentroid, Room->BoundaryVertices))
		{
			State.Left = true;
		}

		// Only add tile if at least one triangle is inside the room
		if (State.Top || State.Right || State.Bottom || State.Left)
		{
			TileTriangleStates.Add(FIntVector2(Row, Column), State);
		}
	}

	// Create preview floors for tiles with triangles in the room
	for (const auto& TilePair : TileTriangleStates)
	{
		int32 Row = TilePair.Key.X;
		int32 Column = TilePair.Key.Y;
		const FTileSectionState& TileSectionState = TilePair.Value;

		// Get tile center location
		FVector TileCenter;
		CurrentLot->TileToGridLocation(Level, Row, Column, true, TileCenter);

		// Apply Z-offset to prevent Z-fighting with terrain/floors
		FVector PreviewLocation = TileCenter;
		PreviewLocation.Z += PreviewZOffset;

		// Create preview floor component with only the triangles inside the room
		UFloorComponent* PreviewFloor = CurrentLot->GenerateFloorSegment(Level, PreviewLocation, RoomPreviewMaterial, TileSectionState);
		if (PreviewFloor)
		{
			RoomPreviewFloorArray.Add(PreviewFloor);
		}
	}

	bShowingRoomPreview = true;
}

void ABuildFloorTool::ClearRoomPreview()
{
	// Destroy all room preview floor components
	for (UFloorComponent* PreviewFloor : RoomPreviewFloorArray)
	{
		if (PreviewFloor && IsValid(PreviewFloor))
		{
			PreviewFloor->DestroyFloor();
		}
	}

	RoomPreviewFloorArray.Empty();
	bShowingRoomPreview = false;
	CurrentRoomID = 0;
}

void ABuildFloorTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	if (!CurrentLot)
	{
		return;
	}

	// Store traced level for use by OnMoved()
	CurrentTracedLevel = TracedLevel;

	int32 outRow=0;
	int32 outColumn=0;
	if (CurrentLot->LocationToTile(MoveLocation, outRow, outColumn))
	{
		CurrentLot->TileToGridLocation(TracedLevel, outRow, outColumn, true, TargetLocation);

		// Update CurrentHoveredTriangle BEFORE OnMoved() so preview uses fresh data
		ETriangleSelection PreviousHoveredTriangle = CurrentHoveredTriangle;

		if (PlacementMode == EFloorPlacementMode::SingleTriangle)
		{
			CurrentHoveredTriangle = DetermineTriangleFromCursorPosition(TargetLocation, MoveLocation);
		}
		else
		{
			CurrentHoveredTriangle = ETriangleSelection::None;
		}

		bool bTileChanged = (GetActorLocation() != TargetLocation);
		bool bTriangleChanged = (PlacementMode == EFloorPlacementMode::SingleTriangle &&
			CurrentHoveredTriangle != PreviousHoveredTriangle &&
			CurrentHoveredTriangle != ETriangleSelection::None);

		if (bTileChanged)
		{
			SetActorLocation(TargetLocation);
			UpdateLocation(GetActorLocation());

			//Tell blueprint children we moved successfully
			OnMoved();

			if (SelectPressed)
			{
				ServerDrag();
			}
			PreviousLocation = GetActorLocation();
		}
		else if (bTriangleChanged)
		{
			// Same tile but different triangle — update preview
			OnMoved();
		}

		// Handle shift key for room fill preview
		// Uses polygon-based room detection (properly handles diagonal walls)
		if (bShiftPressed)
		{
			// Get room ID at cursor position
			int32 CurrentRoomID_Tile = GetRoomIDAtTile(TracedLevel, outRow, outColumn);

			// Only show preview if over a valid room (not outside)
			if (CurrentRoomID_Tile > 0)
			{
				// Determine if we need to refresh the preview
				bool bNeedsRefresh = !bShowingRoomPreview || CurrentRoomID != CurrentRoomID_Tile;

				if (bNeedsRefresh)
				{
					ShowRoomPreview(CurrentRoomID_Tile, TracedLevel);
					CurrentRoomID = CurrentRoomID_Tile;
				}
			}
			else
			{
				// Moved to outside - clear preview
				ClearRoomPreview();
			}
		}
		else
		{
			// Shift released - clear any room preview
			if (bShowingRoomPreview)
			{
				ClearRoomPreview();
			}
		}
	}
}

void ABuildFloorTool::Click_Implementation()
{
	DragCreateVectors = {GetActorLocation(),GetActorLocation()};

	// Use the traced level (from Move) instead of viewing level
	int32 CurrentLevel = CurrentTracedLevel;

	// Get the room ID at the click location for drag boundary checking
	// Uses polygon-based room detection which handles diagonal walls correctly
	int32 ClickRow = 0, ClickColumn = 0;
	if (CurrentLot->LocationToTile(GetActorLocation(), ClickRow, ClickColumn))
	{
		DragStartRoomID = GetRoomIDAtTile(CurrentLevel, ClickRow, ClickColumn);
	}
	else
	{
		DragStartRoomID = 0;
	}

	// If shift is pressed and showing room preview, use room auto-fill mode
	if (bShiftPressed && bShowingRoomPreview)
	{
		// Clear any existing floors in DragCreateFloorArray first
		for (UFloorComponent* ExistingFloor : DragCreateFloorArray)
		{
			if (ExistingFloor)
			{
				ExistingFloor->DestroyFloor();
			}
		}
		DragCreateFloorArray.Empty();
		ClearPreviewCache();

		// Set room fill flag - this prevents drag from adding more floors
		// and ensures BroadcastRelease uses per-triangle processing
		bIsRoomFillOperation = true;

		// Copy room preview floors to DragCreateFloorArray for release handling
		// The room preview floors are already created with correct partial TileSectionState
		for (UFloorComponent* RoomFloor : RoomPreviewFloorArray)
		{
			if (RoomFloor)
			{
				DragCreateFloorArray.Add(RoomFloor);
			}
		}

		// Clear room preview array (but don't destroy floors - they're now in DragCreateFloorArray)
		RoomPreviewFloorArray.Empty();
		bShowingRoomPreview = false;
		CurrentRoomID = 0;
	}
	else
	{
		// Normal single tile placement - reset room fill flag
		bIsRoomFillOperation = false;

		// Determine tile section state based on placement mode
		FTileSectionState TileSectionState;

		if (PlacementMode == EFloorPlacementMode::SingleTriangle && CurrentHoveredTriangle != ETriangleSelection::None)
		{
			// Single triangle mode - only place the hovered triangle
			TileSectionState = CreateSingleTriangleState(CurrentHoveredTriangle);
		}
		else
		{
			// Full tile mode (or SingleTriangle with None fallback) - place all 4 triangles
			TileSectionState = FTileSectionState(); // All triangles enabled by default
		}

		CreateFloorPreview(CurrentLevel, GetActorLocation(), TileSectionState, nullptr);
	}
}

void ABuildFloorTool::Drag_Implementation()
{
	// Skip drag processing for room fill operations - the floors are already set up
	if (bIsRoomFillOperation)
	{
		return;
	}

	// Clear existing tile preview when dragging (we're creating new tiles)
	if (bShowingExistingTilePreview)
	{
		ClearExistingTilePreview();
	}

	//save drag vectors
	DragCreateVectors = {DragCreateVectors.StartOperation, GetActorLocation()};

	// Use the traced level (from Move) instead of viewing level
	// This ensures floors are placed at the correct level when dragging on levels below current view
	int32 CurrentLevel = CurrentTracedLevel;

	FVector SnappedLocation = DragCreateVectors.StartOperation-DragCreateVectors.EndOperation;
	SnappedLocation.Y = FMath::GridSnap(SnappedLocation.Y, CurrentLot->GridTileSize);

	// Create patterned material for drag previews
	UMaterialInstance* DragPreviewMaterial = ValidMaterial;
	if (BaseMaterial && DefaultFloorPattern)
	{
		DragPreviewMaterial = CreatePatternedMaterial(BaseMaterial, DefaultFloorPattern);
	}

	// Build set of current tile locations for this drag operation
	TSet<FVector> CurrentTileSet;

	// Helper lambda to check if a tile should be included based on room boundaries
	// Uses RoomManager polygon check which properly handles diagonal walls
	auto ShouldIncludeTile = [this, CurrentLevel](int32 Row, int32 Column) -> bool
	{
		// If drag started outside all rooms (DragStartRoomID == 0), allow all tiles
		if (DragStartRoomID == 0)
		{
			return true;
		}

		// Check if this tile is in the same room as where the drag started
		int32 TileRoomID = GetRoomIDAtTile(CurrentLevel, Row, Column);
		return TileRoomID == DragStartRoomID;
	};

	// Get the room polygon for triangle centroid checks (if dragging within a room)
	const FRoomData* DragRoom = nullptr;
	if (DragStartRoomID > 0 && CurrentLot->RoomManager)
	{
		DragRoom = CurrentLot->RoomManager->Rooms.Find(DragStartRoomID);
	}

	// Helper lambda to create floor preview at a tile location with per-triangle room membership
	auto CreateFloorPreviewAtTile = [this, CurrentLevel, DragPreviewMaterial, &CurrentTileSet, DragRoom](const FVector& TileCenter, int32 TileRow, int32 TileColumn)
	{
		CurrentTileSet.Add(TileCenter);

		// Always apply Z-offset to prevent Z-fighting (works for terrain AND existing tiles)
		FVector PreviewLocation = TileCenter;
		PreviewLocation.Z += PreviewZOffset;

		// Check if we already have a preview component for this tile
		UFloorComponent** ExistingFloor = PreviewFloorCache.Find(TileCenter);

		if (!ExistingFloor || !*ExistingFloor)
		{
			// Calculate which triangles are inside the room (handles diagonal walls)
			FTileSectionState TileSectionState;

			if (DragRoom && DragRoom->BoundaryVertices.Num() >= 3)
			{
				// Get tile corners for triangle centroid calculation
				TArray<FVector> CornerLocs = CurrentLot->LocationToAllTileCorners(TileCenter, CurrentLevel);

				if (CornerLocs.Num() == 4)
				{
					// Corners: [0]=BottomLeft, [1]=BottomRight, [2]=TopLeft, [3]=TopRight
					TileSectionState.Top = false;
					TileSectionState.Right = false;
					TileSectionState.Bottom = false;
					TileSectionState.Left = false;

					// Top triangle: TopLeft, TopRight, Center
					FVector TopCentroid = (CornerLocs[2] + CornerLocs[3] + TileCenter) / 3.0f;
					if (URoomManagerComponent::IsPointInPolygon(TopCentroid, DragRoom->BoundaryVertices))
					{
						TileSectionState.Top = true;
					}

					// Right triangle: TopRight, BottomRight, Center
					FVector RightCentroid = (CornerLocs[3] + CornerLocs[1] + TileCenter) / 3.0f;
					if (URoomManagerComponent::IsPointInPolygon(RightCentroid, DragRoom->BoundaryVertices))
					{
						TileSectionState.Right = true;
					}

					// Bottom triangle: BottomRight, BottomLeft, Center
					FVector BottomCentroid = (CornerLocs[1] + CornerLocs[0] + TileCenter) / 3.0f;
					if (URoomManagerComponent::IsPointInPolygon(BottomCentroid, DragRoom->BoundaryVertices))
					{
						TileSectionState.Bottom = true;
					}

					// Left triangle: BottomLeft, TopLeft, Center
					FVector LeftCentroid = (CornerLocs[0] + CornerLocs[2] + TileCenter) / 3.0f;
					if (URoomManagerComponent::IsPointInPolygon(LeftCentroid, DragRoom->BoundaryVertices))
					{
						TileSectionState.Left = true;
					}
				}
				else
				{
					// Fallback to full tile if corner lookup failed
					TileSectionState = FTileSectionState();
				}
			}
			else
			{
				// No room constraint (dragging outside) - use full tile
				TileSectionState = FTileSectionState();
			}

			// Only create floor if at least one triangle is inside the room
			if (TileSectionState.Top || TileSectionState.Right || TileSectionState.Bottom || TileSectionState.Left)
			{
				UFloorComponent* NewFloor = CurrentLot->GenerateFloorSegment(CurrentLevel, PreviewLocation, DragPreviewMaterial, TileSectionState);
				PreviewFloorCache.Add(TileCenter, NewFloor);
				DragCreateFloorArray.Add(NewFloor);
			}
		}
	};

	//rectangle
	if (SnappedLocation.Y != 0.0)
	{
		// Calculate drag dimensions
		int32 LastIndexY = FMath::TruncToInt32(FMath::Abs(((DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).Y / CurrentLot->GridTileSize)));
		int32 LastIndexX = FMath::TruncToInt32(FMath::Abs(((DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).X / CurrentLot->GridTileSize)));

		for (int FirstIndexY = 0 ; FirstIndexY<=LastIndexY; FirstIndexY++)
		{
			for (int FirstIndexX = 0 ; FirstIndexX<=LastIndexX; FirstIndexX++)
			{
				FVector StartLocation = FVector(
					DragCreateVectors.StartOperation.X,
					DragCreateVectors.StartOperation.Y + (FMath::Sign<double>((DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).Y) * -1.0f) * (FirstIndexY * CurrentLot->GridTileSize),
					DragCreateVectors.StartOperation.Z);

				FVector DragXDirection = FVector(
					FMath::Sign<double>((DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).X) * -1.0f,
					0,
					(DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).Z);

				FRotator DragRotation = DragXDirection.Rotation();
				FVector TileWorldPos = StartLocation + DragRotation.RotateVector(FVector(FirstIndexX * CurrentLot->GridTileSize, 0.0f, 0.0f));

				// Convert to grid coordinates for room boundary check
				int32 TileRow = 0, TileColumn = 0;
				FVector TileCenter;
				if (CurrentLot->LocationToTile(TileWorldPos, TileRow, TileColumn))
				{
					// Get proper tile center location
					CurrentLot->TileToGridLocation(CurrentLevel, TileRow, TileColumn, true, TileCenter);

					// Check room boundary using RoomManager polygon check (handles diagonals)
					if (ShouldIncludeTile(TileRow, TileColumn))
					{
						CreateFloorPreviewAtTile(TileCenter, TileRow, TileColumn);
					}
				}
			}
		}
	}
	//line
	else
	{
		int32 LastIndex = FMath::TruncToInt32(FMath::Abs(((DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).X / CurrentLot->GridTileSize)));
		for (int FirstIndex = 0 ; FirstIndex<=LastIndex; FirstIndex++)
		{
			FVector DragXDirection = FVector(
				FMath::Sign<double>((DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).X) * -1.0f,
				0,
				(DragCreateVectors.StartOperation - DragCreateVectors.EndOperation).Z);

			FRotator DragRotation = DragXDirection.Rotation();
			// Calculate XY position, then get proper Z from TileToGridLocation (handles basement offset)
			FVector XYPosition = DragCreateVectors.StartOperation + DragRotation.RotateVector(FVector(FirstIndex * CurrentLot->GridTileSize, 0.0f, 0.0f));

			// Convert to tile coordinates and back to get proper Z elevation
			int32 TileRow = 0, TileColumn = 0;
			FVector TileCenter;
			if (CurrentLot->LocationToTile(XYPosition, TileRow, TileColumn))
			{
				CurrentLot->TileToGridLocation(CurrentLevel, TileRow, TileColumn, true, TileCenter);

				// Check room boundary using RoomManager polygon check (handles diagonals)
				if (ShouldIncludeTile(TileRow, TileColumn))
				{
					CreateFloorPreviewAtTile(TileCenter, TileRow, TileColumn);
				}
			}
		}
	}

	// Remove preview components that are no longer in the current selection
	TArray<FVector> TilesToRemove;
	for (auto& Elem : PreviewFloorCache)
	{
		if (!CurrentTileSet.Contains(Elem.Key))
		{
			// This tile is no longer in selection - destroy it
			if (Elem.Value)
			{
				Elem.Value->DestroyFloor();
				DragCreateFloorArray.Remove(Elem.Value);
			}
			TilesToRemove.Add(Elem.Key);
		}
	}

	// Remove destroyed components from cache
	for (const FVector& TileToRemove : TilesToRemove)
	{
		PreviewFloorCache.Remove(TileToRemove);
	}
}

void ABuildFloorTool::BroadcastRelease_Implementation()
{
	if (DragCreateFloorArray.IsValidIndex(0))
	{
		// Use the traced level (from Move/Click/Drag) instead of viewing level
		int32 CurrentLevel = CurrentTracedLevel;

		// Collect floor data from preview components and send via RPC
		// This works on the machine that has the preview data (authority)

		// Collect floors to delete and create
		TArray<int32> DeleteRows;
		TArray<int32> DeleteColumns;
		TArray<FTileSectionState> DeleteExistingStates;

		TArray<FVector> BuildLocations;
		TArray<FTileSectionState> BuildSectionStates;
		TArray<bool> BuildIsUpdateFlags;

		for(UFloorComponent* Floor : DragCreateFloorArray)
		{
			// Get grid coordinates for the floor
			int32 Row, Column;
			if (!CurrentLot->LocationToTile(Floor->FloorData.StartLoc, Row, Column))
			{
				continue; // Invalid location
			}

			if (bDeletionMode)
			{
				// Check if ANY triangle exists at this location (checks all 4 triangles)
				if (CurrentLot->FloorComponent->HasAnyFloorTile(CurrentLevel, Row, Column))
				{
					// Query which triangles actually exist at this location
					FTileSectionState ExistingTriangles = CurrentLot->FloorComponent->GetExistingTriangles(
						CurrentLevel, Row, Column);

					DeleteRows.Add(Row);
					DeleteColumns.Add(Column);
					DeleteExistingStates.Add(ExistingTriangles);
				}
			}
			else
			{
				if (Floor->bValidPlacement)
				{
					// Remove Z-offset to get actual tile location for coordinate lookup
					FVector ActualTileLocation = Floor->FloorData.StartLoc;
					ActualTileLocation.Z -= PreviewZOffset;

					// Get grid coordinates for this floor using actual (non-offset) location
					int32 ActualRow, ActualColumn;
					if (!CurrentLot->LocationToTile(ActualTileLocation, ActualRow, ActualColumn))
					{
						continue; // Invalid location
					}

					// Check which triangles already exist at this location
					FTileSectionState ExistingTriangles = CurrentLot->FloorComponent->GetExistingTriangles(CurrentLevel, ActualRow, ActualColumn);
					const FTileSectionState& DesiredTriangles = Floor->FloorData.TileSectionState;

					// Determine if any of the triangles we want to place already exist
					bool bAnyOverlap = (ExistingTriangles.Top && DesiredTriangles.Top) ||
					                   (ExistingTriangles.Right && DesiredTriangles.Right) ||
					                   (ExistingTriangles.Bottom && DesiredTriangles.Bottom) ||
					                   (ExistingTriangles.Left && DesiredTriangles.Left);

					// For room fill operations, always use per-triangle processing to preserve partial tiles
					// For normal operations, use full tile update if there's overlap in FullTile mode
					bool bIsUpdate = bAnyOverlap && PlacementMode == EFloorPlacementMode::FullTile && !bIsRoomFillOperation;

					BuildLocations.Add(ActualTileLocation);
					BuildSectionStates.Add(DesiredTriangles);
					BuildIsUpdateFlags.Add(bIsUpdate);
				}
			}
		}

		// Send RPCs with collected data (server will multicast to all clients)
		if (DeleteRows.Num() > 0)
		{
			Server_DeleteFloors(CurrentLevel, DeleteRows, DeleteColumns, DeleteExistingStates);
		}

		if (BuildLocations.Num() > 0)
		{
			// Separate updates from creates since they use different BuildServer methods
			TArray<FVector> CreateLocations;
			TArray<FTileSectionState> CreateStates;
			TArray<FVector> UpdateLocations;
			TArray<FTileSectionState> UpdateStates;

			for (int32 i = 0; i < BuildLocations.Num(); i++)
			{
				if (BuildIsUpdateFlags[i])
				{
					UpdateLocations.Add(BuildLocations[i]);
					UpdateStates.Add(BuildSectionStates[i]);
				}
				else
				{
					CreateLocations.Add(BuildLocations[i]);
					CreateStates.Add(BuildSectionStates[i]);
				}
			}

			// Send create floors RPC
			if (CreateLocations.Num() > 0)
			{
				Server_BuildFloors(CurrentLevel, CreateLocations, CreateStates, DefaultFloorPattern, SelectedSwatchIndex, false);
			}

			// Send update floors RPC
			if (UpdateLocations.Num() > 0)
			{
				Server_BuildFloors(CurrentLevel, UpdateLocations, UpdateStates, DefaultFloorPattern, SelectedSwatchIndex, true);
			}
		}

		// Clean up preview components (local cleanup, not replicated)
		for(UFloorComponent* Walls : DragCreateFloorArray)
		{
			if (Walls)
			{
				Walls->DestroyFloor();
			}
		}

		bLockToForwardAxis = false;
		OnReleased();
		DragCreateFloorArray.Empty();

		// Reset room fill flag
		bIsRoomFillOperation = false;

		// Clear the preview cache
		ClearPreviewCache();
	}
}

void ABuildFloorTool::ClearPreviewCache()
{
	// Destroy any remaining preview components in cache
	for (auto& Elem : PreviewFloorCache)
	{
		if (Elem.Value && IsValid(Elem.Value))
		{
			Elem.Value->DestroyFloor();
		}
	}
	PreviewFloorCache.Empty();
}

UMaterialInstanceDynamic* ABuildFloorTool::CreatePatternedMaterial(UMaterialInstance* BaseMaterialTemplate, UFloorPattern* Pattern)
{
	if (!BaseMaterialTemplate)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreatePatternedMaterial: BaseMaterialTemplate is NULL"));
		return nullptr;
	}

	// Create dynamic material instance from the base material
	UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterialTemplate, this);

	if (DynamicMaterial && Pattern)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreatePatternedMaterial: Pattern=%s, bUseColourSwatches=%d, NumSwatches=%d, SelectedSwatchIndex=%d"),
			*Pattern->GetName(), Pattern->bUseColourSwatches, Pattern->ColourSwatches.Num(), SelectedSwatchIndex);

		// Apply pattern textures to the material (matches FloorComponent parameter names)
		if (Pattern->BaseTexture)
		{
			DynamicMaterial->SetTextureParameterValue(FName("FloorMaterial"), Pattern->BaseTexture);
		}
		if (Pattern->NormalMap)
		{
			DynamicMaterial->SetTextureParameterValue(FName("FloorNormal"), Pattern->NormalMap);
		}
		if (Pattern->RoughnessMap)
		{
			DynamicMaterial->SetTextureParameterValue(FName("FloorRoughness"), Pattern->RoughnessMap);
		}

		// Apply color swatch and switches
		// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
		bool bApplySwatch = Pattern->bUseColourSwatches && SelectedSwatchIndex > 0 && Pattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
		DynamicMaterial->SetScalarParameterValue(FName("bUseColourSwatches"), bApplySwatch ? 1.0f : 0.0f);
		DynamicMaterial->SetScalarParameterValue(FName("bUseColourMask"), (bApplySwatch && Pattern->bUseColourMask) ? 1.0f : 0.0f);
		if (bApplySwatch)
		{
			FLinearColor SwatchColor = Pattern->ColourSwatches[SelectedSwatchIndex - 1];
			DynamicMaterial->SetVectorParameterValue(FName("FloorColour"), SwatchColor);
			UE_LOG(LogTemp, Warning, TEXT("CreatePatternedMaterial: Applied swatch color %s at index %d (array index %d)"),
				*SwatchColor.ToString(), SelectedSwatchIndex, SelectedSwatchIndex - 1);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CreatePatternedMaterial: Swatch NOT applied - bUseColourSwatches=%d, SelectedSwatchIndex=%d"),
				Pattern->bUseColourSwatches, SelectedSwatchIndex);
		}

		// Turn off grid for preview (preview shouldn't show grid)
		DynamicMaterial->SetScalarParameterValue(FName("ShowGrid"), 0.0f);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("CreatePatternedMaterial: DynamicMaterial=%p, Pattern=%p"),
			DynamicMaterial, Pattern);
	}

	return DynamicMaterial;
}

void ABuildFloorTool::ShowExistingTilePreview(int32 Level, int32 Row, int32 Column)
{
	if (!CurrentLot || !CurrentLot->FloorComponent)
	{
		return;
	}

	// Check if we're already previewing this exact tile
	if (bShowingExistingTilePreview && PreviewTileRow == Row && PreviewTileColumn == Column && PreviewTileLevel == Level)
	{
		// Already showing preview for this tile, no need to update
		return;
	}

	// If we're already showing a different preview, clear it first
	if (bShowingExistingTilePreview)
	{
		ClearExistingTilePreview();
	}

	// Find the existing tile
	FFloorTileData* ExistingTile = CurrentLot->FloorComponent->FindFloorTile(Level, Row, Column);
	if (!ExistingTile)
	{
		return;
	}

	// Store original pattern for restoration
	OriginalTilePattern = ExistingTile->Pattern;

	// Temporarily update the tile's pattern to the preview pattern
	ExistingTile->Pattern = DefaultFloorPattern;

	// Rebuild this level to show the pattern change
	CurrentLot->FloorComponent->RebuildLevel(Level);

	// Update tracking
	bShowingExistingTilePreview = true;
	PreviewTileRow = Row;
	PreviewTileColumn = Column;
	PreviewTileLevel = Level;
}

void ABuildFloorTool::ClearExistingTilePreview()
{
	if (!bShowingExistingTilePreview || !CurrentLot || !CurrentLot->FloorComponent)
	{
		return;
	}

	// Find the tile we were previewing
	FFloorTileData* ExistingTile = CurrentLot->FloorComponent->FindFloorTile(PreviewTileLevel, PreviewTileRow, PreviewTileColumn);
	if (ExistingTile)
	{
		// Restore original pattern
		ExistingTile->Pattern = OriginalTilePattern;

		// Rebuild this level to restore the original pattern
		CurrentLot->FloorComponent->RebuildLevel(PreviewTileLevel);
	}

	// Reset tracking
	bShowingExistingTilePreview = false;
	PreviewTileRow = -1;
	PreviewTileColumn = -1;
	PreviewTileLevel = -1;
	OriginalTilePattern = nullptr;
}

// ========== Pattern-Region Detection Implementation ==========

UFloorPattern* ABuildFloorTool::GetDominantPatternAtTile(int32 Level, int32 Row, int32 Column)
{
	if (!CurrentLot || !CurrentLot->FloorComponent)
	{
		return nullptr;
	}

	// Get all triangles at this tile location
	TArray<FFloorTileData*> Triangles = CurrentLot->FloorComponent->GetAllTrianglesAtTile(Level, Row, Column);

	if (Triangles.Num() == 0)
	{
		return nullptr; // No floor at this tile
	}

	// Count patterns to find the dominant one
	TMap<UFloorPattern*, int32> PatternCounts;
	for (FFloorTileData* Triangle : Triangles)
	{
		if (Triangle && Triangle->bCommitted)
		{
			int32& Count = PatternCounts.FindOrAdd(Triangle->Pattern);
			Count++;
		}
	}

	// Find the pattern with the highest count
	UFloorPattern* DominantPattern = nullptr;
	int32 MaxCount = 0;
	for (const auto& Pair : PatternCounts)
	{
		if (Pair.Value > MaxCount)
		{
			MaxCount = Pair.Value;
			DominantPattern = Pair.Key;
		}
	}

	return DominantPattern;
}

TArray<FIntVector> ABuildFloorTool::FloodFillPatternRegion(int32 Level, int32 StartRow, int32 StartColumn, UFloorPattern* TargetPattern, int32 RoomID)
{
	TArray<FIntVector> Result;

	if (!CurrentLot || !CurrentLot->FloorComponent || !CurrentLot->RoomManager)
	{
		return Result;
	}

	// BFS flood-fill to find all connected tiles with matching pattern
	TSet<FIntVector> Visited;
	TArray<FIntVector> Queue;

	FIntVector StartTile(StartRow, StartColumn, Level);
	Queue.Add(StartTile);
	Visited.Add(StartTile);

	// 4-way adjacency offsets (Row, Column)
	const FIntVector Offsets[] = {
		FIntVector(1, 0, 0),   // Top (+Row)
		FIntVector(-1, 0, 0),  // Bottom (-Row)
		FIntVector(0, 1, 0),   // Right (+Column)
		FIntVector(0, -1, 0)   // Left (-Column)
	};

	while (Queue.Num() > 0)
	{
		FIntVector Current = Queue[0];
		Queue.RemoveAt(0);

		int32 CurRow = Current.X;
		int32 CurCol = Current.Y;

		// Get the pattern at this tile
		UFloorPattern* CurrentPattern = GetDominantPatternAtTile(Level, CurRow, CurCol);

		// Check if this tile matches our target pattern
		// For nullptr (empty), we check if tile has no floor
		bool bMatches = false;
		if (TargetPattern == nullptr)
		{
			// Looking for empty tiles - check if tile has no floor
			bMatches = !CurrentLot->FloorComponent->HasAnyFloorTile(Level, CurRow, CurCol);
		}
		else
		{
			// Looking for tiles with specific pattern
			bMatches = (CurrentPattern == TargetPattern);
		}

		if (!bMatches)
		{
			continue; // This tile doesn't match, skip it
		}

		// Verify tile is still in the same room (check using polygon test)
		FVector TileCenter;
		CurrentLot->TileToGridLocation(Level, CurRow, CurCol, true, TileCenter);

		int32 TileRoomID = 0;
		for (const auto& RoomPair : CurrentLot->RoomManager->Rooms)
		{
			const FRoomData& Room = RoomPair.Value;
			if (Room.Level != Level || Room.BoundaryVertices.Num() < 3)
			{
				continue;
			}
			if (URoomManagerComponent::IsPointInPolygon(TileCenter, Room.BoundaryVertices))
			{
				TileRoomID = Room.RoomID;
				break;
			}
		}

		// If RoomID constraint is provided (> 0), check that tile is in the same room
		if (RoomID > 0 && TileRoomID != RoomID)
		{
			continue; // Tile is in a different room, don't cross room boundaries
		}

		// This tile is valid - add it to result
		Result.Add(FIntVector(CurRow, CurCol, Level));

		// Add neighbors to queue
		for (const FIntVector& Offset : Offsets)
		{
			FIntVector Neighbor(CurRow + Offset.X, CurCol + Offset.Y, Level);

			// Skip if already visited
			if (Visited.Contains(Neighbor))
			{
				continue;
			}

			// Validate neighbor is within grid bounds
			if (Neighbor.X < 0 || Neighbor.X >= CurrentLot->GridSizeY ||
				Neighbor.Y < 0 || Neighbor.Y >= CurrentLot->GridSizeX)
			{
				continue;
			}

			Visited.Add(Neighbor);
			Queue.Add(Neighbor);
		}
	}

	return Result;
}

void ABuildFloorTool::ShowPatternRegionPreview(const TArray<FIntVector>& TileCoords, int32 Level)
{
	// Clear any existing room preview
	ClearRoomPreview();

	if (TileCoords.Num() == 0)
	{
		return;
	}

	// Create patterned material for region previews
	UMaterialInstance* RegionPreviewMaterial = ValidMaterial;
	if (BaseMaterial && DefaultFloorPattern)
	{
		RegionPreviewMaterial = CreatePatternedMaterial(BaseMaterial, DefaultFloorPattern);
	}

	// Create preview floor for each tile in the region
	for (const FIntVector& TileCoord : TileCoords)
	{
		int32 Row = TileCoord.X;
		int32 Column = TileCoord.Y;

		// Get tile center location
		FVector TileCenter;
		CurrentLot->TileToGridLocation(Level, Row, Column, true, TileCenter);

		// Determine TileSectionState based on existing floor or default to full tile
		FTileSectionState TileSectionState;
		FFloorTileData* ExistingTile = CurrentLot->FloorComponent->FindFloorTile(Level, Row, Column);
		if (ExistingTile)
		{
			// Use existing floor's section state
			TileSectionState = ExistingTile->TileSectionState;
		}
		else
		{
			// Default to full tile (all triangles visible)
			TileSectionState = FTileSectionState();
		}

		// Apply Z-offset to prevent Z-fighting
		FVector PreviewLocation = TileCenter;
		PreviewLocation.Z += PreviewZOffset;

		// Create preview floor component
		UFloorComponent* PreviewFloor = CurrentLot->GenerateFloorSegment(Level, PreviewLocation, RegionPreviewMaterial, TileSectionState);
		if (PreviewFloor)
		{
			RoomPreviewFloorArray.Add(PreviewFloor);
		}
	}

	bShowingRoomPreview = true;
	// Note: CurrentRoomID may not be accurate for pattern regions spanning multiple rooms
	// but it's not critical for the preview functionality
}

int32 ABuildFloorTool::GetRoomIDAtTile(int32 Level, int32 Row, int32 Column)
{
	if (!CurrentLot || !CurrentLot->RoomManager)
	{
		return 0;
	}

	// Get tile center in world coordinates
	FVector TileCenter;
	CurrentLot->TileToGridLocation(Level, Row, Column, true, TileCenter);

	// Check all rooms using polygon test (handles diagonal walls correctly)
	for (const auto& RoomPair : CurrentLot->RoomManager->Rooms)
	{
		const FRoomData& Room = RoomPair.Value;
		if (Room.Level != Level || Room.BoundaryVertices.Num() < 3)
		{
			continue;
		}

		if (URoomManagerComponent::IsPointInPolygon(TileCenter, Room.BoundaryVertices))
		{
			return Room.RoomID;
		}
	}

	return 0; // Outside all rooms
}

// ========== Triangle-Level Pattern Detection Implementation ==========

TArray<FTriangleCoord> ABuildFloorTool::GetAdjacentTriangles(const FTriangleCoord& Coord)
{
	TArray<FTriangleCoord> Adjacent;
	Adjacent.Reserve(3);

	// Each triangle has:
	// - 2 same-tile neighbors (share center vertex)
	// - 1 cross-tile neighbor (share boundary edge)

	switch (Coord.Triangle)
	{
	case ETriangleType::Top:
		// Same-tile: Right and Left (share center)
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Right));
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Left));
		// Cross-tile: Bottom of tile above (Row+1)
		Adjacent.Add(FTriangleCoord(Coord.Row + 1, Coord.Column, Coord.Level, ETriangleType::Bottom));
		break;

	case ETriangleType::Right:
		// Same-tile: Top and Bottom (share center)
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Top));
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Bottom));
		// Cross-tile: Left of tile to the right (Column+1)
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column + 1, Coord.Level, ETriangleType::Left));
		break;

	case ETriangleType::Bottom:
		// Same-tile: Right and Left (share center)
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Right));
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Left));
		// Cross-tile: Top of tile below (Row-1)
		Adjacent.Add(FTriangleCoord(Coord.Row - 1, Coord.Column, Coord.Level, ETriangleType::Top));
		break;

	case ETriangleType::Left:
		// Same-tile: Top and Bottom (share center)
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Top));
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Bottom));
		// Cross-tile: Right of tile to the left (Column-1)
		Adjacent.Add(FTriangleCoord(Coord.Row, Coord.Column - 1, Coord.Level, ETriangleType::Right));
		break;
	}

	return Adjacent;
}

bool ABuildFloorTool::IsWallBlockingTriangles(const FTriangleCoord& From, const FTriangleCoord& To) const
{
	if (!CurrentLot || !CurrentLot->WallGraph)
	{
		return false;
	}

	bool bSameTile = (From.Row == To.Row && From.Column == To.Column && From.Level == To.Level);

	if (bSameTile)
	{
		// Same tile - check for diagonal walls that separate these triangles
		TArray<int32> EdgeIDs = CurrentLot->WallGraph->GetEdgesInTile(From.Row, From.Column, From.Level);

		for (int32 EdgeID : EdgeIDs)
		{
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(EdgeID);
			if (!Edge || !Edge->IsDiagonal())
			{
				continue;
			}

			// Determine diagonal direction based on node positions
			// For tile at (Row, Column):
			// - BottomLeft corner: (Row, Column)
			// - BottomRight corner: (Row, Column+1)
			// - TopLeft corner: (Row+1, Column)
			// - TopRight corner: (Row+1, Column+1)

			int32 TileRow = From.Row;
			int32 TileCol = From.Column;

			// Check if this diagonal goes TL→BR or TR→BL
			bool bIsTLtoBR = (Edge->StartRow == TileRow + 1 && Edge->StartColumn == TileCol &&
			                  Edge->EndRow == TileRow && Edge->EndColumn == TileCol + 1) ||
			                 (Edge->StartRow == TileRow && Edge->StartColumn == TileCol + 1 &&
			                  Edge->EndRow == TileRow + 1 && Edge->EndColumn == TileCol);

			bool bIsTRtoBL = (Edge->StartRow == TileRow + 1 && Edge->StartColumn == TileCol + 1 &&
			                  Edge->EndRow == TileRow && Edge->EndColumn == TileCol) ||
			                 (Edge->StartRow == TileRow && Edge->StartColumn == TileCol &&
			                  Edge->EndRow == TileRow + 1 && Edge->EndColumn == TileCol + 1);

			if (bIsTLtoBR)
			{
				// TL→BR diagonal separates (Top+Right) from (Bottom+Left)
				bool bFromInTopRight = (From.Triangle == ETriangleType::Top || From.Triangle == ETriangleType::Right);
				bool bToInTopRight = (To.Triangle == ETriangleType::Top || To.Triangle == ETriangleType::Right);

				// If one is in TopRight group and other is in BottomLeft group, wall blocks
				if (bFromInTopRight != bToInTopRight)
				{
					return true;
				}
			}
			else if (bIsTRtoBL)
			{
				// TR→BL diagonal separates (Top+Left) from (Bottom+Right)
				bool bFromInTopLeft = (From.Triangle == ETriangleType::Top || From.Triangle == ETriangleType::Left);
				bool bToInTopLeft = (To.Triangle == ETriangleType::Top || To.Triangle == ETriangleType::Left);

				// If one is in TopLeft group and other is in BottomRight group, wall blocks
				if (bFromInTopLeft != bToInTopLeft)
				{
					return true;
				}
			}
		}

		return false; // No diagonal wall blocking these triangles
	}
	else
	{
		// Different tiles - check for edge wall between them
		return CurrentLot->WallGraph->IsWallBetweenTiles(From.Row, From.Column, To.Row, To.Column, From.Level);
	}
}

int32 ABuildFloorTool::CountPatternsInRoom(int32 RoomID, int32 Level) const
{
	if (!CurrentLot || !CurrentLot->FloorComponent || !CurrentLot->RoomManager)
	{
		return 0;
	}

	// Get all tiles in the room using polygon-based detection
	TArray<FTileData> RoomTiles = const_cast<ABuildFloorTool*>(this)->GetTilesInRoom(RoomID, Level);

	if (RoomTiles.Num() == 0)
	{
		return 0;
	}

	// Collect all distinct patterns from floor triangles in this room
	TSet<UFloorPattern*> DistinctPatterns;

	for (const FTileData& Tile : RoomTiles)
	{
		int32 Row = (int32)Tile.TileCoord.X;
		int32 Column = (int32)Tile.TileCoord.Y;

		// Check all 4 triangles at this tile
		TArray<ETriangleType> TriangleTypes = { ETriangleType::Top, ETriangleType::Right, ETriangleType::Bottom, ETriangleType::Left };
		for (ETriangleType TriType : TriangleTypes)
		{
			FFloorTileData* FloorTile = CurrentLot->FloorComponent->FindFloorTile(Level, Row, Column, TriType);
			if (FloorTile && FloorTile->bCommitted)
			{
				DistinctPatterns.Add(FloorTile->Pattern);

				// Early exit if we already found 2+ patterns
				if (DistinctPatterns.Num() >= 2)
				{
					return DistinctPatterns.Num();
				}
			}
		}
	}

	return DistinctPatterns.Num();
}

UFloorPattern* ABuildFloorTool::GetPatternAtTriangle(int32 Level, int32 Row, int32 Column, ETriangleType Triangle)
{
	if (!CurrentLot || !CurrentLot->FloorComponent)
	{
		return nullptr;
	}

	// Find the specific triangle
	FFloorTileData* TileData = CurrentLot->FloorComponent->FindFloorTile(Level, Row, Column, Triangle);
	if (TileData && TileData->bCommitted)
	{
		return TileData->Pattern;
	}

	return nullptr;
}

int32 ABuildFloorTool::GetRoomIDAtTriangle(int32 Level, int32 Row, int32 Column, ETriangleType Triangle)
{
	if (!CurrentLot || !CurrentLot->RoomManager)
	{
		return 0;
	}

	// Use the triangle-first spatial lookup (O(1))
	int32 RoomID = CurrentLot->RoomManager->GetRoomAtTriangleCoords(Row, Column, Level, Triangle);
	if (RoomID > 0)
	{
		return RoomID;
	}

	// Fallback: Use triangle centroid with polygon test
	// Calculate triangle centroid position
	FVector TileCenter;
	CurrentLot->TileToGridLocation(Level, Row, Column, true, TileCenter);
	TArray<FVector> CornerLocs = CurrentLot->LocationToAllTileCorners(TileCenter, Level);

	if (CornerLocs.Num() != 4)
	{
		return 0;
	}

	// Calculate centroid based on triangle type
	// Corners: [0]=BottomLeft, [1]=BottomRight, [2]=TopLeft, [3]=TopRight
	FVector TriangleCentroid;
	switch (Triangle)
	{
	case ETriangleType::Top:
		TriangleCentroid = (CornerLocs[2] + CornerLocs[3] + TileCenter) / 3.0f;
		break;
	case ETriangleType::Right:
		TriangleCentroid = (CornerLocs[3] + CornerLocs[1] + TileCenter) / 3.0f;
		break;
	case ETriangleType::Bottom:
		TriangleCentroid = (CornerLocs[1] + CornerLocs[0] + TileCenter) / 3.0f;
		break;
	case ETriangleType::Left:
		TriangleCentroid = (CornerLocs[0] + CornerLocs[2] + TileCenter) / 3.0f;
		break;
	default:
		return 0;
	}

	// Check all rooms using polygon test with triangle centroid
	for (const auto& RoomPair : CurrentLot->RoomManager->Rooms)
	{
		const FRoomData& Room = RoomPair.Value;
		if (Room.Level != Level || Room.BoundaryVertices.Num() < 3)
		{
			continue;
		}

		if (URoomManagerComponent::IsPointInPolygon(TriangleCentroid, Room.BoundaryVertices))
		{
			return Room.RoomID;
		}
	}

	return 0; // Outside all rooms
}

ETriangleType ABuildFloorTool::SelectionToTriangleType(ETriangleSelection Selection)
{
	switch (Selection)
	{
	case ETriangleSelection::Top:
		return ETriangleType::Top;
	case ETriangleSelection::Right:
		return ETriangleType::Right;
	case ETriangleSelection::Bottom:
		return ETriangleType::Bottom;
	case ETriangleSelection::Left:
		return ETriangleType::Left;
	default:
		return ETriangleType::Top; // Default fallback
	}
}

TArray<FTriangleCoord> ABuildFloorTool::FloodFillPatternRegionTriangles(
	int32 Level,
	int32 StartRow,
	int32 StartColumn,
	ETriangleType StartTriangle,
	UFloorPattern* TargetPattern,
	int32 RoomID)
{
	TArray<FTriangleCoord> Result;

	if (!CurrentLot || !CurrentLot->FloorComponent || !CurrentLot->RoomManager)
	{
		return Result;
	}

	// BFS flood-fill at triangle level
	TSet<FTriangleCoord> Visited;
	TArray<FTriangleCoord> Queue;

	FTriangleCoord StartCoord(StartRow, StartColumn, Level, StartTriangle);
	Queue.Add(StartCoord);
	Visited.Add(StartCoord);

	while (Queue.Num() > 0)
	{
		FTriangleCoord Current = Queue[0];
		Queue.RemoveAt(0);

		// Get pattern at this specific triangle
		UFloorPattern* CurrentPattern = GetPatternAtTriangle(Current.Level, Current.Row, Current.Column, Current.Triangle);
		bool bHasFloor = (CurrentLot->FloorComponent->FindFloorTile(Current.Level, Current.Row, Current.Column, Current.Triangle) != nullptr);

		// Check if this triangle matches our target
		bool bMatches = false;
		if (TargetPattern == nullptr)
		{
			// Looking for empty triangles
			bMatches = !bHasFloor;
		}
		else
		{
			// Looking for triangles with specific pattern
			bMatches = (CurrentPattern == TargetPattern);
		}

		if (!bMatches)
		{
			continue;
		}

		// Check room boundary at triangle level
		int32 TriangleRoomID = GetRoomIDAtTriangle(Current.Level, Current.Row, Current.Column, Current.Triangle);
		if (RoomID > 0 && TriangleRoomID != RoomID)
		{
			continue; // Different room, don't cross boundaries
		}

		// This triangle is valid - add to result
		Result.Add(Current);

		// Add adjacent triangles to queue
		TArray<FTriangleCoord> Neighbors = GetAdjacentTriangles(Current);
		for (const FTriangleCoord& Neighbor : Neighbors)
		{
			// Skip if already visited
			if (Visited.Contains(Neighbor))
			{
				continue;
			}

			// Validate neighbor is within grid bounds
			if (Neighbor.Row < 0 || Neighbor.Row >= CurrentLot->GridSizeY ||
				Neighbor.Column < 0 || Neighbor.Column >= CurrentLot->GridSizeX)
			{
				continue;
			}

			// Skip if wall blocks movement between these triangles
			if (IsWallBlockingTriangles(Current, Neighbor))
			{
				continue;
			}

			Visited.Add(Neighbor);
			Queue.Add(Neighbor);
		}
	}

	return Result;
}

void ABuildFloorTool::ShowPatternRegionPreviewTriangles(const TArray<FTriangleCoord>& Triangles, int32 Level)
{
	// Clear any existing room preview
	ClearRoomPreview();

	if (Triangles.Num() == 0)
	{
		return;
	}

	// Create patterned material for preview
	UMaterialInstance* PreviewMaterial = ValidMaterial;
	if (BaseMaterial && DefaultFloorPattern)
	{
		PreviewMaterial = CreatePatternedMaterial(BaseMaterial, DefaultFloorPattern);
	}

	// Group triangles by tile for efficient preview generation
	// Key: (Row, Column), Value: which triangles to show
	TMap<FIntVector, FTileSectionState> TilePreviewStates;

	for (const FTriangleCoord& TriCoord : Triangles)
	{
		FIntVector TileKey(TriCoord.Row, TriCoord.Column, TriCoord.Level);
		FTileSectionState& State = TilePreviewStates.FindOrAdd(TileKey);

		// Enable the specific triangle in the tile section state
		switch (TriCoord.Triangle)
		{
		case ETriangleType::Top:
			State.Top = true;
			break;
		case ETriangleType::Right:
			State.Right = true;
			break;
		case ETriangleType::Bottom:
			State.Bottom = true;
			break;
		case ETriangleType::Left:
			State.Left = true;
			break;
		}
	}

	// Create preview floor for each tile with its specific triangle states
	for (const auto& TilePair : TilePreviewStates)
	{
		const FIntVector& TileKey = TilePair.Key;
		const FTileSectionState& TileSectionState = TilePair.Value;

		// Get tile center location
		FVector TileCenter;
		CurrentLot->TileToGridLocation(TileKey.Z, TileKey.X, TileKey.Y, true, TileCenter);

		// Apply Z-offset to prevent Z-fighting
		FVector PreviewLocation = TileCenter;
		PreviewLocation.Z += PreviewZOffset;

		// Create preview floor component with specific triangles enabled
		UFloorComponent* PreviewFloor = CurrentLot->GenerateFloorSegment(
			TileKey.Z, PreviewLocation, PreviewMaterial, TileSectionState);

		if (PreviewFloor)
		{
			RoomPreviewFloorArray.Add(PreviewFloor);
		}
	}

	bShowingRoomPreview = true;
}

void ABuildFloorTool::OnMoved_Implementation()
{
	// Only create/update preview if not showing room preview
	if (!bShowingRoomPreview)
	{
		// Destroy old preview if it exists
		if (PreviewFloorMesh && IsValid(PreviewFloorMesh))
		{
			PreviewFloorMesh->DestroyFloor();
			PreviewFloorMesh = nullptr;
		}

		// Determine tile section state based on placement mode
		FTileSectionState TileSectionState;

		if (PlacementMode == EFloorPlacementMode::SingleTriangle)
		{
			// Single triangle mode - only show the hovered triangle
			TileSectionState = CreateSingleTriangleState(CurrentHoveredTriangle);
		}
		else
		{
			// Full tile mode - show all 4 triangles (default behavior)
			TileSectionState = FTileSectionState(); // All triangles enabled by default
		}

		// Create patterned material for preview if pattern is available
		UMaterialInstance* PreviewMaterial = ValidMaterial;
		if (BaseMaterial && DefaultFloorPattern)
		{
			PreviewMaterial = CreatePatternedMaterial(BaseMaterial, DefaultFloorPattern);
		}

		// Always apply Z-offset to prevent Z-fighting (works for terrain AND existing tiles)
		FVector PreviewLocation = TargetLocation;
		PreviewLocation.Z += PreviewZOffset;

		// Generate floor segment at the current traced level (not viewing level)
		PreviewFloorMesh = CurrentLot->GenerateFloorSegment(
			CurrentTracedLevel,
			PreviewLocation,
			PreviewMaterial,
			TileSectionState);

		if (PreviewFloorMesh)
		{
			// Enable custom depth rendering for preview highlighting
			PreviewFloorMesh->SetRenderCustomDepth(true);
		}
	}
	else
	{
		// Clear normal preview when showing room preview
		if (PreviewFloorMesh && IsValid(PreviewFloorMesh))
		{
			PreviewFloorMesh->DestroyFloor();
			PreviewFloorMesh = nullptr;
		}
	}

	// Call parent implementation to allow Blueprint overrides
	Super::OnMoved_Implementation();
}

// ========== Network RPC Implementations ==========

void ABuildFloorTool::Server_BuildFloors_Implementation(int32 Level, const TArray<FVector>& TileLocations, const TArray<FTileSectionState>& SectionStates, UFloorPattern* Pattern, int32 SwatchIndex, bool bIsUpdate)
{
	// Server received the request, multicast to all clients
	Multicast_BuildFloors(Level, TileLocations, SectionStates, Pattern, SwatchIndex, bIsUpdate);
}

void ABuildFloorTool::Multicast_BuildFloors_Implementation(int32 Level, const TArray<FVector>& TileLocations, const TArray<FTileSectionState>& SectionStates, UFloorPattern* Pattern, int32 SwatchIndex, bool bIsUpdate)
{
	if (TileLocations.Num() != SectionStates.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Multicast_BuildFloors: Mismatched array sizes"));
		return;
	}

	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Warning, TEXT("Multicast_BuildFloors: CurrentLot is null"));
		return;
	}

	// Get BuildServer subsystem
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("Multicast_BuildFloors: BuildServer subsystem not found"));
		return;
	}

	BuildServer->SetCurrentLot(CurrentLot);

	// Start batch operation if we have multiple floors
	bool bNeedsBatch = TileLocations.Num() > 1;
	if (bNeedsBatch)
	{
		FString BatchDesc = bIsUpdate ?
			FString::Printf(TEXT("Update %d Floors"), TileLocations.Num()) :
			FString::Printf(TEXT("Build %d Floors"), TileLocations.Num());
		BuildServer->BeginBatch(BatchDesc);
	}

	// Process each floor
	for (int32 i = 0; i < TileLocations.Num(); i++)
	{
		const FVector& TileLocation = TileLocations[i];
		const FTileSectionState& SectionState = SectionStates[i];

		// Get grid coordinates
		int32 Row, Column;
		if (!CurrentLot->LocationToTile(TileLocation, Row, Column))
		{
			continue; // Invalid location
		}

		if (bIsUpdate)
		{
			// Update existing floor pattern
			BuildServer->UpdateFloorPattern(Level, Row, Column, Pattern, BaseMaterial, SwatchIndex);
		}
		else
		{
			// Create new floor
			BuildServer->BuildFloor(Level, TileLocation, Pattern, BaseMaterial, SectionState, SwatchIndex);
		}
	}

	// End batch operation
	if (bNeedsBatch)
	{
		BuildServer->EndBatch();
	}
}

void ABuildFloorTool::Server_DeleteFloors_Implementation(int32 Level, const TArray<int32>& Rows, const TArray<int32>& Columns, const TArray<FTileSectionState>& ExistingStates)
{
	// Server received the request, multicast to all clients
	Multicast_DeleteFloors(Level, Rows, Columns, ExistingStates);
}

void ABuildFloorTool::Multicast_DeleteFloors_Implementation(int32 Level, const TArray<int32>& Rows, const TArray<int32>& Columns, const TArray<FTileSectionState>& ExistingStates)
{
	if (Rows.Num() != Columns.Num() || Rows.Num() != ExistingStates.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Multicast_DeleteFloors: Mismatched array sizes"));
		return;
	}

	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Warning, TEXT("Multicast_DeleteFloors: CurrentLot is null"));
		return;
	}

	// Get BuildServer subsystem
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("Multicast_DeleteFloors: BuildServer subsystem not found"));
		return;
	}

	BuildServer->SetCurrentLot(CurrentLot);

	// Start batch operation if we have multiple floors
	bool bNeedsBatch = Rows.Num() > 1;
	if (bNeedsBatch)
	{
		FString BatchDesc = FString::Printf(TEXT("Delete %d Floors"), Rows.Num());
		BuildServer->BeginBatch(BatchDesc);
	}

	// Process each floor deletion
	for (int32 i = 0; i < Rows.Num(); i++)
	{
		// Create temporary FFloorSegmentData for DeleteFloor command
		FFloorSegmentData TempFloorData;
		TempFloorData.Row = Rows[i];
		TempFloorData.Column = Columns[i];
		TempFloorData.Level = Level;
		TempFloorData.TileSectionState = ExistingStates[i];

		FVector TileCenter;
		CurrentLot->TileToGridLocation(Level, Rows[i], Columns[i], true, TileCenter);
		TempFloorData.StartLoc = TileCenter;

		BuildServer->DeleteFloor(TempFloorData);
	}

	// End batch operation
	if (bNeedsBatch)
	{
		BuildServer->EndBatch();
	}
}

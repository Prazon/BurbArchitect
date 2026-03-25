// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildDiagonalRoomTool.h"
#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"

ABuildDiagonalRoomTool::ABuildDiagonalRoomTool()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABuildDiagonalRoomTool::BeginPlay()
{
	Super::BeginPlay();
}

void ABuildDiagonalRoomTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ABuildDiagonalRoomTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildDiagonalRoomTool::CalculateDiamondCornersFromRect(const FVector& MinCorner, const FVector& MaxCorner, FVector& OutNorth, FVector& OutEast, FVector& OutSouth, FVector& OutWest) const
{
	if (!CurrentLot)
	{
		return;
	}

	// Diamond corners are at the midpoints of the bounding rectangle edges
	// But we need to snap them to grid corners for proper diagonal walls
	float MidX = (MinCorner.X + MaxCorner.X) * 0.5f;
	float MidY = (MinCorner.Y + MaxCorner.Y) * 0.5f;
	float Z = MinCorner.Z;

	// Calculate raw midpoint positions
	FVector RawNorth = FVector(MidX, MaxCorner.Y, Z);
	FVector RawEast = FVector(MaxCorner.X, MidY, Z);
	FVector RawSouth = FVector(MidX, MinCorner.Y, Z);
	FVector RawWest = FVector(MinCorner.X, MidY, Z);

	// Snap each corner to the nearest grid corner
	CurrentLot->LocationToTileCorner(0, RawNorth, OutNorth);
	CurrentLot->LocationToTileCorner(0, RawEast, OutEast);
	CurrentLot->LocationToTileCorner(0, RawSouth, OutSouth);
	CurrentLot->LocationToTileCorner(0, RawWest, OutWest);

	// Preserve Z height
	OutNorth.Z = Z;
	OutEast.Z = Z;
	OutSouth.Z = Z;
	OutWest.Z = Z;
}

void ABuildDiagonalRoomTool::GenerateDiagonalWallSegments(const FVector& StartCorner, const FVector& EndCorner, int32 Level, TArray<UWallComponent*>& OutWalls)
{
	if (!CurrentLot)
	{
		return;
	}

	// Calculate direction signs for diagonal stepping
	FVector Delta = EndCorner - StartCorner;

	if (FMath::Abs(Delta.X) < 0.01f && FMath::Abs(Delta.Y) < 0.01f)
	{
		return; // No distance to cover
	}

	// Determine step direction: each diagonal segment moves ±1 tile in X and ±1 tile in Y
	float StepX = FMath::Sign(Delta.X) * CurrentLot->GridTileSize;
	float StepY = FMath::Sign(Delta.Y) * CurrentLot->GridTileSize;

	// Calculate number of diagonal segments needed
	// For a true diagonal, we step by GridTileSize in both X and Y
	int32 NumStepsX = FMath::Abs(FMath::RoundToInt(Delta.X / CurrentLot->GridTileSize));
	int32 NumStepsY = FMath::Abs(FMath::RoundToInt(Delta.Y / CurrentLot->GridTileSize));
	int32 NumSegments = FMath::Max(NumStepsX, NumStepsY);

	if (NumSegments == 0)
	{
		return;
	}

	// Generate diagonal wall segments
	FVector CurrentPos = StartCorner;
	for (int32 i = 0; i < NumSegments; i++)
	{
		FVector SegmentStart = CurrentPos;
		FVector SegmentEnd = FVector(CurrentPos.X + StepX, CurrentPos.Y + StepY, CurrentPos.Z);

		// Get grid coordinates for cache key
		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (CurrentLot->LocationToTile(SegmentStart, StartRow, StartColumn) &&
			CurrentLot->LocationToTile(SegmentEnd, EndRow, EndColumn))
		{
			int64 WallKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);

			// Check if we already have a preview component for this wall segment
			UWallComponent** ExistingWall = PreviewWallCache.Find(WallKey);

			if (!ExistingWall || !*ExistingWall)
			{
				// Component doesn't exist - create new one
				bool bValidPlacement;
				UWallComponent* NewWall = CurrentLot->GenerateWallSegment(CurrentTracedLevel, SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
				NewWall->bRenderCustomDepth = true;
				NewWall->CustomDepthStencilValue = 1;  // Preview stencil value
				PreviewWallCache.Add(WallKey, NewWall);
				OutWalls.Add(NewWall);
			}
			else
			{
				// Reuse existing cached wall
				OutWalls.Add(*ExistingWall);
			}
		}

		CurrentPos = SegmentEnd;
	}
}

void ABuildDiagonalRoomTool::GenerateDiamondPreviewFromDrag(const FVector& StartLocation, const FVector& EndLocation, int32 Level)
{
	if (!CurrentLot)
	{
		return;
	}

	// Build set of current wall segments for this diamond
	TSet<int64> CurrentWallSet;
	TArray<UWallComponent*> WallsToAdd;

	// Snap both corners to grid first
	FVector SnappedStart, SnappedEnd;
	CurrentLot->LocationToTileCorner(Level, StartLocation, SnappedStart);
	CurrentLot->LocationToTileCorner(Level, EndLocation, SnappedEnd);

	// Sort coordinates to get bounding rectangle
	FVector MinCorner = FVector(FMath::Min(SnappedStart.X, SnappedEnd.X), FMath::Min(SnappedStart.Y, SnappedEnd.Y), SnappedStart.Z);
	FVector MaxCorner = FVector(FMath::Max(SnappedStart.X, SnappedEnd.X), FMath::Max(SnappedStart.Y, SnappedEnd.Y), SnappedStart.Z);

	// Calculate width and height in tiles
	float TileSize = CurrentLot->GridTileSize;
	int32 WidthTiles = FMath::RoundToInt((MaxCorner.X - MinCorner.X) / TileSize);
	int32 HeightTiles = FMath::RoundToInt((MaxCorner.Y - MinCorner.Y) / TileSize);

	// For a proper diamond with all diagonal walls, the bounding box must be SQUARE
	// Use the larger dimension for both, and ensure it's even (minimum 2)
	int32 Size = FMath::Max(WidthTiles, HeightTiles);
	Size = FMath::Max(2, Size + (Size % 2));  // Round up to even, minimum 2

	// Recalculate MaxCorner with square dimensions
	MaxCorner.X = MinCorner.X + (Size * TileSize);
	MaxCorner.Y = MinCorner.Y + (Size * TileSize);

	// Now calculate diamond corners - they will be exactly on grid corners
	// because size is even, so midpoints are on whole tile boundaries
	float MidX = MinCorner.X + (Size / 2) * TileSize;
	float MidY = MinCorner.Y + (Size / 2) * TileSize;

	FVector NorthCorner = FVector(MidX, MaxCorner.Y, MinCorner.Z);
	FVector EastCorner = FVector(MaxCorner.X, MidY, MinCorner.Z);
	FVector SouthCorner = FVector(MidX, MinCorner.Y, MinCorner.Z);
	FVector WestCorner = FVector(MinCorner.X, MidY, MinCorner.Z);

	// Generate the 4 diagonal edges of the diamond
	// NE edge: North → East
	GenerateDiagonalWallSegments(NorthCorner, EastCorner, Level, WallsToAdd);

	// SE edge: East → South
	GenerateDiagonalWallSegments(EastCorner, SouthCorner, Level, WallsToAdd);

	// SW edge: South → West
	GenerateDiagonalWallSegments(SouthCorner, WestCorner, Level, WallsToAdd);

	// NW edge: West → North
	GenerateDiagonalWallSegments(WestCorner, NorthCorner, Level, WallsToAdd);

	// Build the current wall set for tracking
	for (UWallComponent* Wall : WallsToAdd)
	{
		if (Wall)
		{
			int32 StartRow, StartColumn, EndRow, EndColumn;
			if (CurrentLot->LocationToTile(Wall->WallData.StartLoc, StartRow, StartColumn) &&
				CurrentLot->LocationToTile(Wall->WallData.EndLoc, EndRow, EndColumn))
			{
				int64 WallKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
				CurrentWallSet.Add(WallKey);
			}
		}
	}

	// Remove preview components that are no longer in the current selection
	TArray<int64> WallsToRemove;
	for (auto& Elem : PreviewWallCache)
	{
		if (!CurrentWallSet.Contains(Elem.Key))
		{
			if (Elem.Value)
			{
				Elem.Value->DestroyWall();
			}
			WallsToRemove.Add(Elem.Key);
		}
	}

	for (const int64& WallToRemove : WallsToRemove)
	{
		PreviewWallCache.Remove(WallToRemove);
	}

	DragCreateWallArray = WallsToAdd;
}

void ABuildDiagonalRoomTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	// Store traced level for use by BroadcastRelease()
	CurrentTracedLevel = TracedLevel;

	// Snap to tile corner
	if (CurrentLot->LocationToTileCorner(TracedLevel, MoveLocation, TargetLocation))
	{
		if (GetActorLocation() != TargetLocation)
		{
			SetActorLocation(TargetLocation);
			UpdateLocation(GetActorLocation());

			// Tell blueprint children we moved successfully
			OnMoved();

			if (SelectPressed)
			{
				// Dragging - update preview
				ServerDrag();
			}
			else
			{
				PreviousLocation = GetActorLocation();
			}
		}
	}
}

void ABuildDiagonalRoomTool::Click_Implementation()
{
	// Store start location for drag operation (like regular room tool)
	DragCreateVectors = {GetActorLocation(), GetActorLocation()};
	DragCreateWallArray.Empty();
}

void ABuildDiagonalRoomTool::Drag_Implementation()
{
	// Update end location for the drag
	DragCreateVectors = {DragCreateVectors.StartOperation, GetActorLocation()};

	// Generate diamond preview from the drag rectangle
	GenerateDiamondPreviewFromDrag(DragCreateVectors.StartOperation, DragCreateVectors.EndOperation, CurrentTracedLevel);

	bLockToForwardAxis = true;
	PreviousLocation = GetActorLocation();
}

void ABuildDiagonalRoomTool::BroadcastRelease_Implementation()
{
	if (DragCreateWallArray.Num() == 0)
	{
		return;
	}

	// Get BuildServer subsystem
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildDiagonalRoomTool: BuildServer subsystem not found"));
		return;
	}

	BuildServer->SetCurrentLot(CurrentLot);

	// Batch collect walls to create - use WallGraph for O(1) existence checks
	TArray<UWallComponent*> WallsToCreate;
	WallsToCreate.Reserve(DragCreateWallArray.Num());

	for (UWallComponent* Wall : DragCreateWallArray)
	{
		// Get grid coordinates for wall graph lookup
		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (!CurrentLot->LocationToTile(Wall->WallData.StartLoc, StartRow, StartColumn) ||
			!CurrentLot->LocationToTile(Wall->WallData.EndLoc, EndRow, EndColumn))
		{
			continue;
		}

		// Use WallGraph to check if wall already exists (O(1) lookup via spatial hash)
		bool bWallExists = CurrentLot->WallGraph &&
			CurrentLot->WallGraph->IsWallBetweenTiles(StartRow, StartColumn, EndRow, EndColumn, CurrentTracedLevel);

		if (Wall->bValidPlacement && !bWallExists)
		{
			WallsToCreate.Add(Wall);
		}
	}

	// Start batch operation if we have multiple walls
	bool bNeedsBatch = WallsToCreate.Num() > 1;
	if (bNeedsBatch)
	{
		BuildServer->BeginBatch(FString::Printf(TEXT("Build Diagonal Room (%d walls)"), WallsToCreate.Num()));
	}

	// Batch process creations using BuildServer
	for (UWallComponent* WallToCreate : WallsToCreate)
	{
		BuildServer->BuildWall(
			CurrentTracedLevel,
			WallToCreate->WallData.StartLoc,
			WallToCreate->WallData.EndLoc,
			WallHeight,
			DefaultWallPattern,
			BaseMaterial);
	}

	// End batch operation
	if (bNeedsBatch)
	{
		BuildServer->EndBatch();
	}

	// Clean up preview components
	for (UWallComponent* Walls : DragCreateWallArray)
	{
		Walls->DestroyWall();
	}

	// Clear the preview cache
	ClearPreviewCache();

	bLockToForwardAxis = false;
	OnReleased();
	DragCreateWallArray.Empty();
}

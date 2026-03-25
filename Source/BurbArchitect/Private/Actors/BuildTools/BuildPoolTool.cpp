// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildPoolTool.h"
#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"
#include "Components/RoomManagerComponent.h"
#include "Components/WaterComponent.h"
#include "Components/WallGraphComponent.h"

// Sets default values
ABuildPoolTool::ABuildPoolTool()
{
	// Set this actor to call Tick() every frame. You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void ABuildPoolTool::BeginPlay()
{
	Super::BeginPlay();
}

void ABuildPoolTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABuildPoolTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

int32 ABuildPoolTool::GetTargetLevel() const
{
	if (!CurrentPlayerPawn || !CurrentLot)
	{
		return 0;
	}

	// Ground floor level equals the number of basements
	// E.g., if Basements = 1: Level 0 = basement, Level 1 = ground floor
	// When viewing ground floor, build one level below (top basement)
	// When already in basement view, build at current level
	const int32 GroundFloorLevel = CurrentLot->Basements;

	if (CurrentPlayerPawn->CurrentLevel == GroundFloorLevel)
	{
		// On ground floor - build at top basement level
		return GroundFloorLevel - 1;
	}

	// Already in basement or upper floors - build at current level
	return CurrentPlayerPawn->CurrentLevel;
}

void ABuildPoolTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	int32 TargetLevel = GetTargetLevel();

	if (CurrentLot->LocationToTileCorner(TargetLevel, MoveLocation, TargetLocation))
	{
		if (GetActorLocation() != TargetLocation)
		{
			SetActorLocation(TargetLocation);
			UpdateLocation(GetActorLocation());
			// Tell blueprint children we moved successfully
			OnMoved();

			if (SelectPressed)
			{
				ServerDrag();
			}
			else
			{
				PreviousLocation = GetActorLocation();
			}
		}
	}
}

void ABuildPoolTool::Drag_Implementation()
{
	DragCreateVectors = {DragCreateVectors.StartOperation, GetActorLocation()};

	// Build set of current wall segments needed for this room perimeter
	TSet<int64> CurrentWallSet;
	TArray<UWallComponent*> WallsToAdd;

	// Clear and rebuild merge tracking each drag update
	PoolWallsToRemove.Empty();
	PoolRoomsToMerge.Empty();

	FVector TileCornerStart = DragCreateVectors.StartOperation;
	FVector TileCornerEnd = DragCreateVectors.EndOperation;

	// Sort the coordinates for looping
	FVector MinCorner = FVector(FMath::Min(TileCornerStart.X, TileCornerEnd.X), FMath::Min(TileCornerStart.Y, TileCornerEnd.Y), TileCornerStart.Z);
	FVector MaxCorner = FVector(FMath::Max(TileCornerStart.X, TileCornerEnd.X), FMath::Max(TileCornerStart.Y, TileCornerEnd.Y), TileCornerStart.Z);

	// Clamp to minimum 1x1 tile pool to prevent degenerate straight walls
	if (MaxCorner.X - MinCorner.X < CurrentLot->GridTileSize)
	{
		MaxCorner.X = MinCorner.X + CurrentLot->GridTileSize;
	}
	if (MaxCorner.Y - MinCorner.Y < CurrentLot->GridTileSize)
	{
		MaxCorner.Y = MinCorner.Y + CurrentLot->GridTileSize;
	}

	int32 TargetLevel = GetTargetLevel();

	auto AddWallSegment = [&](const FVector& SegmentStart, const FVector& SegmentEnd)
	{
		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (CurrentLot->LocationToTile(SegmentStart, StartRow, StartColumn) &&
			CurrentLot->LocationToTile(SegmentEnd, EndRow, EndColumn))
		{
			int64 WallKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
			CurrentWallSet.Add(WallKey);

			// Check if this is an existing pool wall (will be removed for merging)
			int32 ExistingPoolWallEdgeID = FindExistingPoolWall(StartRow, StartColumn, EndRow, EndColumn, TargetLevel);
			if (ExistingPoolWallEdgeID >= 0)
			{
				// This wall will be removed to merge pools - track it
				PoolWallsToRemove.AddUnique(ExistingPoolWallEdgeID);

				// Track which rooms will be merged (for water volume cleanup)
				const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(ExistingPoolWallEdgeID);
				if (Edge)
				{
					if (Edge->Room1 > 0) PoolRoomsToMerge.Add(Edge->Room1);
					if (Edge->Room2 > 0) PoolRoomsToMerge.Add(Edge->Room2);
				}

				// Don't create preview wall for segments that will be removed
				return;
			}

			UWallComponent** ExistingWall = PreviewWallCache.Find(WallKey);

			if (!ExistingWall || !*ExistingWall)
			{
				bool bValidPlacement;
				UWallComponent* NewWall = CurrentLot->GenerateWallSegment(TargetLevel, SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
				NewWall->bRenderCustomDepth = true;
				NewWall->CustomDepthStencilValue = 1;  // Preview stencil value
				PreviewWallCache.Add(WallKey, NewWall);
				WallsToAdd.Add(NewWall);
			}
			else
			{
				WallsToAdd.Add(*ExistingWall);
			}
		}
	};

	// Top wall
	for (float x = MinCorner.X; x < MaxCorner.X; x += CurrentLot->GridTileSize)
	{
		AddWallSegment(FVector(x, MinCorner.Y, MinCorner.Z), FVector(x + CurrentLot->GridTileSize, MinCorner.Y, MinCorner.Z));
	}

	// Bottom wall
	for (float x = MinCorner.X; x < MaxCorner.X; x += CurrentLot->GridTileSize)
	{
		AddWallSegment(FVector(x, MaxCorner.Y, MinCorner.Z), FVector(x + CurrentLot->GridTileSize, MaxCorner.Y, MinCorner.Z));
	}

	// Left wall
	for (float y = MinCorner.Y; y < MaxCorner.Y; y += CurrentLot->GridTileSize)
	{
		AddWallSegment(FVector(MinCorner.X, y, MinCorner.Z), FVector(MinCorner.X, y + CurrentLot->GridTileSize, MinCorner.Z));
	}

	// Right wall
	for (float y = MinCorner.Y; y < MaxCorner.Y; y += CurrentLot->GridTileSize)
	{
		AddWallSegment(FVector(MaxCorner.X, y, MinCorner.Z), FVector(MaxCorner.X, y + CurrentLot->GridTileSize, MinCorner.Z));
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
	bLockToForwardAxis = true;
	PreviousLocation = TileCornerEnd;
}

void ABuildPoolTool::BroadcastRelease_Implementation()
{
	if (DragCreateWallArray.IsValidIndex(0))
	{
		int32 TargetLevel = GetTargetLevel();

		// Get BuildServer subsystem
		UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
		if (!BuildServer)
		{
			UE_LOG(LogTemp, Error, TEXT("BuildPoolTool: BuildServer subsystem not found"));
			return;
		}

		BuildServer->SetCurrentLot(CurrentLot);

		// ========== STEP 1: REMOVE OLD POOL WATER VOLUMES ==========
		// Remove water from rooms that will be merged BEFORE wall operations
		// (water is tied to room IDs which will change after merge)
		if (PoolRoomsToMerge.Num() > 0 && CurrentLot->WaterComponent)
		{
			UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Removing water from %d pool rooms to merge"), PoolRoomsToMerge.Num());

			for (int32 RoomID : PoolRoomsToMerge)
			{
				if (CurrentLot->WaterComponent->HasPoolWater(RoomID))
				{
					UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Removing pool water for RoomID=%d"), RoomID);
					CurrentLot->WaterComponent->RemovePoolWater(RoomID);
				}
			}
		}

		// ========== STEP 2: BUILD SPATIAL HASH MAP FOR EXISTING WALLS ==========
		// Build spatial hash map ONCE using grid coordinates for O(1) lookup
		// (skip walls that will be deleted for merging)
		TMap<int64, FWallSegmentData*> ExistingWallMap;
		ExistingWallMap.Reserve(CurrentLot->WallComponent->WallDataArray.Num());

		for (FWallSegmentData& ExistingWall : CurrentLot->WallComponent->WallDataArray)
		{
			if (ExistingWall.bCommitted && ExistingWall.Level == TargetLevel)
			{
				// Convert world positions to grid coordinates for hash key
				int32 StartRow, StartColumn, EndRow, EndColumn;
				if (!CurrentLot->LocationToTile(ExistingWall.StartLoc, StartRow, StartColumn) ||
					!CurrentLot->LocationToTile(ExistingWall.EndLoc, EndRow, EndColumn))
				{
					continue; // Skip walls with invalid coordinates
				}

				// Create keys for both directions (walls can match bidirectionally)
				int64 ForwardKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
				int64 ReverseKey = GenerateWallCacheKey(EndRow, EndColumn, StartRow, StartColumn);

				ExistingWallMap.Add(ForwardKey, &ExistingWall);
				if (ForwardKey != ReverseKey)
				{
					ExistingWallMap.Add(ReverseKey, &ExistingWall);
				}
			}
		}

		// ========== STEP 3: COLLECT WALLS TO CREATE ==========
		// Batch collect walls to create (skip duplicates of existing walls)
		TArray<UWallComponent*> WallsToCreate;
		WallsToCreate.Reserve(DragCreateWallArray.Num());

		for (UWallComponent* Wall : DragCreateWallArray)
		{
			// Get grid coordinates for O(1) lookup
			int32 StartRow, StartColumn, EndRow, EndColumn;
			if (!CurrentLot->LocationToTile(Wall->WallData.StartLoc, StartRow, StartColumn) ||
				!CurrentLot->LocationToTile(Wall->WallData.EndLoc, EndRow, EndColumn))
			{
				continue;
			}

			int64 WallKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
			FWallSegmentData** ExistingWallPtr = ExistingWallMap.Find(WallKey);

			if (Wall->bValidPlacement && !ExistingWallPtr)
			{
				WallsToCreate.Add(Wall);
			}
		}

		// ========== STEP 4: DELETE SHARED WALLS AND CREATE NEW POOL WALLS ==========
		// Start batch operation for all wall changes (deletions + creations)
		// This ensures room detection only runs ONCE at the end
		bool bNeedsBatch = WallsToCreate.Num() > 1 || PoolWallsToRemove.Num() > 0;
		if (bNeedsBatch)
		{
			BuildServer->BeginBatch(FString::Printf(TEXT("Build Pool Room (%d walls, %d merged)"), WallsToCreate.Num(), PoolWallsToRemove.Num()));
		}

		// First, delete shared pool walls (inside batch to defer room detection)
		if (PoolWallsToRemove.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Removing %d shared pool walls for merge"), PoolWallsToRemove.Num());

			// Find and delete each shared wall by EdgeID
			for (int32 EdgeID : PoolWallsToRemove)
			{
				// Find the FWallSegmentData with this EdgeID
				FWallSegmentData* WallToDelete = nullptr;
				for (FWallSegmentData& WallData : CurrentLot->WallComponent->WallDataArray)
				{
					if (WallData.WallEdgeID == EdgeID && WallData.bCommitted)
					{
						WallToDelete = &WallData;
						break;
					}
				}

				if (WallToDelete)
				{
					UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Deleting shared wall EdgeID=%d"), EdgeID);
					BuildServer->DeleteWall(*WallToDelete);
				}
			}
		}

		// Then create new pool walls
		// bDeferRoomGeneration = true prevents auto floor generation (we create pool floors manually with bIsPool=true)
		for (UWallComponent* WallToCreate : WallsToCreate)
		{
			BuildServer->BuildWall(
				TargetLevel,
				WallToCreate->WallData.StartLoc,
				WallToCreate->WallData.EndLoc,
				WallHeight,
				DefaultWallPattern,
				BaseMaterial,
				true,   // bDeferRoomGeneration - defer auto floor generation, we create pool floors manually
				true);  // bIsPoolWall - mark as pool wall for debug visualization
		}

		// End batch operation - triggers room detection ONCE for all changes
		if (bNeedsBatch)
		{
			BuildServer->EndBatch();
		}

		// Remove terrain tiles inside the pool area (at ground level)
		// Pool walls are at TargetLevel (basement), but terrain exists at ground level (Basements)
		const int32 GroundLevel = CurrentLot->Basements;

		// Calculate interior tile region from drag corners
		FVector TileCornerStart = DragCreateVectors.StartOperation;
		FVector TileCornerEnd = DragCreateVectors.EndOperation;

		// Get min/max corners (walls are on these edges)
		FVector MinCorner = FVector(
			FMath::Min(TileCornerStart.X, TileCornerEnd.X),
			FMath::Min(TileCornerStart.Y, TileCornerEnd.Y),
			TileCornerStart.Z);
		FVector MaxCorner = FVector(
			FMath::Max(TileCornerStart.X, TileCornerEnd.X),
			FMath::Max(TileCornerStart.Y, TileCornerEnd.Y),
			TileCornerStart.Z);

		// Clamp to minimum 1x1 tile pool to prevent degenerate operations
		if (MaxCorner.X - MinCorner.X < CurrentLot->GridTileSize)
		{
			MaxCorner.X = MinCorner.X + CurrentLot->GridTileSize;
		}
		if (MaxCorner.Y - MinCorner.Y < CurrentLot->GridTileSize)
		{
			MaxCorner.Y = MinCorner.Y + CurrentLot->GridTileSize;
		}

		// Convert corners to tile coordinates
		// Interior tiles are between the wall corners (walls sit on tile edges)
		// Add half tile size to get tile centers for accurate conversion
		const float HalfTile = CurrentLot->GridTileSize * 0.5f;

		int32 MinRow, MinCol, MaxRow, MaxCol;
		if (CurrentLot->LocationToTile(MinCorner + FVector(HalfTile, HalfTile, 0), MinRow, MinCol) &&
			CurrentLot->LocationToTile(MaxCorner - FVector(HalfTile, HalfTile, 0), MaxRow, MaxCol))
		{
			// Remove terrain region at ground level
			if (CurrentLot->TerrainComponent)
			{
				UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Removing terrain from (%d,%d) to (%d,%d) at ground level %d"),
					MinRow, MinCol, MaxRow, MaxCol, GroundLevel);

				CurrentLot->TerrainComponent->RemoveTerrainRegion(GroundLevel, MinRow, MinCol, MaxRow, MaxCol);
			}

			// Create pool floor tiles at TargetLevel (basement level where pool bottom is)
			// These tiles are marked with bIsPool = true for visualization and gameplay
			if (CurrentLot->FloorComponent)
			{
				UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Creating pool floor tiles from (%d,%d) to (%d,%d) at level %d"),
					MinRow, MinCol, MaxRow, MaxCol, TargetLevel);

				// Start batch operation for floor tiles
				CurrentLot->FloorComponent->BeginBatchOperation();

				for (int32 Row = MinRow; Row <= MaxRow; ++Row)
				{
					for (int32 Col = MinCol; Col <= MaxCol; ++Col)
					{
						// Get tile center for this grid position
						FVector TileCenter;
						if (!CurrentLot->TileToGridLocation(TargetLevel, Row, Col, true, TileCenter))
						{
							continue; // Skip if tile location is invalid
						}

						// Use BuildServer to create floor with bIsPool = true
						FTileSectionState FullTile; // All quadrants enabled by default
						BuildServer->BuildFloor(
							TargetLevel,
							TileCenter,
							PoolFloorPattern,
							PoolFloorMaterial,
							FullTile,
							0,  // SwatchIndex
							true,  // bIsPool = true
							false  // bIsPoolEdge = false (interior tiles)
						);
					}
				}

				// End batch operation - rebuilds the mesh
				CurrentLot->FloorComponent->EndBatchOperation();

				UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Created %d pool floor tiles"),
					(MaxRow - MinRow + 1) * (MaxCol - MinCol + 1));
			}

			// ========== POOL WATER GENERATION ==========
			// After walls are created, lookup the pool room (already detected during EndBatch)
			if (CurrentLot->RoomManager && CurrentLot->WaterComponent)
			{
				// Get center tile of the pool to find the room
				int32 CenterRow = (MinRow + MaxRow) / 2;
				int32 CenterCol = (MinCol + MaxCol) / 2;
				FIntVector CenterTile(CenterRow, CenterCol, TargetLevel);

				// Lookup existing room from cache (O(1) lookup)
				int32 RoomID = CurrentLot->RoomManager->GetRoomAtTile(CenterTile);
				FRoomData PoolRoom;

				if (RoomID > 0 && CurrentLot->RoomManager->GetRoom(RoomID, PoolRoom) &&
				    PoolRoom.IsValid() && PoolRoom.BoundaryVertices.Num() >= 3)
				{
					// Calculate water heights
					// PoolFloorZ = pool bottom (basement level)
					// WaterSurfaceZ = pool wall top + designer offset (negative to sit below wall tops)
					const FVector LotLocation = CurrentLot->GetActorLocation();
					const float PoolFloorZ = LotLocation.Z + (CurrentLot->DefaultWallHeight * (TargetLevel - CurrentLot->Basements));
					const float WaterSurfaceZ = PoolFloorZ + WallHeight + WaterSurfaceZOffset;

					UE_LOG(LogTemp, Log, TEXT("BuildPoolTool: Generating pool water for RoomID %d with %d vertices (SurfaceZ=%.1f, FloorZ=%.1f)"),
						PoolRoom.RoomID, PoolRoom.BoundaryVertices.Num(), WaterSurfaceZ, PoolFloorZ);

					// Generate pool water volume
					CurrentLot->WaterComponent->GeneratePoolWater(
						PoolRoom.RoomID,
						PoolRoom.BoundaryVertices,
						WaterSurfaceZ,
						PoolFloorZ,
						DefaultWaterMaterial
					);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("BuildPoolTool: Failed to find pool room at tile (%d,%d,%d), RoomID=%d"),
						CenterRow, CenterCol, TargetLevel, RoomID);
				}
			}
		}

		// Clean up preview components
		for (UWallComponent* Walls : DragCreateWallArray)
		{
			Walls->DestroyWall();
		}

		// Clear the preview cache
		ClearPreviewCache();

		// Clear merge tracking arrays for next operation
		PoolWallsToRemove.Empty();
		PoolRoomsToMerge.Empty();

		bLockToForwardAxis = false;
		OnReleased();
		DragCreateWallArray.Empty();
	}
}

int32 ABuildPoolTool::FindExistingPoolWall(int32 StartRow, int32 StartCol, int32 EndRow, int32 EndCol, int32 Level) const
{
	if (!CurrentLot || !CurrentLot->WallGraph)
	{
		return -1;
	}

	// Get edges in both tiles that this wall segment touches
	TArray<int32> StartTileEdges = CurrentLot->WallGraph->GetEdgesInTile(StartRow, StartCol, Level);
	TArray<int32> EndTileEdges = CurrentLot->WallGraph->GetEdgesInTile(EndRow, EndCol, Level);

	// Find edges that appear in both tiles (the wall between them)
	for (int32 EdgeID : StartTileEdges)
	{
		if (EndTileEdges.Contains(EdgeID))
		{
			// Check if this edge is a committed pool wall
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(EdgeID);
			if (Edge && Edge->bCommitted && Edge->bIsPoolWall)
			{
				return EdgeID;
			}
		}
	}

	return -1;
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildBasementTool.h"
#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"

// Sets default values
ABuildBasementTool::ABuildBasementTool()
{
	// Set this actor to call Tick() every frame. You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void ABuildBasementTool::BeginPlay()
{
	Super::BeginPlay();
}

void ABuildBasementTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABuildBasementTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

int32 ABuildBasementTool::GetTargetLevel() const
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

void ABuildBasementTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
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

void ABuildBasementTool::Drag_Implementation()
{
	DragCreateVectors = {DragCreateVectors.StartOperation, GetActorLocation()};

	// Build set of current wall segments needed for this room perimeter
	TSet<int64> CurrentWallSet;
	TArray<UWallComponent*> WallsToAdd;

	FVector TileCornerStart = DragCreateVectors.StartOperation;
	FVector TileCornerEnd = DragCreateVectors.EndOperation;

	// Sort the coordinates for looping
	FVector MinCorner = FVector(FMath::Min(TileCornerStart.X, TileCornerEnd.X), FMath::Min(TileCornerStart.Y, TileCornerEnd.Y), TileCornerStart.Z);
	FVector MaxCorner = FVector(FMath::Max(TileCornerStart.X, TileCornerEnd.X), FMath::Max(TileCornerStart.Y, TileCornerEnd.Y), TileCornerStart.Z);

	auto AddWallSegment = [&](const FVector& SegmentStart, const FVector& SegmentEnd)
	{
		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (CurrentLot->LocationToTile(SegmentStart, StartRow, StartColumn) &&
			CurrentLot->LocationToTile(SegmentEnd, EndRow, EndColumn))
		{
			int64 WallKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
			CurrentWallSet.Add(WallKey);

			UWallComponent** ExistingWall = PreviewWallCache.Find(WallKey);

			if (!ExistingWall || !*ExistingWall)
			{
				bool bValidPlacement;
				UWallComponent* NewWall = CurrentLot->GenerateWallSegment(GetTargetLevel(), SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
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

void ABuildBasementTool::BroadcastRelease_Implementation()
{
	if (DragCreateWallArray.IsValidIndex(0))
	{
		int32 TargetLevel = GetTargetLevel();

		// Get BuildServer subsystem
		UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
		if (!BuildServer)
		{
			UE_LOG(LogTemp, Error, TEXT("BuildBasementTool: BuildServer subsystem not found"));
			return;
		}

		BuildServer->SetCurrentLot(CurrentLot);

		// Build spatial hash map ONCE using grid coordinates for O(1) lookup
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

		// Batch collect walls to create
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

		// Start batch operation if we have multiple walls
		bool bNeedsBatch = WallsToCreate.Num() > 1;
		if (bNeedsBatch)
		{
			BuildServer->BeginBatch(FString::Printf(TEXT("Build Basement Room (%d walls)"), WallsToCreate.Num()));
		}

		// Batch process creations using BuildServer
		for (UWallComponent* WallToCreate : WallsToCreate)
		{
			BuildServer->BuildWall(
				TargetLevel,
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
}

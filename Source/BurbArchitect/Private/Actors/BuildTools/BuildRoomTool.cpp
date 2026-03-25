// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuildTools/BuildRoomTool.h"

#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"


// Sets default values
ABuildRoomTool::ABuildRoomTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void ABuildRoomTool::BeginPlay()
{
	Super::BeginPlay();
}

void ABuildRoomTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABuildRoomTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildRoomTool::CreateRoomPreview(FVector StartLocation, FVector EndLocation)
{
	bool bValidPlacement;
	FVector TileCornerStart = StartLocation;
	FVector TileCornerEnd = EndLocation;
	
	// Sort the coordinates for looping
	FVector MinCorner = FVector(FMath::Min(TileCornerStart.X, TileCornerEnd.X), FMath::Min(TileCornerStart.Y, TileCornerEnd.Y), TileCornerStart.Z);
	FVector MaxCorner = FVector(FMath::Max(TileCornerStart.X, TileCornerEnd.X), FMath::Max(TileCornerStart.Y, TileCornerEnd.Y), TileCornerStart.Z);
	
	// Top wall
	for (float x = MinCorner.X; x < MaxCorner.X; x += CurrentLot->GridTileSize)
	{
		FVector SegmentStart(x, MinCorner.Y, MinCorner.Z);
		FVector SegmentEnd(x + CurrentLot->GridTileSize, MinCorner.Y, MinCorner.Z);
		CurrentWallComponent = CurrentLot->GenerateWallSegment(CurrentTracedLevel, SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
		CurrentWallComponent->bRenderCustomDepth = true;
		CurrentWallComponent->CustomDepthStencilValue = 1;  // Preview stencil value
		DragCreateWallArray.Add(CurrentWallComponent);
	}

	// Bottom wall
	for (float x = MinCorner.X; x < MaxCorner.X; x += CurrentLot->GridTileSize)
	{
		FVector SegmentStart(x, MaxCorner.Y, MinCorner.Z);
		FVector SegmentEnd(x + CurrentLot->GridTileSize, MaxCorner.Y, MinCorner.Z);
		CurrentWallComponent = CurrentLot->GenerateWallSegment(CurrentTracedLevel, SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
		CurrentWallComponent->bRenderCustomDepth = true;
		CurrentWallComponent->CustomDepthStencilValue = 1;  // Preview stencil value
		DragCreateWallArray.Add(CurrentWallComponent);
	}
	
	// Left wall
	for (float y = MinCorner.Y; y < MaxCorner.Y; y += CurrentLot->GridTileSize)
	{
		FVector SegmentStart(MinCorner.X, y, MinCorner.Z);
		FVector SegmentEnd(MinCorner.X, y + CurrentLot->GridTileSize, MinCorner.Z);
		CurrentWallComponent = CurrentLot->GenerateWallSegment(CurrentTracedLevel, SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
		CurrentWallComponent->bRenderCustomDepth = true;
		CurrentWallComponent->CustomDepthStencilValue = 1;  // Preview stencil value
		DragCreateWallArray.Add(CurrentWallComponent);
	}

	// Right wall
	for (float y = MinCorner.Y; y < MaxCorner.Y; y += CurrentLot->GridTileSize)
	{
		FVector SegmentStart(MaxCorner.X, y, MinCorner.Z);
		FVector SegmentEnd(MaxCorner.X, y + CurrentLot->GridTileSize, MinCorner.Z);
		CurrentWallComponent = CurrentLot->GenerateWallSegment(CurrentTracedLevel, SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
		CurrentWallComponent->bRenderCustomDepth = true;
		CurrentWallComponent->CustomDepthStencilValue = 1;  // Preview stencil value
		DragCreateWallArray.Add(CurrentWallComponent);
	}
	
	bLockToForwardAxis = true;
	PreviousLocation = TileCornerEnd;
}

void ABuildRoomTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	if (!CurrentLot)
	{
		return;
	}

	// CRITICAL: Store traced level for use by BroadcastRelease()
	// Without this, CurrentTracedLevel is uninitialized and walls get created at wrong level
	CurrentTracedLevel = TracedLevel;

	if (CurrentLot->LocationToTileCorner(TracedLevel, MoveLocation, TargetLocation))
	{
		if (GetActorLocation() !=  TargetLocation)
		{
			SetActorLocation(TargetLocation);
			UpdateLocation(GetActorLocation());
			//Tell blueprint children we moved successfully
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

void ABuildRoomTool::Click_Implementation()
{
	DragCreateVectors = {GetActorLocation(),GetActorLocation()};
	DragCreateWallArray.Empty();
}

void ABuildRoomTool::Drag_Implementation()
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

	// Clamp to minimum 1x1 tile room to prevent degenerate straight walls
	if (MaxCorner.X - MinCorner.X < CurrentLot->GridTileSize)
	{
		MaxCorner.X = MinCorner.X + CurrentLot->GridTileSize;
	}
	if (MaxCorner.Y - MinCorner.Y < CurrentLot->GridTileSize)
	{
		MaxCorner.Y = MinCorner.Y + CurrentLot->GridTileSize;
	}

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
				UWallComponent* NewWall = CurrentLot->GenerateWallSegment(CurrentTracedLevel, SegmentStart, SegmentEnd, bValidPlacement, WallHeight);
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

void ABuildRoomTool::BroadcastRelease_Implementation()
{
	if (DragCreateWallArray.IsValidIndex(0))
	{
		// Get BuildServer subsystem
		UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
		if (!BuildServer)
		{
			UE_LOG(LogTemp, Error, TEXT("BuildRoomTool: BuildServer subsystem not found"));
			return;
		}

		BuildServer->SetCurrentLot(CurrentLot);

		// Batch collect walls to create - use WallGraph for O(1) existence checks
		TArray<UWallComponent*> WallsToCreate;
		WallsToCreate.Reserve(DragCreateWallArray.Num());

		for(UWallComponent* Wall : DragCreateWallArray)
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
			BuildServer->BeginBatch(FString::Printf(TEXT("Build Room (%d walls)"), WallsToCreate.Num()));
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
		for(UWallComponent* Walls : DragCreateWallArray)
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

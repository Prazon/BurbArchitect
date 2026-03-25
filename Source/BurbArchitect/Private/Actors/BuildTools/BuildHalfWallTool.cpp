// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuildTools/BuildHalfWallTool.h"

#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"
#include "Net/UnrealNetwork.h"


// Sets default values
ABuildHalfWallTool::ABuildHalfWallTool(): CurrentWallComponent(nullptr)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

void ABuildHalfWallTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

// Called when the game starts or when spawned
void ABuildHalfWallTool::BeginPlay()
{
	Super::BeginPlay();

	// Initialize wall height to half of default wall height
	if (CurrentLot)
	{
		WallHeight = CurrentLot->DefaultWallHeight * 0.5f;
	}
}

void ABuildHalfWallTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up preview cache when tool is destroyed
	ClearPreviewCache();

	if (EndPlayReason != EEndPlayReason::EndPlayInEditor && EndPlayReason != EEndPlayReason::Quit)
	{
		for(UWallComponent* Walls : DragCreateWallArray)
		{
			if (Walls != nullptr)
			{
				Walls->DestroyWall();
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

int64 ABuildHalfWallTool::GenerateWallCacheKey(int32 StartRow, int32 StartColumn, int32 EndRow, int32 EndColumn)
{
	// Encode grid coordinates into int64 for O(1) cache lookup
	// Use bit shifting to pack all coordinates into a single 64-bit key
	int64 Key = (static_cast<int64>(StartRow & 0xFFFF) << 48) |
				(static_cast<int64>(StartColumn & 0xFFFF) << 32) |
				(static_cast<int64>(EndRow & 0xFFFF) << 16) |
				static_cast<int64>(EndColumn & 0xFFFF);
	return Key;
}

void ABuildHalfWallTool::ClearPreviewCache()
{
	// Destroy any remaining preview components in cache
	for (auto& Elem : PreviewWallCache)
	{
		if (Elem.Value && IsValid(Elem.Value))
		{
			Elem.Value->DestroyWall();
		}
	}
	PreviewWallCache.Empty();
}

// Called every frame
void ABuildHalfWallTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildHalfWallTool::CreateWallPreview(int32 Level, int32 Index, FVector StartLocation, FVector Direction)
{
	bool bValidPlacement;
	FVector TileCornerStart = Index > 1 ? PreviousLocation : DragCreateVectors.StartOperation;
	FVector TileCornerEnd;
	CurrentLot->LocationToTileCorner(Level, StartLocation + (Direction * (Index * CurrentLot->GridTileSize) ), TileCornerEnd);
	CurrentWallComponent = CurrentLot->GenerateWallSegment(Level, TileCornerStart, TileCornerEnd, bValidPlacement, WallHeight);
	CurrentWallComponent->bRenderCustomDepth = true;
	CurrentWallComponent->CustomDepthStencilValue = 1;  // Preview stencil value
	bLockToForwardAxis = true;
	DragCreateWallArray.Add(CurrentWallComponent);
	PreviousLocation = TileCornerEnd;
}

void ABuildHalfWallTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
		// Store traced level for use by Drag()
		CurrentTracedLevel = TracedLevel;

		if (CurrentLot->LocationToTileCorner(TracedLevel, MoveLocation/*PlayerController->CursorWorldLocation*/, TargetLocation))
		{
			// Add minimum movement threshold to prevent micro-adjustments and rapid snapping
			// Only update position if we've moved at least 1.0 units
			const float MinimumMovementThreshold = 1.0f;
			const float DistanceMoved = FVector::Dist(GetActorLocation(), TargetLocation);

			if (DistanceMoved >= MinimumMovementThreshold)
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

void ABuildHalfWallTool::Click_Implementation()
{
	PreviousLocation = GetActorLocation();
	DragCreateVectors = {GetActorLocation(),GetActorLocation()};
	DragCreateWallArray.Empty();
}

void ABuildHalfWallTool::Drag_Implementation()
{
	// Constrain cursor position to snapped direction (straight or diagonal)
	FVector ConstrainedCursorPos = ConstrainToLockedDirection(DragCreateVectors.StartOperation, GetActorLocation());

	DragCreateVectors = {DragCreateVectors.StartOperation, ConstrainedCursorPos};

	// Build set of current wall segments needed for this drag operation
	TSet<int64> CurrentWallSet;
	TArray<UWallComponent*> WallsToAdd;

	// Calculate actual drag distance and direction
	FVector DragVector = DragCreateVectors.EndOperation - DragCreateVectors.StartOperation;

	// Get the snapped direction for consistency across all segments
	FVector SnappedDirection = SnapToAllowedDirection(DragVector);

	if (SnappedDirection.IsNearlyZero())
	{
		// Not enough drag distance, clear walls and return
		DragCreateWallArray.Empty();
		ClearPreviewCache();
		return;
	}

	// Calculate number of segments
	// For axis-aligned: one segment = GridTileSize
	// For diagonal: one segment = sqrt(2) * GridTileSize (diagonal of grid square)
	FVector NormalizedDirection = SnappedDirection.GetSafeNormal();
	float DragDistanceInDirection = FVector::DotProduct(DragVector, NormalizedDirection);

	// Check if diagonal (both X and Y components non-zero)
	bool bIsDiagonal = (FMath::Abs(SnappedDirection.X) > 0.01f && FMath::Abs(SnappedDirection.Y) > 0.01f);
	float SegmentDistance = bIsDiagonal ? (CurrentLot->GridTileSize * FMath::Sqrt(2.0f)) : CurrentLot->GridTileSize;

	int32 NumSegments = FMath::Max(1, FMath::FloorToInt32(FMath::Abs(DragDistanceInDirection) / SegmentDistance));

	FVector PrevLoc = DragCreateVectors.StartOperation;

	for (int32 SegmentIndex = 1; SegmentIndex <= NumSegments; SegmentIndex++)
	{
		FVector TileCornerStart = SegmentIndex > 1 ? PrevLoc : DragCreateVectors.StartOperation;
		FVector TileCornerEnd;

		// Step along grid using GridTileSize increments
		// For diagonal: use unnormalized (1,1) so GridTileSize * (1,1) = one diagonal grid step
		// For straight: use (1,0) or (0,1) so GridTileSize * direction = one grid step
		FVector TargetPosition = DragCreateVectors.StartOperation + (SnappedDirection * (SegmentIndex * CurrentLot->GridTileSize));

		CurrentLot->LocationToTileCorner(CurrentTracedLevel, TargetPosition, TileCornerEnd);

		// Get grid coordinates for cache key
		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (CurrentLot->LocationToTile(TileCornerStart, StartRow, StartColumn) &&
			CurrentLot->LocationToTile(TileCornerEnd, EndRow, EndColumn))
		{
			int64 WallKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
			CurrentWallSet.Add(WallKey);

			// Check if we already have a preview component for this wall segment
			UWallComponent** ExistingWall = PreviewWallCache.Find(WallKey);

			if (!ExistingWall || !*ExistingWall)
			{
				// Component doesn't exist - create new one
				bool bValidPlacement;
				UWallComponent* NewWall = CurrentLot->GenerateWallSegment(CurrentTracedLevel, TileCornerStart, TileCornerEnd, bValidPlacement, WallHeight);
				NewWall->bRenderCustomDepth = true;
				NewWall->CustomDepthStencilValue = 1;  // Preview stencil value
				PreviewWallCache.Add(WallKey, NewWall);
				WallsToAdd.Add(NewWall);
			}
			else
			{
				// Reuse existing cached wall
				WallsToAdd.Add(*ExistingWall);
			}
		}

		PrevLoc = TileCornerEnd;
	}

	// Remove preview components that are no longer in the current selection
	TArray<int64> WallsToRemove;
	for (auto& Elem : PreviewWallCache)
	{
		if (!CurrentWallSet.Contains(Elem.Key))
		{
			// This wall is no longer in selection - destroy it
			if (Elem.Value)
			{
				Elem.Value->DestroyWall();
			}
			WallsToRemove.Add(Elem.Key);
		}
	}

	// Remove destroyed components from cache
	for (const int64& WallToRemove : WallsToRemove)
	{
		PreviewWallCache.Remove(WallToRemove);
	}

	// Update DragCreateWallArray with current walls
	DragCreateWallArray = WallsToAdd;
	bLockToForwardAxis = true;
}

void ABuildHalfWallTool::BroadcastRelease_Implementation()
{
	if (DragCreateWallArray.IsValidIndex(0))
	{
		// Half walls are decorative only - they do NOT create WallGraph edges
		// This means they won't participate in room detection or block movement
		// We create rendering-only walls by directly calling WallComponent->CommitWallSection()

		// Build spatial hash map ONCE using grid coordinates for O(1) lookup instead of O(n) per wall
		// Key: encoded grid coordinates, Value: FWallSegmentData pointer
		TMap<int64, FWallSegmentData*> ExistingWallMap;
		ExistingWallMap.Reserve(CurrentLot->WallComponent->WallDataArray.Num());

		for (FWallSegmentData& ExistingWall : CurrentLot->WallComponent->WallDataArray)
		{
			if (ExistingWall.bCommitted && ExistingWall.Level == CurrentTracedLevel)
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
				if (ForwardKey != ReverseKey) // Avoid duplicate entry for walls with same start/end
				{
					ExistingWallMap.Add(ReverseKey, &ExistingWall);
				}
			}
		}

		// Batch collect walls to remove/create
		TArray<FWallSegmentData*> WallsToRemove;
		TArray<UWallComponent*> WallsToCreate;

		WallsToRemove.Reserve(DragCreateWallArray.Num());
		WallsToCreate.Reserve(DragCreateWallArray.Num());

		for(UWallComponent* Wall : DragCreateWallArray)
		{
			// Get grid coordinates for O(1) lookup
			int32 StartRow, StartColumn, EndRow, EndColumn;
			if (!CurrentLot->LocationToTile(Wall->WallData.StartLoc, StartRow, StartColumn) ||
				!CurrentLot->LocationToTile(Wall->WallData.EndLoc, EndRow, EndColumn))
			{
				continue; // Invalid location
			}

			int64 WallKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
			FWallSegmentData** ExistingWallPtr = ExistingWallMap.Find(WallKey);

			if (bDeletionMode)
			{
				if (ExistingWallPtr && *ExistingWallPtr)
				{
					WallsToRemove.Add(*ExistingWallPtr);
				}
			}
			else
			{
				if (Wall->bValidPlacement && !ExistingWallPtr)
				{
					// No existing wall found - store preview component for creation
					WallsToCreate.Add(Wall);
				}
			}
		}

		// Collect indices of walls that need regeneration after deletion
		TArray<int32> WallsToRegenerateAfterDeletion;

		// Process removals - directly remove from WallComponent
		for (FWallSegmentData* WallToRemove : WallsToRemove)
		{
			// Before removing, find connected half walls that need regeneration
			// (they will need their mitring recalculated after this wall is gone)
			for (int32 ConnectedIdx : WallToRemove->ConnectedWallsSections)
			{
				if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(ConnectedIdx))
				{
					FWallSegmentData& ConnectedWall = CurrentLot->WallComponent->WallDataArray[ConnectedIdx];
					// Only regenerate other decorative walls with matching height
					if (ConnectedWall.WallEdgeID == -1 &&
						FMath::IsNearlyEqual(ConnectedWall.Height, WallToRemove->Height, 1.0f))
					{
						WallsToRegenerateAfterDeletion.AddUnique(ConnectedIdx);
					}
				}
			}

			// Find and remove from WallDataArray
			CurrentLot->WallComponent->WallDataArray.RemoveAll([WallToRemove](const FWallSegmentData& Wall)
			{
				return &Wall == WallToRemove;
			});
		}

		// Regenerate connected half walls after deletion (update their mitring)
		for (int32 WallIdx : WallsToRegenerateAfterDeletion)
		{
			if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIdx))
			{
				FWallSegmentData& WallToRegen = CurrentLot->WallComponent->WallDataArray[WallIdx];
				// Clear old connection data before regenerating
				WallToRegen.ConnectedWallsAtStartDir.Empty();
				WallToRegen.ConnectedWallsAtEndDir.Empty();
				WallToRegen.ConnectedWallsSections.Empty();
				CurrentLot->WallComponent->RegenerateWallSection(WallToRegen, false);
			}
		}

		// Track newly created wall indices for second-pass regeneration
		TArray<int32> NewlyCreatedWallIndices;
		NewlyCreatedWallIndices.Reserve(WallsToCreate.Num());

		// Process creations - use correct API flow (matching WallCommand::Commit)
		for (UWallComponent* PreviewWall : WallsToCreate)
		{
			// 1. Generate wall section - this ADDS to WallDataArray and assigns valid WallArrayIndex
			//    This is the critical step that was missing - CommitWallSection expects a wall
			//    that already exists in WallDataArray with a valid index
			FWallSegmentData NewWallData = CurrentLot->WallComponent->GenerateWallSection(
				CurrentTracedLevel,
				PreviewWall->WallData.StartLoc,
				PreviewWall->WallData.EndLoc,
				WallHeight);

			// 2. Mark as decorative (no room detection) - this must happen AFTER GenerateWallSection
			//    because GenerateWallSection initializes the struct
			NewWallData.WallEdgeID = -1;

			// 3. Commit with valid WallArrayIndex - now CommitWallSection can find the wall
			//    in WallDataArray and apply materials/patterns
			CurrentLot->WallComponent->CommitWallSection(NewWallData, DefaultWallPattern, BaseMaterial);

			// Track for second-pass regeneration
			NewlyCreatedWallIndices.Add(NewWallData.WallArrayIndex);
		}

		// Second pass: Regenerate all newly created walls so they can find each other
		// This is necessary because when placing multiple walls at once, earlier walls
		// won't find later walls during their initial regeneration
		if (NewlyCreatedWallIndices.Num() > 1)
		{
			for (int32 WallIdx : NewlyCreatedWallIndices)
			{
				if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallIdx))
				{
					FWallSegmentData& WallToRegen = CurrentLot->WallComponent->WallDataArray[WallIdx];
					// Clear old connection data before regenerating
					WallToRegen.ConnectedWallsAtStartDir.Empty();
					WallToRegen.ConnectedWallsAtEndDir.Empty();
					WallToRegen.ConnectedWallsSections.Empty();
					// Non-recursive to avoid infinite loops - we're handling all walls explicitly
					CurrentLot->WallComponent->RegenerateWallSection(WallToRegen, false);
				}
			}
		}

		bLockToForwardAxis = false;
		OnReleased();

		// Clean up preview components
		for(UWallComponent* Walls : DragCreateWallArray)
		{
			if (Walls)
			{
				Walls->DestroyWall();
			}
		}

		// Clear the preview cache
		ClearPreviewCache();
	}
}

FVector ABuildHalfWallTool::SnapToAllowedDirection(const FVector& Direction) const
{
	// Snap to one of 8 allowed directions: N, S, E, W, NE, NW, SE, SW
	// This ensures walls are either axis-aligned (horizontal/vertical) or 45-degree diagonal

	if (Direction.IsNearlyZero(0.01f))
	{
		return FVector::ZeroVector;
	}

	FVector Normalized = Direction.GetSafeNormal2D();
	float AbsX = FMath::Abs(Normalized.X);
	float AbsY = FMath::Abs(Normalized.Y);

	// Determine if this is closer to axis-aligned or diagonal
	const float DiagonalThreshold = 0.4f; // ~22.5 degrees tolerance

	FVector SnappedDirection;

	// If one component is much stronger, snap to axis-aligned
	if (AbsX > AbsY + DiagonalThreshold)
	{
		// Horizontal (East or West)
		SnappedDirection = FVector(FMath::Sign(Normalized.X), 0.0f, 0.0f);
	}
	else if (AbsY > AbsX + DiagonalThreshold)
	{
		// Vertical (North or South)
		SnappedDirection = FVector(0.0f, FMath::Sign(Normalized.Y), 0.0f);
	}
	else
	{
		// Diagonal (NE, NW, SE, SW)
		// Don't normalize - we want (1,1) so that multiplying by GridTileSize gives proper diagonal step
		SnappedDirection = FVector(FMath::Sign(Normalized.X), FMath::Sign(Normalized.Y), 0.0f);
	}

	return SnappedDirection;
}

FVector ABuildHalfWallTool::ConstrainToLockedDirection(const FVector& StartPos, const FVector& CurrentPos) const
{
	FVector DragVector = CurrentPos - StartPos;

	// Need some minimum movement before constraining direction
	if (DragVector.Size2D() < CurrentLot->GridTileSize * 0.15f)
	{
		return StartPos; // Not enough movement yet, stay at start
	}

	// Dynamically snap to the nearest allowed direction based on current cursor position
	// This allows the user to change between straight and diagonal as they move
	FVector CurrentDirection = SnapToAllowedDirection(DragVector);

	// If no valid direction yet, stay at start
	if (CurrentDirection.IsNearlyZero())
	{
		return StartPos;
	}

	// Project the cursor position onto the snapped direction line
	// Use normalized direction for accurate projection distance
	FVector NormalizedDirection = CurrentDirection.GetSafeNormal();
	float ProjectionDistance = FVector::DotProduct(DragVector, NormalizedDirection);

	// Constrain to current snapped direction using normalized direction
	return StartPos + (NormalizedDirection * ProjectionDistance);
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildFenceTool.h"
#include "Actors/LotManager.h"
#include "Components/FenceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/FenceItem.h"
#include "Engine/StaticMesh.h"

ABuildFenceTool::ABuildFenceTool()
{
	PrimaryActorTick.bCanEverTick = false;
	CurrentFenceItem = nullptr;
}

void ABuildFenceTool::BeginPlay()
{
	Super::BeginPlay();

	// Initialize fence height from CurrentFenceItem if available
	if (CurrentFenceItem)
	{
		WallHeight = CurrentFenceItem->FenceHeight;
	}
}

void ABuildFenceTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up preview meshes when tool ends
	ClearPreviewMeshes();

	Super::EndPlay(EndPlayReason);
}

void ABuildFenceTool::Drag_Implementation()
{
	if (!CurrentFenceItem || !CurrentLot || !CurrentLot->FenceComponent)
	{
		return;
	}

	// Constrain cursor position to snapped direction (straight or diagonal)
	FVector ConstrainedCursorPos = ConstrainToLockedDirection(DragCreateVectors.StartOperation, GetActorLocation());

	DragCreateVectors = {DragCreateVectors.StartOperation, ConstrainedCursorPos};

	// Build set of current fence segments needed for this drag operation
	TSet<int64> CurrentFenceSet;
	TArray<UStaticMeshComponent*> PanelsToAdd;
	TArray<UStaticMeshComponent*> PostsToAdd;

	// Calculate actual drag distance and direction
	FVector DragVector = DragCreateVectors.EndOperation - DragCreateVectors.StartOperation;

	// Get the snapped direction for consistency across all segments
	FVector SnappedDirection = SnapToAllowedDirection(DragVector);

	if (SnappedDirection.IsNearlyZero())
	{
		// Not enough drag distance, clear previews and return
		ClearPreviewMeshes();
		return;
	}

	// Calculate number of segments
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
		FVector TargetPosition = DragCreateVectors.StartOperation + (SnappedDirection * (SegmentIndex * CurrentLot->GridTileSize));

		CurrentLot->LocationToTileCorner(CurrentTracedLevel, TargetPosition, TileCornerEnd);

		// Get grid coordinates for cache key
		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (CurrentLot->LocationToTile(TileCornerStart, StartRow, StartColumn) &&
			CurrentLot->LocationToTile(TileCornerEnd, EndRow, EndColumn))
		{
			int64 FenceKey = GenerateWallCacheKey(StartRow, StartColumn, EndRow, EndColumn);
			CurrentFenceSet.Add(FenceKey);

			// Check if we already have a preview for this fence segment
			TArray<UStaticMeshComponent*>* ExistingPreview = PreviewFenceCache.Find(FenceKey);

			if (!ExistingPreview)
			{
				// Create new preview for this segment
				TArray<UStaticMeshComponent*> NewPreviewMeshes;

				// Calculate post positions
				TArray<FVector> PostPositions = CalculatePreviewPostPositions(TileCornerStart, TileCornerEnd);

				// Spawn posts
				for (const FVector& PostPos : PostPositions)
				{
					UStaticMeshComponent* Post = SpawnPostPreview(PostPos, CurrentTracedLevel);
					if (Post)
					{
						NewPreviewMeshes.Add(Post);
						PostsToAdd.Add(Post);
					}
				}

				// Spawn panels between posts
				for (int32 i = 0; i < PostPositions.Num() - 1; i++)
				{
					UStaticMeshComponent* Panel = SpawnPanelPreview(PostPositions[i], PostPositions[i + 1], CurrentTracedLevel);
					if (Panel)
					{
						NewPreviewMeshes.Add(Panel);
						PanelsToAdd.Add(Panel);
					}
				}

				// Cache this preview
				PreviewFenceCache.Add(FenceKey, NewPreviewMeshes);
			}
			else
			{
				// Reuse existing cached preview
				for (UStaticMeshComponent* Mesh : *ExistingPreview)
				{
					// Separate panels and posts for tracking
					if (Mesh && IsValid(Mesh))
					{
						// Simple heuristic: if it has the panel mesh, it's a panel
						if (CurrentFenceItem->FencePanelMesh.IsValid() &&
							Mesh->GetStaticMesh() == CurrentFenceItem->FencePanelMesh.LoadSynchronous())
						{
							PanelsToAdd.Add(Mesh);
						}
						else
						{
							PostsToAdd.Add(Mesh);
						}
					}
				}
			}
		}

		PrevLoc = TileCornerEnd;
	}

	// Remove preview meshes that are no longer in the current selection
	TArray<int64> PreviewsToRemove;
	for (auto& Elem : PreviewFenceCache)
	{
		if (!CurrentFenceSet.Contains(Elem.Key))
		{
			// This fence segment is no longer in selection - destroy its meshes
			for (UStaticMeshComponent* Mesh : Elem.Value)
			{
				if (Mesh && IsValid(Mesh))
				{
					Mesh->DestroyComponent();
				}
			}
			PreviewsToRemove.Add(Elem.Key);
		}
	}

	// Remove destroyed previews from cache
	for (const int64& PreviewToRemove : PreviewsToRemove)
	{
		PreviewFenceCache.Remove(PreviewToRemove);
	}

	// Update preview arrays with current meshes
	PreviewPanelMeshes = PanelsToAdd;
	PreviewPostMeshes = PostsToAdd;
	bLockToForwardAxis = true;
}

void ABuildFenceTool::BroadcastRelease_Implementation()
{
	if (!CurrentFenceItem || !CurrentLot || !CurrentLot->FenceComponent)
	{
		return;
	}

	if (PreviewPanelMeshes.Num() == 0 && PreviewPostMeshes.Num() == 0)
	{
		return;
	}

	// Build set of unique fence segments to create
	TSet<int64> ProcessedSegments;

	// Iterate through preview cache to get all fence segments
	for (auto& Elem : PreviewFenceCache)
	{
		int64 FenceKey = Elem.Key;

		if (ProcessedSegments.Contains(FenceKey))
		{
			continue;
		}

		// Decode fence key to get grid coordinates
		int32 StartRow = (FenceKey >> 48) & 0xFFFF;
		int32 StartColumn = (FenceKey >> 32) & 0xFFFF;
		int32 EndRow = (FenceKey >> 16) & 0xFFFF;
		int32 EndColumn = FenceKey & 0xFFFF;

		// Convert grid coordinates to world positions
		FVector StartLoc = FVector::ZeroVector;
		FVector EndLoc = FVector::ZeroVector;
		CurrentLot->TileToGridLocation(CurrentTracedLevel, StartRow, StartColumn, true, StartLoc);
		CurrentLot->TileToGridLocation(CurrentTracedLevel, EndRow, EndColumn, true, EndLoc);

		// Check if this fence already exists (for deletion mode)
		bool bShouldCreate = true;

		if (bDeletionMode)
		{
			// Find and remove existing fence segment
			int32 ExistingFenceIndex = CurrentLot->FenceComponent->FindFenceSegmentAtLocation((StartLoc + EndLoc) * 0.5f, 50.0f);
			if (ExistingFenceIndex != -1)
			{
				CurrentLot->FenceComponent->RemoveFenceSegment(ExistingFenceIndex);
			}
			bShouldCreate = false;
		}

		if (bShouldCreate)
		{
			// Create fence segment using FenceComponent
			CurrentLot->FenceComponent->GenerateFenceSegment(CurrentTracedLevel, StartLoc, EndLoc, CurrentFenceItem);
		}

		ProcessedSegments.Add(FenceKey);
	}

	bLockToForwardAxis = false;
	OnReleased();

	// Clean up preview meshes
	ClearPreviewMeshes();
}

UStaticMeshComponent* ABuildFenceTool::SpawnPanelPreview(const FVector& Start, const FVector& End, int32 Level)
{
	if (!CurrentFenceItem || !CurrentFenceItem->FencePanelMesh.IsValid())
	{
		return nullptr;
	}

	// Create panel mesh component
	UStaticMeshComponent* PanelMesh = NewObject<UStaticMeshComponent>(this);
	if (!PanelMesh)
	{
		return nullptr;
	}

	// Load and set static mesh
	UStaticMesh* Mesh = CurrentFenceItem->FencePanelMesh.LoadSynchronous();
	if (!Mesh)
	{
		PanelMesh->DestroyComponent();
		return nullptr;
	}
	PanelMesh->SetStaticMesh(Mesh);

	// Calculate panel transform
	FVector PanelCenter = (Start + End) * 0.5f;
	FVector PanelDir = (End - Start).GetSafeNormal();
	FRotator PanelRotation = PanelDir.Rotation();

	PanelMesh->SetWorldLocation(PanelCenter);
	PanelMesh->SetWorldRotation(PanelRotation);

	// Enable custom depth for preview highlighting
	PanelMesh->bRenderCustomDepth = true;
	PanelMesh->CustomDepthStencilValue = 1; // Preview stencil value

	// Disable collision for preview
	PanelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Register component
	PanelMesh->RegisterComponent();

	return PanelMesh;
}

UStaticMeshComponent* ABuildFenceTool::SpawnPostPreview(const FVector& Location, int32 Level)
{
	if (!CurrentFenceItem || !CurrentFenceItem->FencePostMesh.IsValid())
	{
		return nullptr;
	}

	// Create post mesh component
	UStaticMeshComponent* PostMesh = NewObject<UStaticMeshComponent>(this);
	if (!PostMesh)
	{
		return nullptr;
	}

	// Load and set static mesh
	UStaticMesh* Mesh = CurrentFenceItem->FencePostMesh.LoadSynchronous();
	if (!Mesh)
	{
		PostMesh->DestroyComponent();
		return nullptr;
	}
	PostMesh->SetStaticMesh(Mesh);

	// Set post transform
	PostMesh->SetWorldLocation(Location);

	// Enable custom depth for preview highlighting
	PostMesh->bRenderCustomDepth = true;
	PostMesh->CustomDepthStencilValue = 1; // Preview stencil value

	// Disable collision for preview
	PostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Register component
	PostMesh->RegisterComponent();

	return PostMesh;
}

TArray<FVector> ABuildFenceTool::CalculatePreviewPostPositions(const FVector& StartLoc, const FVector& EndLoc)
{
	TArray<FVector> PostPositions;

	if (!CurrentFenceItem)
	{
		return PostPositions;
	}

	FVector FenceDir = (EndLoc - StartLoc).GetSafeNormal();
	float FenceLength = FVector::Dist(StartLoc, EndLoc);

	// Start with end posts (if enabled)
	TSet<float> PostDistances; // Distances along fence

	if (CurrentFenceItem->bPostsAtEnds)
	{
		PostDistances.Add(0.0f);         // Start
		PostDistances.Add(FenceLength);  // End
	}

	// Add intermediate posts based on PostSpacing
	if (CurrentFenceItem->PostSpacing > 0 && CurrentLot)
	{
		float PostInterval = CurrentLot->GridTileSize * CurrentFenceItem->PostSpacing;

		for (float Dist = PostInterval; Dist < FenceLength; Dist += PostInterval)
		{
			PostDistances.Add(Dist);
		}
	}

	// Convert distances to world positions
	TArray<float> SortedDistances = PostDistances.Array();
	SortedDistances.Sort();

	for (float Dist : SortedDistances)
	{
		// Clamp to fence bounds
		Dist = FMath::Clamp(Dist, 0.0f, FenceLength);
		FVector PostPos = StartLoc + (FenceDir * Dist);
		PostPositions.Add(PostPos);
	}

	return PostPositions;
}

void ABuildFenceTool::ClearPreviewMeshes()
{
	// Destroy all preview meshes in cache
	for (auto& Elem : PreviewFenceCache)
	{
		for (UStaticMeshComponent* Mesh : Elem.Value)
		{
			if (Mesh && IsValid(Mesh))
			{
				Mesh->DestroyComponent();
			}
		}
	}

	PreviewFenceCache.Empty();
	PreviewPanelMeshes.Empty();
	PreviewPostMeshes.Empty();
}

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildDoorTool.h"
#include "BuildGateTool.generated.h"

/**
 * Build Gate Tool - Places gate portals on fences with preview
 * Extends BuildDoorTool to reuse portal placement logic
 *
 * Key Difference: Detects FENCES instead of WALLS
 * - Uses FenceComponent->FindFenceSegmentAtLocation() instead of WallComponent hit detection
 * - Spawns AGateBase instead of ADoorBase
 * - Triggers fence panel/post regeneration when gate is placed
 */
UCLASS()
class BURBARCHITECT_API ABuildGateTool : public ABuildDoorTool
{
	GENERATED_BODY()

public:
	ABuildGateTool();

protected:
	virtual void BeginPlay() override;

	// ==================== TOOL OVERRIDES ====================

	/** Override to detect FENCES instead of walls */
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed,
									FHitResult CursorWorldHitResult, int32 TracedLevel) override;

	/** Override to spawn gate on fence */
	virtual void BroadcastRelease_Implementation() override;

private:
	// ==================== FENCE DETECTION ====================

	/** Currently hovered fence segment index */
	int32 HoveredFenceIndex;

	/** Detect fence segment from hit result
	 *  @param HitResult - Cursor hit result
	 *  @param OutFenceIndex - Output fence segment index
	 *  @return true if fence hit, false otherwise
	 */
	bool DetectFenceSegment(const FHitResult& HitResult, int32& OutFenceIndex);

	/** Validate gate placement on fence (bounds, overlap check)
	 *  @param FenceIndex - Fence segment index
	 *  @param Location - Proposed gate location
	 *  @return true if valid placement, false otherwise
	 */
	bool ValidateGatePlacement(int32 FenceIndex, const FVector& Location);

	/** Snap gate position along fence segment
	 *  @param FenceIndex - Fence segment index
	 *  @param CursorLocation - Raw cursor location
	 *  @return Snapped gate location
	 */
	FVector SnapToFence(int32 FenceIndex, const FVector& CursorLocation);

	/** Calculate gate rotation to match fence direction
	 *  @param FenceIndex - Fence segment index
	 *  @return Gate rotation aligned with fence
	 */
	FRotator CalculateFenceRotation(int32 FenceIndex);
};

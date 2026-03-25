// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildHalfWallTool.h"
#include "BuildFenceTool.generated.h"

// Forward declarations
class UFenceItem;
class UStaticMeshComponent;

/**
 * Build Fence Tool - Creates decorative fences with static mesh panels and posts
 * Extends BuildHalfWallTool to inherit drag/snap behavior
 *
 * IMPORTANT: Fences are DECORATIVE ONLY and do NOT participate in room detection
 * - Uses FenceComponent instead of WallComponent
 * - Spawns static mesh components (panels + posts) instead of procedural walls
 * - Supports multi-level placement
 * - Has collision for character blocking
 */
UCLASS()
class BURBARCHITECT_API ABuildFenceTool : public ABuildHalfWallTool
{
	GENERATED_BODY()

public:
	ABuildFenceTool();

	// ==================== FENCE TOOL PROPERTIES ====================

	/** Current fence item being placed (defines meshes, post spacing, height) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Fence Tool")
	UFenceItem* CurrentFenceItem;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== TOOL OVERRIDES ====================

	/** Override to spawn fence mesh previews instead of wall previews */
	virtual void Drag_Implementation() override;

	/** Override to finalize fence placement using FenceComponent */
	virtual void BroadcastRelease_Implementation() override;

private:
	// ==================== PREVIEW MESHES ====================

	/** Preview panel mesh components during drag */
	UPROPERTY()
	TArray<UStaticMeshComponent*> PreviewPanelMeshes;

	/** Preview post mesh components during drag */
	UPROPERTY()
	TArray<UStaticMeshComponent*> PreviewPostMeshes;

	/** Cache for fence preview (grid-based lookup) */
	TMap<int64, TArray<UStaticMeshComponent*>> PreviewFenceCache;

	// ==================== HELPER METHODS ====================

	/** Spawn fence panel preview mesh
	 *  @param Start - Panel start location
	 *  @param End - Panel end location
	 *  @param Level - Floor level
	 *  @return Spawned preview panel mesh
	 */
	UStaticMeshComponent* SpawnPanelPreview(const FVector& Start, const FVector& End, int32 Level);

	/** Spawn fence post preview mesh
	 *  @param Location - Post location
	 *  @param Level - Floor level
	 *  @return Spawned preview post mesh
	 */
	UStaticMeshComponent* SpawnPostPreview(const FVector& Location, int32 Level);

	/** Calculate post positions for fence segment (no gates during preview)
	 *  @param StartLoc - Fence start location
	 *  @param EndLoc - Fence end location
	 *  @return Array of post positions
	 */
	TArray<FVector> CalculatePreviewPostPositions(const FVector& StartLoc, const FVector& EndLoc);

	/** Clean up all preview meshes */
	void ClearPreviewMeshes();
};

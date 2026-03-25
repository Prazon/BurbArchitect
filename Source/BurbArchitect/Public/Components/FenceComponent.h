// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "FenceComponent.generated.h"

// Forward declarations
class UFenceItem;
class AGateBase;
class UStaticMeshComponent;
class ALotManager;

/**
 * Fence Segment Data - Stores information about a single fence segment
 * Parallel to FWallSegmentData in WallComponent
 */
USTRUCT(BlueprintType)
struct FFenceSegmentData
{
	GENERATED_BODY()

	/** Start location (world space) */
	UPROPERTY(BlueprintReadOnly, Category = "Fence")
	FVector StartLoc;

	/** End location (world space) */
	UPROPERTY(BlueprintReadOnly, Category = "Fence")
	FVector EndLoc;

	/** Floor level (0 = ground, 1 = second floor, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "Fence")
	int32 Level;

	/** Fence data asset (meshes, post spacing, height) */
	UPROPERTY(BlueprintReadOnly, Category = "Fence")
	UFenceItem* FenceItem;

	/** Spawned panel mesh components */
	UPROPERTY()
	TArray<UStaticMeshComponent*> PanelMeshes;

	/** Spawned post mesh components */
	UPROPERTY()
	TArray<UStaticMeshComponent*> PostMeshes;

	/** Gates placed on this fence segment */
	UPROPERTY()
	TArray<AGateBase*> Gates;

	/** Whether this segment is committed (finalized) */
	UPROPERTY()
	bool bCommitted;

	FFenceSegmentData()
		: StartLoc(FVector::ZeroVector)
		, EndLoc(FVector::ZeroVector)
		, Level(0)
		, FenceItem(nullptr)
		, bCommitted(false)
	{}
};

/**
 * Fence Component - Manages all fence segments on a lot
 * Parallel architecture to WallComponent
 * Handles fence rendering, collision, and gate integration
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class BURBARCHITECT_API UFenceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UFenceComponent();

	// ==================== FENCE DATA ====================

	/** All fence segments on this lot */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Fence")
	TArray<FFenceSegmentData> FenceDataArray;

	// ==================== FENCE CREATION ====================

	/** Generate fence segment with panels and posts
	 *  @param Level - Floor level (0 = ground, 1 = second floor, etc.)
	 *  @param StartLoc - World start position (grid corner)
	 *  @param EndLoc - World end position (grid corner)
	 *  @param FenceItem - Fence data asset (meshes, post spacing, etc.)
	 *  @return Index of created fence segment in FenceDataArray
	 */
	UFUNCTION(BlueprintCallable, Category = "Fence")
	int32 GenerateFenceSegment(int32 Level, FVector StartLoc, FVector EndLoc, UFenceItem* FenceItem);

	/** Remove fence segment by index */
	UFUNCTION(BlueprintCallable, Category = "Fence")
	void RemoveFenceSegment(int32 FenceIndex);

	/** Find fence segment at world location (for gate placement)
	 *  @param Location - World location to search near
	 *  @param Tolerance - Search radius tolerance
	 *  @return Index in FenceDataArray, or -1 if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "Fence")
	int32 FindFenceSegmentAtLocation(FVector Location, float Tolerance = 50.0f);

	// ==================== GATE INTEGRATION ====================

	/** Add gate to fence segment - forces posts on each side, adjusts panels
	 *  @param FenceIndex - Index in FenceDataArray
	 *  @param GateLocation - World position of gate center
	 *  @param GateWidth - Width of gate opening
	 *  @param Gate - Spawned gate actor
	 */
	UFUNCTION(BlueprintCallable, Category = "Fence")
	void AddGateToFence(int32 FenceIndex, FVector GateLocation, float GateWidth, AGateBase* Gate);

	/** Remove gate from fence segment - regenerates panels/posts
	 *  @param FenceIndex - Index in FenceDataArray
	 *  @param Gate - Gate to remove
	 */
	UFUNCTION(BlueprintCallable, Category = "Fence")
	void RemoveGateFromFence(int32 FenceIndex, AGateBase* Gate);

protected:
	/** Spawn fence panel mesh at location
	 *  @param Start - Panel start location
	 *  @param End - Panel end location
	 *  @param FenceItem - Fence data asset
	 *  @param Level - Floor level
	 *  @return Spawned panel mesh component
	 */
	UStaticMeshComponent* SpawnFencePanel(const FVector& Start, const FVector& End,
										 UFenceItem* FenceItem, int32 Level);

	/** Spawn fence post mesh at location
	 *  @param Location - Post location
	 *  @param FenceItem - Fence data asset
	 *  @param Level - Floor level
	 *  @return Spawned post mesh component
	 */
	UStaticMeshComponent* SpawnFencePost(const FVector& Location,
										UFenceItem* FenceItem, int32 Level);

	/** Regenerate panels for fence segment (called when gates added/removed)
	 *  @param FenceIndex - Index in FenceDataArray
	 */
	void RegenerateFencePanels(int32 FenceIndex);

	/** Calculate post positions for fence segment (accounts for gates)
	 *  @param FenceData - Fence segment data
	 *  @return Array of post positions in world space
	 */
	TArray<FVector> CalculatePostPositions(const FFenceSegmentData& FenceData);

	/** Get owning LotManager (cached for performance) */
	ALotManager* GetLotManager();

private:
	/** Cached reference to owning LotManager */
	UPROPERTY()
	ALotManager* CachedLotManager;
};

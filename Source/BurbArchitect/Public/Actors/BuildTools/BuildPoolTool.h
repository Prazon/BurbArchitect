// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildRoomTool.h"
#include "BuildPoolTool.generated.h"

/**
 * BuildPoolTool - Creates pool rooms one level below current floor.
 *
 * When viewing ground floor (Level 1): Builds pool at Level 0
 * Essentially the same as BuildBasementTool but for pools.
 */
UCLASS()
class BURBARCHITECT_API ABuildPoolTool : public ABuildRoomTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuildPoolTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Override to snap to pool level grid
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;

	// Override to generate pool wall previews at target level
	virtual void Drag_Implementation() override;

	// Override to commit pool walls
	virtual void BroadcastRelease_Implementation() override;

	// Default water material for pool water volumes (exposed to BP)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool")
	UMaterialInstance* DefaultWaterMaterial;

	// Floor pattern for pool bottom tiles
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool")
	class UFloorPattern* PoolFloorPattern;

	// Material for pool bottom tiles
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool")
	UMaterialInstance* PoolFloorMaterial;

	// Z offset for water surface (negative to lower below ground level)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool")
	float WaterSurfaceZOffset = -5.0f;

protected:
	/**
	 * Helper function to determine the actual level to build on.
	 * Returns one level below current traced level.
	 */
	int32 GetTargetLevel() const;

	/**
	 * Check if a wall segment is an existing committed pool wall.
	 * @param StartRow, StartCol - Grid coords of wall start
	 * @param EndRow, EndCol - Grid coords of wall end
	 * @param Level - Level to check
	 * @return EdgeID if pool wall exists, -1 otherwise
	 */
	int32 FindExistingPoolWall(int32 StartRow, int32 StartCol, int32 EndRow, int32 EndCol, int32 Level) const;

	// Edge IDs of existing pool walls to remove (shared with adjacent pools)
	TArray<int32> PoolWallsToRemove;

	// Room IDs of existing pools that will be merged (for water volume cleanup)
	TSet<int32> PoolRoomsToMerge;
};

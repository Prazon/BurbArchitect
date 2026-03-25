// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildRoomTool.h"
#include "BuildBasementTool.generated.h"

/**
 * BuildBasementTool - Allows building basement rooms while viewing the ground floor.
 *
 * When viewing ground floor (Level 0): Builds rooms at basement level (Level -1)
 * When viewing basement (Level < 0): Behaves as normal BuildRoomTool
 */
UCLASS()
class BURBARCHITECT_API ABuildBasementTool : public ABuildRoomTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuildBasementTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Override to snap to basement grid when on ground floor
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;

	// Override to generate basement wall previews
	virtual void Drag_Implementation() override;

	// Override to commit basement walls
	virtual void BroadcastRelease_Implementation() override;

protected:
	/**
	 * Helper function to determine the actual level to build on.
	 * Returns basement level (CurrentLevel - 1) when viewing ground floor,
	 * otherwise returns current level for normal basement view behavior.
	 */
	int32 GetTargetLevel() const;
};

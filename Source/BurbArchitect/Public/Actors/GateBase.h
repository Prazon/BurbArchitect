// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/DoorBase.h"
#include "GateBase.generated.h"

// Forward declarations
class ALotManager;

/**
 * Gate portal actor - Extends DoorBase to reuse animation system
 * Placed on fences instead of walls (parallel to doors on walls)
 * Forces posts on each side of gate and adjusts fence panels dynamically
 * Reuses skeletal mesh animation from DoorBase (OpenDoor, CloseDoor, etc.)
 */
UCLASS()
class BURBARCHITECT_API AGateBase : public ADoorBase
{
	GENERATED_BODY()

public:
	AGateBase();

protected:
	virtual void BeginPlay() override;

public:
	// ==================== FENCE INTEGRATION ====================

	/** Index of fence segment this gate is placed on */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Gate")
	int32 FenceSegmentIndex;

	/** Reference to owning LotManager */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Gate")
	ALotManager* OwningLot;

	// ==================== GATE LIFECYCLE ====================

	/** Called when gate is placed - forces posts on each side of gate
	 *  Tells FenceComponent to regenerate panels with gate cutout
	 */
	UFUNCTION(BlueprintCallable, Category = "Gate")
	void OnGatePlaced();

	/** Override deletion to regenerate fence panels/posts
	 *  Removes gate from fence and regenerates without cutout
	 */
	virtual void OnDeleted_Implementation() override;
};

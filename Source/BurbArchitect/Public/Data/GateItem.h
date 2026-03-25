// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/DoorItem.h"
#include "GateItem.generated.h"

// Forward declarations
class USkeletalMesh;
class UStaticMesh;

/**
 * Catalog item representing a gate with portal settings and snapping behavior
 * Extends DoorItem to inherit portal properties (size, offset, snapping)
 * Gates are placed on fences (parallel to doors on walls)
 * Reuses door animation system from DoorBase/ADoorBase
 */
UCLASS()
class BURBARCHITECT_API UGateItem : public UDoorItem
{
	GENERATED_BODY()

public:
	UGateItem();

	/** Gate skeletal mesh for animation (can reuse door skeleton) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate|Mesh")
	TSoftObjectPtr<USkeletalMesh> GateSkeletalMesh;

	/** Static mesh for gate panel (replaces door panel) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate|Mesh")
	TSoftObjectPtr<UStaticMesh> GateStaticMesh;

	/** Static mesh for gate frame (replaces door frame) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate|Mesh")
	TSoftObjectPtr<UStaticMesh> GateFrameMesh;
};

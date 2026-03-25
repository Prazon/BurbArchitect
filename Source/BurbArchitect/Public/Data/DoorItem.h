// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/ArchitectureItem.h"
#include "DoorItem.generated.h"

// Forward declarations
class APortalBase;
class UStaticMesh;

/**
 * Catalog item representing a door with portal settings and snapping behavior.
 * Extends ArchitectureItem to include door-specific properties like portal size and snapping.
 * Uses modular static meshes (door panel + frame) that attach to a shared skeletal mesh skeleton.
 */
UCLASS()
class BURBARCHITECT_API UDoorItem : public UArchitectureItem
{
	GENERATED_BODY()

public:
	UDoorItem();

	// Portal shape/size settings (width x height in cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Shape")
	FVector2D PortalSize;

	// Portal position offset from wall placement point (X = horizontal, Y = vertical, in cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Shape")
	FVector2D PortalOffset;

	// Horizontal snapping increment when placing (in cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Snapping")
	float HorizontalSnap;

	// Vertical snapping increment when placing (in cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Snapping")
	float VerticalSnap;

	// Whether this door snaps to floor level
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Snapping")
	bool bSnapsToFloor;

	// The actual portal actor class to spawn when placing this door
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Spawning")
	TSoftClassPtr<APortalBase> ClassToSpawn;

	// Static mesh for the door panel (attaches to skeletal mesh bone in Blueprint)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Mesh")
	TSoftObjectPtr<UStaticMesh> DoorStaticMesh;

	// Static mesh for the door frame (attaches to skeletal mesh bone in Blueprint)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Mesh")
	TSoftObjectPtr<UStaticMesh> DoorFrameMesh;
};

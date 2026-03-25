// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/ArchitectureItem.h"
#include "WindowItem.generated.h"

// Forward declarations
class APortalBase;
class UStaticMesh;

/**
 * Catalog item representing a window with portal settings and snapping behavior.
 * Extends ArchitectureItem to include window-specific properties like portal size and snapping.
 */
UCLASS()
class BURBARCHITECT_API UWindowItem : public UArchitectureItem
{
	GENERATED_BODY()

public:
	UWindowItem();

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

	// Whether this window snaps to floor level
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Snapping")
	bool bSnapsToFloor;

	// The actual portal actor class to spawn when placing this window
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Spawning")
	TSoftClassPtr<APortalBase> ClassToSpawn;

	// Static mesh for the window frame and glass
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal|Mesh")
	TSoftObjectPtr<UStaticMesh> WindowMesh;
};

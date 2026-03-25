// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/CatalogItem.h"
#include "Actors/PlaceableObject.h"
#include "FurnitureItem.generated.h"

/**
 * Defines placement rules and constraints for furniture items.
 * Used by build tools to validate and constrain placement.
 */
USTRUCT(BlueprintType)
struct FPlacementConstraints
{
	GENERATED_BODY()

	// Placement mode determines snap behavior and validation
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	EObjectPlacementMode PlacementMode = EObjectPlacementMode::Floor;

	// === Wall Hanging Settings ===

	// Height above floor for wall-mounted objects (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Wall Hanging", meta = (EditCondition = "PlacementMode == EObjectPlacementMode::WallHanging", EditConditionHides))
	float WallMountHeight = 150.0f;

	// Can rotate 360° on wall surface
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Wall Hanging", meta = (EditCondition = "PlacementMode == EObjectPlacementMode::WallHanging", EditConditionHides))
	bool bCanRotateOnWall = true;

	// Offset from wall surface (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Wall Hanging", meta = (EditCondition = "PlacementMode == EObjectPlacementMode::WallHanging", EditConditionHides))
	float WallOffset = 5.0f;

	// === Floor Placement Settings ===

	// Must be placed adjacent to a wall (toilets, beds)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Floor", meta = (EditCondition = "PlacementMode == EObjectPlacementMode::Floor", EditConditionHides))
	bool bMustBeAgainstWall = false;

	// Auto-rotate to face away from wall when against wall
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Floor", meta = (EditCondition = "PlacementMode == EObjectPlacementMode::Floor && bMustBeAgainstWall", EditConditionHides))
	bool bAutoRotateFromWall = true;

	// Must have floor tile underneath
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Floor", meta = (EditCondition = "PlacementMode == EObjectPlacementMode::Floor", EditConditionHides))
	bool bRequiresFloor = true;

	// === Ceiling Fixture Settings ===

	// Distance from ceiling surface (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Ceiling", meta = (EditCondition = "PlacementMode == EObjectPlacementMode::CeilingFixture", EditConditionHides))
	float CeilingOffset = 20.0f;

	// === General Settings ===

	// Can be placed outside of rooms
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bCanPlaceOutdoors = true;

	// Footprint size in tiles (X, Y, Z)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FVector GridSize = FVector(1.0, 1.0, 1.0);

	// Legacy: Must be placed on or against a wall (deprecated - use PlacementMode instead)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement|Legacy")
	bool bRequiresWall = false;
};

/**
 *  Furniture items represent various pieces of furniture and decor
 *  that can be placed within lots to enhance interior design.
 */
UCLASS()
class BURBARCHITECT_API UFurnitureItem : public UCatalogItem
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere)
	TSoftClassPtr<APlaceableObject> ClassToSpawn;

	// Placement constraints for this furniture item
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FPlacementConstraints PlacementRules;
};

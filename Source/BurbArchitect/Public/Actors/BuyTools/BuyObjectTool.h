// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Actors/PlaceableObject.h"
#include "BuyObjectTool.generated.h"

class UFurnitureItem;

UCLASS()
class BURBARCHITECT_API ABuyObjectTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuyObjectTool();
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void Release_Implementation() override;

	// ========================================
	// Placement Properties
	// ========================================

	// The furniture item being placed (contains placement constraints)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Buy Tool")
	UFurnitureItem* CurrentCatalogItem = nullptr;

	// Visual feedback - is current placement valid?
	UPROPERTY(BlueprintReadOnly, Category = "Buy Tool")
	bool bValidPlacement = true;

	// Current placement mode (derived from CurrentCatalogItem)
	UPROPERTY(BlueprintReadOnly, Category = "Buy Tool")
	EObjectPlacementMode CurrentPlacementMode = EObjectPlacementMode::Floor;

	// ========================================
	// Wall Hanging Helpers
	// ========================================

	/**
	 * Raycast for wall mesh collision
	 * @param FromLocation Origin of raycast (usually camera or cursor)
	 * @param OutHit Hit result containing wall surface data
	 * @return True if wall was hit
	 */
	UFUNCTION(BlueprintCallable, Category = "Buy Tool|Placement")
	bool TraceForWall(const FVector& FromLocation, FHitResult& OutHit);

	/**
	 * Calculate position on wall surface at specified height
	 * @param WallHit Wall surface hit result
	 * @param HeightAboveFloor Height to mount object (cm)
	 * @param WallOffset Distance from wall surface (cm)
	 * @param Level Floor level
	 * @return World position for object
	 */
	UFUNCTION(BlueprintCallable, Category = "Buy Tool|Placement")
	FVector CalculateWallMountPosition(const FHitResult& WallHit, float HeightAboveFloor, float WallOffset, int32 Level);

	/**
	 * Calculate rotation for wall-mounted object (faces away from wall)
	 * @param WallNormal Normal of wall surface
	 * @return Rotation facing away from wall
	 */
	UFUNCTION(BlueprintCallable, Category = "Buy Tool|Placement")
	FRotator CalculateWallMountRotation(const FVector& WallNormal);

	// ========================================
	// Floor Placement Helpers
	// ========================================

	/**
	 * Check if any adjacent tile has a wall (O(1) grid-based query)
	 * @param Row Tile row
	 * @param Col Tile column
	 * @param Level Floor level
	 * @param OutWallNormal Normal vector pointing away from the wall
	 * @return True if wall found adjacent to tile
	 */
	UFUNCTION(BlueprintCallable, Category = "Buy Tool|Placement")
	bool CheckAdjacentWalls(int32 Row, int32 Col, int32 Level, FVector& OutWallNormal);

	/**
	 * Get all wall normals adjacent to a tile (pointing away from walls)
	 * @param Row Tile row
	 * @param Col Tile column
	 * @param Level Floor level
	 * @return Array of wall normals (empty if no walls adjacent)
	 */
	UFUNCTION(BlueprintCallable, Category = "Buy Tool|Placement")
	TArray<FVector> GetAdjacentWallNormals(int32 Row, int32 Col, int32 Level);

	// ========================================
	// Ceiling Placement Helpers
	// ========================================

	/**
	 * Calculate ceiling height for a level
	 * @param Level Floor level
	 * @return World Z coordinate of ceiling
	 */
	UFUNCTION(BlueprintCallable, Category = "Buy Tool|Placement")
	float CalculateCeilingHeight(int32 Level);

	/**
	 * Check if ceiling exists at tile (roof or floor above)
	 * @param Row Tile row
	 * @param Col Tile column
	 * @param Level Floor level
	 * @return True if ceiling exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Buy Tool|Placement")
	bool CheckCeilingExists(int32 Row, int32 Col, int32 Level);

	// ========================================
	// Placement Mode Handlers
	// ========================================

protected:
	/**
	 * Handle Floor placement mode
	 * @param MoveLocation Cursor world location
	 * @param TracedLevel Floor level traced
	 * @param Row Tile row
	 * @param Column Tile column
	 */
	void HandleFloorPlacement(const FVector& MoveLocation, int32 TracedLevel, int32 Row, int32 Column);

	/**
	 * Handle Wall Hanging placement mode
	 * @param MoveLocation Cursor world location
	 * @param TracedLevel Floor level traced
	 * @param CursorHit Hit result from cursor trace
	 */
	void HandleWallHangingPlacement(const FVector& MoveLocation, int32 TracedLevel, const FHitResult& CursorHit);

	/**
	 * Handle Ceiling Fixture placement mode
	 * @param MoveLocation Cursor world location
	 * @param TracedLevel Floor level traced
	 * @param Row Tile row
	 * @param Column Tile column
	 */
	void HandleCeilingFixturePlacement(const FVector& MoveLocation, int32 TracedLevel, int32 Row, int32 Column);

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};

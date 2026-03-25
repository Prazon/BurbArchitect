// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "Actors/LotManager.h"
#include "Components/FloorComponent.h"
#include "Components/TerrainComponent.h"
#include "FloorCommand.generated.h"

/**
 * Operation mode for floor commands
 */
UENUM()
enum class EFloorOperationMode : uint8
{
	Create UMETA(DisplayName = "Create"),
	Delete UMETA(DisplayName = "Delete"),
	Update UMETA(DisplayName = "Update")
};

/**
 * Floor Command - Handles creating, destroying, and updating floor segments
 */
UCLASS()
class UFloorCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	/**
	 * Initialize the floor command for CREATE operation
	 * @param bInIsPool - If true, this floor is part of a pool (sets bIsPool on FFloorTileData)
	 * @param bInIsPoolEdge - If true, this floor is a pool edge tile (sets bIsPoolEdge on FFloorTileData)
	 */
	void Initialize(ALotManager* Lot, int32 InLevel, const FVector& TileCenter, class UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, const FTileSectionState& TileState, int32 InSwatchIndex = 0, bool bInIsPool = false, bool bInIsPoolEdge = false);

	/**
	 * Initialize for an already-created floor (for auto-generated floors from room detection)
	 * This allows the floor to be properly undone without re-creating it
	 * @param bIsCeilingFloor - If true, this floor should be hidden (ceiling visibility)
	 */
	void InitializeForExistingFloor(ALotManager* Lot, const FFloorSegmentData& ExistingFloor, class UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, bool bIsCeilingFloor = false, int32 InSwatchIndex = 0);

	/**
	 * Initialize the floor command for DELETE operation
	 */
	void InitializeDelete(ALotManager* Lot, const FFloorSegmentData& FloorToDelete);

	/**
	 * Initialize the floor command for UPDATE operation
	 */
	void InitializeUpdate(ALotManager* Lot, const FFloorSegmentData& FloorToUpdate, const FTileSectionState& NewTileState, class UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, int32 InSwatchIndex = 0);

	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

protected:
	// Reference to the lot manager
	UPROPERTY()
	ALotManager* LotManager;

	// Operation mode (create, delete, or update)
	UPROPERTY()
	EFloorOperationMode OperationMode;

	// Floor level
	UPROPERTY()
	int32 Level;

	// Tile center location
	UPROPERTY()
	FVector TileCenter;

	// Floor material
	UPROPERTY()
	UMaterialInstance* FloorMaterial;

	// Floor pattern (for texture application)
	UPROPERTY()
	class UFloorPattern* FloorPattern;

	// Color swatch index (for recoloring patterns)
	UPROPERTY()
	int32 SwatchIndex;

	// Tile section state (new state for create/update)
	UPROPERTY()
	FTileSectionState TileSectionState;

	// Old tile section state (for update undo)
	UPROPERTY()
	FTileSectionState OldTileSectionState;

	// Old patterns per triangle (for update undo) - maps triangle type to its original pattern
	UPROPERTY()
	TMap<ETriangleType, UFloorPattern*> OldPatterns;

	// Floor data created by this command
	UPROPERTY()
	FFloorSegmentData FloorData;

	// Whether this floor should be hidden (is a ceiling)
	UPROPERTY()
	bool bIsCeiling;

	// Terrain that was removed when this floor was placed (for restoration on undo/delete)
	UPROPERTY()
	FTerrainSegmentData RemovedTerrainData;

	// Whether terrain was actually removed (false if no terrain existed)
	bool bTerrainWasRemoved;

	// Whether floor operation was successful
	bool bFloorCreated;

	// Whether this floor is part of a pool
	bool bIsPool;

	// Whether this floor is a pool edge tile
	bool bIsPoolEdge;
};

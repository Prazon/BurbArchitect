// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BuildServer.generated.h"

// Forward declarations
class ALotManager;
class UBuildCommand;
class UMaterialInstance;
class UStaticMesh;
class APortalBase;
class UWallPattern;
class UFloorPattern;
struct FWallSegmentData;
struct FFloorSegmentData;
struct FTileSectionState;
struct FRoofDimensions;
struct FStairModuleStructure;

/**
 * Build Server - World Subsystem that manages all building operations with undo/redo support
 * Acts as a middleman between build tools and the LotManager
 * Automatically wraps all building operations in undoable commands
 */
UCLASS()
class BURBARCHITECT_API UBuildServer : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Subsystem lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ========== Lot Management ==========

	/**
	 * Get the current lot manager
	 * Automatically finds the first lot in the world if not set
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server")
	ALotManager* GetCurrentLot();

	/**
	 * Set the current lot manager to build on
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server")
	void SetCurrentLot(ALotManager* Lot);

	// ========== Lot Save/Load Operations ==========

	/**
	 * Load a lot from a save game slot with undo support
	 * Creates a LoadLotCommand that can be undone to restore the previous lot state
	 * @param SlotName The save slot to load from (e.g., "Slot1", "QuickSave")
	 * @return True if load was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Lot")
	bool LoadLotFromSlot(const FString& SlotName);

	/**
	 * Load a lot from a JSON file with undo support
	 * @param FilePath Full path to the JSON file to import
	 * @return True if import was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Lot")
	bool ImportLotFromFile(const FString& FilePath);

	/**
	 * Load a default lot from a data asset with undo support
	 * @param LotAsset The lot data asset to load
	 * @return True if load was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Lot")
	bool LoadDefaultLot(class ULotDataAsset* LotAsset);

	// ========== Wall Operations ==========

	/**
	 * Build a wall segment between two points
	 * @param bDeferRoomGeneration If true, room detection happens but floor/ceiling generation is deferred (for roof walls)
	 * @param bIsPoolWall If true, marks this wall as a pool wall (restricts door placement, shows on multiple levels in debug view)
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Walls")
	void BuildWall(int32 Level, const FVector& StartLoc, const FVector& EndLoc, float Height, UWallPattern* Pattern, UMaterialInstance* BaseMaterial, bool bDeferRoomGeneration = false, bool bIsPoolWall = false);

	/**
	 * Delete a wall segment (finds and removes existing wall)
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Walls")
	void DeleteWall(const FWallSegmentData& WallData);

	// ========== Floor Operations ==========

	/**
	 * Build a floor tile at the specified location
	 * @param bIsPool - If true, marks this floor as part of a pool
	 * @param bIsPoolEdge - If true, marks this floor as a pool edge tile (cement around pool)
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Floors")
	void BuildFloor(int32 Level, const FVector& TileCenter, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, const FTileSectionState& TileState, int32 SwatchIndex = 0, bool bIsPool = false, bool bIsPoolEdge = false);

	/**
	 * Delete a floor tile
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Floors")
	void DeleteFloor(const FFloorSegmentData& FloorData);

	/**
	 * Update an existing floor tile with new tile section state
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Floors")
	void UpdateFloor(const FFloorSegmentData& FloorData, const FTileSectionState& NewTileState, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, int32 SwatchIndex = 0);

	/**
	 * Update an existing floor tile's pattern using grid coordinates
	 * Simplified overload for pattern updates
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Floors")
	void UpdateFloorPattern(int32 Level, int32 Row, int32 Column, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, int32 SwatchIndex = 0);

	// ========== Room Operations (Batch) ==========

	/**
	 * Build a complete room (4 walls) in one undoable operation
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Rooms")
	void BuildRoom(int32 Level, const FVector& StartCorner, const FVector& EndCorner, float WallHeight);

	// ========== Roof Operations ==========

	/**
	 * Build a roof segment
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Roofs")
	void BuildRoof(const FVector& Location, const FVector& Direction, const FRoofDimensions& Dimensions, float RoofThickness, float GableThickness, UMaterialInstance* Material);

	/**
	 * Delete a roof segment
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Roofs")
	void DeleteRoof(const FVector& Location);

	// ========== Stairs Operations ==========

	/**
	 * Build a staircase
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Stairs")
	void BuildStairs(TSubclassOf<class AStairsBase> StairsClass, const FVector& StartLoc, const FVector& Direction, const TArray<FStairModuleStructure>& Structures, float StairsThickness, UStaticMesh* StairTreadMesh, UStaticMesh* StairLandingMesh);

	/**
	 * Delete a staircase
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Stairs")
	void DeleteStairs(const FVector& Location);

	// ========== Portal Operations ==========

	/**
	 * Place a portal (door/window) on a wall
	 * @param WallArrayIndices Array of wall section indices that the portal affects (can span multiple sections)
	 * @param PortalSize Portal dimensions (width x height in cm)
	 * @param PortalOffset Portal position offset from wall placement point
	 * @param WindowMesh Static mesh for window (if placing a window)
	 * @param DoorStaticMesh Static mesh for door panel (if placing a door)
	 * @param DoorFrameMesh Static mesh for door frame (if placing a door)
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Portals")
	void BuildPortal(
		TSubclassOf<APortalBase> PortalClass,
		const FVector& Location,
		const FRotator& Rotation,
		const TArray<int32>& WallArrayIndices,
		const FVector2D& PortalSize = FVector2D::ZeroVector,
		const FVector2D& PortalOffset = FVector2D::ZeroVector,
		TSoftObjectPtr<UStaticMesh> WindowMesh = nullptr,
		TSoftObjectPtr<UStaticMesh> DoorStaticMesh = nullptr,
		TSoftObjectPtr<UStaticMesh> DoorFrameMesh = nullptr
	);

	// ========== Terrain Operations ==========

	/**
	 * Raise terrain at a specific tile
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Terrain")
	void RaiseTerrain(int32 Row, int32 Column, const TArray<float>& Spans, UMaterialInstance* Material);

	/**
	 * Lower terrain at a specific tile
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Terrain")
	void LowerTerrain(int32 Row, int32 Column, const TArray<float>& Spans, UMaterialInstance* Material);

	/**
	 * Flatten terrain at a specific tile to a target height
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Terrain")
	void FlattenTerrain(int32 Row, int32 Column, float TargetHeight);

	// ========== Batch Operations ==========

	/**
	 * Start grouping commands into a single undoable action
	 * All commands executed between BeginBatch and EndBatch will be undone/redone as one
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Batch")
	void BeginBatch(const FString& BatchDescription);

	/**
	 * End batch and commit as single undo step
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Batch")
	void EndBatch();

	/**
	 * Cancel batch without committing
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Batch")
	void CancelBatch();

	/**
	 * Check if currently in a batch operation
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build Server|Batch")
	bool IsInBatch() const { return bInBatch; }

	// ========== Undo/Redo ==========

	/**
	 * Undo the last building operation
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Undo")
	void Undo();

	/**
	 * Redo the last undone operation
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Undo")
	void Redo();

	/**
	 * Check if there are operations available to undo
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build Server|Undo")
	bool CanUndo() const;

	/**
	 * Check if there are operations available to redo
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build Server|Undo")
	bool CanRedo() const;

	/**
	 * Get description of the next undo operation
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build Server|Undo")
	FString GetUndoDescription() const;

	/**
	 * Get description of the next redo operation
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Build Server|Undo")
	FString GetRedoDescription() const;

	/**
	 * Clear all undo/redo history
	 */
	UFUNCTION(BlueprintCallable, Category = "Build Server|Undo")
	void ClearHistory();

	// ========== Room Auto-Generation ==========

	/**
	 * Auto-generate floors and ceilings for a newly detected room
	 * Called after room detection (incremental or batch) to create floor/ceiling geometry
	 * @param RoomID The ID of the room to generate floors/ceilings for
	 */
	void AutoGenerateRoomFloorsAndCeilings(int32 RoomID);

private:
	// Current lot being built on
	UPROPERTY()
	ALotManager* CurrentLot;

	// Command history - undo stack
	UPROPERTY()
	TArray<UBuildCommand*> UndoStack;

	// Command history - redo stack
	UPROPERTY()
	TArray<UBuildCommand*> RedoStack;

	// Maximum history size
	int32 MaxHistorySize;

	// Batch operation state
	bool bInBatch;
	TArray<UBuildCommand*> BatchCommands;
	FString BatchDescription;

	// Helper to execute or batch a command
	void ExecuteOrBatchCommand(UBuildCommand* Command);

	// ========== Room Detection ==========

	/**
	 * Detect and auto-fill rooms after a batch of wall operations completes
	 * Scans tiles adjacent to modified walls and triggers flood fill for enclosed spaces
	 */
	void DetectRoomsForBatch(UBuildCommand* BatchCommand);

	/**
	 * Detect and auto-fill rooms after a single wall operation
	 * Used for non-batched wall builds/deletes
	 */
	void DetectRoomsForWall(int32 Level, const FVector& StartLoc, const FVector& EndLoc);
};

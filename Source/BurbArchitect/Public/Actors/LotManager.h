// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/FloorComponent.h"
#include "Components/WallComponent.h"
#include "Components/WallGraphComponent.h"
#include "Components/TerrainComponent.h"
#include "Components/RoofComponent.h"
#include "Components/StairsComponent.h"
#include "Components/GridComponent.h"
#include "Data/TileData.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "LotManager.generated.h"

// Forward declarations
struct FWallChains;
enum class EBurbMode : uint8;

#define TILE_VALID(Row, Column) ((Row >= 0) && (Row < GridSizeX) && (Column >= 0) && (Column < GridSizeY))

UCLASS(Blueprintable)
class BURBARCHITECT_API ALotManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ALotManager();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// native construct
	virtual void OnConstruction(const FTransform& Transform) override;
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

private:
	UPROPERTY(VisibleAnywhere)
	USceneComponent* RootSceneComponent;

	// Cached current gameplay mode (updated by OnBurbModeChanged)
	// Used to prevent grid toggling in Live mode
	// Initialized to Build in constructor
	EBurbMode CachedBurbMode;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UGridComponent* GridComponent;

	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	int32 RoomCounter;

	// Runtime tile data - don't show in editor or serialize (30,000+ tiles causes massive editor lag!)
	UPROPERTY(Transient)
	TArray<FTileData> GridData;

	// Spatial hash maps for O(1) lookups (Row, Column, Level) -> Array Index
	// These are rebuilt whenever the grid changes
	UPROPERTY(Transient)
	TMap<FIntVector, int32> TileIndexMap;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	UWallComponent* WallComponent;

	// New wall graph system - manages walls as a graph structure
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	UWallGraphComponent* WallGraph;

	// Room manager - handles room detection and caching using wall graph
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Room Management")
	class URoomManagerComponent* RoomManager;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	UFloorComponent* FloorComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	UTerrainComponent* TerrainComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class UWaterComponent* WaterComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	class UFenceComponent* FenceComponent;

	// Neighbourhood integration
	// Reference to parent neighbourhood (manually assigned or auto-discovered)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neighbourhood")
	class ANeighbourhoodManager* ParentNeighbourhood;

	// Lot's position within neighbourhood (in neighbourhood tile coordinates)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neighbourhood", meta = (ClampMin = "0"))
	int32 NeighbourhoodOffsetRow = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neighbourhood", meta = (ClampMin = "0"))
	int32 NeighbourhoodOffsetColumn = 0;

	// Track if lot is placed on neighbourhood
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Neighbourhood")
	bool bIsPlacedOnNeighbourhood = false;

	// Legacy roof components - kept for backward compatibility
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	TArray<URoofComponent*> RoofComponents;

	// New roof actors system
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	TArray<class ARoofBase*> RoofActors;

	// Stairs actors system - manages all stairs (preview and committed)
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	TArray<class AStairsBase*> StairsActors;

	// Runtime wall chain data - rebuilt every frame during cutaway
	UPROPERTY(Transient)
	TArray<FWallChains> WallChains;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	bool bRebuildGrid;

	// Terrain generation guards - track last generated grid size to prevent unnecessary regeneration
	// Must be UPROPERTY to persist across Blueprint compiles and save/load
	UPROPERTY()
	int32 LastGeneratedGridSizeX = -1;

	UPROPERTY()
	int32 LastGeneratedGridSizeY = -1;

	UFUNCTION(BlueprintCallable)
	void GenerateGrid();

	// Generate only the tile data without visual grid lines (call in BeginPlay)
	UFUNCTION(BlueprintCallable)
	void GenerateGridData();

	// Generate the visual grid mesh (call after GenerateGridData)
	UFUNCTION(BlueprintCallable)
	virtual void GenerateGridMesh();

	// Rebuild only grid levels that have been marked dirty (incremental update)
	// More efficient than full regeneration when only upper floors receive new tiles
	void RebuildDirtyGridLevels();

	// Rebuild spatial hash map for fast tile lookups
	void RebuildTileIndexMap();

	// Generate grid tiles above a room (ceiling provides support for upper floor)
	void GenerateTilesAboveRoom(const TArray<FTileData>& RoomTiles, int32 TargetLevel);

	// Generate adjacent expansion tiles around a room (allows room expansion on same level)
	void GenerateAdjacentExpansionTiles(const TArray<FTileData>& RoomTiles, int32 Level);
	
	UFUNCTION(BlueprintCallable)
	float GridWidth() const;
	
	UFUNCTION(BlueprintCallable)	
	float GridHeight() const;
	
	UFUNCTION(BlueprintCallable)	
	FVector GridCenter() const;

	UFUNCTION(BlueprintCallable)
	virtual void GenerateTerrainComponents();

	UFUNCTION(BlueprintCallable)
	UMaterialInstanceDynamic* CreateMaterialInstance(const FLinearColor Colour, const float Opacity, FName Name);

	UFUNCTION(BlueprintCallable)
	bool LocationToTile(const FVector Location, int32& Row, int32& Column) const;
	
	UFUNCTION(BlueprintCallable)
	bool LocationToTileCorner(int32 Level, const FVector Location, FVector& NearestCorner);
	
	UFUNCTION(BlueprintCallable)
	TArray<FVector> LocationToAllTileCorners(const FVector Location, int32 Level);
	
	UFUNCTION(BlueprintCallable)
	FVector LocationToTileCenter(const FVector& Location);
	
	UFUNCTION(BlueprintCallable)
	bool LocationToTileEdge(FVector Location, FVector& TileEdge) const;
	
	UFUNCTION(BlueprintCallable)
	bool TileToGridLocation(int32 Level, const int32 Row, const int32 Column, bool bCenter, FVector& GridLocation) const;
	
	UFUNCTION(BlueprintCallable)
	bool IsOverlappingExistingWallOrActor(FWallSegmentData WallData);

	/**
	 * Check if a new diagonal wall would cross an existing diagonal wall on the same tile.
	 * Uses grid-based detection via WallGraph spatial index.
	 * @param StartLoc Start position of the new wall
	 * @param EndLoc End position of the new wall
	 * @param Level Floor level
	 * @return True if the new diagonal would cross an existing diagonal on the same tile
	 */
	UFUNCTION(BlueprintCallable)
	bool IsCrossingDiagonalWall(const FVector& StartLoc, const FVector& EndLoc, int32 Level) const;

	static bool ContainsVectorWithZInRange(const TArray<FVector>& VectorArray, const FVector& TargetVector, float MinZ, float MaxZ);
	
	UFUNCTION(BlueprintCallable)
	TArray<FTileData> GetTilesAdjacentToWall(const FVector& StartCorner, const FVector& EndCorner, int32 Level) const;

	UFUNCTION(BlueprintCallable)
	bool DoesBridgeOutsideInside(const FVector& StartCorner, const FVector& EndCorner) const;
	
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsWallBetweenTiles(const FTileData& TileA,const FTileData& TileB);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool HasDiagonalWall(FTileData TileA);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	FBox CreateBoundingBox(const FVector& Start, const FVector& End) const;
	
	UFUNCTION(BlueprintCallable, BlueprintPure)
	FVector SnapToPrimaryDirection(const FVector& Vec) const;

	bool FindTerrain(const int32 Row, const int32 Column, FTerrainSegmentData*& OutTerrainSegmentData);

	bool FindTerrainWithVector(FVector& Location, FTerrainSegmentData*& OutTerrainSegmentData);

	float CalculateCenterSpan(int32 outRow, int32 outColumn, int32 x, int32 y, float Radius) const;

	// static float GetIntersectionToRoof(const FVector& StartRoof, const FRoofDimensions& RoofDimensions, const FVector& WallUpPoint, const float WallHeight, const int Level);
	
	FVector GetMouseWorldPosition(float Distance) const;

	UFUNCTION(BlueprintCallable)
	FVector MagnetizeToTileCenter( const FVector& Location, FVector& TargetLocation);

	// Static Functions

	UFUNCTION(BlueprintCallable)
	static bool AreDirectionsAligned(const FVector& DirA, const FVector& DirB);
	
	UFUNCTION(BlueprintCallable)
	static bool AreLinesIntersecting2D(const FVector& Start1, const FVector& End1, const FVector& Start2, const FVector& End2);
	
	UFUNCTION(BlueprintCallable)
	static bool IsPointOnLineSegment(const FVector& A, const FVector& B, const FVector& Z, const float Tolerance);

	UFUNCTION(BlueprintCallable)
	static bool IsPointInArea2D(const FVector& StartDiameter, const FVector& EndDiameter, const FVector& Point);

	// Get the facing direction (normal) of a wall segment
	UFUNCTION(BlueprintCallable)
	static FVector GetWallFacingDirection(const FVector& WallStart, const FVector& WallEnd);

	// Rebuild wall chains by grouping connected, aligned wall segments
	// Call this after walls are built/destroyed to update chains for editing
	UFUNCTION(BlueprintCallable)
	void RebuildWallChains();

	// Find which wall chain contains a specific wall segment index
	UFUNCTION(BlueprintCallable)
	int32 FindWallChainForSegment(int32 WallSegmentIndex) const;

	// Selection :

	UFUNCTION(BlueprintCallable)
	float CalculatePointOnLineDistance(const FVector& CameraLocation, const FVector& WidgetLocation, const FVector& MovementLocation);
	
	UFUNCTION(BlueprintCallable)
	TArray<FTileData> GetNeighboringTiles(const FTileData StartTile, int32 Level);

	UFUNCTION(BlueprintCallable)
	FTileData FindTileByCoord(FVector2D TileCoord, int32 Level);

	// Optimized O(1) grid coordinate lookup using hash map (internal use, not exposed to Blueprint)
	FTileData FindTileByGridCoords(int32 Row, int32 Column, int32 Level) const;

	// Get the room ID at a specific tile position
	// Returns 0 if no room (outside), or RoomID if tile is inside an enclosed room
	// Works with diagonal walls - uses flood-fill room detection results
	UFUNCTION(BlueprintCallable)
	int32 GetRoomAtTile(int32 Level, int32 Row, int32 Column) const;

	UFUNCTION(BlueprintCallable)
	FTileData GetTileDataAtIndex(int32 Index);

	UFUNCTION(BlueprintCallable)
	bool SetTileDataAtIndex(int32 Index, FTileData NewTileData);

	// Debug visualization - draws room numbers above each tile
	UFUNCTION(BlueprintCallable)
	void DebugDrawRoomIDs(float Duration = 5.0f);

	/*** START Room Cache functions ***/

	// Rebuilds the entire room cache by scanning all tiles
	UFUNCTION(BlueprintCallable)
	void BuildRoomCache();

	// Marks a specific room as dirty for recalculation
	// Call this when walls affecting a room are added/removed
	UFUNCTION(BlueprintCallable)
	void InvalidateRoom(int32 RoomID);

	// Marks all rooms as dirty
	UFUNCTION(BlueprintCallable)
	void InvalidateAllRooms();

	// Clears the entire room cache
	UFUNCTION(BlueprintCallable)
	void ClearRoomCache();

	/*** END Room Cache functions ***/

	/*** START Building functions ***/

	//Create a single wall mesh the size of a tile edge
	UFUNCTION(BlueprintCallable)
	UWallComponent* GenerateWallSegment(int32 Level, const FVector& TileCornerStart, const FVector& TileCornerEnd, bool& bValidPlacement, float WallHeight);

	// Legacy roof component method - kept for backward compatibility
	UFUNCTION(BlueprintCallable)
	virtual URoofComponent* GenerateRoofSegment(const FVector& Location, const FVector& Direction, const FRoofDimensions& RoofDimensions, const float RoofThickness, const float GableThickness, UMaterialInstance* ValidMaterial);

	// New roof actor spawning method
	UFUNCTION(BlueprintCallable)
	virtual class ARoofBase* SpawnRoofActor(const FVector& Location, const FVector& Direction, const FRoofDimensions& RoofDimensions, const float RoofThickness, const float GableThickness, UMaterialInstance* ValidMaterial);

	// Helper methods for managing roof components array (legacy)
	UFUNCTION(BlueprintCallable)
	virtual void RemoveRoofComponent(URoofComponent* RoofComp);

	UFUNCTION(BlueprintCallable)
	virtual void ClearAllRoofComponents();

	UFUNCTION(BlueprintCallable, BlueprintPure)
	virtual URoofComponent* GetRoofComponentByType(ERoofType RoofType) const;

	UFUNCTION(BlueprintCallable, BlueprintPure)
	virtual int32 GetRoofComponentCount() const;

	// Helper methods for managing roof actors
	UFUNCTION(BlueprintCallable)
	virtual void RemoveRoofActor(ARoofBase* RoofActor);

	UFUNCTION(BlueprintCallable)
	virtual void ClearAllRoofActors();

	UFUNCTION(BlueprintCallable, BlueprintPure)
	virtual ARoofBase* GetRoofActorByType(ERoofType RoofType) const;

	UFUNCTION(BlueprintCallable, BlueprintPure)
	virtual int32 GetRoofActorCount() const;

	// Stairs actor spawning method
	UFUNCTION(BlueprintCallable)
	class AStairsBase* SpawnStairsActor(TSubclassOf<AStairsBase> StairsClass, const FVector& StartLocation, const FVector& Direction, const TArray<FStairModuleStructure>& Structures, UStaticMesh* StairTreadMesh, UStaticMesh* StairLandingMesh, float StairsThickness = 30.0f);

	// Helper methods for managing stairs actors
	UFUNCTION(BlueprintCallable)
	void RemoveStairsActor(AStairsBase* StairsActor);

	UFUNCTION(BlueprintCallable)
	void ClearAllStairsActors();

	UFUNCTION(BlueprintCallable, BlueprintPure)
	int32 GetStairsActorCount() const;

	// Find a stairs actor at a specific location
	UFUNCTION(BlueprintCallable)
	AStairsBase* FindStairsActorAtLocation(const FVector& Location, const TArray<FStairModuleStructure>& Structures);

	//Create a single floor mesh the size of a tile with 4 parameters to change each triangles material
	UFUNCTION(BlueprintCallable)
	UFloorComponent* GenerateFloorSegment(int32 Level, const FVector& TileCenter, UMaterialInstance* OptionalMaterial, const FTileSectionState& TileSectionState);
	
	/*** START Brushing functions ***/

	UFUNCTION(BlueprintCallable)
	virtual void BrushTerrainRaise(const int32 Row, const int32 Column, TArray<float> Spans, UMaterialInstance* OptionalMaterial);

	UFUNCTION(BlueprintCallable)
	virtual void BrushTerrainLower(const int32 Row, const int32 Column, TArray<float> Spans, UMaterialInstance* OptionalMaterial);

	UFUNCTION(BlueprintCallable)
	virtual void BrushTerrainFlatten(const int32 Row, const int32 Column, const float TargetHeight);

	/*** START Terrain Validation Functions ***/

	/**
	 * Check if terrain at a tile has all 4 corners at exactly the same height (absolute flatness)
	 * Only applies to ground level (Basements) - returns true for any other level
	 *
	 * @param Level - Floor level to check
	 * @param Row - Grid row coordinate
	 * @param Column - Grid column coordinate
	 * @return true if terrain is perfectly flat or if not on ground level, false if sloped
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsTerrainFlatAtTile(int32 Level, int32 Row, int32 Column) const;

	/**
	 * Check if terrain is flat for wall placement (validates both adjacent tiles)
	 * Only applies to ground level (Basements) - returns true for any other level
	 *
	 * @param Level - Floor level to check
	 * @param StartLoc - Wall start location
	 * @param EndLoc - Wall end location
	 * @return true if both adjacent tiles have flat terrain, false if either is sloped
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsTerrainFlatForWall(int32 Level, const FVector& StartLoc, const FVector& EndLoc) const;

	/**
	 * Get terrain-adjusted Z position for ground-level placement
	 * Uses average height of tile's 4 corners (keeps walls/floors level)
	 * Returns terrain-adjusted Z if on flat terrain, otherwise default level-based Z
	 *
	 * @param Level - Floor level
	 * @param Row - Grid row coordinate
	 * @param Column - Grid column coordinate
	 * @param DefaultZ - Fallback Z position if not on flat terrain
	 * @return Z position adjusted for terrain elevation
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	float GetTerrainAdjustedZ(int32 Level, int32 Row, int32 Column, float DefaultZ) const;

	/**
	 * Check if a proposed wall would connect to existing walls at different heights
	 * Prevents walls at different elevations from connecting
	 *
	 * @param StartLoc - Proposed wall start location (with terrain-adjusted Z)
	 * @param EndLoc - Proposed wall end location (with terrain-adjusted Z)
	 * @param Level - Floor level
	 * @return true if height is valid (no conflicts), false if would connect to mismatched heights
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsWallHeightValidForConnection(const FVector& StartLoc, const FVector& EndLoc, int32 Level) const;

	/*** END Terrain Validation Functions ***/

	UFUNCTION(BlueprintCallable)
	void ToggleGrid(bool bVisible);
	
	UFUNCTION(BlueprintCallable)
	void ToggleAllGrid(bool bVisible);

	UFUNCTION(BlueprintCallable)
	void SetCurrentLevel(int32 NewLevel);

	/**
	 * Handler for when gameplay mode changes on BurbPawn
	 * Updates grid visibility: Live mode hides grid/boundaries, Build/Buy shows them
	 * This is client-side only (grid visibility is not replicated)
	 */
	UFUNCTION()
	void OnBurbModeChanged(EBurbMode OldMode, EBurbMode NewMode);

	UFUNCTION(BlueprintCallable)
	void SetBasementViewMode(bool bEnabled);

	/*** END Building functions ***/
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated)
	FText LotName;
	
	UPROPERTY(EditAnywhere, meta = (ClampMin = "8", ClampMax = "100", UIMin = "8", UIMax = "100"), Replicated)
	int32 GridSizeX;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "8", ClampMax = "100", UIMin = "8", UIMax = "100"), Replicated)
	int32 GridSizeY;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "8", ClampMax = "100", UIMin = "8", UIMax = "100"))
	float GridTileSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "100", ClampMax = "500", UIMin = "100", UIMax = "500"))
	float DefaultWallHeight = 300.0f;

	// Terrain-floor interaction configuration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	bool bRemoveTerrainUnderFloors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	bool bRestoreTerrainOnFloorRemoval = true;

	// Use terrain as basement ceilings instead of generating floor geometry
	// When enabled, terrain acts as the ceiling for basement rooms (more efficient)
	// When disabled, actual floor geometry is generated at ceiling level
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	bool bAllowTerrainCeilings = true;

	// Z-offset for basement wall tops below terrain surface to prevent z-fighting
	// Basement walls will stop this many units below the terrain/grid level
	// Recommended: 1-2 units to prevent z-fighting while keeping walls just below terrain
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (ClampMin = "0", ClampMax = "10", UIMin = "0", UIMax = "10"))
	float BasementCeilingOffset = 2.0f;

	UPROPERTY(EditAnywhere)
	float LineOpacity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated)
	int32 Floors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated)
	int32 Basements;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Level")
	int32 CurrentLevel;

	// Global grid visibility toggle (true = grids enabled, false = all grids hidden)
	// When enabled, only the CurrentLevel's grid is visible
	// When disabled, all grids are hidden regardless of level
	bool bShowGrid = true;

	// Track which levels need grid mesh rebuild (for incremental updates)
	// Used when new tiles are added to upper floors during ceiling generation
	TSet<int32> DirtyGridLevels;

	UPROPERTY(EditAnywhere)
	FLinearColor LineColour;

	// Basement view mode settings
	UPROPERTY(EditAnywhere, Category = "Basement View")
	UMaterialInterface* PostProcessBasementMaterial;

	UPROPERTY(EditAnywhere)
	UMaterial* WallMaterial;
	
	UPROPERTY(EditAnywhere)
	UMaterialInstance* DefaultWallMaterial;

	UPROPERTY(EditAnywhere)
	class UWallPattern* DefaultWallPattern;

	UPROPERTY(EditAnywhere)
	UMaterialInstance* DefaultFloorMaterial;

	UPROPERTY(EditAnywhere)
	class UFloorPattern* DefaultFloorPattern;
		
	UPROPERTY(EditAnywhere)
	UMaterialInstance* DefaultTerrainMaterial;

	UPROPERTY(EditAnywhere)
	UMaterialInstance* ValidPreviewMaterial;

	UPROPERTY(EditAnywhere)
	UMaterialInstance* InvalidPreviewMaterial;
	
	UPROPERTY(EditAnywhere)
	UMaterialInterface* GridMaterial;

	UPROPERTY(BlueprintReadOnly)
	UMaterialInstance* LineMaterial;

	// ========================================
	// Save/Load System
	// ========================================

	/**
	 * Saves the current lot to a save game slot
	 * @param SlotName The name of the save slot (e.g., "Slot1", "QuickSave")
	 * @return True if save was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot|Save")
	bool SaveLotToSlot(const FString& SlotName);

	/**
	 * Loads a lot from a save game slot
	 * @param SlotName The name of the save slot to load from
	 * @return True if load was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot|Save")
	bool LoadLotFromSlot(const FString& SlotName);

	/**
	 * Exports the current lot to a JSON file
	 * Useful for sharing lots or debugging
	 * @param FilePath Full path to the file (e.g., "C:/MyLots/Mansion.json")
	 * @return True if export was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot|Save")
	bool ExportLotToFile(const FString& FilePath);

	/**
	 * Imports a lot from a JSON file
	 * @param FilePath Full path to the file to import
	 * @return True if import was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot|Save")
	bool ImportLotFromFile(const FString& FilePath);

	/**
	 * Loads a default lot from a data asset
	 * Used for loading pre-built lots shipped with the game
	 * @param LotAsset The lot data asset to load
	 * @return True if load was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot|Save")
	bool LoadDefaultLot(class ULotDataAsset* LotAsset);

#if WITH_EDITOR
	/**
	 * Saves the current lot as a data asset (Editor only)
	 * Creates a ULotDataAsset that can be packaged with the game
	 * @param AssetName Name for the asset (e.g., "DA_StarterHome")
	 * @param PackagePath Path in Content Browser (e.g., "/BurbArchitect/DefaultLots/")
	 * @return The created data asset, or nullptr on failure
	 */
	UFUNCTION(CallInEditor, Category = "Lot|Save")
	class ULotDataAsset* SaveAsDataAsset(const FString& AssetName, const FString& PackagePath);

	/**
	 * Places lot on neighbourhood (Editor only)
	 * Claims tiles, punches landscape hole, generates terrain, stitches edges
	 */
	UFUNCTION(CallInEditor, Category = "Neighbourhood", meta = (DisplayName = "Place Lot on Neighbourhood"))
	void PlaceLotOnNeighbourhood();

	/**
	 * Unplaces lot from neighbourhood (Editor only)
	 * Releases tiles, restores landscape, destroys lot terrain
	 */
	UFUNCTION(CallInEditor, Category = "Neighbourhood", meta = (DisplayName = "Unplace Lot from Neighbourhood"))
	void UnplaceLotFromNeighbourhood();
#endif

	/**
	 * Clears all lot data (walls, floors, roofs, etc.)
	 * Resets the lot to an empty state
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot|Save")
	void ClearLot();

	// Neighbourhood integration methods

	/**
	 * Snaps lot to neighbourhood grid
	 * Updates NeighbourhoodOffsetRow/Column and actor position
	 */
	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	void SnapToNeighbourhoodGrid();

	/**
	 * Stitches lot terrain edges to neighbourhood landscape
	 * Samples landscape height at boundaries and applies to lot terrain
	 */
	void StitchTerrainToNeighbourhood();

	// Feature Flags

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature Flags")
	bool bEnableTerrainGeneration = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature Flags")
	bool bEnableRoofGeneration = true;

};

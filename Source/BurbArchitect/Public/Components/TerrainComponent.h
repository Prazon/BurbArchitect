// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Components/ActorComponent.h"
#include "Components/TerrainHeightMap.h"
#include "Components/FloorComponent.h"
#include "TerrainComponent.generated.h"




//Definition of a single Terrain segment
USTRUCT(BlueprintType)
struct FTerrainSegmentData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "Terrain Data")
	int32 ArrayIndex = -1;
	
	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	int32 SectionIndex = -1;
	
	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	bool bFlatten = false;

	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	int32 Level=0;

	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	int32 Row=0;
	
	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	int32 Column=0;
	
	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	mutable FVector PointLoc;

	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	TArray<FVector> CornerLocs;

	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	float Width;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Data")
	bool bCommitted;

	UPROPERTY(BlueprintReadWrite, Category="Terrain Data")
	UMaterialInstance* Material;

	// Filled mask for boundary handling (OpenTS2-inspired)
	// 4-bit mask indicating which neighboring tiles exist
	// Bit 0: Top neighbor exists
	// Bit 1: Right neighbor exists
	// Bit 2: Bottom neighbor exists
	// Bit 3: Left neighbor exists
	// NOTE: This is now actively used in RebuildLevel() to cull duplicate edge faces
	// Edge faces are only generated where the corresponding bit is NOT set (no neighbor)
	UPROPERTY()
	uint8 FilledMask = 15; // Default to all neighbors present (0b1111)

	// Partial cutout support - which quadrants should render
	// Inverted from floor: terrain shows where floor DOESN'T exist
	// Used for hole-punching when partial floors are placed above terrain
	UPROPERTY(BlueprintReadWrite, Category = "Terrain Data")
	FTileSectionState TileSectionState;

	// Cached base vertex index in the merged mesh for this tile
	// This allows O(1) lookup during incremental updates instead of O(N) iteration
	// Value of -1 indicates index needs to be recalculated (after rebuild)
	int32 MeshVertexIndex = -1;

	// Actual edge faces generated for this tile (after culling optimization)
	// Bit 0: Top edge faces exist
	// Bit 1: Right edge faces exist
	// Bit 2: Bottom edge faces exist
	// Bit 3: Left edge faces exist
	// This is the inverse of FilledMask - edges exist where neighbors DON'T
	uint8 EdgeFacesMask = 0;

	//Constructor
	FTerrainSegmentData()
	{
		ArrayIndex = -1;
		SectionIndex = -1;
		bCommitted = false;
		PointLoc = FVector(0,0,0);
		FilledMask = 15; // All neighbors present by default
		MeshVertexIndex = -1;
		EdgeFacesMask = 0; // No edge faces by default
		// Initialize all quadrants as visible (full terrain tile by default)
		TileSectionState.Top = true;
		TileSectionState.Right = true;
		TileSectionState.Bottom = true;
		TileSectionState.Left = true;
	}
	
	//Compare
	bool operator==(const FTerrainSegmentData & other) const
	{
		return (other.PointLoc.X == PointLoc.X && other.PointLoc.Y == PointLoc.Y && other.ArrayIndex == ArrayIndex && other.SectionIndex == SectionIndex);
	}
	//Compare
	bool operator!=(const FTerrainSegmentData & other) const
	{
		return (other.PointLoc.X == PointLoc.X && other.PointLoc.Y == PointLoc.Y && other.ArrayIndex != ArrayIndex && other.SectionIndex != SectionIndex);
	}
};

/**
 * 
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UTerrainComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

protected:
	UTerrainComponent(const FObjectInitializer& ObjectInitializer);

	// Called after component is loaded from disk (editor + runtime)
	virtual void PostLoad() override;

	// Called when the game starts
	virtual void BeginPlay() override;

public:
	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable)
	void DestroyTerrain();
	
	UFUNCTION(BlueprintCallable)
	void DestroyTerrainSection(FTerrainSegmentData TerrainSection);
	
	UFUNCTION(BlueprintCallable)
	void CommitTerrain(UMaterialInstance* DefaultTerrainMaterial);

	UFUNCTION(BlueprintCallable)
	void CommitTerrainSection(FTerrainSegmentData InTerrainData, UMaterialInstance* DefaultTerrainMaterial);
	
	UFUNCTION(BlueprintCallable)
	bool FindExistingTerrainSection(const int32 Row, const int32 Column, FTerrainSegmentData OutTerrain);

	UFUNCTION(BlueprintCallable)
	FTerrainSegmentData GenerateTerrainSection(const int32 Row, const int32 Column, const FVector& PointLoc, UMaterialInstance* OptionalMaterial);

	UFUNCTION(BlueprintCallable)
	FTerrainSegmentData GenerateTerrainMeshSection(FTerrainSegmentData InTerrainData);
	
	void FlattenTerrainOn(FTerrainSegmentData InTerrainData, const  int32 Level);

	void ForceUpdateCorner(FVector Corner);

	// void ModifyTerrainOn(FTerrainSegmentData InTerrainData);

	// FVector AdjustCornerZ(FVector Corner);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FTerrainSegmentData> TerrainDataArray;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bValidPlacement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCommitted;

	//Terrain Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material", Meta = (ExposeOnSpawn = true))
	UMaterialInstance* TerrainMaterial;

	// Mesh information
	bool bGlobalMeshDataUpdated = false;
	bool bCreateCollision = true;

	// Grid-based corner height storage (per level)
	// Maps Level -> Height Map for O(1) corner lookups
	UPROPERTY()
	TMap<int32, FTerrainHeightMap> HeightMaps;

	// Initialize height map for a specific level
	void InitializeHeightMap(int32 Level, int32 GridWidth, int32 GridHeight, float TileSize, float BaseZ);

	// Get or create height map for a level
	FTerrainHeightMap* GetOrCreateHeightMap(int32 Level);

	// Get height map for a level (const version)
	const FTerrainHeightMap* GetHeightMap(int32 Level) const;

	// Update corner height and regenerate affected terrain tiles
	// bBypassLock: If true, bypasses corner locking (used when placing floors/walls that need to flatten terrain)
	void UpdateCornerHeight(int32 Level, int32 CornerRow, int32 CornerColumn, float NewHeight, bool bBypassLock = false);

	// Get all terrain tiles affected by a corner
	TArray<FTerrainSegmentData*> GetTilesAffectedByCorner(int32 Level, int32 CornerRow, int32 CornerColumn);

	/**
	 * Sample terrain elevation at a specific grid position
	 * Returns the terrain Z coordinate at the tile's center
	 *
	 * @param Level - The floor level
	 * @param Row - Grid row coordinate
	 * @param Column - Grid column coordinate
	 * @return World Z coordinate of terrain at this position, or 0 if no terrain exists
	 */
	UFUNCTION(BlueprintCallable)
	float SampleTerrainElevation(int32 Level, int32 Row, int32 Column);

	/**
	 * Flatten a rectangular region of terrain to a target height (OpenTS2-style)
	 * All corners in the region are set to the same height
	 *
	 * @param FromRow - Starting row (inclusive)
	 * @param FromCol - Starting column (inclusive)
	 * @param ToRow - Ending row (exclusive)
	 * @param ToCol - Ending column (exclusive)
	 * @param TargetHeight - World Z coordinate to flatten to
	 * @param Level - Floor level
	 */
	UFUNCTION(BlueprintCallable)
	void FlattenRegion(int32 FromRow, int32 FromCol, int32 ToRow, int32 ToCol, float TargetHeight, int32 Level);

	/**
	 * Smooth terrain in a circular region using Laplacian smoothing
	 * Each corner height is blended toward the average of its neighbors
	 *
	 * @param Level - Floor level
	 * @param CenterRow - Center row of smoothing region (corner coordinates)
	 * @param CenterCol - Center column of smoothing region (corner coordinates)
	 * @param Radius - Radius in tiles (affects corners within this distance)
	 * @param Strength - Blending strength (0.0 = no change, 1.0 = full average)
	 */
	UFUNCTION(BlueprintCallable)
	void SmoothCornerRegion(int32 Level, int32 CenterRow, int32 CenterCol, float Radius, float Strength);

	/**
	 * Flatten terrain corners under a floor tile based on which quadrants have floor coverage
	 * Only flattens corners that are covered by floor sections (handles diagonal/partial floors)
	 *
	 * @param Level - Floor level
	 * @param Row - Tile row coordinate
	 * @param Column - Tile column coordinate
	 * @param TileSectionState - Which quadrants have floor coverage (Top, Right, Bottom, Left)
	 * @param TargetHeight - World Z coordinate to flatten terrain corners to (typically floor Z - offset)
	 * @param bBypassLock - If true, bypasses corner locking during flattening (used when placing floors)
	 */
	UFUNCTION(BlueprintCallable)
	void FlattenTerrainUnderFloor(int32 Level, int32 Row, int32 Column, const FTileSectionState& TileSectionState, float TargetHeight, bool bBypassLock = false);

	/**
	 * Flatten terrain corners along a wall segment to prevent Z-fighting
	 * Flattens all corners between start and end points (handles horizontal, vertical, and diagonal walls)
	 *
	 * @param Level - Floor level
	 * @param StartRow - Starting corner row coordinate
	 * @param StartColumn - Starting corner column coordinate
	 * @param EndRow - Ending corner row coordinate
	 * @param EndColumn - Ending corner column coordinate
	 * @param TargetHeight - World Z coordinate to flatten terrain corners to (typically wall base Z - offset)
	 * @param bBypassLock - If true, bypasses corner locking during flattening (used when placing walls)
	 */
	UFUNCTION(BlueprintCallable)
	void FlattenTerrainAlongWall(int32 Level, int32 StartRow, int32 StartColumn, int32 EndRow, int32 EndColumn, float TargetHeight, bool bBypassLock = false);

	// Calculate filled mask for a terrain tile based on neighboring tiles
	// Returns 4-bit mask where each bit indicates if a neighbor exists
	uint8 CalculateFilledMask(int32 Row, int32 Column, int32 Level) const;

	// Clear all terrain meshes and data
	UFUNCTION(BlueprintCallable)
	void DestroyAllTerrain();

	// Remove all terrain tiles not on ground level (cleanup for legacy data)
	// Returns number of tiles removed
	UFUNCTION(BlueprintCallable)
	int32 RemoveInvalidTerrainTiles();

	// Apply shared material to all terrain sections
	UFUNCTION(BlueprintCallable)
	void ApplySharedTerrainMaterial(UMaterialInstance* Material);

	// ========== Merged Mesh System (OpenTS2-style) ==========

	// Helper to create unique spatial key from grid coordinates
	static inline int32 MakeGridKey(int32 Level, int32 Row, int32 Column)
	{
		// Pack into 32-bit key: 8 bits level (0-255), 12 bits row (0-4095), 12 bits column (0-4095)
		return (Level << 24) | ((Row & 0xFFF) << 12) | (Column & 0xFFF);
	}

	// Find terrain tile at grid coordinates (O(1) lookup)
	FTerrainSegmentData* FindTerrainTile(int32 Level, int32 Row, int32 Column);

	// Add terrain tile to spatial map
	void AddTerrainTile(const FTerrainSegmentData& TileData, UMaterialInstance* Material = nullptr);

	// Remove terrain tile from spatial map
	UFUNCTION(BlueprintCallable)
	void RemoveTerrainTile(int32 Level, int32 Row, int32 Column);

	// Remove a rectangular region of terrain tiles (for stairs, basement openings, etc.)
	UFUNCTION(BlueprintCallable)
	void RemoveTerrainRegion(int32 Level, int32 FromRow, int32 FromCol, int32 ToRow, int32 ToCol);

	// Mark level as needing mesh rebuild
	void MarkLevelDirty(int32 Level);

	// Begin batch operation (suppresses invalidation)
	UFUNCTION(BlueprintCallable)
	void BeginBatchOperation();

	// End batch operation (rebuilds all dirty levels)
	UFUNCTION(BlueprintCallable)
	void EndBatchOperation();

	// Rebuild merged mesh for a specific level
	void RebuildLevel(int32 Level);

	// Migrate existing per-tile terrain to merged mesh system
	UFUNCTION(BlueprintCallable)
	void MigrateToMergedMesh();

	// O(1) spatial lookup: Key = (Level << 24) | (Row << 12) | Column
	// Maps grid coordinates to index in TerrainDataArray
	TMap<int32, int32> TerrainSpatialMap;

	// Track which levels need mesh rebuild
	TSet<int32> DirtyLevels;

	// Material per level for merged mesh system
	TMap<int32, UMaterialInstance*> LevelMaterials;

	// Batch operation flag - suppresses invalidation until EndBatchOperation()
	bool bInBatchOperation = false;

	// Terrain thickness configuration
	// Thickness extends downward from terrain surface
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	float TerrainThickness = 600.0f;

	// Maximum terrain height above lot base (in world units)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Constraints")
	float MaxTerrainHeight = 900.0f;

	// Minimum terrain depth below lot base -(TerrainThickness-10)
	UPROPERTY(BlueprintReadOnly, Category = "Terrain|Constraints")
	float MinTerrainHeight = -(TerrainThickness-10);

	// Side/cliff face material (separate from top grass/terrain material)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Materials")
	UMaterialInstance* SideMaterial;

	// Default side/cliff material to use if SideMaterial is not set
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Materials")
	UMaterialInstance* DefaultSideMaterial;

	// ========== Mesh Section Indexing ==========
	// Terrain ALWAYS uses exactly 2 mesh sections (no multi-level support):
	// - Section 0: Top surface (grass/terrain material, editable heights)
	// - Section 1: Sides + Bottom (cliff/dirt material, static geometry)
	// Level parameter is IGNORED - terrain is always ground floor only

	static inline int32 GetTopSectionIndex(int32 Level) { return 0; }
	static inline int32 GetSidesAndBottomSectionIndex(int32 Level) { return 1; }

	// ========== Incremental Update System ==========

	// Track dirty corners for incremental mesh updates
	// Key = (Level << 24) | (CornerRow << 12) | CornerColumn
	TSet<int32> DirtyCorners;

	// Update only the vertices affected by dirty corners (much faster than full rebuild)
	void UpdateDirtyVertices();

	// Mark a corner as dirty for incremental update
	void MarkCornerDirty(int32 Level, int32 CornerRow, int32 CornerColumn);

	// Clear all dirty corner tracking
	void ClearDirtyCorners();

	// Helper to pack corner coordinates into a single key
	static inline int32 MakeCornerKey(int32 Level, int32 CornerRow, int32 CornerColumn)
	{
		return (Level << 24) | ((CornerRow & 0xFFF) << 12) | (CornerColumn & 0xFFF);
	}
};

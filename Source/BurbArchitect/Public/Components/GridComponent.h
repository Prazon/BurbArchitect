// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Data/TileData.h"
#include "GridComponent.generated.h"

/**
 * GridComponent - Manages visualization of the lot grid system
 *
 * Renders grid tiles as merged mesh sections (1 draw call per level) while maintaining:
 * - Per-level visibility control
 * - Per-tile elevation control
 * - Per-tile visibility control (via degenerate triangles)
 * - Debug visualization via vertex colors (room IDs, states, etc.)
 * - Material-driven grid line rendering
 *
 * Architecture:
 * - One mesh section per level (section index = level)
 * - All tiles on a level merged into that level's section
 * - Tile N uses vertices [LocalIndex*4, LocalIndex*4+3] within its level's mesh
 * - Individual updates modify cached arrays then upload to GPU (~5-10µs)
 * - Degenerate triangles (all indices = 0) used for individual tile visibility
 * - Per-level visibility uses SetMeshSectionVisible (O(1), no rebuild)
 *
 * Boundary lines are drawn separately using LineBatchComponent for clean 1px lines.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UGridComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UGridComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * Generates the entire grid mesh from tile data
	 * Creates one merged mesh section containing all tiles (1 draw call)
	 *
	 * @param GridData - Array of tile data containing positions and properties
	 * @param InGridSizeX - Number of tiles in X direction
	 * @param InGridSizeY - Number of tiles in Y direction
	 * @param InGridTileSize - Size of each tile in world units
	 * @param GridMaterial - Material to apply to grid (should render lines procedurally)
	 * @param ExcludeLevel - Level to exclude from mesh generation (default -1 = no exclusion). Used to skip ground floor grid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void GenerateGridMesh(const TArray<FTileData>& GridData, int32 InGridSizeX, int32 InGridSizeY, float InGridTileSize, UMaterialInterface* GridMaterial, int32 ExcludeLevel = -1);

	/**
	 * Rebuilds grid mesh for a specific level only
	 * More efficient than full regeneration when tiles are added to one level
	 *
	 * @param Level - Level to rebuild
	 * @param GridData - Complete grid data array
	 * @param GridMaterial - Material to apply
	 * @param ExcludeLevel - Level to exclude from generation (default -1 = no exclusion). Used to skip ground floor grid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void RebuildGridLevel(int32 Level, const TArray<FTileData>& GridData, UMaterialInterface* GridMaterial, int32 ExcludeLevel = -1);

	/**
	 * Updates the vertex color for a specific tile
	 * Useful for debug visualization (room IDs, selection states, etc.)
	 *
	 * @param TileIndex - Index into the GridData array
	 * @param Color - New color to apply to all vertices of this tile
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void UpdateTileColor(int32 TileIndex, FColor Color);

	/**
	 * Shows or hides a specific tile
	 * Note: Currently recreates the mesh section with/without triangles
	 *
	 * @param TileIndex - Index into the GridData array
	 * @param bIsVisible - True to show, false to hide
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void SetTileVisibility(int32 TileIndex, bool bIsVisible);

	/**
	 * Draws boundary lines around the entire lot using LineBatchComponent
	 * Creates a clean 1px white rectangle outline
	 *
	 * @param LotWidth - Width of the lot in world units
	 * @param LotHeight - Height of the lot in world units
	 * @param LotLocation - World location of the lot origin
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void DrawBoundaryLines(float LotWidth, float LotHeight, const FVector& LotLocation);

	/**
	 * Clears all boundary lines
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void ClearBoundaryLines();

	/**
	 * Updates vertex colors for all tiles based on their room IDs
	 * Color-codes tiles for debug visualization
	 *
	 * @param GridData - Array of tile data containing room IDs
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void UpdateAllTileColorsFromRoomIDs(const TArray<FTileData>& GridData);

	/**
	 * Sets the visibility of the entire grid
	 *
	 * @param bShowGrid - True to show grid, false to hide
	 * @param bHideBoundaryLines - If true, also hide boundary lines (for live mode). If false, keep boundary lines visible (for build mode)
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void SetGridVisibility(bool bShowGrid, bool bHideBoundaryLines = false);

	/**
	 * Sets the visibility of a specific level's grid section
	 * Much more efficient than calling SetTileVisibility for every tile on a level
	 *
	 * @param Level - The level to show/hide
	 * @param bShow - True to show, false to hide
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void SetLevelVisibility(int32 Level, bool bShow);

protected:
	// Line batch component for rendering boundary lines
	UPROPERTY()
	ULineBatchComponent* BoundaryLineComponent;

	// References to other components for material parameter updates
	UPROPERTY()
	class UFloorComponent* FloorComponent;

	UPROPERTY()
	class UTerrainComponent* TerrainComponent;

	// Grid configuration (cached for updates)
	int32 GridSizeX;
	int32 GridSizeY;
	float GridTileSize;

	// Per-level cached mesh data
	// Each level has its own merged mesh section, indexed by level
	// Not serialized - regenerated on demand from FTileData
	TMap<int32, TArray<FVector>> LevelVertices;
	TMap<int32, TArray<int32>> LevelTriangles;
	TMap<int32, TArray<FVector>> LevelNormals;
	TMap<int32, TArray<FVector2D>> LevelUVs;
	TMap<int32, TArray<FColor>> LevelVertexColors;
	TMap<int32, TArray<FProcMeshTangent>> LevelTangents;

	// Spatial lookup: TileIndex -> (Level, LocalIndexInLevel)
	// Allows finding which level section a tile belongs to
	TMap<int32, FIntPoint> TileToLevelMap; // X = Level, Y = LocalIndex

	// Helper function to create a single tile quad
	void CreateTileQuad(int32 TileIndex, const FVector& TileCenter, int32 Level, FColor VertexColor, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs, TArray<FColor>& VertexColors, TArray<FProcMeshTangent>& Tangents);

	// Helper function to get a color from room ID for debug visualization
	FColor GetColorFromRoomID(int32 RoomID) const;

	// Helper to get level and local index for a tile
	// Returns true if found, false otherwise
	bool GetTileLevelAndIndex(int32 TileIndex, int32& OutLevel, int32& OutLocalIndex) const;

	// Helper to get vertex array start index within a level (5 vertices per tile)
	int32 GetLocalVertexStart(int32 LocalIndex) const { return LocalIndex * 5; }

	// Helper to get triangle array start index within a level (12 indices per tile = 4 triangles)
	int32 GetLocalTriangleStart(int32 LocalIndex) const { return LocalIndex * 12; }
};

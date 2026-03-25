// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TerrainHeightMap.generated.h"

/**
 * Helper class for storing terrain corner heights in a shared grid
 * Inspired by OpenTS2's height storage approach for efficient corner sharing
 * Corners are stored at grid intersections and shared between adjacent tiles
 */
USTRUCT(BlueprintType)
struct FTerrainHeightMap
{
	GENERATED_BODY()

	// Constructor
	FTerrainHeightMap()
		: GridWidth(0)
		, GridHeight(0)
		, TileSize(100.0f)
		, BaseZ(0.0f)
	{
	}

	// Initialize the height map with grid dimensions
	void Initialize(int32 InGridWidth, int32 InGridHeight, float InTileSize, float InBaseZ = 0.0f)
	{
		GridWidth = InGridWidth;
		GridHeight = InGridHeight;
		TileSize = InTileSize;
		BaseZ = InBaseZ;

		// Corner grid is (Width+1) x (Height+1) because corners are at tile edges
		const int32 CornerCount = (GridWidth + 1) * (GridHeight + 1);
		Heights.SetNumZeroed(CornerCount);
	}

	// Get height at a specific corner (Row, Column)
	// Row/Column are in corner space (0 to GridWidth/GridHeight inclusive)
	float GetCornerHeight(int32 CornerRow, int32 CornerColumn) const
	{
		if (!IsValidCorner(CornerRow, CornerColumn))
		{
			return BaseZ;
		}

		const int32 Index = CornerRow * (GridWidth + 1) + CornerColumn;

		// Defensive bounds check - prevents crash if HeightMap dimensions don't match grid dimensions
		// This can occur during editor property changes when GridSizeX/Y update before HeightMap regenerates
		if (!Heights.IsValidIndex(Index))
		{
			UE_LOG(LogTemp, Error, TEXT("TerrainHeightMap: Invalid corner index %d (Row=%d, Col=%d, GridSize=%dx%d, ArraySize=%d)"),
				Index, CornerRow, CornerColumn, GridWidth, GridHeight, Heights.Num());
			return BaseZ;
		}

		return Heights[Index];
	}

	// Set height at a specific corner
	void SetCornerHeight(int32 CornerRow, int32 CornerColumn, float Height)
	{
		if (!IsValidCorner(CornerRow, CornerColumn))
		{
			return;
		}

		const int32 Index = CornerRow * (GridWidth + 1) + CornerColumn;

		// Defensive bounds check - prevents crash if HeightMap dimensions don't match grid dimensions
		if (!Heights.IsValidIndex(Index))
		{
			UE_LOG(LogTemp, Error, TEXT("TerrainHeightMap: Invalid corner index %d for SetCornerHeight (Row=%d, Col=%d, GridSize=%dx%d, ArraySize=%d)"),
				Index, CornerRow, CornerColumn, GridWidth, GridHeight, Heights.Num());
			return;
		}

		Heights[Index] = Height;
	}

	// Get corner index for storage in FTileData
	int32 GetCornerIndex(int32 CornerRow, int32 CornerColumn) const
	{
		if (!IsValidCorner(CornerRow, CornerColumn))
		{
			return -1;
		}

		return CornerRow * (GridWidth + 1) + CornerColumn;
	}

	// Get world Z position for a corner
	float GetCornerWorldZ(int32 CornerRow, int32 CornerColumn) const
	{
		return BaseZ + GetCornerHeight(CornerRow, CornerColumn);
	}

	// Validate corner coordinates
	bool IsValidCorner(int32 CornerRow, int32 CornerColumn) const
	{
		return CornerRow >= 0 && CornerRow <= GridWidth &&
		       CornerColumn >= 0 && CornerColumn <= GridHeight;
	}

	// Get corner coordinates for a tile (returns indices for [BottomLeft, BottomRight, TopLeft, TopRight])
	TArray<int32> GetTileCornerIndices(int32 TileRow, int32 TileColumn) const
	{
		TArray<int32> Indices;
		Indices.SetNum(4);

		// Corner coordinates relative to tile
		// Tile (Row, Col) has corners at:
		// BottomLeft: (Row, Col)
		// BottomRight: (Row, Col+1)
		// TopLeft: (Row+1, Col)
		// TopRight: (Row+1, Col+1)

		Indices[0] = GetCornerIndex(TileRow, TileColumn);         // BottomLeft
		Indices[1] = GetCornerIndex(TileRow, TileColumn + 1);     // BottomRight
		Indices[2] = GetCornerIndex(TileRow + 1, TileColumn);     // TopLeft
		Indices[3] = GetCornerIndex(TileRow + 1, TileColumn + 1); // TopRight

		return Indices;
	}

	// Get average height of 4 corners (for center vertex calculation)
	float GetAverageHeight(int32 CornerRow0, int32 CornerCol0,
	                       int32 CornerRow1, int32 CornerCol1,
	                       int32 CornerRow2, int32 CornerCol2,
	                       int32 CornerRow3, int32 CornerCol3) const
	{
		const float H0 = GetCornerHeight(CornerRow0, CornerCol0);
		const float H1 = GetCornerHeight(CornerRow1, CornerCol1);
		const float H2 = GetCornerHeight(CornerRow2, CornerCol2);
		const float H3 = GetCornerHeight(CornerRow3, CornerCol3);

		return (H0 + H1 + H2 + H3) / 4.0f;
	}

	// Get average height for a tile's 4 corners
	float GetTileCenterHeight(int32 TileRow, int32 TileColumn) const
	{
		return GetAverageHeight(
			TileRow, TileColumn,           // BottomLeft
			TileRow, TileColumn + 1,       // BottomRight
			TileRow + 1, TileColumn,       // TopLeft
			TileRow + 1, TileColumn + 1    // TopRight
		);
	}

	// Adjust height at a corner (delta modification)
	void AdjustCornerHeight(int32 CornerRow, int32 CornerColumn, float DeltaHeight)
	{
		if (!IsValidCorner(CornerRow, CornerColumn))
		{
			return;
		}

		const int32 Index = CornerRow * (GridWidth + 1) + CornerColumn;

		// Defensive bounds check - prevents crash if HeightMap dimensions don't match grid dimensions
		if (!Heights.IsValidIndex(Index))
		{
			UE_LOG(LogTemp, Error, TEXT("TerrainHeightMap: Invalid corner index %d for AdjustCornerHeight (Row=%d, Col=%d, GridSize=%dx%d, ArraySize=%d)"),
				Index, CornerRow, CornerColumn, GridWidth, GridHeight, Heights.Num());
			return;
		}

		Heights[Index] += DeltaHeight;
	}

	// Flatten a corner to a specific height
	void FlattenCorner(int32 CornerRow, int32 CornerColumn, float TargetHeight)
	{
		SetCornerHeight(CornerRow, CornerColumn, TargetHeight);
	}

	// Reset all heights to base level
	void Reset()
	{
		Heights.SetNumZeroed((GridWidth + 1) * (GridHeight + 1));
	}

	// Get the base Z level for this height map
	float GetBaseZ() const
	{
		return BaseZ;
	}

	// Get grid dimensions
	int32 GetGridWidth() const
	{
		return GridWidth;
	}

	int32 GetGridHeight() const
	{
		return GridHeight;
	}

private:
	// Grid dimensions in tiles
	UPROPERTY()
	int32 GridWidth;

	UPROPERTY()
	int32 GridHeight;

	// Size of each tile
	UPROPERTY()
	float TileSize;

	// Base Z position (ground level)
	UPROPERTY()
	float BaseZ;

	// Height values at corners (relative to BaseZ)
	// Array size is (GridWidth+1) * (GridHeight+1)
	// Index = CornerRow * (GridHeight+1) + CornerColumn
	UPROPERTY()
	TArray<float> Heights;
};

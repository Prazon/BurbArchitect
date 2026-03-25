// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TileTriangleData.h"
#include "TileData.generated.h"

//Data that represents a single Tile
USTRUCT(BlueprintType)
struct FTileData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TileData")
	int32 TileIndex = -1;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TileData")
	int32 Level;

	//X:Row Y:Column
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TileData")
	FVector2D TileCoord;

	//Corner fvectors
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TileData")
	TArray<FVector> CornerLocs;

	//Center fvector
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "TileData")
	FVector Center;

	// Corner height indices for grid-based height storage
	// Maps to shared corner height grid: [BottomLeft, BottomRight, TopLeft, TopRight]
	UPROPERTY()
	TArray<int32> CornerHeightIndices;

	// DEPRECATED: Single room ID - kept for backwards compatibility only
	// Use TriangleOwnership instead for accurate per-triangle room assignment
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use GetPrimaryRoomID() or TriangleOwnership instead"))
	int32 RoomID_DEPRECATED;

	// NEW: Per-triangle room ownership (replaces single RoomID)
	// Supports tiles split by diagonal walls
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TileData")
	FTileTriangleData TriangleOwnership;

	//used for flood fill algo, false by default and true temporarily when flood filling
	UPROPERTY()
	bool bSeeded = false;

	//used for various algorithms, represents a tile that exists in the 1-tile layer that surrounds a grid
	UPROPERTY(EditAnywhere, Category = "TileData")
	bool bOutOfBounds = false;

	// ========== Backwards Compatibility Layer ==========

	/**
	 * Get the primary room ID for this tile (backwards compatibility)
	 * Returns the dominant room if tile is split, or the single room if uniform
	 * @return Primary room ID (0 if exterior)
	 */
	FORCEINLINE int32 GetPrimaryRoomID() const
	{
		return TriangleOwnership.GetDominantRoomID();
	}

	/**
	 * DEPRECATED: Get room ID using old property name for compatibility
	 * New code should use GetPrimaryRoomID() or access TriangleOwnership directly
	 */
	FORCEINLINE int32 GetRoomID() const
	{
		return GetPrimaryRoomID();
	}

	/**
	 * Set all triangles to the same room (backwards compatibility)
	 * New code should use TriangleOwnership.SetTriangleRoom() for precise control
	 * @param NewRoomID Room ID to assign to all triangles
	 */
	FORCEINLINE void SetRoomID(int32 NewRoomID)
	{
		TriangleOwnership.SetAllTriangles(NewRoomID);
		RoomID_DEPRECATED = NewRoomID; // Keep deprecated field in sync
	}

	/**
	 * Check if this tile is split between multiple rooms
	 * @return True if triangles belong to different rooms
	 */
	FORCEINLINE bool IsRoomSplit() const
	{
		return TriangleOwnership.IsSplit();
	}

	/**
	 * Get all unique room IDs present in this tile
	 * @return Array of unique room IDs (excluding 0/exterior)
	 */
	FORCEINLINE TArray<int32> GetAllRoomIDs() const
	{
		return TriangleOwnership.GetUniqueRoomIDs();
	}

	// Note: Blueprint functions are not supported in structs
	// Use GetPrimaryRoomID() and SetRoomID() from C++ or access TriangleOwnership directly from Blueprint

	//Compare
    bool operator==(const FTileData & other) const
    {
    	return (other.TileCoord == TileCoord && other.Level == Level);
    }
    //Compare
    bool operator!=(const FTileData & other) const
    {
    	return (other.TileCoord != TileCoord && other.Level != Level);
    }
};

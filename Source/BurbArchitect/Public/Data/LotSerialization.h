// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LotSerialization.generated.h"

/**
 * Grid configuration data
 * Stores the lot's grid dimensions and tile size
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FLotGridConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Grid")
	int32 GridSizeX = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Grid")
	int32 GridSizeY = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Grid")
	float GridTileSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Grid")
	int32 Floors = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Grid")
	int32 Basements = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Grid")
	int32 CurrentLevel = 0;
};

/**
 * Serialized wall node data
 * Represents a vertex in the wall graph at a specific grid location
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedWallNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 NodeID = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 Row = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 Col = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 Level = 0;
};

/**
 * Serialized wall edge data
 * Represents a wall segment connecting two nodes
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedWallEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 EdgeID = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 StartNodeID = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 EndNodeID = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 Room1 = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	int32 Room2 = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	float Height = 300.0f;

	// Full asset path to wall pattern (e.g., "/Game/Patterns/WP_Brick.WP_Brick")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	FString PatternPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Wall")
	FString MaterialPath;
};

/**
 * Serialized floor tile data
 * Represents a single floor tile at a grid location
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedFloorTile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Floor")
	int32 Row = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Floor")
	int32 Col = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Floor")
	int32 Level = 0;

	// Full asset path to floor pattern (e.g., "/Game/Patterns/FP_Wood.FP_Wood")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Floor")
	FString PatternPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Floor")
	FString MaterialPath;

	// Bitfield for which tile sections are active (Top=1, Right=2, Bottom=4, Left=8)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Floor")
	uint8 TileSections = 15; // Default: all sections active
};

/**
 * Serialized roof data
 * Represents a roof structure
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedRoofData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	FString RoofClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	FVector StartLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	float Width = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	float Length = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	float Pitch = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	float Height = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	float RoofThickness = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	float GableThickness = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	int32 Level = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Roof")
	FString MaterialPath;
};

/**
 * Serialized stair module data
 * Represents a single module (tread or landing) in a staircase
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedStairModule
{
	GENERATED_BODY()

	// Module type: 0 = Tread, 1 = Landing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	uint8 ModuleType = 0;

	// Turning socket: 0 = Idle, 1 = Right, 2 = Left
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	uint8 TurningSocket = 0;
};

/**
 * Serialized stairs data
 * Represents a staircase structure
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedStairsData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	FString StairsClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	FVector StartLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	int32 StartLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	int32 EndLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	TArray<FSerializedStairModule> Modules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	float StairsThickness = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	FString TreadMeshPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Stairs")
	FString LandingMeshPath;
};

/**
 * Serialized terrain tile data
 * Represents a single terrain tile's corner heights
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedTerrainTile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	int32 Row = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	int32 Column = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	int32 Level = 0;

	// Heights for the 4 corners of this tile (BottomLeft, BottomRight, TopLeft, TopRight)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	TArray<float> CornerHeights;
};

/**
 * Serialized terrain data
 * Represents the heightmap for terrain
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedTerrainData
{
	GENERATED_BODY()

	// Array of terrain tiles with their positions and corner heights
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	TArray<FSerializedTerrainTile> Tiles;

	// Legacy heightmap data (deprecated, kept for backward compatibility)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	TArray<float> Heightmap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	int32 ResolutionX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Terrain")
	int32 ResolutionY = 0;
};

/**
 * Serialized portal data
 * Represents a door or window placed in a wall
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedPortalData
{
	GENERATED_BODY()

	// Full path to the portal class (e.g., /Game/Content/Portals/BP_Door.BP_Door_C)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Portal")
	FString PortalClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Portal")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Portal")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Portal")
	int32 Level = 0;

	// ID of the wall edge this portal is attached to (-1 if not attached)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Portal")
	int32 AttachedWallEdgeID = -1;
};

/**
 * Serialized placed object data
 * Represents furniture or decorations placed in the lot
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedPlacedObject
{
	GENERATED_BODY()

	// Full path to the object class (e.g., /Game/Content/Objects/BP_Chair.BP_Chair_C)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Object")
	FString ObjectClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Object")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Object")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Object")
	FVector Scale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Object")
	int32 Level = 0;
};

/**
 * Serialized pool water data
 * Represents a pool water volume
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedPoolWater
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Water")
	int32 RoomID = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Water")
	int32 Level = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Water")
	TArray<FVector> BoundaryVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Water")
	float WaterSurfaceZ = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Water")
	float PoolFloorZ = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Water")
	FString MaterialPath;
};

/**
 * Serialized fence segment data
 * Represents a fence segment between two points
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedFenceSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Fence")
	FVector StartLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Fence")
	FVector EndLoc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Fence")
	int32 Level = 0;

	// Full asset path to fence item (e.g., "/Game/Fences/FI_WoodPicket.FI_WoodPicket")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot|Fence")
	FString FenceItemPath;
};

/**
 * Master serialized lot data
 * Contains all data needed to save/load a complete lot
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FSerializedLotData
{
	GENERATED_BODY()

	// Grid configuration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	FLotGridConfig GridConfig;

	// Wall graph data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedWallNode> WallNodes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedWallEdge> WallEdges;

	// Floor data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedFloorTile> FloorTiles;

	// Roof data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedRoofData> Roofs;

	// Stairs data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedStairsData> Stairs;

	// Terrain data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	FSerializedTerrainData Terrain;

	// Portals (doors/windows)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedPortalData> Portals;

	// Placed objects (furniture, decorations)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedPlacedObject> PlacedObjects;

	// Pool water volumes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedPoolWater> PoolWater;

	// Fence segments
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<FSerializedFenceSegment> Fences;

	// Room IDs for each tile (flattened grid: [Level * GridSizeX * GridSizeY + Row * GridSizeX + Col])
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<int32> TileRoomIDs;

	// Upper floor tiles (tiles above ground floor that need to be recreated)
	// Stored as packed int32: (Level << 24) | (Row << 12) | Column
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	TArray<int32> UpperFloorTiles;

	// Optional metadata
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	FString LotName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lot")
	FDateTime SaveTimestamp;
};

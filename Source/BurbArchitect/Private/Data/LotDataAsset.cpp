// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/LotDataAsset.h"

bool ULotDataAsset::ValidateLotData() const
{
	// Check grid dimensions are valid
	if (LotData.GridConfig.GridSizeX <= 0 || LotData.GridConfig.GridSizeY <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: Invalid grid dimensions: %dx%d"),
			LotData.GridConfig.GridSizeX, LotData.GridConfig.GridSizeY);
		return false;
	}

	if (LotData.GridConfig.GridTileSize <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: Invalid tile size: %f"), LotData.GridConfig.GridTileSize);
		return false;
	}

	if (LotData.GridConfig.Floors <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: Invalid floor count: %d"), LotData.GridConfig.Floors);
		return false;
	}

	// Validate wall nodes
	for (const FSerializedWallNode& Node : LotData.WallNodes)
	{
		if (Node.Row < 0 || Node.Row > LotData.GridConfig.GridSizeX ||
			Node.Col < 0 || Node.Col > LotData.GridConfig.GridSizeY ||
			Node.Level < 0 || Node.Level >= LotData.GridConfig.Floors)
		{
			UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: Wall node %d has invalid coordinates: (%d, %d, %d)"),
				Node.NodeID, Node.Row, Node.Col, Node.Level);
			return false;
		}
	}

	// Validate wall edges reference valid nodes
	TSet<int32> NodeIDs;
	for (const FSerializedWallNode& Node : LotData.WallNodes)
	{
		NodeIDs.Add(Node.NodeID);
	}

	for (const FSerializedWallEdge& Edge : LotData.WallEdges)
	{
		if (!NodeIDs.Contains(Edge.StartNodeID) || !NodeIDs.Contains(Edge.EndNodeID))
		{
			UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: Wall edge %d references invalid nodes: %d -> %d"),
				Edge.EdgeID, Edge.StartNodeID, Edge.EndNodeID);
			return false;
		}
	}

	// Validate floor tiles
	for (const FSerializedFloorTile& Tile : LotData.FloorTiles)
	{
		if (Tile.Row < 0 || Tile.Row >= LotData.GridConfig.GridSizeX ||
			Tile.Col < 0 || Tile.Col >= LotData.GridConfig.GridSizeY ||
			Tile.Level < 0 || Tile.Level >= LotData.GridConfig.Floors)
		{
			UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: Floor tile has invalid coordinates: (%d, %d, %d)"),
				Tile.Row, Tile.Col, Tile.Level);
			return false;
		}
	}

	// Validate room IDs array size if present
	const int32 ExpectedRoomIDCount = LotData.GridConfig.GridSizeX * LotData.GridConfig.GridSizeY * LotData.GridConfig.Floors;
	if (LotData.TileRoomIDs.Num() > 0 && LotData.TileRoomIDs.Num() != ExpectedRoomIDCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: RoomID array size mismatch. Expected %d, got %d"),
			ExpectedRoomIDCount, LotData.TileRoomIDs.Num());
		return false;
	}

	// Validate terrain heightmap dimensions
	if (LotData.Terrain.Heightmap.Num() > 0)
	{
		const int32 ExpectedHeightmapSize = LotData.Terrain.ResolutionX * LotData.Terrain.ResolutionY;
		if (LotData.Terrain.Heightmap.Num() != ExpectedHeightmapSize)
		{
			UE_LOG(LogTemp, Warning, TEXT("LotDataAsset: Terrain heightmap size mismatch. Expected %d, got %d"),
				ExpectedHeightmapSize, LotData.Terrain.Heightmap.Num());
			return false;
		}
	}

	return true;
}

FString ULotDataAsset::GetLotName() const
{
	if (!LotData.LotName.IsEmpty())
	{
		return LotData.LotName;
	}

	// Fall back to asset name
	return GetName();
}

FString ULotDataAsset::GetDescription() const
{
	return LotData.Description;
}

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/FloorComponent.h" // For ETriangleType enum
#include "TileTriangleData.generated.h"

/**
 * Represents per-triangle room ownership for a tile.
 * Each tile is divided into 4 triangles (Top, Right, Bottom, Left) that can belong to different rooms.
 * This enables proper handling of diagonal walls that split tiles.
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FTileTriangleData
{
	GENERATED_BODY()

	/**
	 * Room ID for each triangle in the tile.
	 * Indexed by ETriangleType: [0]=Top, [1]=Right, [2]=Bottom, [3]=Left
	 * Value of 0 means no room (outside/exterior)
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Room")
	TArray<int32> TriangleRoomIDs;

	FTileTriangleData()
	{
		// Initialize with 4 triangles, all unassigned (room 0)
		TriangleRoomIDs.Init(0, 4);
	}

	/**
	 * Check if this tile is split between multiple rooms
	 * @return True if triangles belong to different rooms
	 */
	FORCEINLINE bool IsSplit() const
	{
		if (TriangleRoomIDs.Num() != 4) return false;

		int32 FirstRoom = TriangleRoomIDs[0];
		for (int32 i = 1; i < 4; i++)
		{
			if (TriangleRoomIDs[i] != FirstRoom)
				return true;
		}
		return false;
	}

	/**
	 * Get the dominant room ID (the room that owns the most triangles)
	 * Used for backwards compatibility and simplified queries
	 * @return Room ID that owns the most triangles, or 0 if all are exterior
	 */
	FORCEINLINE int32 GetDominantRoomID() const
	{
		if (TriangleRoomIDs.Num() != 4) return 0;

		// Count occurrences of each room ID
		TMap<int32, int32> RoomCounts;
		for (int32 RoomID : TriangleRoomIDs)
		{
			if (RoomID > 0) // Ignore exterior (0)
			{
				RoomCounts.FindOrAdd(RoomID)++;
			}
		}

		// Find room with most triangles
		int32 DominantRoom = 0;
		int32 MaxCount = 0;
		for (const auto& Pair : RoomCounts)
		{
			if (Pair.Value > MaxCount)
			{
				MaxCount = Pair.Value;
				DominantRoom = Pair.Key;
			}
		}

		return DominantRoom;
	}

	/**
	 * Set all triangles to the same room (for backwards compatibility)
	 * @param RoomID The room ID to assign to all triangles
	 */
	FORCEINLINE void SetAllTriangles(int32 RoomID)
	{
		if (TriangleRoomIDs.Num() != 4)
		{
			TriangleRoomIDs.Init(RoomID, 4);
		}
		else
		{
			for (int32& TriangleRoom : TriangleRoomIDs)
			{
				TriangleRoom = RoomID;
			}
		}
	}

	/**
	 * Get room ID for a specific triangle
	 * @param TriangleType Which triangle to query
	 * @return Room ID for that triangle, or 0 if invalid
	 */
	FORCEINLINE int32 GetTriangleRoom(ETriangleType TriangleType) const
	{
		int32 Index = (int32)TriangleType;
		if (Index >= 0 && Index < TriangleRoomIDs.Num())
		{
			return TriangleRoomIDs[Index];
		}
		return 0;
	}

	/**
	 * Set room ID for a specific triangle
	 * @param TriangleType Which triangle to set
	 * @param RoomID The room ID to assign
	 */
	FORCEINLINE void SetTriangleRoom(ETriangleType TriangleType, int32 RoomID)
	{
		int32 Index = (int32)TriangleType;
		if (Index >= 0 && Index < TriangleRoomIDs.Num())
		{
			TriangleRoomIDs[Index] = RoomID;
		}
	}

	/**
	 * Get all unique room IDs in this tile (excluding 0/exterior)
	 * @return Array of unique room IDs
	 */
	TArray<int32> GetUniqueRoomIDs() const
	{
		TArray<int32> UniqueRooms;
		for (int32 RoomID : TriangleRoomIDs)
		{
			if (RoomID > 0 && !UniqueRooms.Contains(RoomID))
			{
				UniqueRooms.Add(RoomID);
			}
		}
		return UniqueRooms;
	}

	/**
	 * Check if any triangle belongs to a specific room
	 * @param RoomID The room ID to check for
	 * @return True if any triangle belongs to this room
	 */
	FORCEINLINE bool ContainsRoom(int32 RoomID) const
	{
		return TriangleRoomIDs.Contains(RoomID);
	}

	/**
	 * Count how many triangles belong to a specific room
	 * @param RoomID The room ID to count
	 * @return Number of triangles (0-4) belonging to this room
	 */
	FORCEINLINE int32 CountTrianglesInRoom(int32 RoomID) const
	{
		int32 Count = 0;
		for (int32 TriangleRoom : TriangleRoomIDs)
		{
			if (TriangleRoom == RoomID)
				Count++;
		}
		return Count;
	}

	/**
	 * Reset all triangles to exterior (room 0)
	 */
	FORCEINLINE void Reset()
	{
		SetAllTriangles(0);
	}

	// Serialization
	bool Serialize(FArchive& Ar)
	{
		Ar << TriangleRoomIDs;
		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FTileTriangleData> : public TStructOpsTypeTraitsBase2<FTileTriangleData>
{
	enum
	{
		WithSerializer = true,
	};
};

/**
 * Coordinate for addressing a specific triangle within the tile grid.
 * Used for triangle-first room detection where each triangle is the fundamental unit.
 * Each tile contains 4 triangles (Top, Right, Bottom, Left) that can belong to different rooms.
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FTriangleCoord
{
	GENERATED_BODY()

	// Grid row of the tile containing this triangle
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Triangle")
	int32 Row = 0;

	// Grid column of the tile containing this triangle
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Triangle")
	int32 Column = 0;

	// Floor level
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Triangle")
	int32 Level = 0;

	// Which triangle within the tile (Top, Right, Bottom, Left)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Triangle")
	ETriangleType Triangle = ETriangleType::Top;

	FTriangleCoord() = default;

	FTriangleCoord(int32 InRow, int32 InColumn, int32 InLevel, ETriangleType InTriangle)
		: Row(InRow), Column(InColumn), Level(InLevel), Triangle(InTriangle)
	{
	}

	// Construct from tile coordinate and triangle type
	FTriangleCoord(const FIntVector& TileCoord, ETriangleType InTriangle)
		: Row(TileCoord.X), Column(TileCoord.Y), Level(TileCoord.Z), Triangle(InTriangle)
	{
	}

	/**
	 * Get the tile coordinate (without triangle info)
	 * @return FIntVector with X=Row, Y=Column, Z=Level
	 */
	FORCEINLINE FIntVector GetTileCoord() const
	{
		return FIntVector(Row, Column, Level);
	}

	/**
	 * Get a packed 64-bit key for use in spatial maps
	 * Format: [Level:16][Row:16][Column:16][Triangle:8][Reserved:8]
	 * Supports grids up to 65535x65535 with 65535 levels
	 * @return Unique 64-bit key for this triangle
	 */
	FORCEINLINE uint64 GetPackedKey() const
	{
		return (static_cast<uint64>(Level) << 48) |
		       (static_cast<uint64>(Row) << 32) |
		       (static_cast<uint64>(Column) << 16) |
		       (static_cast<uint64>(Triangle) << 8);
	}

	/**
	 * Create a FTriangleCoord from a packed key
	 * @param PackedKey The 64-bit packed key
	 * @return Unpacked triangle coordinate
	 */
	static FORCEINLINE FTriangleCoord FromPackedKey(uint64 PackedKey)
	{
		FTriangleCoord Coord;
		Coord.Level = static_cast<int32>((PackedKey >> 48) & 0xFFFF);
		Coord.Row = static_cast<int32>((PackedKey >> 32) & 0xFFFF);
		Coord.Column = static_cast<int32>((PackedKey >> 16) & 0xFFFF);
		Coord.Triangle = static_cast<ETriangleType>((PackedKey >> 8) & 0xFF);
		return Coord;
	}

	// Comparison operators
	bool operator==(const FTriangleCoord& Other) const
	{
		return Row == Other.Row && Column == Other.Column &&
		       Level == Other.Level && Triangle == Other.Triangle;
	}

	bool operator!=(const FTriangleCoord& Other) const
	{
		return !(*this == Other);
	}

	// For use in TSet/TMap
	friend uint32 GetTypeHash(const FTriangleCoord& Coord)
	{
		return HashCombine(
			HashCombine(GetTypeHash(Coord.Row), GetTypeHash(Coord.Column)),
			HashCombine(GetTypeHash(Coord.Level), GetTypeHash(static_cast<uint8>(Coord.Triangle)))
		);
	}

	// Debug string
	FString ToString() const
	{
		static const TCHAR* TriangleNames[] = { TEXT("Top"), TEXT("Right"), TEXT("Bottom"), TEXT("Left") };
		const TCHAR* TriName = (static_cast<int32>(Triangle) < 4) ? TriangleNames[static_cast<int32>(Triangle)] : TEXT("Invalid");
		return FString::Printf(TEXT("(%d,%d,%d:%s)"), Row, Column, Level, TriName);
	}
};
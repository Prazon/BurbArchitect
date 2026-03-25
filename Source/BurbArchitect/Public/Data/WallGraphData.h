// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WallGraphData.generated.h"

class UWallPattern;

/**
 * Represents a node/vertex in the wall graph
 * Nodes are connection points where walls meet
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FWallNode
{
	GENERATED_BODY()

	// Unique identifier for this node
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 NodeID = -1;

	// World position of this node (can be grid-snapped but not constrained to grid)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	FVector Position = FVector::ZeroVector;

	// Floor level (0 = basement/ground depending on setup)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 Level = 0;

	// Grid coordinates for fast spatial queries (derived from Position)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 Row = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 Column = 0;

	// All wall edges connected to this node (for fast adjacency queries)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	TArray<int32> ConnectedEdgeIDs;

	// Default constructor
	FWallNode()
		: NodeID(-1)
		, Position(FVector::ZeroVector)
		, Level(0)
		, Row(0)
		, Column(0)
	{}

	// Constructor with parameters
	FWallNode(int32 InNodeID, const FVector& InPosition, int32 InLevel, int32 InRow, int32 InColumn)
		: NodeID(InNodeID)
		, Position(InPosition)
		, Level(InLevel)
		, Row(InRow)
		, Column(InColumn)
	{}

	// Equality operator for comparisons
	bool operator==(const FWallNode& Other) const
	{
		return NodeID == Other.NodeID;
	}

	// Hash function for use in TSet/TMap
	friend uint32 GetTypeHash(const FWallNode& Node)
	{
		return GetTypeHash(Node.NodeID);
	}
};

/**
 * Represents an edge/segment in the wall graph
 * Edges are the actual walls connecting two nodes
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FWallEdge
{
	GENERATED_BODY()

	// Unique identifier for this edge
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 EdgeID = -1;

	// Start node ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 FromNodeID = -1;

	// End node ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 ToNodeID = -1;

	// Floor level
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 Level = 0;

	// Room on one side of the wall (0 = outside/no room)
	// Determined by right-hand rule: standing at FromNode looking toward ToNode, Room1 is on the right
	// MUTABLE: Can be updated dynamically as rooms change
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	mutable int32 Room1 = 0;

	// Room on the other side of the wall (0 = outside/no room)
	// MUTABLE: Can be updated dynamically as rooms change
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	mutable int32 Room2 = 0;

	// NEW: Generation number when Room1 was assigned
	// Used to determine if room assignment is stale
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	mutable int32 Room1Generation = -1;

	// NEW: Generation number when Room2 was assigned
	// Used to determine if room assignment is stale
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	mutable int32 Room2Generation = -1;

	// Wall height at start
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	float Height = 300.0f;

	// Wall height at end (for sloped walls)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	float EndHeight = 300.0f;

	// Wall thickness
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	float Thickness = 20.0f;

	// Wall pattern/material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	UWallPattern* Pattern = nullptr;

	// Whether this wall has been committed (vs preview)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	bool bCommitted = false;

	// Whether this wall is a pool interior wall (restricts door placement)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	bool bIsPoolWall = false;

	// Grid coordinates of start node (cached for fast spatial queries)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 StartRow = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 StartColumn = 0;

	// Grid coordinates of end node (cached for fast spatial queries)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 EndRow = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 EndColumn = 0;

	// Default constructor
	FWallEdge()
		: EdgeID(-1)
		, FromNodeID(-1)
		, ToNodeID(-1)
		, Level(0)
		, Room1(0)
		, Room2(0)
		, Room1Generation(-1)
		, Room2Generation(-1)
		, Height(300.0f)
		, EndHeight(300.0f)
		, Thickness(20.0f)
		, Pattern(nullptr)
		, bCommitted(false)
		, bIsPoolWall(false)
		, StartRow(0)
		, StartColumn(0)
		, EndRow(0)
		, EndColumn(0)
	{}

	// Equality operator
	bool operator==(const FWallEdge& Other) const
	{
		return EdgeID == Other.EdgeID;
	}

	// Hash function
	friend uint32 GetTypeHash(const FWallEdge& Edge)
	{
		return GetTypeHash(Edge.EdgeID);
	}

	// Check if this edge connects two specific nodes (order-independent)
	bool ConnectsNodes(int32 NodeA, int32 NodeB) const
	{
		return (FromNodeID == NodeA && ToNodeID == NodeB) ||
		       (FromNodeID == NodeB && ToNodeID == NodeA);
	}

	// Get the "other" node from this edge given one node
	int32 GetOtherNode(int32 NodeID) const
	{
		if (FromNodeID == NodeID)
			return ToNodeID;
		else if (ToNodeID == NodeID)
			return FromNodeID;
		return -1;
	}

	// Check if this is a diagonal wall (not axis-aligned)
	bool IsDiagonal() const
	{
		return (StartRow != EndRow) && (StartColumn != EndColumn);
	}

	// Check if this is an axis-aligned wall
	bool IsAxisAligned() const
	{
		return (StartRow == EndRow) || (StartColumn == EndColumn);
	}

	// NEW: Check if Room1 assignment is current for given generation
	bool IsRoom1Current(int32 CurrentGeneration) const
	{
		return Room1Generation >= CurrentGeneration;
	}

	// NEW: Check if Room2 assignment is current for given generation
	bool IsRoom2Current(int32 CurrentGeneration) const
	{
		return Room2Generation >= CurrentGeneration;
	}

	// NEW: Check if both room assignments are current
	bool AreRoomsCurrent(int32 CurrentGeneration) const
	{
		return IsRoom1Current(CurrentGeneration) && IsRoom2Current(CurrentGeneration);
	}

	// NEW: Clear room assignments (called when wall is invalidated)
	void InvalidateRooms()
	{
		Room1 = 0;
		Room2 = 0;
		Room1Generation = -1;
		Room2Generation = -1;
	}

	// NEW: Set room assignment with generation tracking
	void SetRoom1(int32 RoomID, int32 Generation)
	{
		Room1 = RoomID;
		Room1Generation = Generation;
	}

	void SetRoom2(int32 RoomID, int32 Generation)
	{
		Room2 = RoomID;
		Room2Generation = Generation;
	}
};

/**
 * Represents a collinear run of wall edges through degree-2 nodes.
 * Ported from Godot WallGraph.WallRun - groups edges that share the same
 * direction into a single logical run for portal spanning and coordinate mapping.
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FWallRun
{
	GENERATED_BODY()

	// Ordered edge IDs along the run (start to end)
	UPROPERTY(BlueprintReadOnly, Category = "Wall Graph")
	TArray<int32> EdgeIDs;

	// Length of each edge in the same order as EdgeIDs
	UPROPERTY(BlueprintReadOnly, Category = "Wall Graph")
	TArray<float> EdgeLengths;

	// Sum of all edge lengths
	UPROPERTY(BlueprintReadOnly, Category = "Wall Graph")
	float TotalLength = 0.f;

	// Normalized direction of the run (from first edge's FromNode toward last edge's ToNode)
	UPROPERTY(BlueprintReadOnly, Category = "Wall Graph")
	FVector Direction = FVector::ZeroVector;

	bool IsValid() const { return EdgeIDs.Num() > 0 && TotalLength > 0.f; }
};

/**
 * Result of mapping a run-space parameter to a specific edge.
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FWallRunEdgeMapping
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Wall Graph")
	int32 EdgeID = -1;

	// 0..1 parameter along the edge in FromNode->ToNode direction
	UPROPERTY(BlueprintReadOnly, Category = "Wall Graph")
	float LocalT = 0.f;
};

/**
 * Represents an intersection point where multiple walls meet
 * Used for calculating proper corner mitring and T-junctions
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FWallIntersection
{
	GENERATED_BODY()

	// Node ID at the intersection point
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	int32 NodeID = -1;

	// All edges meeting at this intersection
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	TArray<int32> ConnectedEdgeIDs;

	// Whether this is a simple intersection (2 walls, simple corner/cap)
	// vs complex (3+ walls, T-junction or cross)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	bool bIsSimple = false;

	// Calculated extents for each wall at this intersection (for mitring)
	// Maps EdgeID -> extent distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	TMap<int32, float> WallExtents;

	// Default constructor
	FWallIntersection()
		: NodeID(-1)
		, bIsSimple(false)
	{}

	// Constructor with node
	explicit FWallIntersection(int32 InNodeID)
		: NodeID(InNodeID)
		, bIsSimple(false)
	{}

	// Update simple flag based on connected edges
	void UpdateSimpleFlag()
	{
		bIsSimple = (ConnectedEdgeIDs.Num() == 2);
	}
};

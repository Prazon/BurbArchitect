// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Data/WallGraphData.h"
#include "WallGraphComponent.generated.h"

class UWallPattern;
class ALotManager;

/**
 * Wall Graph Component
 *
 * Manages walls as a graph structure (nodes and edges) rather than tile-based arrays.
 * Inspired by OpenTS2's wall graph system, this provides:
 * - O(1) wall queries via spatial indexing
 * - Room adjacency stored directly on walls
 * - Support for arbitrary wall angles (diagonal, etc.)
 * - Clean intersection handling for T-junctions and corners
 *
 * Key Concepts:
 * - Nodes (FWallNode): Connection points where walls meet
 * - Edges (FWallEdge): The actual wall segments connecting nodes
 * - Intersections: Pre-computed data for proper corner mitring
 * - Spatial Index: Fast lookup of walls by grid tile
 */
UCLASS(ClassGroup=(BurbArchitect), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UWallGraphComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWallGraphComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	//~ Begin UActorComponent Interface
	virtual void OnComponentCreated() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	//~ End UActorComponent Interface

	// ========================================
	// Core Graph Data
	// ========================================

	// All nodes in the wall graph (NodeID -> Node)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	TMap<int32, FWallNode> Nodes;

	// All edges in the wall graph (EdgeID -> Edge)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	TMap<int32, FWallEdge> Edges;

	// Intersection data for proper corner mitring (NodeID -> Intersection)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Graph")
	TMap<int32, FWallIntersection> Intersections;

	// ========================================
	// Spatial Indexing
	// ========================================

	// Spatial index: Grid tile -> Edge IDs passing through that tile
	// Key is (Row + Column * 10000) for fast hashing
	// NOTE: TMultiMap cannot be UPROPERTY - not supported by UHT
	TMultiMap<int64, int32> TileToEdges;

	// Node spatial index: Grid tile -> Node IDs at that tile
	// NOTE: TMultiMap cannot be UPROPERTY - not supported by UHT
	TMultiMap<int64, int32> TileToNodes;

	// Level index: Level -> Edge IDs at that level (for O(1) GetEdgesAtLevel queries)
	// NOTE: TMap with TSet cannot be UPROPERTY - not supported by UHT
	TMap<int32, TSet<int32>> EdgesByLevel;

	// ========================================
	// ID Generation
	// ========================================

	UPROPERTY()
	int32 NextNodeID = 0;

	UPROPERTY()
	int32 NextEdgeID = 0;

	// ========================================
	// Core Operations
	// ========================================

	/**
	 * Add a new node to the wall graph
	 * @param Position World position of the node
	 * @param Level Floor level
	 * @param Row Grid row coordinate (for spatial indexing)
	 * @param Column Grid column coordinate (for spatial indexing)
	 * @return NodeID of the newly created node, or -1 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	int32 AddNode(const FVector& Position, int32 Level, int32 Row, int32 Column);

	/**
	 * Find an existing node at a specific grid position and level
	 * @param Row Grid row
	 * @param Column Grid column
	 * @param Level Floor level
	 * @return NodeID if found, -1 otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	int32 FindNodeAt(int32 Row, int32 Column, int32 Level) const;

	/**
	 * Add a new wall edge to the graph
	 * @param FromNodeID Start node
	 * @param ToNodeID End node
	 * @param Height Wall height
	 * @param Thickness Wall thickness
	 * @param Pattern Wall pattern/material
	 * @return EdgeID of newly created edge, or -1 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	int32 AddEdge(int32 FromNodeID, int32 ToNodeID, float Height, float Thickness, UWallPattern* Pattern = nullptr);

	/**
	 * Remove an edge from the graph
	 * Also removes orphaned nodes (nodes with no connected edges)
	 * @param EdgeID Edge to remove
	 * @return True if removed successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool RemoveEdge(int32 EdgeID);

	/**
	 * Remove a node from the graph
	 * Also removes all connected edges
	 * @param NodeID Node to remove
	 * @return True if removed successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool RemoveNode(int32 NodeID);

	/**
	 * Clear all graph data
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	void ClearGraph();

	// ========================================
	// Query Methods
	// ========================================

	/**
	 * Check if a wall exists between two world positions
	 * @param PosA First position
	 * @param PosB Second position
	 * @param Level Floor level
	 * @return True if a wall edge connects these positions
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool IsWallBetweenPositions(const FVector& PosA, const FVector& PosB, int32 Level) const;

	/**
	 * Check if a wall exists between two grid coordinates
	 * @param RowA First tile row
	 * @param ColA First tile column
	 * @param RowB Second tile row
	 * @param ColB Second tile column
	 * @param Level Floor level
	 * @return True if a wall exists between these tiles
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool IsWallBetweenTiles(int32 RowA, int32 ColA, int32 RowB, int32 ColB, int32 Level) const;

	/**
	 * Get all edges connected to a node
	 * @param NodeID Node to query
	 * @return Array of edge IDs connected to this node
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	TArray<int32> GetEdgesAtNode(int32 NodeID) const;

	/**
	 * Get all edges passing through a grid tile
	 * @param Row Grid row
	 * @param Column Grid column
	 * @param Level Floor level
	 * @return Array of edge IDs in this tile
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	TArray<int32> GetEdgesInTile(int32 Row, int32 Column, int32 Level) const;

	/**
	 * Find an edge connecting two specific nodes
	 * @param NodeA First node ID
	 * @param NodeB Second node ID
	 * @return Edge ID if found, -1 otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	int32 FindEdgeBetweenNodes(int32 NodeA, int32 NodeB) const;

	/**
	 * Check if a path exists between two nodes (excluding a specific edge)
	 * Used to detect if adding an edge would close a loop
	 * @param StartNodeID Starting node
	 * @param EndNodeID Target node
	 * @param ExcludeEdgeID Edge to exclude from traversal (the newly added edge)
	 * @return True if a path exists between the nodes without using the excluded edge
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool DoesPathExistBetweenNodes(int32 StartNodeID, int32 EndNodeID, int32 ExcludeEdgeID) const;

	/**
	 * Get all edges bounding a specific room
	 * @param RoomID Room to query
	 * @return Array of edge IDs that have this room as Room1 or Room2
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	TArray<int32> GetEdgesBoundingRoom(int32 RoomID) const;

	/**
	 * Get all nodes at a specific level
	 * @param Level Floor level
	 * @return Array of node IDs at this level
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	TArray<int32> GetNodesAtLevel(int32 Level) const;

	/**
	 * Get all edges at a specific level
	 * @param Level Floor level
	 * @return Array of edge IDs at this level
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	TArray<int32> GetEdgesAtLevel(int32 Level) const;

	// ========================================
	// Wall Runs (Collinear Edge Grouping)
	// ========================================

	/**
	 * Get the collinear wall run containing the given edge.
	 * Walks through degree-2 nodes in both directions collecting edges that share
	 * the same direction (within a tight dot-product tolerance).
	 * Results are cached and invalidated when edges are added/removed.
	 * @param EdgeID Any edge in the run
	 * @return The wall run containing this edge (may be a single-edge run)
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	FWallRun GetWallRun(int32 EdgeID);

	/**
	 * Convert a run-space parameter (0..1 across the full run) to a specific edge and local t.
	 * @param Run The wall run
	 * @param RunT 0..1 parameter along the full run
	 * @return Edge ID and local t in FromNode->ToNode space
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	FWallRunEdgeMapping RunTToEdge(const FWallRun& Run, float RunT) const;

	/**
	 * Convert an edge-local parameter to run-space parameter (0..1 across full run).
	 * @param Run The wall run
	 * @param EdgeID The edge within the run
	 * @param LocalT 0..1 parameter along the edge (FromNode->ToNode)
	 * @return 0..1 parameter along the full run
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	float EdgeTToRunT(const FWallRun& Run, int32 EdgeID, float LocalT) const;

	/**
	 * Invalidate the wall run cache (called when edges are added/removed).
	 */
	void InvalidateWallRunCache();

	// ========================================
	// Portal Validation (Junction & Bounds Checking)
	// ========================================

	/**
	 * Find the wall edge ID from a WallComponent array index
	 * Uses the EdgeID stored in FWallSegmentData.WallEdgeID
	 * @param WallComponent The wall component to query
	 * @param WallArrayIndex Index into WallDataArray
	 * @return EdgeID if found, -1 otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	int32 GetEdgeIDFromWallIndex(const class UWallComponent* WallComponent, int32 WallArrayIndex) const;

	/**
	 * Check if a position along a wall edge is near a junction node (3+ walls connected)
	 * Used to prevent portal placement at T-junctions and corners
	 * @param EdgeID The wall edge
	 * @param WorldPosition Position to check
	 * @param JunctionThreshold Distance from node to be considered "at junction" (default 50cm)
	 * @return True if position is near a junction node
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool IsPositionNearJunction(int32 EdgeID, const FVector& WorldPosition, float JunctionThreshold = 50.0f) const;

	/**
	 * Check if a portal box is entirely within the wall segment bounds
	 * Prevents portals from extending past wall endpoints or overhanging edges
	 * @param EdgeID The wall edge
	 * @param PortalCenter Portal center position (world space)
	 * @param PortalExtent Portal half-extents (local space)
	 * @param PortalRotation Portal rotation quaternion
	 * @return True if portal is entirely within wall bounds
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool IsPortalWithinWallBounds(int32 EdgeID, const FVector& PortalCenter, const FVector& PortalExtent, const FQuat& PortalRotation) const;

	// ========================================
	// Intersection Management
	// ========================================

	/**
	 * Rebuild all intersection data for proper corner mitring
	 * Should be called after adding/removing walls
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	void RebuildIntersections();

	/**
	 * Get intersection data at a specific node
	 * @param NodeID Node to query
	 * @param OutIntersection The intersection data if found
	 * @return True if intersection was found, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool GetIntersectionAtNode(int32 NodeID, FWallIntersection& OutIntersection) const;

	/**
	 * Calculate wall extents at an intersection for proper mitring
	 * @param NodeID Node at the intersection
	 */
	void CalculateIntersectionExtents(int32 NodeID);

	// ========================================
	// Room Assignment
	// ========================================

	/**
	 * Efficiently assign a room ID to boundary edges using pre-traced boundary
	 * This is O(N) where N = boundary edge count, much faster than tile-based assignment
	 * @param RoomID The room ID to assign
	 * @param BoundaryEdges Array of edge IDs that form the room boundary (from TraceRoomBoundary)
	 * @param bIsRoom1Side True if this room is on the Room1 side of walls, false for Room2 side
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	void AssignRoomToBoundaryEdges(int32 RoomID, const TArray<int32>& BoundaryEdges, const FVector& RoomCentroid);

	/**
	 * DEPRECATED: Assign room IDs to walls based on which tiles they separate
	 * This function is O(N^2 * M) and very slow for large rooms.
	 * Use AssignRoomToBoundaryEdges instead.
	 * @param RoomID The room ID to assign
	 * @param TileCoords Grid coordinates of all tiles in the room (Row, Column, Level)
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph", meta = (DeprecatedFunction, DeprecationMessage = "Use AssignRoomToBoundaryEdges for much better performance"))
	void AssignRoomIDsToWalls(int32 RoomID, const TArray<FIntVector>& TileCoords);

	/**
	 * Auto-assign room IDs to a newly placed wall by sampling tiles on both sides
	 * Used when walls are placed inside existing rooms (not forming new boundaries)
	 * Queries RoomManager to find which rooms are on +normal and -normal sides
	 * @param EdgeID The newly created wall edge ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	void AssignRoomIDsToNewWall(int32 EdgeID);

	/**
	 * Assign a room ID to a wall edge using geometric half-plane test
	 * Used for walls that weren't assigned during room detection (e.g., roof gable walls)
	 * Determines which side (Room1 or Room2) the room centroid is on and assigns accordingly
	 * @param EdgeID The wall edge ID to assign
	 * @param RoomID The room ID to assign
	 * @param RoomCentroid The centroid position of the room (for half-plane test)
	 * @return True if room was assigned to at least one side
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall Graph")
	bool AssignRoomToWallByGeometry(int32 EdgeID, int32 RoomID, const FVector& RoomCentroid);

	/**
	 * Determine which side of a wall (Room1 or Room2) a tile is on
	 * Uses 2D cross product to determine sidedness
	 * @param Edge The wall edge
	 * @param TileRow Grid row of tile
	 * @param TileColumn Grid column of tile
	 * @param OutIsRoom1Side True if tile is on Room1 side, false if Room2 side
	 * @return True if tile is adjacent to this wall
	 */
	bool GetWallSideForTile(const FWallEdge& Edge, int32 TileRow, int32 TileColumn, bool& OutIsRoom1Side) const;

	/**
	 * Get all tiles that a wall edge passes through using Bresenham's line algorithm
	 * This is the authoritative list of tiles affected by a wall edge
	 * @param Edge The wall edge
	 * @return Array of tile coordinates (Row, Column, Level) the edge passes through
	 */
	TArray<FIntVector> GetTilesAlongEdge(const FWallEdge& Edge) const;

	// ========================================
	// Spatial Indexing (Internal)
	// ========================================

private:
	/**
	 * Get all adjacent edges at a node (excluding the current edge)
	 * Works for any node type: simple corners (2 edges), T-junctions (3 edges), cross junctions (4+ edges)
	 * @param CurrentEdgeID The current edge to exclude
	 * @param NodeID The node where we want to find adjacent edges
	 * @return Array of adjacent edge IDs (empty if node not found or only has 1 edge)
	 */
	TArray<int32> GetAllAdjacentEdgesAtNode(int32 CurrentEdgeID, int32 NodeID) const;

	/**
	 * Check if a portal corner can span onto an adjacent wall at a corner
	 * Projects the corner onto the adjacent wall and validates it's within bounds
	 * @param CornerPosition World position of portal corner
	 * @param AdjacentEdgeID The adjacent wall edge ID
	 * @return True if corner falls within adjacent wall bounds
	 */
	bool CanCornerSpanToAdjacentWall(const FVector& CornerPosition, int32 AdjacentEdgeID) const;

	/**
	 * Add an edge to the spatial index
	 * Calculates which tiles the edge passes through using line rasterization
	 * @param EdgeID Edge to index
	 */
	void AddToSpatialIndex(int32 EdgeID);

	/**
	 * Remove an edge from the spatial index
	 * @param EdgeID Edge to remove
	 */
	void RemoveFromSpatialIndex(int32 EdgeID);

	/**
	 * Add a node to the spatial index
	 * @param NodeID Node to index
	 */
	void AddNodeToSpatialIndex(int32 NodeID);

	/**
	 * Remove a node from the spatial index
	 * @param NodeID Node to remove
	 */
	void RemoveNodeFromSpatialIndex(int32 NodeID);

	/**
	 * Generate spatial index key from grid coordinates
	 * @param Row Grid row
	 * @param Column Grid column
	 * @param Level Floor level
	 * @return Hash key for spatial index
	 */
	FORCEINLINE int64 GetSpatialKey(int32 Row, int32 Column, int32 Level) const
	{
		// Encode as: Level * 100000000 + Row * 10000 + Column
		return (static_cast<int64>(Level) * 100000000LL) + (static_cast<int64>(Row) * 10000LL) + static_cast<int64>(Column);
	}

	// Cached wall runs: EdgeID -> shared FWallRun
	// Invalidated when edges are added or removed
	TMap<int32, FWallRun> WallRunCache;

	/**
	 * Walk from a seed edge through one endpoint collecting collinear edges.
	 * @param SeedEdgeID The starting edge
	 * @param bWalkFromNode If true, walk from FromNode; if false, from ToNode
	 * @param RefDirection Normalized direction of the run
	 * @param OutEdgeIDs Collected edge IDs in walk order
	 */
	void WalkRunDirection(int32 SeedEdgeID, bool bWalkFromNode, const FVector& RefDirection, TArray<int32>& OutEdgeIDs) const;

	/**
	 * Check if an edge's FromNode->ToNode direction is aligned with the run direction.
	 * @return true if aligned, false if reversed
	 */
	bool IsEdgeAlignedWithRun(const FWallRun& Run, int32 EdgeID) const;

	/**
	 * Check if a diagonal wall segment separates two tiles geometrically
	 * Uses 2D cross product to determine if tiles are on opposite sides of the wall line
	 * @param Edge The diagonal wall edge
	 * @param RowA First tile row
	 * @param ColA First tile column
	 * @param RowB Second tile row
	 * @param ColB Second tile column
	 * @return True if the wall line passes between and separates the two tiles
	 */
	bool DoesDiagonalWallSeparateTiles(const FWallEdge& Edge, int32 RowA, int32 ColA, int32 RowB, int32 ColB) const;

	// ========================================
	// Debug Visualization
	// ========================================

public:
#if WITH_EDITOR
	/**
	 * Draw debug visualization of the wall graph in the editor viewport
	 * Shows nodes as spheres and edges as lines with room ID colors
	 */
	void DrawWallGraphDebug(FPrimitiveDrawInterface* PDI);

	/**
	 * Draw debug labels for nodes and edges
	 */
	void DrawWallGraphLabels(FPrimitiveDrawInterface* PDI);
#endif

	// ========================================
	// Statistics
	// ========================================

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Wall Graph")
	int32 GetNodeCount() const { return Nodes.Num(); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Wall Graph")
	int32 GetEdgeCount() const { return Edges.Num(); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Wall Graph")
	int32 GetIntersectionCount() const { return Intersections.Num(); }
};

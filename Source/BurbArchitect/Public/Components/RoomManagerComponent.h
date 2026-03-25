// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/TileTriangleData.h"
#include "RoomManagerComponent.generated.h"

class UWallGraphComponent;
class ALotManager;

// Delegate for room change events
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnRoomsChanged, const TArray<int32>&, const TArray<int32>&, const TArray<int32>&);
// Parameters: AddedRoomIDs, ModifiedRoomIDs, RemovedRoomIDs

/**
 * Cached representation of a detected room as a polygon
 * Supports arbitrary polygon shapes including diagonal walls
 */
USTRUCT(BlueprintType)
struct BURBARCHITECT_API FRoomData
{
	GENERATED_BODY()

	// Unique room identifier
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	int32 RoomID = 0;

	// Ordered wall edge IDs forming the closed polygon boundary
	// Edges are in clockwise order when viewed from above
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	TArray<int32> BoundaryEdges;

	// Ordered vertices of the room polygon (world positions)
	// Derived from wall graph nodes, used for rendering and spatial queries
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	TArray<FVector> BoundaryVertices;

	// PRIMARY: All triangles inside this room (triangle-first detection)
	// Each triangle is independently tested against the room polygon
	// This is the authoritative source for room interior
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	TArray<FTriangleCoord> InteriorTriangles;

	// DERIVED: Unique tile coordinates that have at least one triangle in this room
	// Computed from InteriorTriangles for backwards compatibility
	// Call RebuildInteriorTilesFromTriangles() to populate
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	TArray<FIntVector> InteriorTiles;

	/**
	 * Rebuild InteriorTiles from InteriorTriangles
	 * Called automatically after triangle detection, or manually if needed
	 */
	void RebuildInteriorTilesFromTriangles()
	{
		TSet<FIntVector> UniqueTiles;
		for (const FTriangleCoord& Tri : InteriorTriangles)
		{
			UniqueTiles.Add(Tri.GetTileCoord());
		}
		InteriorTiles = UniqueTiles.Array();
	}

	/**
	 * Get the number of triangles in a specific tile that belong to this room
	 * @param TileCoord The tile to query
	 * @return Number of triangles (0-4) in this tile belonging to this room
	 */
	int32 GetTriangleCountInTile(const FIntVector& TileCoord) const
	{
		int32 Count = 0;
		for (const FTriangleCoord& Tri : InteriorTriangles)
		{
			if (Tri.GetTileCoord() == TileCoord)
			{
				Count++;
			}
		}
		return Count;
	}

	// Geometric center of the polygon (area-weighted)
	// Always used as the rotation origin for room manipulation
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	FVector Centroid = FVector::ZeroVector;

	// Floor level this room exists on
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	int32 Level = 0;

	// If true, room data needs recalculation due to wall changes
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	bool bDirty = false;

	// If true, this room was created by a roof and should not generate ceilings/above-level grids
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	bool bIsRoofRoom = false;

	// ========== NEW: Hierarchical Room Support ==========

	// Parent room ID for nested rooms (0 = top-level room)
	UPROPERTY(BlueprintReadWrite, Category = "Room Hierarchy")
	int32 ParentRoomID = 0;

	// Child room IDs for rooms contained within this one
	UPROPERTY(BlueprintReadWrite, Category = "Room Hierarchy")
	TArray<int32> ChildRoomIDs;

	// ========== NEW: Generation & Change Tracking ==========

	// Generation number when this room was last detected/modified
	// Used to determine if room data is stale
	UPROPERTY(BlueprintReadWrite, Category = "Room Tracking")
	int32 GenerationNumber = 0;

	// If false, room has been invalidated (split/merged/destroyed)
	// Kept for history/undo purposes
	UPROPERTY(BlueprintReadWrite, Category = "Room Tracking")
	bool bIsValidRoom = true;

	// If room was subdivided, these are the resulting room IDs
	UPROPERTY(BlueprintReadWrite, Category = "Room Tracking")
	TArray<int32> SplitIntoRoomIDs;

	// If room was merged into another, this is the target room ID
	UPROPERTY(BlueprintReadWrite, Category = "Room Tracking")
	int32 MergedIntoRoomID = 0;

	// Timestamp of last modification (for debugging/history)
	UPROPERTY(BlueprintReadWrite, Category = "Room Tracking")
	FDateTime LastModified;

	// Calculate the area of this polygon room
	float GetArea() const;

	// Check if room polygon is convex (all interior angles < 180°)
	bool IsConvex() const;

	// Test if a world point is inside this room's polygon
	bool ContainsPoint(const FVector& Point) const;

	// Check if this room data is valid and not invalidated
	bool IsValid() const
	{
		return bIsValidRoom && RoomID > 0 && BoundaryEdges.Num() >= 3 && BoundaryVertices.Num() >= 3;
	}

	// Check if this room has been subdivided into other rooms
	bool WasSubdivided() const
	{
		return !bIsValidRoom && SplitIntoRoomIDs.Num() > 0;
	}

	// Check if this room was merged into another
	bool WasMerged() const
	{
		return !bIsValidRoom && MergedIntoRoomID > 0;
	}
};

/**
 * Room Manager Component
 * Handles room detection, caching, and high-level room operations
 *
 * Responsibilities:
 * - Detect rooms from wall graph as closed polygons
 * - Cache room data for efficient queries
 * - Provide room selection and manipulation
 * - Manage room-level operations (resize, move, rotate)
 *
 * Works with WallGraphComponent for low-level wall data
 */
UCLASS(ClassGroup=(BurbArchitect), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API URoomManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoomManagerComponent();

protected:
	virtual void BeginPlay() override;

public:
	// ========================================
	// Events
	// ========================================

	// Event broadcast when rooms are added, modified, or removed
	FOnRoomsChanged OnRoomsChanged;

	// ========================================
	// Room Cache
	// ========================================

	// Cached rooms indexed by RoomID
	UPROPERTY()
	TMap<int32, FRoomData> Rooms;

	// Next available room ID (only used when AvailableRoomIDs is empty)
	UPROPERTY()
	int32 NextRoomID = 1;

	// Pool of recycled room IDs from invalidated rooms
	// These are reused before incrementing NextRoomID
	UPROPERTY()
	TArray<int32> AvailableRoomIDs;

	/**
	 * Get the next room ID to use
	 * Recycles IDs from invalidated rooms before incrementing NextRoomID
	 * @return A unique room ID to use for a new room
	 */
	int32 GetNextRoomID();

	/**
	 * Return a room ID to the available pool for recycling
	 * Called when a room is invalidated/removed
	 * @param RoomID The room ID to recycle
	 */
	void RecycleRoomID(int32 RoomID);

	// PRIMARY spatial cache: Triangle coordinate -> Room ID (for O(1) lookups)
	// Uses packed 64-bit key from FTriangleCoord::GetPackedKey()
	// This is the authoritative source for spatial room queries
	UPROPERTY()
	TMap<uint64, int32> TriangleToRoomMap;

	// DERIVED spatial cache: Tile coordinate -> Room ID (for backwards compatibility)
	// Returns the dominant room in the tile (room with most triangles)
	// Updated from TriangleToRoomMap whenever rooms are detected/invalidated
	UPROPERTY()
	TMap<FIntVector, int32> TileToRoomMap;

	// ========================================
	// NEW: Room Invalidation & Generation Tracking
	// ========================================

	// Current generation number (increments on each detection pass)
	UPROPERTY()
	int32 CurrentGeneration = 0;

	// Rooms that have been invalidated but kept for history
	UPROPERTY()
	TMap<int32, FRoomData> InvalidatedRooms;

	// Set of room IDs that need re-detection
	UPROPERTY()
	TSet<int32> DirtyRooms;

	// Set of tile coordinates that need room reassignment
	UPROPERTY()
	TSet<FIntVector> DirtyTiles;

	// Maps wall edge IDs to the rooms that use them
	// Not exposed to Blueprint due to TArray limitation
	TMultiMap<int32, int32> WallToRoomMap;

	// ========================================
	// NEW: Incremental Room Detection Methods
	// ========================================

	/**
	 * Called when walls are added or removed
	 * Triggers incremental room detection
	 * @param AddedWalls Array of newly added wall edge IDs
	 * @param RemovedWalls Array of removed wall edge IDs
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void OnWallsModified(const TArray<int32>& AddedWalls, const TArray<int32>& RemovedWalls);

	/**
	 * Invalidate all rooms in a given area
	 * Marks rooms as dirty for re-detection
	 * @param AffectedTiles Tile coordinates that were modified
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void InvalidateRoomsInArea(const TArray<FIntVector>& AffectedTiles);

	/**
	 * Invalidate rooms along a specific wall
	 * Marks Room1 and Room2 of the wall as dirty
	 * @param WallEdgeID Wall edge ID to invalidate around
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void InvalidateRoomsAlongWall(int32 WallEdgeID);

	/**
	 * Detect room changes after invalidation
	 * Processes dirty rooms to detect splits, merges, and modifications
	 */
	void DetectRoomChanges();

	/**
	 * Handle room subdivision when a wall is added through an existing room
	 * @param RoomID The room that may have been subdivided
	 */
	void HandleRoomSubdivision(int32 RoomID);

	/**
	 * Detect if rooms have been merged after wall removal
	 */
	void DetectRoomMergers();

	/**
	 * Create a new room from a boundary polygon
	 * @param BoundaryEdges Ordered edge IDs forming the room boundary
	 * @return New FRoomData structure
	 */
	FRoomData CreateRoomFromBoundary(const TArray<int32>& BoundaryEdges);

	/**
	 * Find all closed loops within an area
	 * Used for detecting subdivisions
	 * @param SearchArea Set of edge IDs to search within
	 * @return Array of closed loops (each is an array of edge IDs)
	 */
	TArray<TArray<int32>> FindAllClosedLoopsInArea(const TArray<int32>& SearchArea);

	/**
	 * Update wall-room assignments dynamically
	 * Sets Room1/Room2 on edges based on current room configuration
	 */
	void UpdateWallRoomAssignments();

	/**
	 * Assign triangles of tiles to a specific room
	 * Uses triangle centroids for accurate assignment with diagonal walls
	 * @param RoomID The room to assign triangles to
	 * @param TileCoords Tile coordinates to process
	 */
	void AssignTrianglesToRoom(int32 RoomID, const TArray<FIntVector>& TileCoords);

	/**
	 * Rebuild all spatial indices after room changes
	 */
	void RebuildSpatialIndices();

	// ========================================
	// Room Detection
	// ========================================

	/**
	 * Detect all rooms on a specific level
	 * Walks the wall graph to find closed polygon boundaries
	 * @param Level Floor level to detect rooms on
	 * @return Number of rooms detected
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	int32 DetectAllRooms(int32 Level);

	/**
	 * Incrementally detect room(s) formed by a newly added edge
	 * Much faster than DetectAllRooms - only processes the new edge
	 * @param EdgeID The newly added edge ID
	 * @return Number of new rooms detected (typically 0, 1, or 2)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	int32 DetectRoomFromNewEdge(int32 EdgeID);

	/**
	 * Incrementally detect room(s) formed by a newly added edge (with output array)
	 * Much faster than DetectAllRooms - only processes the new edge
	 * @param EdgeID The newly added edge ID
	 * @param OutDetectedRoomIDs Output array of newly detected room IDs
	 * @return Number of new rooms detected (typically 0, 1, or 2)
	 */
	int32 DetectRoomFromNewEdgeWithIDs(int32 EdgeID, TArray<int32>& OutDetectedRoomIDs);

	/**
	 * Detect a single room starting from a tile coordinate
	 * Uses wall graph to find room boundary, then flood-fills interior
	 * @param TileCoord Tile coordinate inside the room
	 * @return Room data if detected, invalid room data if no room found
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	FRoomData DetectRoomFromTile(const FIntVector& TileCoord);

	/**
	 * Detect a room starting from a specific wall edge
	 * Walks the boundary edges to form a closed polygon
	 * @param EdgeID Starting edge ID from wall graph
	 * @param bUseRoom1Side If true, detect room on Room1 side, else Room2 side
	 * @return Room data if detected, invalid room data if no closed loop found
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	FRoomData DetectRoomFromEdge(int32 EdgeID, bool bUseRoom1Side);

	/**
	 * Trace room boundary by walking wall graph edges
	 * Follows edges in clockwise order to form closed polygon
	 * @param StartEdgeID Edge to start tracing from
	 * @param bClockwise If true, trace clockwise, else counterclockwise
	 * @return Ordered array of boundary edges forming closed loop (empty if no loop found)
	 */
	TArray<int32> TraceRoomBoundary(int32 StartEdgeID, bool bClockwise = true);

	/**
	 * Convert boundary edges to ordered polygon vertices
	 * Extracts node positions from wall graph in boundary order
	 * @param BoundaryEdges Ordered edge IDs forming room boundary
	 * @return Ordered vertices defining polygon shape
	 */
	TArray<FVector> EdgesToVertices(const TArray<int32>& BoundaryEdges);

	/**
	 * Sort unordered boundary edges into traversal sequence
	 * Takes edges that form a closed loop but are in random order
	 * and sorts them so each edge connects to the next
	 * @param UnorderedEdges Unordered edge IDs that form a closed boundary
	 * @return Edges sorted into traversal order (empty if edges don't form valid loop)
	 */
	TArray<int32> SortBoundaryEdgesIntoSequence(const TArray<int32>& UnorderedEdges);

	/**
	 * TRIANGLE-FIRST: Find interior triangles within a polygon boundary
	 * Tests each triangle centroid independently - the fundamental unit is the triangle
	 * This is the primary detection method that supports diagonal walls correctly
	 * @param Level Floor level
	 * @param BoundaryEdges The edge IDs that form the room boundary
	 * @param BoundaryVertices Pre-computed polygon vertices (optimization)
	 * @param OutTriangles Filled with all triangles inside the room
	 * @return True if interior detection succeeded
	 */
	bool DetectInteriorTriangles(int32 Level, const TArray<int32>& BoundaryEdges, const TArray<FVector>& BoundaryVertices, TArray<FTriangleCoord>& OutTriangles);

	/**
	 * DEPRECATED: Find interior tiles within a polygon boundary
	 * Use DetectInteriorTriangles() instead for accurate diagonal wall support
	 * Kept for backwards compatibility - internally calls DetectInteriorTriangles
	 * @param StartTile Starting tile (unused, kept for API compatibility)
	 * @param Level Floor level
	 * @param BoundaryEdges The edge IDs that form the room boundary
	 * @param OutTiles Filled with all tiles inside the room (derived from triangles)
	 * @return True if interior detection succeeded
	 */
	bool FloodFillPolygonInterior(const FIntVector& StartTile, int32 Level, const TArray<int32>& BoundaryEdges, TArray<FIntVector>& OutTiles);

	// ========================================
	// Polygon Geometry
	// ========================================

	/**
	 * Calculate geometric center (centroid) of a polygon
	 * Uses area-weighted formula for accuracy
	 * @param Vertices Ordered polygon vertices
	 * @return Centroid position (always used for room rotation)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	static FVector CalculatePolygonCentroid(const TArray<FVector>& Vertices);

	/**
	 * Calculate area of a polygon
	 * Uses shoelace formula
	 * @param Vertices Ordered polygon vertices
	 * @return Area in square units (always positive)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	static float CalculatePolygonArea(const TArray<FVector>& Vertices);

	/**
	 * Calculate signed area of a polygon (for winding order detection)
	 * Positive = counter-clockwise, Negative = clockwise (when viewed from above, +Z)
	 * Used to detect exterior boundary traces
	 * @param Vertices Ordered polygon vertices
	 * @return Signed area (positive for CCW, negative for CW)
	 */
	static float CalculateSignedPolygonArea(const TArray<FVector>& Vertices);

	/**
	 * Check if a traced boundary is an exterior trace (wrapping around the outside of rooms)
	 * Exterior traces have the wrong winding order relative to their traversal direction
	 * @param BoundaryVertices The polygon vertices from EdgesToVertices()
	 * @param bClockwiseTrace True if traced clockwise (Room1 side)
	 * @return True if this is an exterior trace that should be rejected
	 */
	bool IsExteriorBoundaryTrace(const TArray<FVector>& BoundaryVertices, bool bClockwiseTrace) const;

	/**
	 * Calculate 2D bounding box of a polygon (ignoring Z)
	 * @param Vertices Ordered polygon vertices
	 * @param OutMinRow Minimum row coordinate
	 * @param OutMaxRow Maximum row coordinate
	 * @param OutMinCol Minimum column coordinate
	 * @param OutMaxCol Maximum column coordinate
	 */
	void CalculatePolygonBoundingBox(const TArray<FVector>& Vertices, int32& OutMinRow, int32& OutMaxRow, int32& OutMinCol, int32& OutMaxCol) const;

	/**
	 * Check if a polygon is valid (no self-intersection, minimum vertices)
	 * @param Vertices Polygon vertices to validate
	 * @return True if valid room polygon
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	static bool IsValidRoomPolygon(const TArray<FVector>& Vertices);

	/**
	 * Test if a point is inside a polygon
	 * Uses ray casting algorithm
	 * @param Point World position to test
	 * @param Vertices Polygon vertices
	 * @return True if point is inside polygon
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	static bool IsPointInPolygon(const FVector& Point, const TArray<FVector>& Vertices);

	// ========================================
	// Room Cache Management
	// ========================================

	/**
	 * Get the total number of rooms currently cached
	 * @return Number of rooms in the cache
	 */
	UFUNCTION(BlueprintPure, Category = "Room Manager")
	int32 GetRoomCount() const;

	/**
	 * Get room by ID
	 * @param RoomID Room identifier
	 * @param OutRoom Room data if found
	 * @return True if room was found
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	bool GetRoom(int32 RoomID, FRoomData& OutRoom);

	/**
	 * Get room ID for a specific triangle (primary lookup - O(1))
	 * @param TriCoord Triangle coordinate to query
	 * @return RoomID if triangle is in a room, 0 if outside or not found
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	int32 GetRoomAtTriangle(const FTriangleCoord& TriCoord);

	/**
	 * Get room ID for a specific triangle by components (primary lookup - O(1))
	 * @param Row Grid row
	 * @param Column Grid column
	 * @param Level Floor level
	 * @param Triangle Which triangle (Top, Right, Bottom, Left)
	 * @return RoomID if triangle is in a room, 0 if outside or not found
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	int32 GetRoomAtTriangleCoords(int32 Row, int32 Column, int32 Level, ETriangleType Triangle);

	/**
	 * Get all room IDs present in a specific tile
	 * @param TileCoord Tile coordinate to query
	 * @return Array of unique room IDs (may be empty, or contain 1-4 rooms for split tiles)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	TArray<int32> GetRoomsAtTile(const FIntVector& TileCoord);

	/**
	 * Get room ID for a specific tile (backwards-compatible lookup)
	 * Returns the dominant room (room with most triangles in this tile)
	 * @param TileCoord Tile coordinate to query
	 * @return RoomID if tile is in a room, 0 if outside or not found
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	int32 GetRoomAtTile(const FIntVector& TileCoord);

	/**
	 * Invalidate a specific room (mark for recalculation)
	 * @param RoomID Room to invalidate
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void InvalidateRoom(int32 RoomID);

	/**
	 * Invalidate all rooms (mark all for recalculation)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void InvalidateAllRooms();

	/**
	 * Rebuild a specific room from scratch
	 * @param RoomID Room to rebuild
	 * @return True if rebuild succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	bool RebuildRoom(int32 RoomID);

	/**
	 * Clear all cached rooms
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void ClearRoomCache();

	// ========================================
	// Room Selection (for future manipulation)
	// ========================================

	// Currently selected room ID (0 = none)
	UPROPERTY(BlueprintReadWrite, Category = "Room Manager")
	int32 SelectedRoomID = 0;

	/**
	 * Select a room by ID
	 * @param RoomID Room to select
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void SelectRoom(int32 RoomID);

	/**
	 * Deselect current room
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	void DeselectRoom();

	/**
	 * Get currently selected room
	 * @param OutRoom Selected room data if found
	 * @return True if a room is selected
	 */
	UFUNCTION(BlueprintCallable, Category = "Room Manager")
	bool GetSelectedRoom(FRoomData& OutRoom);

	// ========================================
	// Component References
	// ========================================

	// Reference to wall graph (populated on BeginPlay)
	UPROPERTY()
	UWallGraphComponent* WallGraph = nullptr;

	// Reference to lot manager (populated on BeginPlay)
	UPROPERTY()
	ALotManager* LotManager = nullptr;

private:
	// ========================================
	// Persistent Blocked Map Cache (for incremental detection)
	// ========================================

	// Cached blocked map per level: Level -> (Triangle -> Set of blocked neighbor triangles)
	TMap<int32, TMap<uint64, TSet<uint64>>> CachedBlockedMaps;

	// Cached wall-to-blocked tracking per level for incremental removal
	// Level -> (MinTriKey -> (MaxTriKey -> EdgeID))
	TMap<int32, TMap<uint64, TMap<uint64, int32>>> CachedBlockedToWall;

	// Track which levels have valid cached blocked maps
	TSet<int32> ValidBlockedMapLevels;

	/**
	 * Ensure the blocked map is cached for a level, building it if necessary
	 * @param Level Floor level to ensure cache for
	 */
	void EnsureBlockedMapCached(int32 Level);

	/**
	 * Invalidate the cached blocked map for a level (forces rebuild on next use)
	 * @param Level Floor level to invalidate
	 */
	void InvalidateBlockedMapCache(int32 Level);

	/**
	 * Incrementally add a single wall's blocked connections to the cached map
	 * Much faster than rebuilding the entire blocked map
	 * @param EdgeID The wall edge to add
	 * @param OutAffectedTriangles Triangles adjacent to the wall (for local detection)
	 */
	void IncrementalAddWallToBlockedMap(int32 EdgeID, TArray<FTriangleCoord>& OutAffectedTriangles);

	/**
	 * Incrementally remove a single wall's blocked connections from the cached map
	 * @param EdgeID The wall edge to remove
	 * @param OutAffectedTriangles Triangles that were adjacent to the wall
	 */
	void IncrementalRemoveWallFromBlockedMap(int32 EdgeID, TArray<FTriangleCoord>& OutAffectedTriangles);

	/**
	 * Detect rooms by flood filling only from specific affected triangles
	 * Much faster than DetectAllRooms when only a small area changed
	 * @param Level Floor level
	 * @param AffectedTriangles Triangles to start flood fill from
	 * @param OutDetectedRoomIDs Output array of detected room IDs
	 * @return Number of rooms detected
	 */
	int32 DetectRoomsFromAffectedTriangles(int32 Level, const TArray<FTriangleCoord>& AffectedTriangles, TArray<int32>& OutDetectedRoomIDs);

	/**
	 * Handle wall removal and detect room merges
	 * @param EdgeID The removed wall edge ID
	 * @param Level Floor level
	 * @param OutMergedRoomIDs Output array of room IDs that were merged/changed
	 * @return Number of rooms affected
	 */
	int32 HandleWallRemovalDetection(int32 EdgeID, int32 Level, TArray<int32>& OutMergedRoomIDs);

	// ========================================
	// Flood Fill Room Detection (New Algorithm)
	// ========================================

	/**
	 * Represents a blocked connection between two adjacent triangles due to a wall
	 * Stored as packed keys for efficient lookup
	 */
	struct FBlockedConnection
	{
		uint64 TriangleA;
		uint64 TriangleB;
		int32 WallEdgeID;  // The wall that blocks this connection
	};

	/**
	 * Build a map of blocked triangle connections from all walls at a level
	 * Each wall blocks specific triangle pairs from connecting during flood fill
	 * Uses a collision-free adjacency map structure
	 * @param Level Floor level to build map for
	 * @param OutBlockedMap Map from triangle key to set of triangles it cannot reach
	 * @param OutBlockedToWall Map from (TriA, TriB) pair to wall edge ID - uses min key as outer map key
	 */
	void BuildWallBlockedMap(int32 Level, TMap<uint64, TSet<uint64>>& OutBlockedMap, TMap<uint64, TMap<uint64, int32>>& OutBlockedToWall);

	/**
	 * Helper to add a blocked connection (bidirectional) to the blocked map
	 * @param BlockedMap The blocked connection map to update
	 * @param TriKeyA First triangle key
	 * @param TriKeyB Second triangle key
	 */
	static void AddBlockedConnection(TMap<uint64, TSet<uint64>>& BlockedMap, uint64 TriKeyA, uint64 TriKeyB);

	/**
	 * Helper to check if two triangles are blocked from each other
	 * @param BlockedMap The blocked connection map
	 * @param TriKeyA First triangle key
	 * @param TriKeyB Second triangle key
	 * @return True if the connection is blocked
	 */
	static bool IsConnectionBlocked(const TMap<uint64, TSet<uint64>>& BlockedMap, uint64 TriKeyA, uint64 TriKeyB);

	/**
	 * Get all neighboring triangles for a given triangle
	 * Returns up to 3 neighbors: 2 within same tile, 1 in adjacent tile
	 * @param Coord Triangle coordinate
	 * @param OutNeighbors Array to fill with neighbor coordinates
	 */
	void GetTriangleNeighbors(const FTriangleCoord& Coord, TArray<FTriangleCoord>& OutNeighbors);

	/**
	 * Flood fill from a starting triangle to find all connected triangles
	 * Walls act as barriers - blocked connections stop the fill
	 * @param Start Starting triangle coordinate
	 * @param BlockedMap Map of blocked connections (triangle -> set of blocked neighbors)
	 * @param Visited Set of already visited triangle keys (updated during fill)
	 * @param OutRegion Array to fill with all triangles in this region
	 */
	void FloodFillFromTriangle(const FTriangleCoord& Start, const TMap<uint64, TSet<uint64>>& BlockedMap,
	                           TSet<uint64>& Visited, TArray<FTriangleCoord>& OutRegion);

	/**
	 * Check if a region touches the grid boundary (exterior region)
	 * Exterior regions are not valid rooms
	 * @param Region Array of triangles in the region
	 * @return True if any triangle is on the grid boundary
	 */
	bool IsRegionExterior(const TArray<FTriangleCoord>& Region);

	/**
	 * Filter out nested rooms using area-based approach
	 * Keeps smallest rooms, discards larger rooms that fully contain smaller ones
	 * @param Regions Array of potential room regions (modified in place)
	 */
	void FilterNestedRegions(TArray<TArray<FTriangleCoord>>& Regions);

	/**
	 * Check if RegionA fully contains RegionB
	 * @param RegionA Larger region to test
	 * @param RegionB Smaller region to test containment of
	 * @return True if all triangles of RegionB are in RegionA
	 */
	bool DoesRegionContain(const TArray<FTriangleCoord>& RegionA, const TArray<FTriangleCoord>& RegionB);

	/**
	 * Derive boundary edges from a flood-filled region
	 * Finds walls that separate this region from other regions/exterior
	 * @param Region Array of triangles in the room
	 * @param BlockedToWall Nested map from (TriA -> TriB -> WallEdgeID) for wall lookup
	 * @param Visited Set of all visited triangles (to identify boundaries)
	 * @param OutBoundaryEdges Array to fill with boundary edge IDs
	 */
	void DeriveBoundaryFromRegion(const TArray<FTriangleCoord>& Region, const TMap<uint64, TMap<uint64, int32>>& BlockedToWall,
	                              const TSet<uint64>& AllVisited, TArray<int32>& OutBoundaryEdges);

	// ========================================
	// Face-Tracing Room Detection (ported from Godot RoomDetector)
	// ========================================

	/** Adjacency entry for angular-sorted neighbor list used in face tracing */
	struct FAdjacencyEntry
	{
		int32 NeighborNodeID;
		float Angle;
	};

	/** Result of tracing a single face of the planar wall graph */
	struct FTracedFace
	{
		TArray<int32> NodeIDs;     // Ordered node IDs forming the face boundary
		TArray<int32> EdgeIDs;     // Edge ID connecting NodeIDs[i] to NodeIDs[(i+1)%N]
		TArray<FVector> Vertices;  // World positions of the boundary nodes
		float SignedArea = 0.f;    // Positive = CCW (interior face), Negative = CW (exterior)
	};

	/**
	 * Build angular-sorted adjacency map from wall graph edges on a level.
	 * Prunes degree-1 dead-end nodes recursively (they can't form closed rooms).
	 * @param Level Floor level
	 * @return NodeID -> sorted array of neighbors ordered by angle
	 */
	TMap<int32, TArray<FAdjacencyEntry>> BuildFaceTracingAdjacency(int32 Level);

	/**
	 * Trace all faces of the planar wall graph using half-edge traversal.
	 * At each node, takes the previous neighbor in angular order to trace the smallest face.
	 * @param Adjacency Angular-sorted adjacency from BuildFaceTracingAdjacency
	 * @return All traced faces (both interior and exterior)
	 */
	TArray<FTracedFace> TraceAllGraphFaces(const TMap<int32, TArray<FAdjacencyEntry>>& Adjacency);

	/**
	 * Remove collinear intermediate vertices from a polygon.
	 * Critical for diagonal walls built segment-by-segment which create redundant vertices
	 * that cause point_in_polygon double-toggles.
	 * @param Polygon Input polygon vertices
	 * @return Simplified polygon (minimum 3 vertices, or original if simplification fails)
	 */
	static TArray<FVector> SimplifyRoomPolygon(const TArray<FVector>& Polygon);

	/**
	 * Robust point-in-polygon test that handles points exactly on polygon edges.
	 * First checks edge proximity (within epsilon), then falls back to ray casting.
	 * @param Point World position to test (2D, Z ignored)
	 * @param Polygon Polygon vertices
	 * @param bIncludeEdges If true, points on polygon edges count as inside (default). False for strict interior only.
	 * @return True if point is inside (or on boundary when bIncludeEdges is true)
	 */
	static bool IsPointInPolygonRobust(const FVector& Point, const TArray<FVector>& Polygon, bool bIncludeEdges = true);

	/**
	 * Check if a point lies on a line segment within epsilon tolerance (2D, Z ignored).
	 */
	static bool IsPointOnSegment2D(const FVector& Point, const FVector& A, const FVector& B, float Epsilon = 0.001f);

	/**
	 * Stamp Room1/Room2 on all wall edges for a level by sampling tiles
	 * perpendicular to each edge's midpoint. Replaces per-room boundary assignment
	 * with a single unified pass over all edges.
	 * @param Level Floor level to stamp
	 */
	void StampEdgeRoomIDs(int32 Level);

	// ========================================
	// Legacy Helper Functions (kept for compatibility)
	// ========================================

	/**
	 * Find the next edge in a boundary walk (LEGACY - used by TraceRoomBoundary)
	 */
	int32 FindNextBoundaryEdge(int32 CurrentNodeID, int32 PreviousEdgeID, bool bClockwise);

	/**
	 * Get the opposite node of an edge
	 */
	int32 GetOppositeNode(int32 EdgeID, int32 NodeID);

	/**
	 * Get tiles adjacent to an edge (one on each side)
	 */
	bool GetTilesAdjacentToEdge(int32 EdgeID, FIntVector& OutTile1, FIntVector& OutTile2);

	/**
	 * Check if two line segments intersect (2D, ignoring Z)
	 */
	static bool DoLineSegmentsIntersect(const FVector& A1, const FVector& A2, const FVector& B1, const FVector& B2);
};

// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/RoomManagerComponent.h"
#include "Components/WallGraphComponent.h"
#include "Components/FloorComponent.h" // For ETriangleType enum
#include "Actors/LotManager.h"
#include "BurbArchitectDebug.h"
#include "DrawDebugHelpers.h"
#include "Containers/Queue.h"

// Internal helper structure for pending room data during atomic detection
// Used to collect room data without modifying state until all rooms are validated
struct FPendingRoom
{
	TArray<int32> BoundaryEdges;
	TArray<FVector> BoundaryVertices;
	TArray<FTriangleCoord> InteriorTriangles;  // PRIMARY: triangle-first storage
	TArray<FIntVector> InteriorTiles;          // DERIVED: computed from triangles
	FVector Centroid = FVector::ZeroVector;
	int32 Level = 0;
	bool bUseRoom1Side = true;  // Which side of the wall this room is on
	TSet<int32> OverlappingRoomIDs;  // Existing rooms that overlap with this pending room

	bool IsValid() const
	{
		return BoundaryEdges.Num() >= 3 && BoundaryVertices.Num() >= 3 && InteriorTriangles.Num() > 0;
	}

	// Rebuild InteriorTiles from InteriorTriangles
	void RebuildInteriorTilesFromTriangles()
	{
		TSet<FIntVector> UniqueTiles;
		for (const FTriangleCoord& Tri : InteriorTriangles)
		{
			UniqueTiles.Add(Tri.GetTileCoord());
		}
		InteriorTiles = UniqueTiles.Array();
	}
};

// Helper: Determine which side of a 2D line a point is on
// Returns > 0 for left side, < 0 for right side, 0 if on line
static float GetSideOfLine2D(const FVector& LineStart, const FVector& LineEnd, const FVector& Point)
{
	return (LineEnd.X - LineStart.X) * (Point.Y - LineStart.Y) - (LineEnd.Y - LineStart.Y) * (Point.X - LineStart.X);
}

// Helper: Check if a point is on the same side of a diagonal as a reference point (the room centroid)
// Used for tiles containing diagonal walls where IsPointInPolygon may be ambiguous
static bool IsPointOnSameSideOfDiagonal(const FVector& DiagStart, const FVector& DiagEnd,
                                         const FVector& TestPoint, const FVector& ReferencePoint)
{
	float TestSide = GetSideOfLine2D(DiagStart, DiagEnd, TestPoint);
	float RefSide = GetSideOfLine2D(DiagStart, DiagEnd, ReferencePoint);

	// Both on same side if signs match (both positive or both negative)
	// Also return true if either is exactly on the line (zero) for tolerance
	if (FMath::Abs(TestSide) < KINDA_SMALL_NUMBER || FMath::Abs(RefSide) < KINDA_SMALL_NUMBER)
		return true;

	return (TestSide > 0.0f) == (RefSide > 0.0f);
}

URoomManagerComponent::URoomManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URoomManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Get references to sibling components
	LotManager = Cast<ALotManager>(GetOwner());
	if (LotManager)
	{
		WallGraph = LotManager->WallGraph;
	}

	if (!WallGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("RoomManager: WallGraph component not found!"));
	}
}

// ========================================
// FRoomData Methods
// ========================================

float FRoomData::GetArea() const
{
	return URoomManagerComponent::CalculatePolygonArea(BoundaryVertices);
}

bool FRoomData::IsConvex() const
{
	if (BoundaryVertices.Num() < 3)
		return false;

	// Check if all cross products have same sign (convex)
	bool bHasPositive = false;
	bool bHasNegative = false;

	for (int32 i = 0; i < BoundaryVertices.Num(); i++)
	{
		const FVector& V1 = BoundaryVertices[i];
		const FVector& V2 = BoundaryVertices[(i + 1) % BoundaryVertices.Num()];
		const FVector& V3 = BoundaryVertices[(i + 2) % BoundaryVertices.Num()];

		FVector Edge1 = V2 - V1;
		FVector Edge2 = V3 - V2;
		float CrossZ = FVector::CrossProduct(Edge1, Edge2).Z;

		if (CrossZ > 0)
			bHasPositive = true;
		else if (CrossZ < 0)
			bHasNegative = true;

		if (bHasPositive && bHasNegative)
			return false; // Mixed signs = concave
	}

	return true;
}

bool FRoomData::ContainsPoint(const FVector& Point) const
{
	return URoomManagerComponent::IsPointInPolygon(Point, BoundaryVertices);
}

// ========================================
// Room Detection
// ========================================

int32 URoomManagerComponent::DetectAllRooms(int32 Level)
{
	if (!WallGraph || !LotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("RoomManager: Missing WallGraph or LotManager"));
		return 0;
	}

	CurrentGeneration++;

	UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectAllRooms - Starting face-tracing detection for level %d (generation %d)"), Level, CurrentGeneration);

	// Invalidate blocked map cache for this level (no longer primary algorithm)
	InvalidateBlockedMapCache(Level);

	// Remove existing rooms on this level
	TArray<int32> RoomsToRemove;
	for (const auto& Pair : Rooms)
	{
		if (Pair.Value.Level == Level)
		{
			RoomsToRemove.Add(Pair.Key);
		}
	}

	for (int32 RoomID : RoomsToRemove)
	{
		FRoomData* Room = Rooms.Find(RoomID);
		if (Room)
		{
			for (const FTriangleCoord& Tri : Room->InteriorTriangles)
			{
				TriangleToRoomMap.Remove(Tri.GetPackedKey());

				FIntVector TileCoord = Tri.GetTileCoord();
				int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
				if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
				{
					LotManager->GridData[*TileIndexPtr].TriangleOwnership.SetTriangleRoom(Tri.Triangle, 0);
				}
			}
			for (const FIntVector& TileCoord : Room->InteriorTiles)
			{
				TileToRoomMap.Remove(TileCoord);
			}
		}

		Rooms.Remove(RoomID);
		RecycleRoomID(RoomID);
	}

	// ========================================
	// FACE-TRACING ROOM DETECTION
	// ========================================

	// Step 1: Build angular-sorted adjacency with degree-1 pruning
	TMap<int32, TArray<FAdjacencyEntry>> Adjacency = BuildFaceTracingAdjacency(Level);

	if (Adjacency.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectAllRooms - No valid adjacency (no walls or only dead-ends)"));
		return 0;
	}

	// Step 2: Trace all faces of the planar graph
	TArray<FTracedFace> AllFaces = TraceAllGraphFaces(Adjacency);

	// Step 3: Filter to interior faces (positive signed area = CCW winding)
	TArray<FTracedFace> InteriorFaces;
	for (FTracedFace& Face : AllFaces)
	{
		if (Face.SignedArea > 0.0001f)
		{
			InteriorFaces.Add(MoveTemp(Face));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectAllRooms - Traced %d total faces, %d interior"), AllFaces.Num(), InteriorFaces.Num());

	// Step 4: Sort by area ascending (smallest first for nested room handling)
	InteriorFaces.Sort([](const FTracedFace& A, const FTracedFace& B) {
		return FMath::Abs(A.SignedArea) < FMath::Abs(B.SignedArea);
	});

	// Step 5: Create rooms from faces, assign tiles/triangles via point-in-polygon
	int32 GridSizeX = LotManager->GridSizeX;
	int32 GridSizeY = LotManager->GridSizeY;
	float TileSize = LotManager->GridTileSize;
	float HalfTile = TileSize * 0.5f;
	TSet<uint64> AssignedTriangles;  // Prevents double-assignment for nested rooms
	int32 RoomsDetected = 0;

	for (FTracedFace& Face : InteriorFaces)
	{
		// Simplify polygon (remove collinear intermediate vertices from diagonal walls)
		TArray<FVector> SimplifiedVerts = SimplifyRoomPolygon(Face.Vertices);

		// Compute bounding box for tile iteration
		int32 MinRow, MaxRow, MinCol, MaxCol;
		CalculatePolygonBoundingBox(SimplifiedVerts, MinRow, MaxRow, MinCol, MaxCol);
		MinRow = FMath::Max(0, MinRow - 1);
		MaxRow = FMath::Min(GridSizeX - 1, MaxRow + 1);
		MinCol = FMath::Max(0, MinCol - 1);
		MaxCol = FMath::Min(GridSizeY - 1, MaxCol + 1);

		// Find interior triangles via point-in-polygon
		TArray<FTriangleCoord> InteriorTriangles;
		for (int32 Row = MinRow; Row <= MaxRow; Row++)
		{
			for (int32 Col = MinCol; Col <= MaxCol; Col++)
			{
				FVector TileCenter;
				if (!LotManager->TileToGridLocation(Level, Row, Col, true, TileCenter))
					continue;

				FVector TopLeft = TileCenter + FVector(-HalfTile, HalfTile, 0.0f);
				FVector TopRight = TileCenter + FVector(HalfTile, HalfTile, 0.0f);
				FVector BottomRight = TileCenter + FVector(HalfTile, -HalfTile, 0.0f);
				FVector BottomLeft = TileCenter + FVector(-HalfTile, -HalfTile, 0.0f);

				TArray<FVector> TriangleCentroids = {
					(TopLeft + TopRight + TileCenter) / 3.0f,       // Top
					(TopRight + BottomRight + TileCenter) / 3.0f,   // Right
					(BottomRight + BottomLeft + TileCenter) / 3.0f, // Bottom
					(BottomLeft + TopLeft + TileCenter) / 3.0f      // Left
				};

				for (int32 TriIdx = 0; TriIdx < 4; TriIdx++)
				{
					FTriangleCoord TriCoord(Row, Col, Level, static_cast<ETriangleType>(TriIdx));
					uint64 TriKey = TriCoord.GetPackedKey();

					if (AssignedTriangles.Contains(TriKey))
						continue;

					if (IsPointInPolygonRobust(TriangleCentroids[TriIdx], SimplifiedVerts))
					{
						AssignedTriangles.Add(TriKey);
						InteriorTriangles.Add(TriCoord);
					}
				}
			}
		}

		// Skip faces with too few triangles (noise)
		if (InteriorTriangles.Num() < 4)
			continue;

		// Create room data
		int32 NewRoomID = GetNextRoomID();
		FRoomData NewRoom;
		NewRoom.RoomID = NewRoomID;
		NewRoom.Level = Level;
		NewRoom.bIsValidRoom = true;
		NewRoom.GenerationNumber = CurrentGeneration;
		NewRoom.InteriorTriangles = InteriorTriangles;
		NewRoom.RebuildInteriorTilesFromTriangles();

		// Sort boundary edges into traversal sequence
		NewRoom.BoundaryEdges = SortBoundaryEdgesIntoSequence(Face.EdgeIDs);
		if (NewRoom.BoundaryEdges.Num() < 3)
		{
			// Fallback: use face edge IDs directly
			NewRoom.BoundaryEdges = Face.EdgeIDs;
		}

		NewRoom.BoundaryVertices = SimplifiedVerts;
		NewRoom.Centroid = CalculatePolygonCentroid(SimplifiedVerts);

		// Populate spatial caches
		for (const FTriangleCoord& Tri : NewRoom.InteriorTriangles)
		{
			TriangleToRoomMap.Add(Tri.GetPackedKey(), NewRoomID);

			FIntVector TileCoord = Tri.GetTileCoord();
			int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
			if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
			{
				LotManager->GridData[*TileIndexPtr].TriangleOwnership.SetTriangleRoom(Tri.Triangle, NewRoomID);
			}
		}

		// Populate TileToRoomMap using tile center point-in-polygon (matches Godot approach)
		// This ensures every tile inside the room polygon gets assigned, even tiles
		// split by diagonal walls where triangle ownership is ambiguous.
		for (int32 Row = MinRow; Row <= MaxRow; Row++)
		{
			for (int32 Col = MinCol; Col <= MaxCol; Col++)
			{
				FIntVector TileCoord(Row, Col, Level);
				if (TileToRoomMap.Contains(TileCoord))
					continue;  // Already assigned to a smaller room (nesting)

				FVector TileCenter;
				if (!LotManager->TileToGridLocation(Level, Row, Col, true, TileCenter))
					continue;

				if (IsPointInPolygonRobust(TileCenter, SimplifiedVerts))
				{
					TileToRoomMap.Add(TileCoord, NewRoomID);
				}
			}
		}

		Rooms.Add(NewRoomID, NewRoom);
		RoomsDetected++;

		UE_LOG(LogTemp, Log, TEXT("RoomManager: Detected room %d with %d triangles, %d tiles, %d boundary edges (face-traced)"),
			NewRoomID, NewRoom.InteriorTriangles.Num(), NewRoom.InteriorTiles.Num(), NewRoom.BoundaryEdges.Num());
	}

	// Step 6: Stamp Room1/Room2 on all edges via perpendicular sampling
	StampEdgeRoomIDs(Level);

	UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectAllRooms - Detected %d rooms on level %d (face-tracing)"), RoomsDetected, Level);
	return RoomsDetected;
}

int32 URoomManagerComponent::DetectRoomFromNewEdge(int32 EdgeID)
{
	TArray<int32> DummyOutput;
	return DetectRoomFromNewEdgeWithIDs(EdgeID, DummyOutput);
}

int32 URoomManagerComponent::DetectRoomFromNewEdgeWithIDs(int32 EdgeID, TArray<int32>& OutDetectedRoomIDs)
{
	// ========================================
	// INCREMENTAL APPROACH: Only detect rooms affected by the new wall
	// Now delegates to full face-tracing re-detection on the affected level.
	// Face tracing is O(E) and fast enough for typical lot sizes.
	// ========================================

	if (!WallGraph || !LotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("RoomManager::DetectRoomFromNewEdge: Missing WallGraph or LotManager"));
		return 0;
	}

	const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
	if (!Edge)
	{
		UE_LOG(LogTemp, Error, TEXT("RoomManager::DetectRoomFromNewEdge: Edge %d not found"), EdgeID);
		return 0;
	}

	OutDetectedRoomIDs.Empty();
	int32 Level = Edge->Level;
	int32 GenBefore = CurrentGeneration;

	// Full re-detection on this level using face tracing
	int32 NumDetected = DetectAllRooms(Level);

	// Collect room IDs created in this detection pass
	for (const auto& Pair : Rooms)
	{
		if (Pair.Value.Level == Level && Pair.Value.GenerationNumber > GenBefore)
		{
			OutDetectedRoomIDs.Add(Pair.Key);
		}
	}

	return OutDetectedRoomIDs.Num();
}

FRoomData URoomManagerComponent::DetectRoomFromTile(const FIntVector& TileCoord)
{
	FRoomData InvalidRoom;
	InvalidRoom.RoomID = 0;

	if (!WallGraph || !LotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("RoomManager::DetectRoomFromTile: Missing WallGraph or LotManager"));
		return InvalidRoom;
	}

	int32 Row = TileCoord.X;
	int32 Col = TileCoord.Y;
	int32 Level = TileCoord.Z;

	UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectRoomFromTile: Starting detection for tile (%d,%d,%d)"),
		Row, Col, Level);

	// Get all edges that pass through or are indexed to this tile
	TArray<int32> EdgesAtTile = WallGraph->GetEdgesInTile(Row, Col, Level);

	UE_LOG(LogTemp, Log, TEXT("  Found %d edges at tile (%d,%d,%d)"), EdgesAtTile.Num(), Row, Col, Level);

	if (EdgesAtTile.Num() == 0)
	{
		// No edges at this tile - try neighboring tiles to find boundary edges
		// A tile can be inside a room even if no edges pass through it
		struct FNeighborCheck
		{
			int32 NeighborRow, NeighborCol;
			FString Direction;
		};

		TArray<FNeighborCheck> Neighbors = {
			{Row, Col + 1, TEXT("North")},
			{Row, Col - 1, TEXT("South")},
			{Row - 1, Col, TEXT("West")},
			{Row + 1, Col, TEXT("East")}
		};

		for (const FNeighborCheck& Neighbor : Neighbors)
		{
			TArray<int32> NeighborEdges = WallGraph->GetEdgesInTile(Neighbor.NeighborRow, Neighbor.NeighborCol, Level);

			for (int32 EdgeID : NeighborEdges)
			{
				// Check if there's a wall between this tile and neighbor
				if (WallGraph->IsWallBetweenTiles(Row, Col, Neighbor.NeighborRow, Neighbor.NeighborCol, Level))
				{
					UE_LOG(LogTemp, Log, TEXT("  Found boundary wall edge %d in %s neighbor"), EdgeID, *Neighbor.Direction);

					// Try to detect room from this boundary edge
					FRoomData Room = DetectRoomFromEdge(EdgeID, true);
					if (Room.IsValid())
					{
						// Check if tile is in room (interior or on boundary)
						bool bTileInRoom = Room.InteriorTiles.Contains(TileCoord);
						if (!bTileInRoom)
						{
							// Check if tile is on boundary
							for (int32 BoundaryEdgeID : Room.BoundaryEdges)
							{
								const FWallEdge* Edge = WallGraph->Edges.Find(BoundaryEdgeID);
								if (Edge)
								{
									const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
									const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
									if (FromNode && ToNode)
									{
										if ((FromNode->Row == TileCoord.X && FromNode->Column == TileCoord.Y && FromNode->Level == TileCoord.Z) ||
											(ToNode->Row == TileCoord.X && ToNode->Column == TileCoord.Y && ToNode->Level == TileCoord.Z))
										{
											bTileInRoom = true;
											break;
										}
									}
								}
							}
						}

						if (bTileInRoom)
						{
							UE_LOG(LogTemp, Log, TEXT("  SUCCESS: Found valid room from neighbor edge"));
							return Room;
						}
					}

					// Try the other side
					Room = DetectRoomFromEdge(EdgeID, false);
					if (Room.IsValid())
					{
						// Check if tile is in room (interior or on boundary)
						bool bTileInRoom = Room.InteriorTiles.Contains(TileCoord);
						if (!bTileInRoom)
						{
							// Check if tile is on boundary
							for (int32 BoundaryEdgeID : Room.BoundaryEdges)
							{
								const FWallEdge* Edge = WallGraph->Edges.Find(BoundaryEdgeID);
								if (Edge)
								{
									const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
									const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
									if (FromNode && ToNode)
									{
										if ((FromNode->Row == TileCoord.X && FromNode->Column == TileCoord.Y && FromNode->Level == TileCoord.Z) ||
											(ToNode->Row == TileCoord.X && ToNode->Column == TileCoord.Y && ToNode->Level == TileCoord.Z))
										{
											bTileInRoom = true;
											break;
										}
									}
								}
							}
						}

						if (bTileInRoom)
						{
							UE_LOG(LogTemp, Log, TEXT("  SUCCESS: Found valid room from neighbor edge (other side)"));
							return Room;
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("RoomManager::DetectRoomFromTile: No boundary walls found around tile"));
		return InvalidRoom;
	}

	// Try each edge found at this tile
	for (int32 EdgeID : EdgesAtTile)
	{
		UE_LOG(LogTemp, Log, TEXT("  Trying edge %d (Room1 side)"), EdgeID);

		// Try Room1 side
		FRoomData Room = DetectRoomFromEdge(EdgeID, true);
		if (Room.IsValid())
		{
			// Check if the detected room contains our starting tile (interior or on boundary)
			bool bTileInRoom = Room.InteriorTiles.Contains(TileCoord);

			// Also check if tile is on the room boundary by checking if any boundary edge touches this tile
			if (!bTileInRoom && WallGraph)
			{
				for (int32 BoundaryEdgeID : Room.BoundaryEdges)
				{
					const FWallEdge* Edge = WallGraph->Edges.Find(BoundaryEdgeID);
					if (Edge)
					{
						const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
						const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
						if (FromNode && ToNode)
						{
							// Check if this edge touches our tile
							if ((FromNode->Row == TileCoord.X && FromNode->Column == TileCoord.Y && FromNode->Level == TileCoord.Z) ||
								(ToNode->Row == TileCoord.X && ToNode->Column == TileCoord.Y && ToNode->Level == TileCoord.Z))
							{
								bTileInRoom = true;
								break;
							}
						}
					}
				}
			}

			if (bTileInRoom)
			{
				UE_LOG(LogTemp, Log, TEXT("  SUCCESS: Found valid room containing the tile (Room1 side)"));
				return Room;
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("  Edge %d Room1 side: detected room but doesn't contain tile"), EdgeID);
			}
		}

		// Try Room2 side
		UE_LOG(LogTemp, Log, TEXT("  Trying edge %d (Room2 side)"), EdgeID);
		Room = DetectRoomFromEdge(EdgeID, false);
		if (Room.IsValid())
		{
			// Check if the detected room contains our starting tile (interior or on boundary)
			bool bTileInRoom = Room.InteriorTiles.Contains(TileCoord);

			// Also check if tile is on the room boundary
			if (!bTileInRoom && WallGraph)
			{
				for (int32 BoundaryEdgeID : Room.BoundaryEdges)
				{
					const FWallEdge* Edge = WallGraph->Edges.Find(BoundaryEdgeID);
					if (Edge)
					{
						const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
						const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
						if (FromNode && ToNode)
						{
							// Check if this edge touches our tile
							if ((FromNode->Row == TileCoord.X && FromNode->Column == TileCoord.Y && FromNode->Level == TileCoord.Z) ||
								(ToNode->Row == TileCoord.X && ToNode->Column == TileCoord.Y && ToNode->Level == TileCoord.Z))
							{
								bTileInRoom = true;
								break;
							}
						}
					}
				}
			}

			if (bTileInRoom)
			{
				UE_LOG(LogTemp, Log, TEXT("  SUCCESS: Found valid room containing the tile (Room2 side)"));
				return Room;
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("  Edge %d Room2 side: detected room but doesn't contain tile"), EdgeID);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("RoomManager::DetectRoomFromTile: Could not detect valid room for tile"));
	return InvalidRoom;
}

FRoomData URoomManagerComponent::DetectRoomFromEdge(int32 EdgeID, bool bUseRoom1Side)
{
	FRoomData Room;
	Room.RoomID = 0; // Invalid until proven valid

	if (!WallGraph || !LotManager)
		return Room;

	// Trace boundary edges using the specified side
	TArray<int32> BoundaryEdges = TraceRoomBoundary(EdgeID, bUseRoom1Side);

	if (BoundaryEdges.Num() < 3)
	{
		// Not a valid closed loop
		return Room;
	}

	// Convert edges to vertices
	TArray<FVector> Vertices = EdgesToVertices(BoundaryEdges);

	if (!IsValidRoomPolygon(Vertices))
	{
		UE_LOG(LogTemp, Warning, TEXT("RoomManager: Invalid room polygon detected"));
		return Room;
	}

	// Check for exterior trace (wrong winding order = traced around outside of rooms)
	if (IsExteriorBoundaryTrace(Vertices, bUseRoom1Side))
	{
		UE_LOG(LogTemp, Warning, TEXT("RoomManager: Exterior boundary trace detected - rejecting"));
		return Room;
	}

	// Find a tile inside the polygon for flood fill
	const FWallEdge* FirstEdge = WallGraph->Edges.Find(BoundaryEdges[0]);
	if (!FirstEdge)
		return Room;

	const FWallNode* FirstNode = WallGraph->Nodes.Find(FirstEdge->FromNodeID);
	if (!FirstNode)
		return Room;

	// Calculate centroid to find interior point
	FVector Centroid = CalculatePolygonCentroid(Vertices);
	int32 CentroidRow, CentroidCol;
	if (!LotManager->LocationToTile(Centroid, CentroidRow, CentroidCol))
	{
		UE_LOG(LogTemp, Warning, TEXT("RoomManager: Failed to convert centroid to tile"));
		return Room;
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager: Room boundary has %d vertices, centroid at world (%.1f, %.1f), tile (%d, %d)"),
		Vertices.Num(), Centroid.X, Centroid.Y, CentroidRow, CentroidCol);

	// TRIANGLE-FIRST: Detect interior triangles
	TArray<FTriangleCoord> InteriorTriangles;
	if (!DetectInteriorTriangles(FirstNode->Level, BoundaryEdges, Vertices, InteriorTriangles))
	{
		UE_LOG(LogTemp, Warning, TEXT("RoomManager: Triangle detection failed"));
		return Room;
	}

	// Derive InteriorTiles from triangles
	TSet<FIntVector> UniqueTiles;
	for (const FTriangleCoord& Tri : InteriorTriangles)
	{
		UniqueTiles.Add(Tri.GetTileCoord());
	}
	TArray<FIntVector> InteriorTiles = UniqueTiles.Array();

	UE_LOG(LogTemp, Log, TEXT("RoomManager: Found %d interior triangles, %d tiles"), InteriorTriangles.Num(), InteriorTiles.Num());

	// Successfully detected room - populate data
	Room.RoomID = GetNextRoomID();
	Room.BoundaryEdges = BoundaryEdges;
	Room.BoundaryVertices = Vertices;
	Room.InteriorTriangles = InteriorTriangles;  // PRIMARY
	Room.InteriorTiles = InteriorTiles;          // DERIVED
	Room.Centroid = Centroid;
	Room.Level = FirstNode->Level;
	Room.bDirty = false;

	// Check for overlapping rooms using triangle map
	TSet<int32> RoomsToInvalidate;
	int32 OverlappingTriangleCount = 0;

	for (const FTriangleCoord& Tri : InteriorTriangles)
	{
		const int32* ExistingRoomPtr = TriangleToRoomMap.Find(Tri.GetPackedKey());
		if (ExistingRoomPtr && *ExistingRoomPtr > 0)
		{
			RoomsToInvalidate.Add(*ExistingRoomPtr);
			OverlappingTriangleCount++;
		}
	}

	// SANITY CHECK: If this room would invalidate more than 1 existing room,
	// it's likely an exterior boundary trace (mega-room) and should be rejected.
	// Subdivision is handled separately via OnWallsModified, so this won't break that case.
	if (RoomsToInvalidate.Num() > 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoomManager: REJECTING room %d - would invalidate %d existing rooms (likely exterior boundary trace)"),
			Room.RoomID, RoomsToInvalidate.Num());

		// Recycle the room ID we allocated since we're not using it
		RecycleRoomID(Room.RoomID);

		// Return invalid room
		FRoomData InvalidRoom;
		InvalidRoom.RoomID = 0;
		return InvalidRoom;
	}

	// NESTED ROOM CHECK: If this room overlaps with exactly 1 existing room AND
	// ALL triangles overlap (room is entirely inside the other), AND the boundaries don't share edges,
	// then this is a nested room - carve it out instead of destroying the outer room.
	bool bIsNestedRoom = false;
	int32 ParentRoomID = 0;

	if (RoomsToInvalidate.Num() == 1 && OverlappingTriangleCount == InteriorTriangles.Num())
	{
		// All tiles overlap with a single existing room - potential nested room
		ParentRoomID = RoomsToInvalidate.Array()[0];
		FRoomData* ParentRoom = Rooms.Find(ParentRoomID);

		if (ParentRoom)
		{
			// Check if boundaries share any edges (if they do, it's subdivision, not nesting)
			bool bSharesBoundaryEdge = false;
			TSet<int32> ParentBoundarySet(ParentRoom->BoundaryEdges);

			for (int32 NewBoundaryEdge : BoundaryEdges)
			{
				if (ParentBoundarySet.Contains(NewBoundaryEdge))
				{
					bSharesBoundaryEdge = true;
					break;
				}
			}

			if (!bSharesBoundaryEdge)
			{
				// This is a nested room - walls don't connect to outer boundary
				bIsNestedRoom = true;

				UE_LOG(LogTemp, Warning, TEXT("RoomManager: NESTED ROOM detected - Room %d is inside Room %d (carving out %d triangles)"),
					Room.RoomID, ParentRoomID, InteriorTriangles.Num());

				// Carve out: Remove nested triangles from parent room
				for (const FTriangleCoord& Tri : InteriorTriangles)
				{
					ParentRoom->InteriorTriangles.Remove(Tri);
				}
				// Rebuild parent's InteriorTiles from remaining triangles
				ParentRoom->RebuildInteriorTilesFromTriangles();

				// Establish parent-child relationship
				Room.ParentRoomID = ParentRoomID;
				ParentRoom->ChildRoomIDs.Add(Room.RoomID);

				UE_LOG(LogTemp, Log, TEXT("  Parent Room %d now has %d triangles, Child Room %d has %d triangles"),
					ParentRoomID, ParentRoom->InteriorTriangles.Num(), Room.RoomID, InteriorTriangles.Num());
			}
		}
	}

	// Only invalidate existing rooms if NOT a nested room case
	if (!bIsNestedRoom)
	{
		for (int32 OldRoomID : RoomsToInvalidate)
		{
			FRoomData* OldRoom = Rooms.Find(OldRoomID);
			if (!OldRoom) continue;

			UE_LOG(LogTemp, Warning, TEXT("  INVALIDATING Room %d (overlaps with new Room %d)"), OldRoomID, Room.RoomID);

			// TRIANGLE-FIRST: Clear triangles from TriangleToRoomMap and GridData
			for (const FTriangleCoord& Tri : OldRoom->InteriorTriangles)
			{
				TriangleToRoomMap.Remove(Tri.GetPackedKey());

				if (LotManager)
				{
					FIntVector TileCoord = Tri.GetTileCoord();
					int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
					if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
					{
						FTileData& Tile = LotManager->GridData[*TileIndexPtr];
						if (Tile.TriangleOwnership.GetTriangleRoom(Tri.Triangle) == OldRoomID)
						{
							Tile.TriangleOwnership.SetTriangleRoom(Tri.Triangle, 0);
						}
					}
				}
			}

			// Remove from derived TileToRoomMap
			for (const FIntVector& OldTileCoord : OldRoom->InteriorTiles)
			{
				TileToRoomMap.Remove(OldTileCoord);
			}

			// Clear old room's Room1/Room2 assignments from boundary edges
			for (int32 OldBoundaryEdgeID : OldRoom->BoundaryEdges)
			{
				FWallEdge* OldBoundaryEdge = WallGraph->Edges.Find(OldBoundaryEdgeID);
				if (OldBoundaryEdge)
				{
					if (OldBoundaryEdge->Room1 == OldRoomID)
					{
						OldBoundaryEdge->Room1 = 0;
					}
					if (OldBoundaryEdge->Room2 == OldRoomID)
					{
						OldBoundaryEdge->Room2 = 0;
					}
				}
			}

			// Store in invalidated rooms for history and remove from active rooms
			OldRoom->bIsValidRoom = false;
			InvalidatedRooms.Add(OldRoomID, *OldRoom);
			Rooms.Remove(OldRoomID);
			RecycleRoomID(OldRoomID);

			UE_LOG(LogTemp, Warning, TEXT("  Room %d removed (replaced by Room %d)"), OldRoomID, Room.RoomID);
		}
	}

	// TRIANGLE-FIRST: Populate spatial caches from InteriorTriangles
	for (const FTriangleCoord& Tri : InteriorTriangles)
	{
		// Primary spatial cache: TriangleToRoomMap
		uint64 TriKey = Tri.GetPackedKey();
		TriangleToRoomMap.Remove(TriKey);
		TriangleToRoomMap.Add(TriKey, Room.RoomID);

		// Update GridData triangle ownership - always assign since triangle is inside this room's polygon
		if (LotManager)
		{
			FIntVector TileCoord = Tri.GetTileCoord();
			int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
			if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
			{
				FTileData& Tile = LotManager->GridData[*TileIndexPtr];
				Tile.TriangleOwnership.SetTriangleRoom(Tri.Triangle, Room.RoomID);
			}
		}
	}

	// DERIVED: Update TileToRoomMap for backwards compatibility
	for (const FIntVector& TileCoord : InteriorTiles)
	{
		int32 OwnedCount = 0;
		for (int32 TriIdx = 0; TriIdx < 4; TriIdx++)
		{
			FTriangleCoord TestTri(TileCoord, static_cast<ETriangleType>(TriIdx));
			const int32* OwnerPtr = TriangleToRoomMap.Find(TestTri.GetPackedKey());
			if (OwnerPtr && *OwnerPtr == Room.RoomID)
			{
				OwnedCount++;
			}
		}

		if (OwnedCount >= 2)
		{
			TileToRoomMap.Remove(TileCoord);
			TileToRoomMap.Add(TileCoord, Room.RoomID);
		}
	}

	// Assign room IDs to boundary walls using geometric centroid-based approach
	// This determines which side of each wall the room is on using half-plane tests
	// Direction-independent and always correct regardless of traversal direction
	if (WallGraph)
	{
		WallGraph->AssignRoomToBoundaryEdges(Room.RoomID, BoundaryEdges, Centroid);

		UE_LOG(LogTemp, Log, TEXT("RoomManager: Assigned RoomID %d to %d boundary edges using centroid at (%.1f, %.1f)"),
			Room.RoomID, BoundaryEdges.Num(), Centroid.X, Centroid.Y);
	}

	return Room;
}

TArray<int32> URoomManagerComponent::TraceRoomBoundary(int32 StartEdgeID, bool bClockwise)
{
	TArray<int32> BoundaryEdges;

	if (!WallGraph)
		return BoundaryEdges;

	const FWallEdge* StartEdge = WallGraph->Edges.Find(StartEdgeID);
	if (!StartEdge)
		return BoundaryEdges;

	// Start at FromNode and traverse toward ToNode
	int32 CurrentEdgeID = StartEdgeID;
	int32 CurrentNodeID = StartEdge->ToNodeID; // We'll arrive at ToNode after traversing this edge
	int32 PreviousNodeID = StartEdge->FromNodeID;
	TSet<int32> VisitedEdges;

	const int32 MaxIterations = 1000; // Safety limit
	int32 Iterations = 0;

	while (Iterations < MaxIterations)
	{
		// Add current edge to boundary
		BoundaryEdges.Add(CurrentEdgeID);
		VisitedEdges.Add(CurrentEdgeID);

		// DEBUG: Draw current edge being traced (GREEN)
		const FWallEdge* CurrentEdge = WallGraph->Edges.Find(CurrentEdgeID);
		if (CurrentEdge && GetWorld())
		{
			const FWallNode* FromNode = WallGraph->Nodes.Find(CurrentEdge->FromNodeID);
			const FWallNode* ToNode = WallGraph->Nodes.Find(CurrentEdge->ToNodeID);
			if (FromNode && ToNode)
			{
				FVector V1 = FromNode->Position;
				FVector V2 = ToNode->Position;
				V1.Z += 75.0f; // Different height than other debug
				V2.Z += 75.0f;
				if (BurbArchitectDebug::IsRoomDebugEnabled())
			{
				DrawDebugLine(GetWorld(), V1, V2, FColor::Green, false, 30.0f, 0, 8.0f);
			}
			}
		}

		// Find next edge at current node that isn't the edge we came from
		int32 NextEdgeID = FindNextBoundaryEdge(CurrentNodeID, CurrentEdgeID, bClockwise);

		if (NextEdgeID == -1)
		{
			// Dead end - no valid next edge
			UE_LOG(LogTemp, Warning, TEXT("RoomManager: Dead end at node %d (edge %d, iteration %d)"),
				CurrentNodeID, CurrentEdgeID, Iterations);
			break;
		}

		if (VisitedEdges.Contains(NextEdgeID))
		{
			// Check if we've completed the loop back to start
			if (NextEdgeID == StartEdgeID && BoundaryEdges.Num() >= 3)
			{
				// Successfully traced closed loop
				// DEBUG: Draw closing edge in YELLOW
				const FWallEdge* ClosingEdge = WallGraph->Edges.Find(NextEdgeID);
				if (ClosingEdge && GetWorld())
				{
					const FWallNode* FromNode = WallGraph->Nodes.Find(ClosingEdge->FromNodeID);
					const FWallNode* ToNode = WallGraph->Nodes.Find(ClosingEdge->ToNodeID);
					if (FromNode && ToNode)
					{
						FVector V1 = FromNode->Position;
						FVector V2 = ToNode->Position;
						V1.Z += 75.0f;
						V2.Z += 75.0f;
						if (BurbArchitectDebug::IsRoomDebugEnabled())
					{
						DrawDebugLine(GetWorld(), V1, V2, FColor::Yellow, false, 30.0f, 0, 10.0f);
					}
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("RoomManager: Traced closed boundary with %d edges (YELLOW line shows loop close)"), BoundaryEdges.Num());
				return BoundaryEdges;
			}
			else
			{
				// Already visited but not completing a valid loop
				UE_LOG(LogTemp, Warning, TEXT("RoomManager: Hit visited edge %d at iteration %d without valid loop"),
					NextEdgeID, Iterations);
				break;
			}
		}

		// Get the next edge to determine which node we're moving to
		const FWallEdge* NextEdge = WallGraph->Edges.Find(NextEdgeID);
		if (!NextEdge)
		{
			UE_LOG(LogTemp, Error, TEXT("RoomManager: Next edge %d not found"), NextEdgeID);
			break;
		}

		// Determine which node is the "next" node (the one we haven't visited yet on this edge)
		int32 NextNodeID;
		if (NextEdge->FromNodeID == CurrentNodeID)
		{
			NextNodeID = NextEdge->ToNodeID;
		}
		else if (NextEdge->ToNodeID == CurrentNodeID)
		{
			NextNodeID = NextEdge->FromNodeID;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("RoomManager: Edge %d doesn't connect to current node %d"), NextEdgeID, CurrentNodeID);
			break;
		}

		// Move to next edge and node
		PreviousNodeID = CurrentNodeID;
		CurrentNodeID = NextNodeID;
		CurrentEdgeID = NextEdgeID;
		Iterations++;
	}

	// Failed to find closed loop
	UE_LOG(LogTemp, Warning, TEXT("RoomManager: Failed to trace closed boundary (iterations: %d, edges: %d)"),
		Iterations, BoundaryEdges.Num());
	BoundaryEdges.Empty();
	return BoundaryEdges;
}

TArray<FVector> URoomManagerComponent::EdgesToVertices(const TArray<int32>& BoundaryEdges)
{
	TArray<FVector> Vertices;

	if (!WallGraph || BoundaryEdges.Num() == 0)
		return Vertices;

	// Reconstruct the traversal order by following edges in sequence
	// The first edge determines the starting node
	const FWallEdge* FirstEdge = WallGraph->Edges.Find(BoundaryEdges[0]);
	if (!FirstEdge)
		return Vertices;

	// Start at FromNode of the first edge
	int32 CurrentNodeID = FirstEdge->FromNodeID;
	int32 PreviousNodeID = -1;

	for (int32 EdgeID : BoundaryEdges)
	{
		const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
		if (!Edge)
			continue;

		// Add current node position
		const FWallNode* CurrentNode = WallGraph->Nodes.Find(CurrentNodeID);
		if (CurrentNode)
		{
			Vertices.Add(CurrentNode->Position);
		}

		// Determine which node is next (the opposite end of this edge)
		int32 NextNodeID;
		if (Edge->FromNodeID == CurrentNodeID)
		{
			NextNodeID = Edge->ToNodeID;
		}
		else if (Edge->ToNodeID == CurrentNodeID)
		{
			NextNodeID = Edge->FromNodeID;
		}
		else
		{
			// Edge doesn't connect to current node - this shouldn't happen in a valid boundary trace
			UE_LOG(LogTemp, Error, TEXT("RoomManager: EdgesToVertices - Edge %d doesn't connect to current node %d"), EdgeID, CurrentNodeID);
			break;
		}

		// Move to next node
		PreviousNodeID = CurrentNodeID;
		CurrentNodeID = NextNodeID;
	}

	return Vertices;
}

TArray<int32> URoomManagerComponent::SortBoundaryEdgesIntoSequence(const TArray<int32>& UnorderedEdges)
{
	TArray<int32> SortedEdges;

	if (!WallGraph || UnorderedEdges.Num() == 0)
		return SortedEdges;

	// Build a map of node connections: NodeID -> array of edges connected to that node
	TMap<int32, TArray<int32>> NodeToEdges;
	for (int32 EdgeID : UnorderedEdges)
	{
		const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
		if (!Edge)
			continue;

		NodeToEdges.FindOrAdd(Edge->FromNodeID).Add(EdgeID);
		NodeToEdges.FindOrAdd(Edge->ToNodeID).Add(EdgeID);
	}

	// Start with the first edge
	TSet<int32> UsedEdges;
	int32 CurrentEdgeID = UnorderedEdges[0];
	const FWallEdge* CurrentEdge = WallGraph->Edges.Find(CurrentEdgeID);
	if (!CurrentEdge)
		return SortedEdges;

	SortedEdges.Add(CurrentEdgeID);
	UsedEdges.Add(CurrentEdgeID);

	// Track current node (we'll move to ToNode of first edge)
	int32 CurrentNodeID = CurrentEdge->ToNodeID;

	// Keep finding the next connected edge until we've used all edges or can't continue
	while (SortedEdges.Num() < UnorderedEdges.Num())
	{
		// Find an unused edge connected to current node
		TArray<int32>* EdgesAtNode = NodeToEdges.Find(CurrentNodeID);
		if (!EdgesAtNode)
			break;

		int32 NextEdgeID = INDEX_NONE;
		for (int32 EdgeID : *EdgesAtNode)
		{
			if (!UsedEdges.Contains(EdgeID))
			{
				NextEdgeID = EdgeID;
				break;
			}
		}

		if (NextEdgeID == INDEX_NONE)
			break;

		// Add this edge to the sorted list
		SortedEdges.Add(NextEdgeID);
		UsedEdges.Add(NextEdgeID);

		// Move to the other end of this edge
		const FWallEdge* NextEdge = WallGraph->Edges.Find(NextEdgeID);
		if (NextEdge)
		{
			if (NextEdge->FromNodeID == CurrentNodeID)
				CurrentNodeID = NextEdge->ToNodeID;
			else
				CurrentNodeID = NextEdge->FromNodeID;
		}
		else
		{
			break;
		}
	}

	// Verify we used all edges (closed loop)
	if (SortedEdges.Num() != UnorderedEdges.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("RoomManager: SortBoundaryEdgesIntoSequence - Only sorted %d of %d edges (incomplete loop)"),
			SortedEdges.Num(), UnorderedEdges.Num());
	}

	return SortedEdges;
}

// ========================================
// Triangle-First Interior Detection
// ========================================

bool URoomManagerComponent::DetectInteriorTriangles(int32 Level, const TArray<int32>& BoundaryEdges, const TArray<FVector>& BoundaryVertices, TArray<FTriangleCoord>& OutTriangles)
{
	if (!WallGraph || !LotManager)
		return false;

	OutTriangles.Empty();

	if (BoundaryVertices.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("DetectInteriorTriangles: Invalid polygon (less than 3 vertices)"));
		return false;
	}

	// Calculate bounding box of the polygon
	int32 MinRow, MaxRow, MinCol, MaxCol;
	CalculatePolygonBoundingBox(BoundaryVertices, MinRow, MaxRow, MinCol, MaxCol);

	// Expand bounding box by 1 to ensure we catch all edge triangles
	MinRow = FMath::Max(0, MinRow - 1);
	MaxRow = FMath::Min(LotManager->GridSizeX - 1, MaxRow + 1);
	MinCol = FMath::Max(0, MinCol - 1);
	MaxCol = FMath::Min(LotManager->GridSizeY - 1, MaxCol + 1);

	UE_LOG(LogTemp, Log, TEXT("DetectInteriorTriangles: Testing triangles in bounding box (%d,%d) to (%d,%d) on level %d"),
		MinRow, MinCol, MaxRow, MaxCol, Level);

	const float TileSize = LotManager->GridTileSize;
	const float HalfTile = TileSize * 0.5f;

	int32 TrianglesFound = 0;

	// Iterate through all tiles in bounding box
	for (int32 Row = MinRow; Row <= MaxRow; Row++)
	{
		for (int32 Col = MinCol; Col <= MaxCol; Col++)
		{
			// Get tile center in world coordinates
			FVector TileCenter;
			if (!LotManager->TileToGridLocation(Level, Row, Col, true, TileCenter))
				continue;

			// Calculate the 4 corners of the tile (Top = +Y = +Row, matching LocationToAllTileCorners)
			FVector TopLeft = TileCenter + FVector(-HalfTile, HalfTile, 0.0f);
			FVector TopRight = TileCenter + FVector(HalfTile, HalfTile, 0.0f);
			FVector BottomRight = TileCenter + FVector(HalfTile, -HalfTile, 0.0f);
			FVector BottomLeft = TileCenter + FVector(-HalfTile, -HalfTile, 0.0f);

			// Calculate centroid of each of the 4 triangles
			// Triangle centroids are the definitive test points
			TArray<FVector> TriangleCentroids = {
				(TopLeft + TopRight + TileCenter) / 3.0f,      // Top (ETriangleType::Top = 0)
				(TopRight + BottomRight + TileCenter) / 3.0f,  // Right (ETriangleType::Right = 1)
				(BottomRight + BottomLeft + TileCenter) / 3.0f, // Bottom (ETriangleType::Bottom = 2)
				(BottomLeft + TopLeft + TileCenter) / 3.0f     // Left (ETriangleType::Left = 3)
			};

			// Test each triangle independently
			for (int32 TriIdx = 0; TriIdx < 4; TriIdx++)
			{
				if (IsPointInPolygon(TriangleCentroids[TriIdx], BoundaryVertices))
				{
					ETriangleType TriType = static_cast<ETriangleType>(TriIdx);
					OutTriangles.Add(FTriangleCoord(Row, Col, Level, TriType));
					TrianglesFound++;
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DetectInteriorTriangles: Found %d triangles inside room polygon"), TrianglesFound);

	return OutTriangles.Num() > 0;
}

bool URoomManagerComponent::FloodFillPolygonInterior(const FIntVector& StartTile, int32 Level, const TArray<int32>& BoundaryEdges, TArray<FIntVector>& OutTiles)
{
	if (!WallGraph || !LotManager)
		return false;

	OutTiles.Empty();

	// Build a set to avoid duplicates
	TSet<FIntVector> TileSet;

	// Step 1: Add tiles adjacent to ALL boundary walls
	// This ensures narrow corridors (< 1 tile wide) are detected
	// For diagonal walls: add tiles the wall passes through
	// For orthogonal walls: add tiles adjacent to the wall
	UE_LOG(LogTemp, Log, TEXT("FloodFillPolygonInterior: Checking %d boundary edges"), BoundaryEdges.Num());

	int32 DiagonalWallCount = 0;
	int32 OrthogonalWallCount = 0;

	for (int32 EdgeID : BoundaryEdges)
	{
		const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
		if (!Edge)
			continue;

		if (Edge->IsDiagonal())
		{
			DiagonalWallCount++;

			// Get all tiles this diagonal edge passes through
			TArray<FIntVector> EdgeTiles = WallGraph->GetTilesAlongEdge(*Edge);

			// Add all these tiles to our room
			for (const FIntVector& Tile : EdgeTiles)
			{
				TileSet.Add(Tile);

				// DEBUG: Cyan sphere for diagonal wall tiles
				if (GetWorld() && BurbArchitectDebug::IsRoomDebugEnabled())
				{
					FVector TileCenter;
					if (LotManager->TileToGridLocation(Tile.Z, Tile.X, Tile.Y, true, TileCenter))
					{
						DrawDebugSphere(GetWorld(), TileCenter + FVector(0, 0, 50.0f), 20.0f, 8,
							FColor::Cyan, false, 30.0f);
					}
				}
			}
		}
		else
		{
			OrthogonalWallCount++;

			// For orthogonal walls, add tiles on both sides of the wall
			// This catches narrow corridors where the polygon is too thin for point testing

			// Get wall endpoints to determine which tiles are adjacent
			int32 MinRow = FMath::Min(Edge->StartRow, Edge->EndRow);
			int32 MaxRow = FMath::Max(Edge->StartRow, Edge->EndRow);
			int32 MinCol = FMath::Min(Edge->StartColumn, Edge->EndColumn);
			int32 MaxCol = FMath::Max(Edge->StartColumn, Edge->EndColumn);

			// Horizontal wall (same row)
			if (Edge->StartRow == Edge->EndRow)
			{
				int32 WallRow = Edge->StartRow;

				// Add tiles on both sides of the wall
				for (int32 Col = MinCol; Col <= MaxCol; Col++)
				{
					// Tile above the wall
					if (WallRow > 0)
					{
						TileSet.Add(FIntVector(WallRow - 1, Col, Level));
					}
					// Tile below the wall
					if (WallRow < LotManager->GridSizeX)
					{
						TileSet.Add(FIntVector(WallRow, Col, Level));
					}
				}
			}
			// Vertical wall (same column)
			else if (Edge->StartColumn == Edge->EndColumn)
			{
				int32 WallCol = Edge->StartColumn;

				// Add tiles on both sides of the wall
				for (int32 Row = MinRow; Row <= MaxRow; Row++)
				{
					// Tile to the left of the wall
					if (WallCol > 0)
					{
						TileSet.Add(FIntVector(Row, WallCol - 1, Level));
					}
					// Tile to the right of the wall
					if (WallCol < LotManager->GridSizeY)
					{
						TileSet.Add(FIntVector(Row, WallCol, Level));
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Added %d candidate tiles from %d diagonal walls and %d orthogonal walls"),
		TileSet.Num(), DiagonalWallCount, OrthogonalWallCount);

	// Step 2: Build boundary vertices for polygon testing
	TArray<FVector> BoundaryVertices = EdgesToVertices(BoundaryEdges);
	if (BoundaryVertices.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("FloodFillPolygonInterior: Invalid polygon (less than 3 vertices)"));
		// Still return the wall tiles we found
		OutTiles = TileSet.Array();
		return OutTiles.Num() > 0;
	}

	// Step 3: Filter candidate tiles - test the CENTROIDS of the 4 TRIANGLES that make up each tile
	// Each tile quad is divided into 4 triangles: Top, Right, Bottom, Left
	// Triangle centroid = (V1 + V2 + V3) / 3
	TSet<FIntVector> FilteredTiles;
	for (const FIntVector& CandidateTile : TileSet)
	{
		FVector TileCenter;
		if (!LotManager->TileToGridLocation(CandidateTile.Z, CandidateTile.X, CandidateTile.Y, true, TileCenter))
			continue;

		const float TileSize = LotManager->GridTileSize;
		const float HalfTile = TileSize * 0.5f;

		// Get the 4 corners of the tile
		FVector TopLeft = TileCenter + FVector(-HalfTile, -HalfTile, 0.0f);
		FVector TopRight = TileCenter + FVector(HalfTile, -HalfTile, 0.0f);
		FVector BottomRight = TileCenter + FVector(HalfTile, HalfTile, 0.0f);
		FVector BottomLeft = TileCenter + FVector(-HalfTile, HalfTile, 0.0f);

		// Calculate centroid of each of the 4 triangles
		// Top triangle: TopLeft, TopRight, Center
		FVector TopCentroid = (TopLeft + TopRight + TileCenter) / 3.0f;
		// Right triangle: TopRight, BottomRight, Center
		FVector RightCentroid = (TopRight + BottomRight + TileCenter) / 3.0f;
		// Bottom triangle: BottomRight, BottomLeft, Center
		FVector BottomCentroid = (BottomRight + BottomLeft + TileCenter) / 3.0f;
		// Left triangle: BottomLeft, TopLeft, Center
		FVector LeftCentroid = (BottomLeft + TopLeft + TileCenter) / 3.0f;

		// Test each triangle centroid + tile center
		TArray<FVector> TestPoints = {
			TileCenter,
			TopCentroid,
			RightCentroid,
			BottomCentroid,
			LeftCentroid
		};

		// Include tile if ANY triangle centroid is inside the polygon
		bool bAnyTriangleInside = false;
		int32 PassedTestPointIndex = -1;
		for (int32 i = 0; i < TestPoints.Num(); i++)
		{
			if (IsPointInPolygon(TestPoints[i], BoundaryVertices))
			{
				bAnyTriangleInside = true;
				PassedTestPointIndex = i;
				break;
			}
		}

		if (bAnyTriangleInside)
		{
			FilteredTiles.Add(CandidateTile);

			// DIAGNOSTIC: Log which test point passed for potential duplicate tiles
			static const TCHAR* TestPointNames[] = { TEXT("Center"), TEXT("Top"), TEXT("Right"), TEXT("Bottom"), TEXT("Left") };
			UE_LOG(LogTemp, Verbose, TEXT("    Tile (%d,%d,%d) PASSED polygon test via %s point"),
				CandidateTile.X, CandidateTile.Y, CandidateTile.Z,
				PassedTestPointIndex >= 0 && PassedTestPointIndex < 5 ? TestPointNames[PassedTestPointIndex] : TEXT("Unknown"));
		}
	}

	// Replace TileSet with filtered tiles
	TileSet = FilteredTiles;
	UE_LOG(LogTemp, Log, TEXT("After filtering: %d tiles remain inside polygon"), TileSet.Num());

	// Calculate bounding box of the polygon
	int32 MinRow, MaxRow, MinCol, MaxCol;
	CalculatePolygonBoundingBox(BoundaryVertices, MinRow, MaxRow, MinCol, MaxCol);

	// Expand bounding box by 1 to ensure we catch all tiles
	MinRow = FMath::Max(0, MinRow - 1);
	MaxRow = FMath::Min(LotManager->GridSizeX - 1, MaxRow + 1);
	MinCol = FMath::Max(0, MinCol - 1);
	MaxCol = FMath::Min(LotManager->GridSizeY - 1, MaxCol + 1);

	UE_LOG(LogTemp, Log, TEXT("Testing interior tiles in bounding box: (%d,%d) to (%d,%d)"),
		MinRow, MinCol, MaxRow, MaxCol);

	// Step 4: Test remaining tiles in bounding box for interior inclusion
	// Use triangle centroids - same as Step 3
	int32 InteriorTilesAdded = 0;
	for (int32 Row = MinRow; Row <= MaxRow; Row++)
	{
		for (int32 Col = MinCol; Col <= MaxCol; Col++)
		{
			FIntVector TileCoord(Row, Col, Level);

			// Skip if already added
			if (TileSet.Contains(TileCoord))
				continue;

			// Get tile center in world coordinates
			FVector TileCenter;
			if (!LotManager->TileToGridLocation(Level, Row, Col, true, TileCenter))
				continue;

			const float TileSize = LotManager->GridTileSize;
			const float HalfTile = TileSize * 0.5f;

			// Get the 4 corners of the tile (Top = +Y = +Row, matching LocationToAllTileCorners)
			FVector TopLeft = TileCenter + FVector(-HalfTile, HalfTile, 0.0f);
			FVector TopRight = TileCenter + FVector(HalfTile, HalfTile, 0.0f);
			FVector BottomRight = TileCenter + FVector(HalfTile, -HalfTile, 0.0f);
			FVector BottomLeft = TileCenter + FVector(-HalfTile, -HalfTile, 0.0f);

			// Calculate centroid of each of the 4 triangles
			FVector TopCentroid = (TopLeft + TopRight + TileCenter) / 3.0f;
			FVector RightCentroid = (TopRight + BottomRight + TileCenter) / 3.0f;
			FVector BottomCentroid = (BottomRight + BottomLeft + TileCenter) / 3.0f;
			FVector LeftCentroid = (BottomLeft + TopLeft + TileCenter) / 3.0f;

			// Test triangle centroids + tile center
			TArray<FVector> TestPoints = {
				TileCenter,
				TopCentroid,
				RightCentroid,
				BottomCentroid,
				LeftCentroid
			};

			// Include tile if ANY triangle centroid is inside the polygon
			bool bAnyTriangleInside = false;
			for (const FVector& TestPoint : TestPoints)
			{
				if (IsPointInPolygon(TestPoint, BoundaryVertices))
				{
					bAnyTriangleInside = true;
					break;
				}
			}

			if (bAnyTriangleInside)
			{
				TileSet.Add(TileCoord);
				InteriorTilesAdded++;

				// DEBUG: Green sphere for interior tiles
				if (GetWorld() && BurbArchitectDebug::IsRoomDebugEnabled())
				{
					DrawDebugSphere(GetWorld(), TileCenter + FVector(0, 0, 100.0f), 15.0f, 8,
						FColor::Green, false, 30.0f);
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("FloodFillPolygonInterior: %d wall tiles + %d interior tiles = %d total tiles"),
		TileSet.Num() - InteriorTilesAdded, InteriorTilesAdded, TileSet.Num());

	// Convert set to array for output
	OutTiles = TileSet.Array();
	return OutTiles.Num() > 0;
}

// ========================================
// Polygon Geometry
// ========================================

FVector URoomManagerComponent::CalculatePolygonCentroid(const TArray<FVector>& Vertices)
{
	if (Vertices.Num() == 0)
		return FVector::ZeroVector;

	if (Vertices.Num() < 3)
	{
		// Degenerate polygon, just average the points
		FVector Sum = FVector::ZeroVector;
		for (const FVector& V : Vertices)
		{
			Sum += V;
		}
		return Sum / Vertices.Num();
	}

	// Area-weighted centroid for 2D polygon (ignoring Z)
	float SignedArea = 0.0f;
	FVector Centroid = FVector::ZeroVector;
	float Z = Vertices[0].Z; // Use Z from first vertex

	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		const FVector& V1 = Vertices[i];
		const FVector& V2 = Vertices[(i + 1) % Vertices.Num()];

		float A = (V1.X * V2.Y - V2.X * V1.Y);
		SignedArea += A;
		Centroid.X += (V1.X + V2.X) * A;
		Centroid.Y += (V1.Y + V2.Y) * A;
	}

	SignedArea *= 0.5f;

	if (FMath::Abs(SignedArea) > KINDA_SMALL_NUMBER)
	{
		Centroid.X /= (6.0f * SignedArea);
		Centroid.Y /= (6.0f * SignedArea);
	}
	else
	{
		// Fallback to simple average if area is too small
		for (const FVector& V : Vertices)
		{
			Centroid += V;
		}
		Centroid /= Vertices.Num();
	}

	Centroid.Z = Z;
	return Centroid;
}

float URoomManagerComponent::CalculatePolygonArea(const TArray<FVector>& Vertices)
{
	if (Vertices.Num() < 3)
		return 0.0f;

	// Shoelace formula
	float Area = 0.0f;
	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		const FVector& V1 = Vertices[i];
		const FVector& V2 = Vertices[(i + 1) % Vertices.Num()];
		Area += (V1.X * V2.Y - V2.X * V1.Y);
	}

	return FMath::Abs(Area) * 0.5f;
}

float URoomManagerComponent::CalculateSignedPolygonArea(const TArray<FVector>& Vertices)
{
	if (Vertices.Num() < 3)
		return 0.0f;

	// Shoelace formula - returns signed area
	// Positive = counter-clockwise winding (when viewed from +Z)
	// Negative = clockwise winding
	float SignedArea = 0.0f;
	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		const FVector& V1 = Vertices[i];
		const FVector& V2 = Vertices[(i + 1) % Vertices.Num()];
		SignedArea += (V1.X * V2.Y - V2.X * V1.Y);
	}

	return SignedArea * 0.5f;
}

bool URoomManagerComponent::IsExteriorBoundaryTrace(const TArray<FVector>& BoundaryVertices, bool bClockwiseTrace) const
{
	if (BoundaryVertices.Num() < 3)
		return true; // Invalid polygon is considered exterior

	float SignedArea = CalculateSignedPolygonArea(BoundaryVertices);

	// When tracing clockwise (Room1 side / interior), we expect:
	// - The polygon vertices to be in clockwise order (negative signed area)
	// When tracing counter-clockwise (Room2 side), we expect:
	// - The polygon vertices to be in counter-clockwise order (positive signed area)
	//
	// If the signs don't match, we've traced the exterior by mistake.
	// This happens when the trace wraps around multiple rooms on the outside.

	bool bIsExterior = false;
	if (bClockwiseTrace)
	{
		// Clockwise trace should produce negative signed area (CW polygon)
		// If positive, we traced the exterior (CCW = wrapping around outside)
		bIsExterior = (SignedArea > 0.0f);
	}
	else
	{
		// Counter-clockwise trace should produce positive signed area (CCW polygon)
		// If negative, we traced the exterior (CW = wrapping around outside)
		bIsExterior = (SignedArea < 0.0f);
	}

	if (bIsExterior)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoomManager: Detected exterior boundary trace (signed area=%.1f, clockwise=%s) - REJECTING"),
			SignedArea, bClockwiseTrace ? TEXT("true") : TEXT("false"));
	}

	return bIsExterior;
}

bool URoomManagerComponent::IsValidRoomPolygon(const TArray<FVector>& Vertices)
{
	// Must have at least 3 vertices
	if (Vertices.Num() < 3)
		return false;

	// Check for self-intersection
	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		const FVector& A1 = Vertices[i];
		const FVector& A2 = Vertices[(i + 1) % Vertices.Num()];

		for (int32 j = i + 2; j < Vertices.Num(); j++)
		{
			// Skip adjacent edges
			if (j == Vertices.Num() - 1 && i == 0)
				continue;

			const FVector& B1 = Vertices[j];
			const FVector& B2 = Vertices[(j + 1) % Vertices.Num()];

			if (DoLineSegmentsIntersect(A1, A2, B1, B2))
			{
				UE_LOG(LogTemp, Warning, TEXT("RoomManager: Polygon self-intersection detected"));
				return false;
			}
		}
	}

	return true;
}

bool URoomManagerComponent::IsPointInPolygon(const FVector& Point, const TArray<FVector>& Vertices)
{
	if (Vertices.Num() < 3)
		return false;

	// Ray casting algorithm (2D, ignoring Z)
	// Use inclusive boundary checks (<= and >) to handle points exactly on edges
	// This is critical for diagonal walls on grid-aligned tiles
	int32 Crossings = 0;
	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		const FVector& V1 = Vertices[i];
		const FVector& V2 = Vertices[(i + 1) % Vertices.Num()];

		// Ray casting along +X axis from point
		// Edge crosses the horizontal ray if:
		// - Edge goes upward through point Y: V1.Y <= Point.Y && V2.Y > Point.Y
		// - Edge goes downward through point Y: V1.Y > Point.Y && V2.Y <= Point.Y
		if (((V1.Y <= Point.Y) && (V2.Y > Point.Y)) || ((V1.Y > Point.Y) && (V2.Y <= Point.Y)))
		{
			float VT = (Point.Y - V1.Y) / (V2.Y - V1.Y);
			if (Point.X < V1.X + VT * (V2.X - V1.X))
			{
				Crossings++;
			}
		}
	}

	// Odd number of crossings = inside
	return (Crossings % 2) == 1;
}

void URoomManagerComponent::CalculatePolygonBoundingBox(const TArray<FVector>& Vertices, int32& OutMinRow, int32& OutMaxRow, int32& OutMinCol, int32& OutMaxCol) const
{
	if (!LotManager || Vertices.Num() == 0)
	{
		OutMinRow = OutMaxRow = OutMinCol = OutMaxCol = 0;
		return;
	}

	// Initialize with first vertex
	FVector FirstVertex = Vertices[0];
	int32 FirstRow, FirstCol;
	if (!LotManager->LocationToTile(FirstVertex, FirstRow, FirstCol))
	{
		OutMinRow = OutMaxRow = OutMinCol = OutMaxCol = 0;
		return;
	}

	OutMinRow = OutMaxRow = FirstRow;
	OutMinCol = OutMaxCol = FirstCol;

	// Expand bounding box for each vertex
	for (int32 i = 1; i < Vertices.Num(); i++)
	{
		int32 Row, Col;
		if (LotManager->LocationToTile(Vertices[i], Row, Col))
		{
			OutMinRow = FMath::Min(OutMinRow, Row);
			OutMaxRow = FMath::Max(OutMaxRow, Row);
			OutMinCol = FMath::Min(OutMinCol, Col);
			OutMaxCol = FMath::Max(OutMaxCol, Col);
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("RoomManager: Calculated bounding box from %d vertices: (%d,%d) to (%d,%d)"),
		Vertices.Num(), OutMinRow, OutMinCol, OutMaxRow, OutMaxCol);
}

// ========================================
// Room Cache Management
// ========================================

int32 URoomManagerComponent::GetRoomCount() const
{
	return Rooms.Num();
}

bool URoomManagerComponent::GetRoom(int32 RoomID, FRoomData& OutRoom)
{
	FRoomData* Found = Rooms.Find(RoomID);
	if (Found)
	{
		OutRoom = *Found;
		return true;
	}
	return false;
}

int32 URoomManagerComponent::GetRoomAtTriangle(const FTriangleCoord& TriCoord)
{
	// O(1) lookup using triangle spatial cache (primary lookup)
	const int32* RoomIDPtr = TriangleToRoomMap.Find(TriCoord.GetPackedKey());
	return RoomIDPtr ? *RoomIDPtr : 0; // 0 = outside
}

int32 URoomManagerComponent::GetRoomAtTriangleCoords(int32 Row, int32 Column, int32 Level, ETriangleType Triangle)
{
	FTriangleCoord TriCoord(Row, Column, Level, Triangle);
	return GetRoomAtTriangle(TriCoord);
}

TArray<int32> URoomManagerComponent::GetRoomsAtTile(const FIntVector& TileCoord)
{
	// Query all 4 triangles and collect unique room IDs
	TSet<int32> UniqueRooms;

	for (int32 TriIdx = 0; TriIdx < 4; TriIdx++)
	{
		ETriangleType TriType = static_cast<ETriangleType>(TriIdx);
		FTriangleCoord TriCoord(TileCoord, TriType);

		const int32* RoomIDPtr = TriangleToRoomMap.Find(TriCoord.GetPackedKey());
		if (RoomIDPtr && *RoomIDPtr > 0)
		{
			UniqueRooms.Add(*RoomIDPtr);
		}
	}

	return UniqueRooms.Array();
}

int32 URoomManagerComponent::GetRoomAtTile(const FIntVector& TileCoord)
{
	// Triangle-first approach: query all 4 triangles and return dominant room
	TMap<int32, int32> RoomCounts;

	for (int32 TriIdx = 0; TriIdx < 4; TriIdx++)
	{
		ETriangleType TriType = static_cast<ETriangleType>(TriIdx);
		FTriangleCoord TriCoord(TileCoord, TriType);

		const int32* RoomIDPtr = TriangleToRoomMap.Find(TriCoord.GetPackedKey());
		if (RoomIDPtr && *RoomIDPtr > 0)
		{
			RoomCounts.FindOrAdd(*RoomIDPtr)++;
		}
	}

	// Find room with most triangles (dominant room)
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

void URoomManagerComponent::InvalidateRoom(int32 RoomID)
{
	FRoomData* Room = Rooms.Find(RoomID);
	if (Room)
	{
		Room->bDirty = true;

		UE_LOG(LogTemp, Log, TEXT("RoomManager: Invalidated room %d"),
			RoomID);
	}
}

void URoomManagerComponent::InvalidateAllRooms()
{
	for (auto& Pair : Rooms)
	{
		Pair.Value.bDirty = true;
	}

	// Invalidate all blocked map caches
	ValidBlockedMapLevels.Empty();

	UE_LOG(LogTemp, Log, TEXT("RoomManager: Invalidated all %d rooms and blocked map caches"), Rooms.Num());
}

bool URoomManagerComponent::RebuildRoom(int32 RoomID)
{
	FRoomData* Room = Rooms.Find(RoomID);
	if (!Room || Room->BoundaryEdges.Num() == 0)
		return false;

	// Remove old room's tiles from spatial cache
	for (const FIntVector& TileCoord : Room->InteriorTiles)
	{
		TileToRoomMap.Remove(TileCoord);
	}

	// Re-detect room from first boundary edge
	FRoomData NewRoom = DetectRoomFromEdge(Room->BoundaryEdges[0], true);
	if (NewRoom.IsValid())
	{
		NewRoom.RoomID = RoomID; // Preserve original ID

		// Update spatial cache with new room's tiles
		for (const FIntVector& TileCoord : NewRoom.InteriorTiles)
		{
			TileToRoomMap.Add(TileCoord, RoomID);
		}

		Rooms[RoomID] = NewRoom;
		UE_LOG(LogTemp, Log, TEXT("RoomManager: Rebuilt room %d"), RoomID);
		return true;
	}

	return false;
}

void URoomManagerComponent::ClearRoomCache()
{
	Rooms.Empty();
	TileToRoomMap.Empty();
	TriangleToRoomMap.Empty();
	AvailableRoomIDs.Empty();
	NextRoomID = 1;
	SelectedRoomID = 0;

	// Clear blocked map cache
	CachedBlockedMaps.Empty();
	CachedBlockedToWall.Empty();
	ValidBlockedMapLevels.Empty();

	UE_LOG(LogTemp, Log, TEXT("RoomManager: Cleared room cache and blocked map cache"));
}

int32 URoomManagerComponent::GetNextRoomID()
{
	// Prefer recycled IDs to keep numbers low
	if (AvailableRoomIDs.Num() > 0)
	{
		// Sort to get the lowest available ID first (keeps IDs compact)
		AvailableRoomIDs.Sort();
		int32 RecycledID = AvailableRoomIDs[0];
		AvailableRoomIDs.RemoveAt(0);
		UE_LOG(LogTemp, Log, TEXT("RoomManager: Recycling room ID %d (%d IDs still available)"),
			RecycledID, AvailableRoomIDs.Num());
		return RecycledID;
	}

	// No recycled IDs available, increment counter
	return NextRoomID++;
}

void URoomManagerComponent::RecycleRoomID(int32 RoomID)
{
	if (RoomID > 0 && !AvailableRoomIDs.Contains(RoomID))
	{
		AvailableRoomIDs.Add(RoomID);
		UE_LOG(LogTemp, Log, TEXT("RoomManager: Room ID %d returned to pool (%d IDs now available)"),
			RoomID, AvailableRoomIDs.Num());
	}
}

// ========================================
// Room Selection
// ========================================

void URoomManagerComponent::SelectRoom(int32 RoomID)
{
	if (Rooms.Contains(RoomID))
	{
		SelectedRoomID = RoomID;
		UE_LOG(LogTemp, Log, TEXT("RoomManager: Selected room %d"), RoomID);
	}
}

void URoomManagerComponent::DeselectRoom()
{
	SelectedRoomID = 0;
	UE_LOG(LogTemp, Log, TEXT("RoomManager: Deselected room"));
}

bool URoomManagerComponent::GetSelectedRoom(FRoomData& OutRoom)
{
	if (SelectedRoomID > 0)
	{
		FRoomData* Found = Rooms.Find(SelectedRoomID);
		if (Found)
		{
			OutRoom = *Found;
			return true;
		}
	}
	return false;
}

// ========================================
// Face-Tracing Room Detection (ported from Godot RoomDetector)
// ========================================

TMap<int32, TArray<URoomManagerComponent::FAdjacencyEntry>> URoomManagerComponent::BuildFaceTracingAdjacency(int32 Level)
{
	TMap<int32, TArray<FAdjacencyEntry>> Adj;

	if (!WallGraph)
		return Adj;

	// Build initial adjacency from edges on this level
	for (const auto& EdgePair : WallGraph->Edges)
	{
		const FWallEdge& Edge = EdgePair.Value;
		if (Edge.Level != Level)
			continue;

		// Skip pool walls (they don't form room boundaries, similar to fences in Godot)
		if (Edge.bIsPoolWall)
			continue;

		if (!Adj.Contains(Edge.FromNodeID))
			Adj.Add(Edge.FromNodeID, TArray<FAdjacencyEntry>());
		if (!Adj.Contains(Edge.ToNodeID))
			Adj.Add(Edge.ToNodeID, TArray<FAdjacencyEntry>());

		Adj[Edge.FromNodeID].Add({Edge.ToNodeID, 0.f});
		Adj[Edge.ToNodeID].Add({Edge.FromNodeID, 0.f});
	}

	// Prune degree-1 nodes recursively (dead ends can't form rooms)
	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;
		TArray<int32> ToRemove;
		for (const auto& Pair : Adj)
		{
			if (Pair.Value.Num() <= 1)
				ToRemove.Add(Pair.Key);
		}
		for (int32 NodeID : ToRemove)
		{
			if (!Adj.Contains(NodeID))
				continue;
			const TArray<FAdjacencyEntry>& Neighbors = Adj[NodeID];
			for (const FAdjacencyEntry& Entry : Neighbors)
			{
				if (TArray<FAdjacencyEntry>* NeighborAdj = Adj.Find(Entry.NeighborNodeID))
				{
					NeighborAdj->RemoveAll([NodeID](const FAdjacencyEntry& E) {
						return E.NeighborNodeID == NodeID;
					});
				}
			}
			Adj.Remove(NodeID);
			bChanged = true;
		}
	}

	// Compute angles and sort each neighbor list
	for (auto& Pair : Adj)
	{
		const FWallNode* Node = WallGraph->Nodes.Find(Pair.Key);
		if (!Node)
			continue;

		for (FAdjacencyEntry& Entry : Pair.Value)
		{
			const FWallNode* Neighbor = WallGraph->Nodes.Find(Entry.NeighborNodeID);
			if (!Neighbor)
				continue;

			float DeltaX = Neighbor->Position.X - Node->Position.X;
			float DeltaY = Neighbor->Position.Y - Node->Position.Y;
			Entry.Angle = FMath::Atan2(DeltaY, DeltaX);
			if (Entry.Angle < 0.f)
				Entry.Angle += UE_TWO_PI;
		}

		Pair.Value.Sort([](const FAdjacencyEntry& A, const FAdjacencyEntry& B) {
			return A.Angle < B.Angle;
		});
	}

	return Adj;
}

TArray<URoomManagerComponent::FTracedFace> URoomManagerComponent::TraceAllGraphFaces(
	const TMap<int32, TArray<FAdjacencyEntry>>& Adjacency)
{
	if (!WallGraph)
		return {};

	// Build node pair -> edge ID lookup for extracting boundary edge IDs
	TMap<uint64, int32> NodePairToEdge;
	for (const auto& EdgePair : WallGraph->Edges)
	{
		const FWallEdge& Edge = EdgePair.Value;
		uint32 MinID = (uint32)FMath::Min(Edge.FromNodeID, Edge.ToNodeID);
		uint32 MaxID = (uint32)FMath::Max(Edge.FromNodeID, Edge.ToNodeID);
		uint64 Key = ((uint64)MinID << 32) | (uint64)MaxID;
		NodePairToEdge.Add(Key, Edge.EdgeID);
	}

	// Track visited directed edges: packed key = U * large_prime + V
	TSet<int64> Visited;
	TArray<FTracedFace> Faces;
	const int64 PackMul = 1000003LL; // Large prime to avoid collisions

	for (const auto& NodePair : Adjacency)
	{
		int32 NodeID = NodePair.Key;
		const TArray<FAdjacencyEntry>& Neighbors = NodePair.Value;

		for (const FAdjacencyEntry& Entry : Neighbors)
		{
			int32 U = NodeID;
			int32 V = Entry.NeighborNodeID;
			int64 EdgeKey = (int64)U * PackMul + V;

			if (Visited.Contains(EdgeKey))
				continue;

			// Trace a face starting with directed edge U -> V
			FTracedFace Face;
			int32 StartU = U;
			int32 StartV = V;
			int32 CurrentU = U;
			int32 CurrentV = V;
			int32 MaxSteps = Adjacency.Num() * 2 + 10;
			int32 StepCount = 0;

			while (true)
			{
				int64 EK = (int64)CurrentU * PackMul + CurrentV;
				Visited.Add(EK);

				const FWallNode* NodeU = WallGraph->Nodes.Find(CurrentU);
				if (NodeU)
				{
					Face.NodeIDs.Add(CurrentU);
					Face.Vertices.Add(NodeU->Position);
				}

				// Find edge ID between CurrentU and CurrentV
				uint32 MinN = (uint32)FMath::Min(CurrentU, CurrentV);
				uint32 MaxN = (uint32)FMath::Max(CurrentU, CurrentV);
				uint64 PairKey = ((uint64)MinN << 32) | (uint64)MaxN;
				if (const int32* FoundEdgeID = NodePairToEdge.Find(PairKey))
				{
					Face.EdgeIDs.Add(*FoundEdgeID);
				}

				// At node CurrentV, find CurrentU in its neighbor list
				const TArray<FAdjacencyEntry>* VNeighbors = Adjacency.Find(CurrentV);
				if (!VNeighbors || VNeighbors->Num() == 0)
					break;

				int32 FoundIdx = -1;
				for (int32 i = 0; i < VNeighbors->Num(); i++)
				{
					if ((*VNeighbors)[i].NeighborNodeID == CurrentU)
					{
						FoundIdx = i;
						break;
					}
				}

				if (FoundIdx == -1)
					break;

				// Take PREVIOUS entry in angular order (wrapping) — traces CCW faces
				int32 PrevIdx = (FoundIdx - 1 + VNeighbors->Num()) % VNeighbors->Num();
				int32 NextNode = (*VNeighbors)[PrevIdx].NeighborNodeID;

				CurrentU = CurrentV;
				CurrentV = NextNode;

				StepCount++;
				if (CurrentU == StartU && CurrentV == StartV)
					break;
				if (StepCount > MaxSteps)
					break;
			}

			if (Face.Vertices.Num() >= 3)
			{
				Face.SignedArea = CalculateSignedPolygonArea(Face.Vertices);
				Faces.Add(MoveTemp(Face));
			}
		}
	}

	return Faces;
}

TArray<FVector> URoomManagerComponent::SimplifyRoomPolygon(const TArray<FVector>& Polygon)
{
	int32 N = Polygon.Num();
	if (N < 3)
		return Polygon;

	TArray<FVector> Result;
	for (int32 i = 0; i < N; i++)
	{
		const FVector& Prev = Polygon[(i - 1 + N) % N];
		const FVector& Curr = Polygon[i];
		const FVector& Next = Polygon[(i + 1) % N];

		// Cross product of (Curr-Prev) x (Next-Curr) on XY plane
		float Cross = (Curr.X - Prev.X) * (Next.Y - Curr.Y) - (Curr.Y - Prev.Y) * (Next.X - Curr.X);
		if (FMath::Abs(Cross) > 0.0001f)
		{
			Result.Add(Curr);
		}
	}

	return Result.Num() >= 3 ? Result : Polygon;
}

bool URoomManagerComponent::IsPointOnSegment2D(const FVector& Point, const FVector& A, const FVector& B, float Epsilon)
{
	// Cross product for distance from line (2D)
	float Cross = (B.X - A.X) * (Point.Y - A.Y) - (B.Y - A.Y) * (Point.X - A.X);
	float SegLenSq = FMath::Square(B.X - A.X) + FMath::Square(B.Y - A.Y);

	if (SegLenSq < Epsilon * Epsilon)
		return FVector::DistSquared2D(Point, A) < Epsilon * Epsilon;

	// Distance from line = |cross| / length
	if (Cross * Cross > Epsilon * Epsilon * SegLenSq)
		return false;

	// Check within segment bounds via dot product projection
	float Dot = (Point.X - A.X) * (B.X - A.X) + (Point.Y - A.Y) * (B.Y - A.Y);
	return Dot >= -Epsilon && Dot <= SegLenSq + Epsilon;
}

bool URoomManagerComponent::IsPointInPolygonRobust(const FVector& Point, const TArray<FVector>& Polygon, bool bIncludeEdges)
{
	int32 N = Polygon.Num();
	if (N < 3)
		return false;

	// Optionally check if point is on any edge (within epsilon) — counts as inside
	if (bIncludeEdges)
	{
		int32 J = N - 1;
		for (int32 I = 0; I < N; I++)
		{
			if (IsPointOnSegment2D(Point, Polygon[J], Polygon[I]))
				return true;
			J = I;
		}
	}

	// Standard ray casting (even-odd rule) on XY plane
	bool bInside = false;
	J = N - 1;
	for (int32 I = 0; I < N; I++)
	{
		const FVector& PolyI = Polygon[I];
		const FVector& PolyJ = Polygon[J];

		if (((PolyI.Y > Point.Y) != (PolyJ.Y > Point.Y)) &&
			(Point.X < (PolyJ.X - PolyI.X) * (Point.Y - PolyI.Y) / (PolyJ.Y - PolyI.Y) + PolyI.X))
		{
			bInside = !bInside;
		}
		J = I;
	}

	return bInside;
}

void URoomManagerComponent::StampEdgeRoomIDs(int32 Level)
{
	if (!WallGraph || !LotManager)
		return;

	// Use 0.75 * TileSize so perpendicular samples reliably cross into adjacent
	// tiles even for 45-degree diagonal walls (0.5 * TileSize only offsets ~0.35
	// tiles per axis at 45 degrees, landing on the boundary or same tile)
	const float SampleOffset = LotManager->GridTileSize * 0.75f;

	// Collect room polygons on this level for direct point-in-polygon testing
	// This avoids TileToRoomMap sampling which fails for diagonal walls
	// (perpendicular samples land in the same tile for 45-degree walls)
	TArray<TPair<int32, TArray<FVector>>> RoomPolygons;
	for (const auto& RoomPair : Rooms)
	{
		if (RoomPair.Value.Level == Level && RoomPair.Value.BoundaryVertices.Num() >= 3)
		{
			RoomPolygons.Add(TPair<int32, TArray<FVector>>(RoomPair.Key, RoomPair.Value.BoundaryVertices));
		}
	}

	for (auto& EdgePair : WallGraph->Edges)
	{
		FWallEdge& Edge = EdgePair.Value;
		if (Edge.Level != Level)
			continue;

		const FWallNode* FromNode = WallGraph->Nodes.Find(Edge.FromNodeID);
		const FWallNode* ToNode = WallGraph->Nodes.Find(Edge.ToNodeID);
		if (!FromNode || !ToNode)
			continue;

		// Edge direction and left-normal perpendicular
		FVector2D Dir(ToNode->Position.X - FromNode->Position.X, ToNode->Position.Y - FromNode->Position.Y);
		float DirLen = Dir.Size();
		if (DirLen < KINDA_SMALL_NUMBER)
			continue;
		Dir /= DirLen;
		FVector2D LeftNormal(-Dir.Y, Dir.X);

		// Sample midpoint offset perpendicular to each side
		FVector Mid = (FromNode->Position + ToNode->Position) * 0.5f;
		FVector SampleLeft = Mid + FVector(LeftNormal.X * SampleOffset, LeftNormal.Y * SampleOffset, 0.f);
		FVector SampleRight = Mid - FVector(LeftNormal.X * SampleOffset, LeftNormal.Y * SampleOffset, 0.f);

		// Test sample points directly against room polygons (robust for any wall angle)
		// Room1 = left/+normal side, Room2 = right/-normal side
		Edge.Room1 = 0;
		Edge.Room2 = 0;

		for (const auto& RoomPoly : RoomPolygons)
		{
			// Use strict interior test (bIncludeEdges=false) so a sample near a
			// shared boundary doesn't match both adjacent room polygons
			if (Edge.Room1 == 0 && IsPointInPolygonRobust(SampleLeft, RoomPoly.Value, false))
			{
				Edge.Room1 = RoomPoly.Key;
			}
			if (Edge.Room2 == 0 && IsPointInPolygonRobust(SampleRight, RoomPoly.Value, false))
			{
				Edge.Room2 = RoomPoly.Key;
			}
			if (Edge.Room1 != 0 && Edge.Room2 != 0)
				break;
		}

		Edge.Room1Generation = CurrentGeneration;
		Edge.Room2Generation = CurrentGeneration;
	}
}

// ========================================
// Helper Functions
// ========================================

int32 URoomManagerComponent::FindNextBoundaryEdge(int32 CurrentNodeID, int32 PreviousEdgeID, bool bClockwise)
{
	if (!WallGraph)
		return -1;

	const FWallNode* Node = WallGraph->Nodes.Find(CurrentNodeID);
	if (!Node)
		return -1;

	if (Node->ConnectedEdgeIDs.Num() < 2)
		return -1; // Dead end

	// Get previous edge direction
	const FWallEdge* PrevEdge = WallGraph->Edges.Find(PreviousEdgeID);
	if (!PrevEdge)
		return -1;

	// Determine incoming direction
	FVector IncomingDir;
	if (PrevEdge->ToNodeID == CurrentNodeID)
	{
		const FWallNode* FromNode = WallGraph->Nodes.Find(PrevEdge->FromNodeID);
		IncomingDir = (Node->Position - FromNode->Position).GetSafeNormal2D();
	}
	else
	{
		const FWallNode* ToNode = WallGraph->Nodes.Find(PrevEdge->ToNodeID);
		IncomingDir = (Node->Position - ToNode->Position).GetSafeNormal2D();
	}

	// Find the edge that turns most to the right (clockwise) or left (counterclockwise)
	int32 BestEdgeID = -1;
	float BestAngle = bClockwise ? -FLT_MAX : FLT_MAX;

	for (int32 EdgeID : Node->ConnectedEdgeIDs)
	{
		if (EdgeID == PreviousEdgeID)
			continue;

		const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
		if (!Edge)
			continue;

		// Get outgoing direction
		FVector OutgoingDir;
		if (Edge->FromNodeID == CurrentNodeID)
		{
			const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
			OutgoingDir = (ToNode->Position - Node->Position).GetSafeNormal2D();
		}
		else
		{
			const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
			OutgoingDir = (FromNode->Position - Node->Position).GetSafeNormal2D();
		}

		// Calculate angle between incoming and outgoing
		float CrossZ = FVector::CrossProduct(IncomingDir, OutgoingDir).Z;
		float Dot = FVector::DotProduct(IncomingDir, OutgoingDir);
		float Angle = FMath::Atan2(CrossZ, Dot);

		// Choose based on direction
		bool bIsBest = false;
		if (bClockwise)
		{
			// For clockwise, we want the smallest positive angle (rightmost turn)
			if (Angle > BestAngle)
			{
				BestAngle = Angle;
				BestEdgeID = EdgeID;
				bIsBest = true;
			}
		}
		else
		{
			// For counterclockwise, we want the largest negative angle (leftmost turn)
			if (Angle < BestAngle)
			{
				BestAngle = Angle;
				BestEdgeID = EdgeID;
				bIsBest = true;
			}
		}

		// DEBUG: Draw candidate edges
		// Will be overdrawn with GREEN if selected, stays RED if rejected
		if (!bIsBest && GetWorld())
		{
			const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
			const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
			if (FromNode && ToNode)
			{
				FVector V1 = FromNode->Position;
				FVector V2 = ToNode->Position;
				V1.Z += 75.0f;
				V2.Z += 75.0f;
				if (BurbArchitectDebug::IsRoomDebugEnabled())
			{
				DrawDebugLine(GetWorld(), V1, V2, FColor::Red, false, 30.0f, 0, 3.0f);
			}
			}
		}
	}

	return BestEdgeID;
}

bool URoomManagerComponent::GetTilesAdjacentToEdge(int32 EdgeID, FIntVector& OutTile1, FIntVector& OutTile2)
{
	if (!WallGraph || !LotManager)
		return false;

	const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
	if (!Edge)
		return false;

	const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
	const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
	if (!FromNode || !ToNode)
		return false;

	// Calculate edge direction (from FromNode to ToNode)
	FVector EdgeDir = (ToNode->Position - FromNode->Position).GetSafeNormal2D();

	// Calculate perpendicular vector (90 degrees clockwise for Room1 side, counter-clockwise for Room2 side)
	// Right-hand rule: standing at FromNode looking toward ToNode, Room1 is on the right
	FVector Perp1(EdgeDir.Y, -EdgeDir.X, 0.0f);  // Perpendicular right (Room1 side)
	FVector Perp2(-EdgeDir.Y, EdgeDir.X, 0.0f);  // Perpendicular left (Room2 side)

	// Get edge midpoint
	FVector Midpoint = (FromNode->Position + ToNode->Position) * 0.5f;

	// Offset by half a tile size to get points clearly on each side
	float OffsetDistance = LotManager->GridTileSize * 0.5f;
	FVector Point1 = Midpoint + Perp1 * OffsetDistance;
	FVector Point2 = Midpoint + Perp2 * OffsetDistance;

	// Convert to tile coordinates
	int32 Row1, Col1, Row2, Col2;
	if (!LotManager->LocationToTile(Point1, Row1, Col1))
		return false;
	if (!LotManager->LocationToTile(Point2, Row2, Col2))
		return false;

	OutTile1 = FIntVector(Row1, Col1, Edge->Level);
	OutTile2 = FIntVector(Row2, Col2, Edge->Level);

	return true;
}

int32 URoomManagerComponent::GetOppositeNode(int32 EdgeID, int32 NodeID)
{
	if (!WallGraph)
		return -1;

	const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
	if (!Edge)
		return -1;

	if (Edge->FromNodeID == NodeID)
		return Edge->ToNodeID;
	else if (Edge->ToNodeID == NodeID)
		return Edge->FromNodeID;

	return -1;
}

bool URoomManagerComponent::DoLineSegmentsIntersect(const FVector& A1, const FVector& A2, const FVector& B1, const FVector& B2)
{
	// 2D line segment intersection (ignoring Z)
	float Denom = (B2.Y - B1.Y) * (A2.X - A1.X) - (B2.X - B1.X) * (A2.Y - A1.Y);

	if (FMath::Abs(Denom) < KINDA_SMALL_NUMBER)
		return false; // Parallel

	float UA = ((B2.X - B1.X) * (A1.Y - B1.Y) - (B2.Y - B1.Y) * (A1.X - B1.X)) / Denom;
	float UB = ((A2.X - A1.X) * (A1.Y - B1.Y) - (A2.Y - A1.Y) * (A1.X - B1.X)) / Denom;

	// Check if intersection point is within both segments
	return (UA > 0.0f && UA < 1.0f && UB > 0.0f && UB < 1.0f);
}

// ========================================
// Incremental Room Detection (Phase 2)
// ========================================

void URoomManagerComponent::OnWallsModified(const TArray<int32>& AddedWalls, const TArray<int32>& RemovedWalls)
{
	UE_LOG(LogTemp, Log, TEXT("RoomManager::OnWallsModified - %d added, %d removed (INCREMENTAL)"),
		AddedWalls.Num(), RemovedWalls.Num());

	if (!WallGraph)
	{
		return;
	}

	// ========================================
	// INCREMENTAL APPROACH: Only process affected walls
	// Much faster than re-detecting all rooms on affected levels
	// ========================================

	// Track room changes for notification
	TArray<int32> AddedRoomIDs;
	TArray<int32> ModifiedRoomIDs;
	TArray<int32> RemovedRoomIDs;

	// Store current room IDs for comparison
	TSet<int32> RoomIDsBefore;
	for (const auto& Pair : Rooms)
	{
		RoomIDsBefore.Add(Pair.Key);
	}

	// Process removed walls first (may cause room merges)
	for (int32 WallID : RemovedWalls)
	{
		// Get the edge level before it's removed (edge should still exist at this point)
		const FWallEdge* Edge = WallGraph->Edges.Find(WallID);
		int32 Level = 0;
		if (Edge)
		{
			Level = Edge->Level;
		}
		else
		{
			// Edge already removed, try to find level from rooms that had this wall in boundary
			for (const auto& Pair : Rooms)
			{
				if (Pair.Value.BoundaryEdges.Contains(WallID))
				{
					Level = Pair.Value.Level;
					break;
				}
			}
		}

		TArray<int32> MergedRoomIDs;
		HandleWallRemovalDetection(WallID, Level, MergedRoomIDs);

		for (int32 RoomID : MergedRoomIDs)
		{
			if (!RoomIDsBefore.Contains(RoomID))
			{
				AddedRoomIDs.AddUnique(RoomID);
			}
			else
			{
				ModifiedRoomIDs.AddUnique(RoomID);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("RoomManager::OnWallsModified - Wall %d removed, %d rooms affected"),
			WallID, MergedRoomIDs.Num());
	}

	// Process added walls (may cause new rooms or room subdivision)
	for (int32 WallID : AddedWalls)
	{
		TArray<int32> DetectedRoomIDs;
		DetectRoomFromNewEdgeWithIDs(WallID, DetectedRoomIDs);

		for (int32 RoomID : DetectedRoomIDs)
		{
			if (!RoomIDsBefore.Contains(RoomID))
			{
				AddedRoomIDs.AddUnique(RoomID);
			}
			else
			{
				ModifiedRoomIDs.AddUnique(RoomID);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("RoomManager::OnWallsModified - Wall %d added, %d rooms detected"),
			WallID, DetectedRoomIDs.Num());
	}

	// Find removed rooms (rooms that existed before but don't anymore)
	for (int32 OldRoomID : RoomIDsBefore)
	{
		if (!Rooms.Contains(OldRoomID))
		{
			RemovedRoomIDs.AddUnique(OldRoomID);
		}
	}

	// Notify listeners
	OnRoomsChanged.Broadcast(AddedRoomIDs, ModifiedRoomIDs, RemovedRoomIDs);

	UE_LOG(LogTemp, Log, TEXT("RoomManager::OnWallsModified - Complete. Added %d, modified %d, removed %d rooms"),
		AddedRoomIDs.Num(), ModifiedRoomIDs.Num(), RemovedRoomIDs.Num());
}

void URoomManagerComponent::InvalidateRoomsInArea(const TArray<FIntVector>& AffectedTiles)
{
	for (const FIntVector& TileCoord : AffectedTiles)
	{
		// Find which rooms contain this tile
		int32 RoomID = TileToRoomMap.FindRef(TileCoord);
		if (RoomID > 0)
		{
			DirtyRooms.Add(RoomID);
		}

		DirtyTiles.Add(TileCoord);
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::InvalidateRoomsInArea - Invalidated %d tiles, marked %d rooms dirty"),
		AffectedTiles.Num(), DirtyRooms.Num());
}

void URoomManagerComponent::InvalidateRoomsAlongWall(int32 WallEdgeID)
{
	if (!WallGraph) return;

	const FWallEdge* Edge = WallGraph->Edges.Find(WallEdgeID);
	if (!Edge) return;

	// Mark rooms on both sides as dirty
	if (Edge->Room1 > 0)
	{
		DirtyRooms.Add(Edge->Room1);
		UE_LOG(LogTemp, Log, TEXT("RoomManager::InvalidateRoomsAlongWall - Invalidated Room %d (wall %d Room1)"), Edge->Room1, WallEdgeID);
	}

	if (Edge->Room2 > 0)
	{
		DirtyRooms.Add(Edge->Room2);
		UE_LOG(LogTemp, Log, TEXT("RoomManager::InvalidateRoomsAlongWall - Invalidated Room %d (wall %d Room2)"), Edge->Room2, WallEdgeID);
	}

	// Invalidate the edge's room assignments
	const_cast<FWallEdge*>(Edge)->InvalidateRooms();
}

void URoomManagerComponent::DetectRoomChanges()
{
	CurrentGeneration++;

	UE_LOG(LogTemp, Warning, TEXT("RoomManager::DetectRoomChanges - Generation %d, processing %d dirty rooms"),
		CurrentGeneration, DirtyRooms.Num());

	TSet<int32> ProcessedRooms;

	for (int32 RoomID : DirtyRooms)
	{
		if (ProcessedRooms.Contains(RoomID)) continue;
		ProcessedRooms.Add(RoomID);

		FRoomData* OldRoom = Rooms.Find(RoomID);
		if (!OldRoom) continue;

		// Check if room still forms a valid closed loop
		TArray<int32> ValidBoundary;
		for (int32 EdgeID : OldRoom->BoundaryEdges)
		{
			if (WallGraph->Edges.Contains(EdgeID))
			{
				ValidBoundary.Add(EdgeID);
			}
		}

		// Try to trace a closed loop from the remaining boundary
		if (ValidBoundary.Num() >= 3)
		{
			TArray<int32> TracedBoundary = TraceRoomBoundary(ValidBoundary[0], true);

			if (TracedBoundary.Num() >= 3)
			{
				// Room still exists, might have changed shape
				OldRoom->BoundaryEdges = TracedBoundary;
				OldRoom->BoundaryVertices = EdgesToVertices(TracedBoundary);
				OldRoom->GenerationNumber = CurrentGeneration;

				// TODO: Recalculate interior tiles via FloodFillPolygonInterior

				UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectRoomChanges - Room %d modified (still valid)"), RoomID);
			}
			else
			{
				// Room was subdivided or destroyed
				HandleRoomSubdivision(RoomID);
			}
		}
		else
		{
			// Room destroyed
			OldRoom->bIsValidRoom = false;
			InvalidatedRooms.Add(RoomID, *OldRoom);
			Rooms.Remove(RoomID);
			RecycleRoomID(RoomID);

			UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectRoomChanges - Room %d destroyed"), RoomID);
		}
	}

	// TODO: Detect new rooms from added walls (similar logic to existing DetectRoomFromNewEdge)

	DirtyRooms.Empty();
}

void URoomManagerComponent::HandleRoomSubdivision(int32 OriginalRoomID)
{
	FRoomData* OriginalRoom = Rooms.Find(OriginalRoomID);
	if (!OriginalRoom) return;

	UE_LOG(LogTemp, Warning, TEXT("RoomManager::HandleRoomSubdivision - Handling subdivision of Room %d"), OriginalRoomID);

	// Find all closed loops within the original boundary area
	TArray<TArray<int32>> NewLoops = FindAllClosedLoopsInArea(OriginalRoom->BoundaryEdges);

	if (NewLoops.Num() == 0)
	{
		// Room completely destroyed
		OriginalRoom->bIsValidRoom = false;
		InvalidatedRooms.Add(OriginalRoomID, *OriginalRoom);
		Rooms.Remove(OriginalRoomID);
		RecycleRoomID(OriginalRoomID);
		UE_LOG(LogTemp, Log, TEXT("RoomManager::HandleRoomSubdivision - Room %d completely destroyed"), OriginalRoomID);
	}
	else if (NewLoops.Num() == 1)
	{
		// Room modified but not subdivided
		OriginalRoom->BoundaryEdges = NewLoops[0];
		OriginalRoom->BoundaryVertices = EdgesToVertices(NewLoops[0]);
		OriginalRoom->GenerationNumber = CurrentGeneration;
		UE_LOG(LogTemp, Log, TEXT("RoomManager::HandleRoomSubdivision - Room %d modified (not subdivided)"), OriginalRoomID);
	}
	else
	{
		// Room subdivided into multiple rooms
		OriginalRoom->bIsValidRoom = false;

		for (const TArray<int32>& Loop : NewLoops)
		{
			int32 NewRoomID = GetNextRoomID();
			FRoomData NewRoom = CreateRoomFromBoundary(Loop);
			NewRoom.ParentRoomID = OriginalRoomID;
			NewRoom.GenerationNumber = CurrentGeneration;

			OriginalRoom->SplitIntoRoomIDs.Add(NewRoomID);
			Rooms.Add(NewRoomID, NewRoom);

			UE_LOG(LogTemp, Log, TEXT("RoomManager::HandleRoomSubdivision - Room %d split into new Room %d"), OriginalRoomID, NewRoomID);
		}

		InvalidatedRooms.Add(OriginalRoomID, *OriginalRoom);
		Rooms.Remove(OriginalRoomID);
		RecycleRoomID(OriginalRoomID);
	}
}

void URoomManagerComponent::DetectRoomMergers()
{
	// TODO: Implement room merger detection
	// This is called when walls are removed and adjacent rooms might merge
	UE_LOG(LogTemp, Warning, TEXT("RoomManager::DetectRoomMergers - STUB (not yet implemented)"));
}

FRoomData URoomManagerComponent::CreateRoomFromBoundary(const TArray<int32>& BoundaryEdges)
{
	FRoomData NewRoom;
	NewRoom.RoomID = GetNextRoomID();
	NewRoom.BoundaryEdges = BoundaryEdges;
	NewRoom.BoundaryVertices = EdgesToVertices(BoundaryEdges);
	NewRoom.Centroid = CalculatePolygonCentroid(NewRoom.BoundaryVertices);
	NewRoom.GenerationNumber = CurrentGeneration;
	NewRoom.bIsValidRoom = true;

	// Determine level from first edge
	if (BoundaryEdges.Num() > 0 && WallGraph)
	{
		const FWallEdge* FirstEdge = WallGraph->Edges.Find(BoundaryEdges[0]);
		if (FirstEdge)
		{
			NewRoom.Level = FirstEdge->Level;
		}
	}

	// TODO: Flood fill interior tiles
	// FIntVector StartTile(...);
	// FloodFillPolygonInterior(StartTile, NewRoom.Level, BoundaryEdges, NewRoom.InteriorTiles);

	UE_LOG(LogTemp, Log, TEXT("RoomManager::CreateRoomFromBoundary - Created Room %d with %d boundary edges"),
		NewRoom.RoomID, BoundaryEdges.Num());

	return NewRoom;
}

TArray<TArray<int32>> URoomManagerComponent::FindAllClosedLoopsInArea(const TArray<int32>& SearchArea)
{
	TArray<TArray<int32>> FoundLoops;
	TSet<int32> ProcessedEdges;

	for (int32 StartEdgeID : SearchArea)
	{
		if (ProcessedEdges.Contains(StartEdgeID)) continue;
		if (!WallGraph || !WallGraph->Edges.Contains(StartEdgeID)) continue;

		// Try both directions
		for (bool bClockwise : {true, false})
		{
			TArray<int32> Loop = TraceRoomBoundary(StartEdgeID, bClockwise);

			if (Loop.Num() >= 3)
			{
				// Verify this is a new loop
				bool bIsDuplicate = false;
				for (const TArray<int32>& ExistingLoop : FoundLoops)
				{
					if (Loop.Num() == ExistingLoop.Num())
					{
						TArray<int32> SortedLoop = Loop;
						TArray<int32> SortedExisting = ExistingLoop;
						SortedLoop.Sort();
						SortedExisting.Sort();

						if (SortedLoop == SortedExisting)
						{
							bIsDuplicate = true;
							break;
						}
					}
				}

				if (!bIsDuplicate)
				{
					FoundLoops.Add(Loop);
					ProcessedEdges.Append(Loop);
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::FindAllClosedLoopsInArea - Found %d closed loops in search area of %d edges"),
		FoundLoops.Num(), SearchArea.Num());

	return FoundLoops;
}

void URoomManagerComponent::UpdateWallRoomAssignments()
{
	// TODO: Implement wall-room assignment updates
	// Sets Room1/Room2 on edges based on current room configuration
	UE_LOG(LogTemp, Warning, TEXT("RoomManager::UpdateWallRoomAssignments - STUB (not yet implemented)"));
}

void URoomManagerComponent::AssignTrianglesToRoom(int32 RoomID, const TArray<FIntVector>& TileCoords)
{
	// TODO: Implement triangle-level room assignment
	// Uses triangle centroids for accurate assignment with diagonal walls
	UE_LOG(LogTemp, Warning, TEXT("RoomManager::AssignTrianglesToRoom - STUB (not yet implemented)"));
}

void URoomManagerComponent::RebuildSpatialIndices()
{
	// TRIANGLE-FIRST: Rebuild all spatial caches from room data
	TileToRoomMap.Empty();
	TriangleToRoomMap.Empty();
	WallToRoomMap.Empty();

	// First, clear all triangle ownership in GridData
	if (LotManager)
	{
		for (FTileData& Tile : LotManager->GridData)
		{
			// Clear per-triangle ownership (not SetRoomID which overwrites all triangles)
			Tile.TriangleOwnership.Reset();
		}
	}

	// TRIANGLE-FIRST: Populate from InteriorTriangles (primary source)
	for (const auto& Pair : Rooms)
	{
		const FRoomData& Room = Pair.Value;
		if (!Room.bIsValidRoom) continue;

		// Populate TriangleToRoomMap and GridData.TriangleOwnership from InteriorTriangles
		for (const FTriangleCoord& Tri : Room.InteriorTriangles)
		{
			// Primary spatial cache
			TriangleToRoomMap.Add(Tri.GetPackedKey(), Room.RoomID);

			// Update GridData triangle ownership
			if (LotManager)
			{
				FIntVector TileCoord = Tri.GetTileCoord();
				int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
				if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
				{
					LotManager->GridData[*TileIndexPtr].TriangleOwnership.SetTriangleRoom(Tri.Triangle, Room.RoomID);
				}
			}
		}

		// DERIVED: Populate TileToRoomMap from InteriorTiles
		// Each tile gets assigned to the room that owns the most triangles
		for (const FIntVector& TileCoord : Room.InteriorTiles)
		{
			// Count triangles owned by this room in this tile
			int32 OwnedCount = 0;
			for (int32 TriIdx = 0; TriIdx < 4; TriIdx++)
			{
				FTriangleCoord TestTri(TileCoord, static_cast<ETriangleType>(TriIdx));
				const int32* OwnerPtr = TriangleToRoomMap.Find(TestTri.GetPackedKey());
				if (OwnerPtr && *OwnerPtr == Room.RoomID)
				{
					OwnedCount++;
				}
			}

			// If this room owns majority of triangles, claim the tile
			if (OwnedCount >= 2)
			{
				TileToRoomMap.Add(TileCoord, Room.RoomID);
			}
		}

		// Populate wall-to-room map
		for (int32 EdgeID : Room.BoundaryEdges)
		{
			WallToRoomMap.Add(EdgeID, Room.RoomID);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::RebuildSpatialIndices - Rebuilt indices for %d rooms (%d triangles, %d tiles, %d wall associations)"),
		Rooms.Num(), TriangleToRoomMap.Num(), TileToRoomMap.Num(), WallToRoomMap.Num());
}

// ========================================
// Flood Fill Room Detection Implementation
// ========================================

void URoomManagerComponent::AddBlockedConnection(TMap<uint64, TSet<uint64>>& BlockedMap, uint64 TriKeyA, uint64 TriKeyB)
{
	// Add bidirectional blocking - both A->B and B->A
	BlockedMap.FindOrAdd(TriKeyA).Add(TriKeyB);
	BlockedMap.FindOrAdd(TriKeyB).Add(TriKeyA);
}

bool URoomManagerComponent::IsConnectionBlocked(const TMap<uint64, TSet<uint64>>& BlockedMap, uint64 TriKeyA, uint64 TriKeyB)
{
	// Check if A is blocked from B (only need to check one direction since we store both)
	const TSet<uint64>* BlockedSet = BlockedMap.Find(TriKeyA);
	return BlockedSet && BlockedSet->Contains(TriKeyB);
}

void URoomManagerComponent::BuildWallBlockedMap(int32 Level, TMap<uint64, TSet<uint64>>& OutBlockedMap, TMap<uint64, TMap<uint64, int32>>& OutBlockedToWall)
{
	OutBlockedMap.Empty();
	OutBlockedToWall.Empty();

	if (!WallGraph || !LotManager)
	{
		return;
	}

	// Helper lambda to add blocked pair with wall ID tracking
	auto AddBlockedPair = [&OutBlockedMap, &OutBlockedToWall](uint64 KeyA, uint64 KeyB, int32 EdgeID)
	{
		// Add to blocked map (bidirectional)
		URoomManagerComponent::AddBlockedConnection(OutBlockedMap, KeyA, KeyB);

		// Track which wall blocks this pair (use smaller key as outer map key for consistency)
		uint64 MinKey = FMath::Min(KeyA, KeyB);
		uint64 MaxKey = FMath::Max(KeyA, KeyB);
		OutBlockedToWall.FindOrAdd(MinKey).Add(MaxKey, EdgeID);
	};

	// Get all edges at this level
	TArray<int32> LevelEdges = WallGraph->GetEdgesAtLevel(Level);
	int32 BlockedConnectionCount = 0;

	for (int32 EdgeID : LevelEdges)
	{
		const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
		if (!Edge) continue;

		const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
		const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
		if (!FromNode || !ToNode) continue;

		int32 FromRow = FromNode->Row;
		int32 FromCol = FromNode->Column;
		int32 ToRow = ToNode->Row;
		int32 ToCol = ToNode->Column;

		// Determine wall orientation and which triangles it blocks
		bool bIsHorizontal = (FromRow == ToRow);
		bool bIsVertical = (FromCol == ToCol);
		bool bIsDiagonal = !bIsHorizontal && !bIsVertical;

		if (bIsHorizontal)
		{
			// Horizontal wall: blocks Top <-> Bottom between adjacent rows
			// Wall at row R blocks Tile(R-1, C).Top <-> Tile(R, C).Bottom for each column C in wall span
			int32 MinCol = FMath::Min(FromCol, ToCol);
			int32 MaxCol = FMath::Max(FromCol, ToCol);
			int32 WallRow = FromRow;

			for (int32 Col = MinCol; Col < MaxCol; Col++)
			{
				// Only add if both tiles exist in the grid
				if (WallRow - 1 >= 0 && WallRow < LotManager->GridSizeY && Col >= 0 && Col < LotManager->GridSizeX)
				{
					FTriangleCoord TriAbove(WallRow - 1, Col, Level, ETriangleType::Top);
					FTriangleCoord TriBelow(WallRow, Col, Level, ETriangleType::Bottom);

					AddBlockedPair(TriAbove.GetPackedKey(), TriBelow.GetPackedKey(), EdgeID);
					BlockedConnectionCount++;
				}
			}
		}
		else if (bIsVertical)
		{
			// Vertical wall: blocks Left <-> Right between adjacent columns
			// Wall at col C blocks Tile(R, C-1).Right <-> Tile(R, C).Left for each row R in wall span
			int32 MinRow = FMath::Min(FromRow, ToRow);
			int32 MaxRow = FMath::Max(FromRow, ToRow);
			int32 WallCol = FromCol;

			for (int32 Row = MinRow; Row < MaxRow; Row++)
			{
				// Only add if both tiles exist in the grid
				if (WallCol - 1 >= 0 && WallCol < LotManager->GridSizeX && Row >= 0 && Row < LotManager->GridSizeY)
				{
					FTriangleCoord TriLeft(Row, WallCol - 1, Level, ETriangleType::Right);
					FTriangleCoord TriRight(Row, WallCol, Level, ETriangleType::Left);

					AddBlockedPair(TriLeft.GetPackedKey(), TriRight.GetPackedKey(), EdgeID);
					BlockedConnectionCount++;
				}
			}
		}
		else if (bIsDiagonal)
		{
			// Diagonal wall: blocks triangles within tiles that the diagonal passes through
			// A diagonal from (R1,C1) to (R2,C2) can span MULTIPLE tiles along the diagonal

			// Determine diagonal direction
			bool bFromTopLeft = (FromRow < ToRow && FromCol < ToCol) || (ToRow < FromRow && ToCol < FromCol);

			// Calculate the step direction
			int32 RowStep = (FromRow < ToRow) ? 1 : -1;
			int32 ColStep = (FromCol < ToCol) ? 1 : -1;

			// Number of tiles the diagonal passes through
			int32 NumTiles = FMath::Abs(ToRow - FromRow);

			// Iterate through each tile along the diagonal
			for (int32 i = 0; i < NumTiles; i++)
			{
				int32 TileRow = FMath::Min(FromRow + i * RowStep, FromRow + (i + 1) * RowStep);
				int32 TileCol = FMath::Min(FromCol + i * ColStep, FromCol + (i + 1) * ColStep);

				// Validate tile is in grid
				if (TileRow < 0 || TileRow >= LotManager->GridSizeY ||
				    TileCol < 0 || TileCol >= LotManager->GridSizeX)
				{
					continue;
				}

				FTriangleCoord TriTop(TileRow, TileCol, Level, ETriangleType::Top);
				FTriangleCoord TriRight(TileRow, TileCol, Level, ETriangleType::Right);
				FTriangleCoord TriLeft(TileRow, TileCol, Level, ETriangleType::Left);
				FTriangleCoord TriBottom(TileRow, TileCol, Level, ETriangleType::Bottom);

				if (bFromTopLeft)
				{
					// TopLeft to BottomRight diagonal (in rendering: BottomLeft to TopRight)
					// Separates {Top, Left} from {Right, Bottom}
					// Blocks: Top <-> Right and Bottom <-> Left (across the diagonal)
					AddBlockedPair(TriTop.GetPackedKey(), TriRight.GetPackedKey(), EdgeID);
					AddBlockedPair(TriBottom.GetPackedKey(), TriLeft.GetPackedKey(), EdgeID);
					BlockedConnectionCount += 2;
				}
				else
				{
					// TopRight to BottomLeft diagonal (in rendering: BottomRight to TopLeft)
					// Separates {Top, Right} from {Left, Bottom}
					// Blocks: Top <-> Left and Right <-> Bottom (across the diagonal)
					AddBlockedPair(TriTop.GetPackedKey(), TriLeft.GetPackedKey(), EdgeID);
					AddBlockedPair(TriRight.GetPackedKey(), TriBottom.GetPackedKey(), EdgeID);
					BlockedConnectionCount += 2;
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::BuildWallBlockedMap - Built blocked map for level %d: %d blocked connections from %d walls"),
		Level, BlockedConnectionCount, LevelEdges.Num());
}

// ========================================
// Incremental Blocked Map Cache Methods
// ========================================

void URoomManagerComponent::EnsureBlockedMapCached(int32 Level)
{
	if (ValidBlockedMapLevels.Contains(Level))
	{
		return; // Already cached and valid
	}

	// Build the blocked map for this level and cache it
	TMap<uint64, TSet<uint64>>& BlockedMap = CachedBlockedMaps.FindOrAdd(Level);
	TMap<uint64, TMap<uint64, int32>>& BlockedToWall = CachedBlockedToWall.FindOrAdd(Level);

	BuildWallBlockedMap(Level, BlockedMap, BlockedToWall);

	ValidBlockedMapLevels.Add(Level);

	UE_LOG(LogTemp, Log, TEXT("RoomManager::EnsureBlockedMapCached - Cached blocked map for level %d"), Level);
}

void URoomManagerComponent::InvalidateBlockedMapCache(int32 Level)
{
	ValidBlockedMapLevels.Remove(Level);
	UE_LOG(LogTemp, Log, TEXT("RoomManager::InvalidateBlockedMapCache - Invalidated cache for level %d"), Level);
}

void URoomManagerComponent::IncrementalAddWallToBlockedMap(int32 EdgeID, TArray<FTriangleCoord>& OutAffectedTriangles)
{
	OutAffectedTriangles.Empty();

	if (!WallGraph || !LotManager)
	{
		return;
	}

	const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
	if (!Edge)
	{
		return;
	}

	const FWallNode* FromNode = WallGraph->Nodes.Find(Edge->FromNodeID);
	const FWallNode* ToNode = WallGraph->Nodes.Find(Edge->ToNodeID);
	if (!FromNode || !ToNode)
	{
		return;
	}

	int32 Level = Edge->Level;

	// Ensure we have a cached blocked map for this level
	EnsureBlockedMapCached(Level);

	TMap<uint64, TSet<uint64>>& BlockedMap = CachedBlockedMaps.FindOrAdd(Level);
	TMap<uint64, TMap<uint64, int32>>& BlockedToWall = CachedBlockedToWall.FindOrAdd(Level);

	int32 FromRow = FromNode->Row;
	int32 FromCol = FromNode->Column;
	int32 ToRow = ToNode->Row;
	int32 ToCol = ToNode->Column;

	// Helper lambda to add blocked pair
	auto AddBlockedPair = [&BlockedMap, &BlockedToWall, &OutAffectedTriangles, EdgeID](const FTriangleCoord& TriA, const FTriangleCoord& TriB)
	{
		uint64 KeyA = TriA.GetPackedKey();
		uint64 KeyB = TriB.GetPackedKey();

		// Add to blocked map (bidirectional)
		AddBlockedConnection(BlockedMap, KeyA, KeyB);

		// Track which wall blocks this pair
		uint64 MinKey = FMath::Min(KeyA, KeyB);
		uint64 MaxKey = FMath::Max(KeyA, KeyB);
		BlockedToWall.FindOrAdd(MinKey).Add(MaxKey, EdgeID);

		// Track affected triangles (without duplicates)
		OutAffectedTriangles.AddUnique(TriA);
		OutAffectedTriangles.AddUnique(TriB);
	};

	// Determine wall orientation and add blocked connections
	bool bIsHorizontal = (FromRow == ToRow);
	bool bIsVertical = (FromCol == ToCol);
	bool bIsDiagonal = !bIsHorizontal && !bIsVertical;

	if (bIsHorizontal)
	{
		int32 MinCol = FMath::Min(FromCol, ToCol);
		int32 MaxCol = FMath::Max(FromCol, ToCol);
		int32 WallRow = FromRow;

		for (int32 Col = MinCol; Col < MaxCol; Col++)
		{
			if (WallRow - 1 >= 0 && WallRow < LotManager->GridSizeY && Col >= 0 && Col < LotManager->GridSizeX)
			{
				FTriangleCoord TriAbove(WallRow - 1, Col, Level, ETriangleType::Top);
				FTriangleCoord TriBelow(WallRow, Col, Level, ETriangleType::Bottom);
				AddBlockedPair(TriAbove, TriBelow);
			}
		}
	}
	else if (bIsVertical)
	{
		int32 MinRow = FMath::Min(FromRow, ToRow);
		int32 MaxRow = FMath::Max(FromRow, ToRow);
		int32 WallCol = FromCol;

		for (int32 Row = MinRow; Row < MaxRow; Row++)
		{
			if (WallCol - 1 >= 0 && WallCol < LotManager->GridSizeX && Row >= 0 && Row < LotManager->GridSizeY)
			{
				FTriangleCoord TriLeft(Row, WallCol - 1, Level, ETriangleType::Right);
				FTriangleCoord TriRight(Row, WallCol, Level, ETriangleType::Left);
				AddBlockedPair(TriLeft, TriRight);
			}
		}
	}
	else if (bIsDiagonal)
	{
		bool bFromTopLeft = (FromRow < ToRow && FromCol < ToCol) || (ToRow < FromRow && ToCol < FromCol);
		int32 RowStep = (FromRow < ToRow) ? 1 : -1;
		int32 ColStep = (FromCol < ToCol) ? 1 : -1;
		int32 NumTiles = FMath::Abs(ToRow - FromRow);

		for (int32 i = 0; i < NumTiles; i++)
		{
			int32 TileRow = FMath::Min(FromRow + i * RowStep, FromRow + (i + 1) * RowStep);
			int32 TileCol = FMath::Min(FromCol + i * ColStep, FromCol + (i + 1) * ColStep);

			if (TileRow < 0 || TileRow >= LotManager->GridSizeY ||
			    TileCol < 0 || TileCol >= LotManager->GridSizeX)
			{
				continue;
			}

			FTriangleCoord TriTop(TileRow, TileCol, Level, ETriangleType::Top);
			FTriangleCoord TriRight(TileRow, TileCol, Level, ETriangleType::Right);
			FTriangleCoord TriLeft(TileRow, TileCol, Level, ETriangleType::Left);
			FTriangleCoord TriBottom(TileRow, TileCol, Level, ETriangleType::Bottom);

			if (bFromTopLeft)
			{
				AddBlockedPair(TriTop, TriRight);
				AddBlockedPair(TriBottom, TriLeft);
			}
			else
			{
				AddBlockedPair(TriTop, TriLeft);
				AddBlockedPair(TriRight, TriBottom);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::IncrementalAddWallToBlockedMap - Added edge %d, affected %d triangles"),
		EdgeID, OutAffectedTriangles.Num());
}

void URoomManagerComponent::IncrementalRemoveWallFromBlockedMap(int32 EdgeID, TArray<FTriangleCoord>& OutAffectedTriangles)
{
	OutAffectedTriangles.Empty();

	if (!WallGraph || !LotManager)
	{
		return;
	}

	const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
	if (!Edge)
	{
		return;
	}

	int32 Level = Edge->Level;

	// If no cached map exists, nothing to remove from
	if (!ValidBlockedMapLevels.Contains(Level))
	{
		return;
	}

	TMap<uint64, TSet<uint64>>* BlockedMapPtr = CachedBlockedMaps.Find(Level);
	TMap<uint64, TMap<uint64, int32>>* BlockedToWallPtr = CachedBlockedToWall.Find(Level);

	if (!BlockedMapPtr || !BlockedToWallPtr)
	{
		return;
	}

	TMap<uint64, TSet<uint64>>& BlockedMap = *BlockedMapPtr;
	TMap<uint64, TMap<uint64, int32>>& BlockedToWall = *BlockedToWallPtr;

	// Find all blocked pairs that this edge created and remove them
	TArray<TPair<uint64, uint64>> PairsToRemove;

	for (auto& OuterPair : BlockedToWall)
	{
		uint64 MinKey = OuterPair.Key;
		for (auto& InnerPair : OuterPair.Value)
		{
			if (InnerPair.Value == EdgeID)
			{
				uint64 MaxKey = InnerPair.Key;
				PairsToRemove.Add(TPair<uint64, uint64>(MinKey, MaxKey));

				// Track affected triangles
				OutAffectedTriangles.AddUnique(FTriangleCoord::FromPackedKey(MinKey));
				OutAffectedTriangles.AddUnique(FTriangleCoord::FromPackedKey(MaxKey));
			}
		}
	}

	// Remove the blocked connections
	for (const auto& Pair : PairsToRemove)
	{
		uint64 KeyA = Pair.Key;
		uint64 KeyB = Pair.Value;

		// Remove from blocked map (bidirectional)
		if (TSet<uint64>* SetA = BlockedMap.Find(KeyA))
		{
			SetA->Remove(KeyB);
			if (SetA->Num() == 0)
			{
				BlockedMap.Remove(KeyA);
			}
		}
		if (TSet<uint64>* SetB = BlockedMap.Find(KeyB))
		{
			SetB->Remove(KeyA);
			if (SetB->Num() == 0)
			{
				BlockedMap.Remove(KeyB);
			}
		}

		// Remove from tracking map
		if (TMap<uint64, int32>* InnerMap = BlockedToWall.Find(KeyA))
		{
			InnerMap->Remove(KeyB);
			if (InnerMap->Num() == 0)
			{
				BlockedToWall.Remove(KeyA);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::IncrementalRemoveWallFromBlockedMap - Removed edge %d, affected %d triangles"),
		EdgeID, OutAffectedTriangles.Num());
}

int32 URoomManagerComponent::DetectRoomsFromAffectedTriangles(int32 Level, const TArray<FTriangleCoord>& AffectedTriangles, TArray<int32>& OutDetectedRoomIDs)
{
	OutDetectedRoomIDs.Empty();

	if (!WallGraph || !LotManager || AffectedTriangles.Num() == 0)
	{
		return 0;
	}

	// Ensure blocked map is cached
	EnsureBlockedMapCached(Level);

	const TMap<uint64, TSet<uint64>>* BlockedMapPtr = CachedBlockedMaps.Find(Level);
	const TMap<uint64, TMap<uint64, int32>>* BlockedToWallPtr = CachedBlockedToWall.Find(Level);

	if (!BlockedMapPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("RoomManager::DetectRoomsFromAffectedTriangles - No blocked map for level %d"), Level);
		return 0;
	}

	const TMap<uint64, TSet<uint64>>& BlockedMap = *BlockedMapPtr;
	const TMap<uint64, TMap<uint64, int32>>& BlockedToWall = BlockedToWallPtr ? *BlockedToWallPtr : TMap<uint64, TMap<uint64, int32>>();

	// Increment generation for this detection pass
	CurrentGeneration++;

	// Track visited triangles for this local detection
	TSet<uint64> LocalVisited;

	// Regions detected from the affected area
	TArray<TArray<FTriangleCoord>> DetectedRegions;

	// Flood fill from each affected triangle that hasn't been visited yet
	for (const FTriangleCoord& StartTri : AffectedTriangles)
	{
		uint64 StartKey = StartTri.GetPackedKey();

		if (LocalVisited.Contains(StartKey))
		{
			continue;
		}

		// Flood fill from this triangle
		TArray<FTriangleCoord> Region;
		FloodFillFromTriangle(StartTri, BlockedMap, LocalVisited, Region);

		if (Region.Num() > 0)
		{
			DetectedRegions.Add(MoveTemp(Region));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectRoomsFromAffectedTriangles - Flood fill from %d triangles found %d regions"),
		AffectedTriangles.Num(), DetectedRegions.Num());

	// Process each detected region
	for (const TArray<FTriangleCoord>& Region : DetectedRegions)
	{
		// Check if this region is exterior (touches grid boundary)
		if (IsRegionExterior(Region))
		{
			// Exterior region - clear room assignments for these triangles
			for (const FTriangleCoord& Tri : Region)
			{
				uint64 TriKey = Tri.GetPackedKey();
				int32* ExistingRoomID = TriangleToRoomMap.Find(TriKey);
				if (ExistingRoomID && *ExistingRoomID > 0)
				{
					// This triangle was in a room but is now exterior
					// The room needs invalidation (handled below)
					TriangleToRoomMap.Remove(TriKey);

					// Update GridData
					FIntVector TileCoord = Tri.GetTileCoord();
					int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
					if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
					{
						LotManager->GridData[*TileIndexPtr].TriangleOwnership.SetTriangleRoom(Tri.Triangle, 0);
					}
				}
			}
			continue;
		}

		// Skip very small regions
		if (Region.Num() < 4)
		{
			continue;
		}

		// Check if this region overlaps with an existing room
		TSet<int32> OverlappingRoomIDs;
		for (const FTriangleCoord& Tri : Region)
		{
			int32* ExistingRoomID = TriangleToRoomMap.Find(Tri.GetPackedKey());
			if (ExistingRoomID && *ExistingRoomID > 0)
			{
				OverlappingRoomIDs.Add(*ExistingRoomID);
			}
		}

		if (OverlappingRoomIDs.Num() == 0)
		{
			// New room - no overlap with existing rooms
			int32 NewRoomID = GetNextRoomID();
			FRoomData NewRoom;
			NewRoom.RoomID = NewRoomID;
			NewRoom.Level = Level;
			NewRoom.bIsValidRoom = true;
			NewRoom.GenerationNumber = CurrentGeneration;
			NewRoom.InteriorTriangles = Region;
			NewRoom.RebuildInteriorTilesFromTriangles();

			// Derive boundary edges
			TArray<int32> UnorderedBoundaryEdges;
			TSet<uint64> RegionSet;
			for (const FTriangleCoord& Tri : Region)
			{
				RegionSet.Add(Tri.GetPackedKey());
			}
			DeriveBoundaryFromRegion(Region, BlockedToWall, RegionSet, UnorderedBoundaryEdges);
			NewRoom.BoundaryEdges = SortBoundaryEdgesIntoSequence(UnorderedBoundaryEdges);
			NewRoom.BoundaryVertices = EdgesToVertices(NewRoom.BoundaryEdges);

			// Calculate centroid
			if (NewRoom.BoundaryVertices.Num() >= 3)
			{
				NewRoom.Centroid = CalculatePolygonCentroid(NewRoom.BoundaryVertices);
			}

			// Populate spatial caches
			for (const FTriangleCoord& Tri : NewRoom.InteriorTriangles)
			{
				TriangleToRoomMap.Add(Tri.GetPackedKey(), NewRoomID);

				FIntVector TileCoord = Tri.GetTileCoord();
				int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
				if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
				{
					LotManager->GridData[*TileIndexPtr].TriangleOwnership.SetTriangleRoom(Tri.Triangle, NewRoomID);
				}
			}

			// Update TileToRoomMap
			for (const FIntVector& TileCoord : NewRoom.InteriorTiles)
			{
				TileToRoomMap.Add(TileCoord, NewRoomID);
			}

			Rooms.Add(NewRoomID, MoveTemp(NewRoom));
			OutDetectedRoomIDs.Add(NewRoomID);

			UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectRoomsFromAffectedTriangles - Created new room %d with %d triangles"),
				NewRoomID, Region.Num());
		}
		else if (OverlappingRoomIDs.Num() == 1)
		{
			// Region overlaps with exactly one existing room
			// This could be the same room (unchanged) or a modified room
			int32 ExistingRoomID = OverlappingRoomIDs.Array()[0];
			FRoomData* ExistingRoom = Rooms.Find(ExistingRoomID);

			if (ExistingRoom)
			{
				// Check if the room has actually changed
				if (ExistingRoom->InteriorTriangles.Num() != Region.Num())
				{
					// Room was split - this region is a subset
					// Remove old room data from spatial caches
					for (const FTriangleCoord& OldTri : ExistingRoom->InteriorTriangles)
					{
						TriangleToRoomMap.Remove(OldTri.GetPackedKey());
					}
					for (const FIntVector& OldTile : ExistingRoom->InteriorTiles)
					{
						TileToRoomMap.Remove(OldTile);
					}

					// Update room with new region
					ExistingRoom->InteriorTriangles = Region;
					ExistingRoom->RebuildInteriorTilesFromTriangles();
					ExistingRoom->GenerationNumber = CurrentGeneration;

					// Re-derive boundary
					TArray<int32> UnorderedBoundaryEdges;
					TSet<uint64> RegionSet;
					for (const FTriangleCoord& Tri : Region)
					{
						RegionSet.Add(Tri.GetPackedKey());
					}
					DeriveBoundaryFromRegion(Region, BlockedToWall, RegionSet, UnorderedBoundaryEdges);
					ExistingRoom->BoundaryEdges = SortBoundaryEdgesIntoSequence(UnorderedBoundaryEdges);
					ExistingRoom->BoundaryVertices = EdgesToVertices(ExistingRoom->BoundaryEdges);

					if (ExistingRoom->BoundaryVertices.Num() >= 3)
					{
						ExistingRoom->Centroid = CalculatePolygonCentroid(ExistingRoom->BoundaryVertices);
					}

					// Re-populate spatial caches
					for (const FTriangleCoord& Tri : ExistingRoom->InteriorTriangles)
					{
						TriangleToRoomMap.Add(Tri.GetPackedKey(), ExistingRoomID);

						FIntVector TileCoord = Tri.GetTileCoord();
						int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
						if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
						{
							LotManager->GridData[*TileIndexPtr].TriangleOwnership.SetTriangleRoom(Tri.Triangle, ExistingRoomID);
						}
					}

					for (const FIntVector& TileCoord : ExistingRoom->InteriorTiles)
					{
						TileToRoomMap.Add(TileCoord, ExistingRoomID);
					}

					OutDetectedRoomIDs.Add(ExistingRoomID);

					UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectRoomsFromAffectedTriangles - Updated room %d (now %d triangles)"),
						ExistingRoomID, Region.Num());
				}
				// else: Room unchanged, no action needed
			}
		}
		else
		{
			// Region overlaps multiple rooms - this shouldn't happen with correct wall placement
			// but handle it by creating a new room from the merged region
			UE_LOG(LogTemp, Warning, TEXT("RoomManager::DetectRoomsFromAffectedTriangles - Region overlaps %d rooms, treating as merge"),
				OverlappingRoomIDs.Num());

			// Remove overlapping rooms
			for (int32 OldRoomID : OverlappingRoomIDs)
			{
				FRoomData* OldRoom = Rooms.Find(OldRoomID);
				if (OldRoom)
				{
					for (const FTriangleCoord& OldTri : OldRoom->InteriorTriangles)
					{
						TriangleToRoomMap.Remove(OldTri.GetPackedKey());
					}
					for (const FIntVector& OldTile : OldRoom->InteriorTiles)
					{
						TileToRoomMap.Remove(OldTile);
					}
				}
				Rooms.Remove(OldRoomID);
				RecycleRoomID(OldRoomID);
			}

			// Create merged room
			int32 NewRoomID = GetNextRoomID();
			FRoomData NewRoom;
			NewRoom.RoomID = NewRoomID;
			NewRoom.Level = Level;
			NewRoom.bIsValidRoom = true;
			NewRoom.GenerationNumber = CurrentGeneration;
			NewRoom.InteriorTriangles = Region;
			NewRoom.RebuildInteriorTilesFromTriangles();

			TArray<int32> UnorderedBoundaryEdges;
			TSet<uint64> RegionSet;
			for (const FTriangleCoord& Tri : Region)
			{
				RegionSet.Add(Tri.GetPackedKey());
			}
			DeriveBoundaryFromRegion(Region, BlockedToWall, RegionSet, UnorderedBoundaryEdges);
			NewRoom.BoundaryEdges = SortBoundaryEdgesIntoSequence(UnorderedBoundaryEdges);
			NewRoom.BoundaryVertices = EdgesToVertices(NewRoom.BoundaryEdges);

			if (NewRoom.BoundaryVertices.Num() >= 3)
			{
				NewRoom.Centroid = CalculatePolygonCentroid(NewRoom.BoundaryVertices);
			}

			for (const FTriangleCoord& Tri : NewRoom.InteriorTriangles)
			{
				TriangleToRoomMap.Add(Tri.GetPackedKey(), NewRoomID);

				FIntVector TileCoord = Tri.GetTileCoord();
				int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileCoord);
				if (TileIndexPtr && LotManager->GridData.IsValidIndex(*TileIndexPtr))
				{
					LotManager->GridData[*TileIndexPtr].TriangleOwnership.SetTriangleRoom(Tri.Triangle, NewRoomID);
				}
			}

			for (const FIntVector& TileCoord : NewRoom.InteriorTiles)
			{
				TileToRoomMap.Add(TileCoord, NewRoomID);
			}

			Rooms.Add(NewRoomID, MoveTemp(NewRoom));
			OutDetectedRoomIDs.Add(NewRoomID);

			UE_LOG(LogTemp, Log, TEXT("RoomManager::DetectRoomsFromAffectedTriangles - Created merged room %d with %d triangles"),
				NewRoomID, Region.Num());
		}
	}

	return OutDetectedRoomIDs.Num();
}

int32 URoomManagerComponent::HandleWallRemovalDetection(int32 EdgeID, int32 Level, TArray<int32>& OutMergedRoomIDs)
{
	OutMergedRoomIDs.Empty();

	// Get affected triangles from wall removal
	TArray<FTriangleCoord> AffectedTriangles;
	IncrementalRemoveWallFromBlockedMap(EdgeID, AffectedTriangles);

	if (AffectedTriangles.Num() == 0)
	{
		return 0;
	}

	// Find rooms that were adjacent to this wall
	TSet<int32> AdjacentRoomIDs;
	for (const FTriangleCoord& Tri : AffectedTriangles)
	{
		int32* RoomIDPtr = TriangleToRoomMap.Find(Tri.GetPackedKey());
		if (RoomIDPtr && *RoomIDPtr > 0)
		{
			AdjacentRoomIDs.Add(*RoomIDPtr);
		}
	}

	if (AdjacentRoomIDs.Num() < 2)
	{
		// Wall wasn't between two different rooms, no merge needed
		return 0;
	}

	// Invalidate the adjacent rooms - they may have merged
	for (int32 RoomID : AdjacentRoomIDs)
	{
		FRoomData* Room = Rooms.Find(RoomID);
		if (Room)
		{
			// Remove room data from spatial caches
			for (const FTriangleCoord& Tri : Room->InteriorTriangles)
			{
				TriangleToRoomMap.Remove(Tri.GetPackedKey());
			}
			for (const FIntVector& TileCoord : Room->InteriorTiles)
			{
				TileToRoomMap.Remove(TileCoord);
			}
		}
		Rooms.Remove(RoomID);
		RecycleRoomID(RoomID);
	}

	// Detect rooms from affected area
	return DetectRoomsFromAffectedTriangles(Level, AffectedTriangles, OutMergedRoomIDs);
}

void URoomManagerComponent::GetTriangleNeighbors(const FTriangleCoord& Coord, TArray<FTriangleCoord>& OutNeighbors)
{
	OutNeighbors.Empty();

	// Each triangle has up to 3 neighbors:
	// - 2 within the same tile (share the center vertex)
	// - 1 in an adjacent tile (share an edge)

	switch (Coord.Triangle)
	{
	case ETriangleType::Top:
		// Same tile neighbors: Left, Right
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Left));
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Right));
		// Adjacent tile neighbor: Top = +Row direction, so neighbor is Row+1's Bottom triangle
		if (LotManager && Coord.Row < LotManager->GridSizeY - 1)
		{
			OutNeighbors.Add(FTriangleCoord(Coord.Row + 1, Coord.Column, Coord.Level, ETriangleType::Bottom));
		}
		break;

	case ETriangleType::Right:
		// Same tile neighbors: Top, Bottom
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Top));
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Bottom));
		// Adjacent tile neighbor: Tile to the right (Col+1), Left triangle
		if (LotManager && Coord.Column < LotManager->GridSizeX - 1)
		{
			OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column + 1, Coord.Level, ETriangleType::Left));
		}
		break;

	case ETriangleType::Bottom:
		// Same tile neighbors: Left, Right
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Left));
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Right));
		// Adjacent tile neighbor: Bottom = -Row direction, so neighbor is Row-1's Top triangle
		if (Coord.Row > 0)
		{
			OutNeighbors.Add(FTriangleCoord(Coord.Row - 1, Coord.Column, Coord.Level, ETriangleType::Top));
		}
		break;

	case ETriangleType::Left:
		// Same tile neighbors: Top, Bottom
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Top));
		OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column, Coord.Level, ETriangleType::Bottom));
		// Adjacent tile neighbor: Tile to the left (Col-1), Right triangle
		if (Coord.Column > 0)
		{
			OutNeighbors.Add(FTriangleCoord(Coord.Row, Coord.Column - 1, Coord.Level, ETriangleType::Right));
		}
		break;
	}
}

void URoomManagerComponent::FloodFillFromTriangle(const FTriangleCoord& Start, const TMap<uint64, TSet<uint64>>& BlockedMap,
                                                   TSet<uint64>& Visited, TArray<FTriangleCoord>& OutRegion)
{
	OutRegion.Empty();

	TQueue<FTriangleCoord> Queue;
	Queue.Enqueue(Start);

	while (!Queue.IsEmpty())
	{
		FTriangleCoord Current;
		Queue.Dequeue(Current);

		uint64 CurrentKey = Current.GetPackedKey();

		// Skip if already visited
		if (Visited.Contains(CurrentKey))
		{
			continue;
		}

		// Mark as visited and add to region
		Visited.Add(CurrentKey);
		OutRegion.Add(Current);

		// Get neighbors
		TArray<FTriangleCoord> Neighbors;
		GetTriangleNeighbors(Current, Neighbors);

		for (const FTriangleCoord& Neighbor : Neighbors)
		{
			uint64 NeighborKey = Neighbor.GetPackedKey();

			// Skip if already visited
			if (Visited.Contains(NeighborKey))
			{
				continue;
			}

			// Check if connection is blocked by a wall (using collision-free lookup)
			if (IsConnectionBlocked(BlockedMap, CurrentKey, NeighborKey))
			{
				// Wall blocks this connection - don't add to queue
				continue;
			}

			// Connection is open - add to queue
			Queue.Enqueue(Neighbor);
		}
	}
}

bool URoomManagerComponent::IsRegionExterior(const TArray<FTriangleCoord>& Region)
{
	if (!LotManager)
	{
		return true;  // Can't determine, assume exterior
	}

	int32 MaxRow = LotManager->GridSizeY - 1;
	int32 MaxCol = LotManager->GridSizeX - 1;

	// A region is exterior if ANY of its triangles are in a tile at the grid boundary
	// This is simple and robust - interior rooms are fully enclosed by walls,
	// so they can't reach the grid boundary
	for (const FTriangleCoord& Tri : Region)
	{
		if (Tri.Row == 0 || Tri.Row == MaxRow || Tri.Column == 0 || Tri.Column == MaxCol)
		{
			return true;
		}
	}

	return false;
}

bool URoomManagerComponent::DoesRegionContain(const TArray<FTriangleCoord>& RegionA, const TArray<FTriangleCoord>& RegionB)
{
	// Build a set from RegionA for O(1) lookup
	TSet<uint64> RegionASet;
	for (const FTriangleCoord& Tri : RegionA)
	{
		RegionASet.Add(Tri.GetPackedKey());
	}

	// Check if all triangles of RegionB are in RegionA
	for (const FTriangleCoord& Tri : RegionB)
	{
		if (!RegionASet.Contains(Tri.GetPackedKey()))
		{
			return false;
		}
	}

	return true;
}

void URoomManagerComponent::FilterNestedRegions(TArray<TArray<FTriangleCoord>>& Regions)
{
	if (Regions.Num() <= 1)
	{
		return;
	}

	// Sort by size (smallest first)
	Regions.Sort([](const TArray<FTriangleCoord>& A, const TArray<FTriangleCoord>& B)
	{
		return A.Num() < B.Num();
	});

	TArray<TArray<FTriangleCoord>> Kept;

	for (const TArray<FTriangleCoord>& Candidate : Regions)
	{
		// Check if this region contains any already-kept region
		bool bContainsKept = false;

		for (const TArray<FTriangleCoord>& Existing : Kept)
		{
			if (DoesRegionContain(Candidate, Existing))
			{
				bContainsKept = true;
				break;
			}
		}

		// Keep this region if it doesn't contain any already-kept regions
		if (!bContainsKept)
		{
			Kept.Add(Candidate);
		}
	}

	Regions = MoveTemp(Kept);

	UE_LOG(LogTemp, Log, TEXT("RoomManager::FilterNestedRegions - Filtered to %d regions"), Regions.Num());
}

void URoomManagerComponent::DeriveBoundaryFromRegion(const TArray<FTriangleCoord>& Region,
                                                      const TMap<uint64, TMap<uint64, int32>>& BlockedToWall,
                                                      const TSet<uint64>& AllVisited,
                                                      TArray<int32>& OutBoundaryEdges)
{
	OutBoundaryEdges.Empty();

	// Build set of triangles in this region for O(1) lookup
	TSet<uint64> RegionSet;
	for (const FTriangleCoord& Tri : Region)
	{
		RegionSet.Add(Tri.GetPackedKey());
	}

	TSet<int32> BoundaryEdgeSet;

	// For each triangle in region, check its neighbors
	for (const FTriangleCoord& Tri : Region)
	{
		uint64 TriKey = Tri.GetPackedKey();

		TArray<FTriangleCoord> Neighbors;
		GetTriangleNeighbors(Tri, Neighbors);

		for (const FTriangleCoord& Neighbor : Neighbors)
		{
			uint64 NeighborKey = Neighbor.GetPackedKey();

			// If neighbor is NOT in this region, there should be a wall between them
			if (!RegionSet.Contains(NeighborKey))
			{
				// Look up the wall that blocks this connection
				// Use consistent key ordering (min key as outer map key)
				uint64 MinKey = FMath::Min(TriKey, NeighborKey);
				uint64 MaxKey = FMath::Max(TriKey, NeighborKey);

				const TMap<uint64, int32>* InnerMap = BlockedToWall.Find(MinKey);
				if (InnerMap)
				{
					const int32* WallEdgePtr = InnerMap->Find(MaxKey);
					if (WallEdgePtr)
					{
						BoundaryEdgeSet.Add(*WallEdgePtr);
					}
				}
			}
		}
	}

	OutBoundaryEdges = BoundaryEdgeSet.Array();
}

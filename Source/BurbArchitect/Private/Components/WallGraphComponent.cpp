// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/WallGraphComponent.h"
#include "Components/WallComponent.h"
#include "Components/RoomManagerComponent.h"
#include "Actors/LotManager.h"
#include "BurbArchitectDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

UWallGraphComponent::UWallGraphComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bVisualizeComponent = true;
}

void UWallGraphComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                        FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// No-op: Wall graph is passive data structure
}

void UWallGraphComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	// Initialize graph
	ClearGraph();
}

void UWallGraphComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ClearGraph();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

// ========================================
// Core Operations
// ========================================

int32 UWallGraphComponent::AddNode(const FVector& Position, int32 Level, int32 Row, int32 Column)
{
	// Check if node already exists at this position
	int32 ExistingNodeID = FindNodeAt(Row, Column, Level);
	if (ExistingNodeID != -1)
	{
		return ExistingNodeID; // Return existing node
	}

	// Create new node
	int32 NewNodeID = NextNodeID++;
	FWallNode NewNode(NewNodeID, Position, Level, Row, Column);

	Nodes.Add(NewNodeID, NewNode);
	AddNodeToSpatialIndex(NewNodeID);

	UE_LOG(LogTemp, Log, TEXT("WallGraph: Added node %d at (%d, %d, %d)"), NewNodeID, Row, Column, Level);

	return NewNodeID;
}

int32 UWallGraphComponent::FindNodeAt(int32 Row, int32 Column, int32 Level) const
{
	int64 SpatialKey = GetSpatialKey(Row, Column, Level);
	TArray<int32> NodeIDs;
	TileToNodes.MultiFind(SpatialKey, NodeIDs);

	for (int32 NodeID : NodeIDs)
	{
		const FWallNode* Node = Nodes.Find(NodeID);
		if (Node && Node->Row == Row && Node->Column == Column && Node->Level == Level)
		{
			return NodeID;
		}
	}

	return -1;
}

int32 UWallGraphComponent::AddEdge(int32 FromNodeID, int32 ToNodeID, float Height, float Thickness, UWallPattern* Pattern)
{
	// Validate nodes exist
	const FWallNode* FromNode = Nodes.Find(FromNodeID);
	const FWallNode* ToNode = Nodes.Find(ToNodeID);

	if (!FromNode || !ToNode)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph: Cannot add edge - invalid node IDs (%d, %d)"), FromNodeID, ToNodeID);
		return -1;
	}

	// Nodes must be on same level
	if (FromNode->Level != ToNode->Level)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph: Cannot add edge - nodes on different levels (%d vs %d)"),
		       FromNode->Level, ToNode->Level);
		return -1;
	}

	// NORMALIZE EDGE DIRECTION
	// Convention: Edges should always go from "lower" to "higher" coordinate
	// Lower = smaller Row, or if same Row, smaller Column
	// This ensures consistent PosY/NegY face orientation regardless of how user drew the wall
	bool bShouldSwap = false;
	if (FromNode->Row > ToNode->Row)
	{
		bShouldSwap = true;
	}
	else if (FromNode->Row == ToNode->Row && FromNode->Column > ToNode->Column)
	{
		bShouldSwap = true;
	}

	if (bShouldSwap)
	{
		// Swap node IDs to normalize direction
		Swap(FromNodeID, ToNodeID);
		FromNode = Nodes.Find(FromNodeID);
		ToNode = Nodes.Find(ToNodeID);
		UE_LOG(LogTemp, Log, TEXT("WallGraph: Normalized edge direction (swapped nodes)"));
	}

	// Check if edge already exists (check both directions since we normalized)
	int32 ExistingEdgeID = FindEdgeBetweenNodes(FromNodeID, ToNodeID);
	if (ExistingEdgeID != -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallGraph: Edge already exists between nodes %d and %d"), FromNodeID, ToNodeID);
		return ExistingEdgeID;
	}

	// Create new edge
	int32 NewEdgeID = NextEdgeID++;
	FWallEdge NewEdge;
	NewEdge.EdgeID = NewEdgeID;
	NewEdge.FromNodeID = FromNodeID;
	NewEdge.ToNodeID = ToNodeID;
	NewEdge.Level = FromNode->Level;
	NewEdge.Height = Height;
	NewEdge.EndHeight = Height;
	NewEdge.Thickness = Thickness;
	NewEdge.Pattern = Pattern;
	NewEdge.bCommitted = true;

	// Cache grid coordinates (using normalized direction)
	NewEdge.StartRow = FromNode->Row;
	NewEdge.StartColumn = FromNode->Column;
	NewEdge.EndRow = ToNode->Row;
	NewEdge.EndColumn = ToNode->Column;

	// Add to graph
	Edges.Add(NewEdgeID, NewEdge);

	// Update node connectivity
	FWallNode* MutableFromNode = Nodes.Find(FromNodeID);
	FWallNode* MutableToNode = Nodes.Find(ToNodeID);
	MutableFromNode->ConnectedEdgeIDs.Add(NewEdgeID);
	MutableToNode->ConnectedEdgeIDs.Add(NewEdgeID);

	// Add to spatial index
	AddToSpatialIndex(NewEdgeID);

	// Add to level index for fast GetEdgesAtLevel() queries
	TSet<int32>& LevelEdges = EdgesByLevel.FindOrAdd(FromNode->Level);
	LevelEdges.Add(NewEdgeID);

	UE_LOG(LogTemp, Log, TEXT("WallGraph: Added edge %d from node %d (%d,%d) to node %d (%d,%d)"),
	       NewEdgeID, FromNodeID, FromNode->Row, FromNode->Column, ToNodeID, ToNode->Row, ToNode->Column);

	// DEBUG: Visualize nodes with spheres colored by connection count
	if (BurbArchitectDebug::IsWallDebugEnabled() && GetWorld())
	{
		// From node
		int32 FromEdgeCount = MutableFromNode->ConnectedEdgeIDs.Num();
		FColor FromColor = FromEdgeCount == 1 ? FColor::White : (FromEdgeCount == 2 ? FColor::Green : FColor::Red);
		DrawDebugSphere(GetWorld(), FromNode->Position + FVector(0,0,25), 15.0f, 8, FromColor, false, 30.0f);

		if (FromEdgeCount > 2)
		{
			UE_LOG(LogTemp, Error, TEXT("WallGraph: Node %d at (%d,%d,%d) has %d edges (PHANTOM EDGES!):"),
				FromNodeID, FromNode->Row, FromNode->Column, FromNode->Level, FromEdgeCount);
			for (int32 EdgeID : MutableFromNode->ConnectedEdgeIDs)
			{
				const FWallEdge* Edge = Edges.Find(EdgeID);
				if (Edge)
				{
					UE_LOG(LogTemp, Error, TEXT("  Edge %d: (%d,%d) -> (%d,%d)"),
						EdgeID, Edge->StartRow, Edge->StartColumn, Edge->EndRow, Edge->EndColumn);
				}
			}
		}

		// To node
		int32 ToEdgeCount = MutableToNode->ConnectedEdgeIDs.Num();
		FColor ToColor = ToEdgeCount == 1 ? FColor::White : (ToEdgeCount == 2 ? FColor::Green : FColor::Red);
		DrawDebugSphere(GetWorld(), ToNode->Position + FVector(0,0,25), 15.0f, 8, ToColor, false, 30.0f);

		if (ToEdgeCount > 2)
		{
			UE_LOG(LogTemp, Error, TEXT("WallGraph: Node %d at (%d,%d,%d) has %d edges (PHANTOM EDGES!):"),
				ToNodeID, ToNode->Row, ToNode->Column, ToNode->Level, ToEdgeCount);
			for (int32 EdgeID : MutableToNode->ConnectedEdgeIDs)
			{
				const FWallEdge* Edge = Edges.Find(EdgeID);
				if (Edge)
				{
					UE_LOG(LogTemp, Error, TEXT("  Edge %d: (%d,%d) -> (%d,%d)"),
						EdgeID, Edge->StartRow, Edge->StartColumn, Edge->EndRow, Edge->EndColumn);
				}
			}
		}
	}

	InvalidateWallRunCache();
	return NewEdgeID;
}

bool UWallGraphComponent::RemoveEdge(int32 EdgeID)
{
	FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
	{
		return false;
	}

	// Remove from level index
	TSet<int32>* LevelEdges = EdgesByLevel.Find(Edge->Level);
	if (LevelEdges)
	{
		LevelEdges->Remove(EdgeID);
	}

	// Remove from spatial index
	RemoveFromSpatialIndex(EdgeID);

	// Remove from node connectivity
	FWallNode* FromNode = Nodes.Find(Edge->FromNodeID);
	FWallNode* ToNode = Nodes.Find(Edge->ToNodeID);

	if (FromNode)
	{
		FromNode->ConnectedEdgeIDs.Remove(EdgeID);

		// Remove node if orphaned (no more connections)
		if (FromNode->ConnectedEdgeIDs.Num() == 0)
		{
			RemoveNode(Edge->FromNodeID);
		}
	}

	if (ToNode)
	{
		ToNode->ConnectedEdgeIDs.Remove(EdgeID);

		// Remove node if orphaned
		if (ToNode->ConnectedEdgeIDs.Num() == 0)
		{
			RemoveNode(Edge->ToNodeID);
		}
	}

	// Remove edge
	Edges.Remove(EdgeID);

	InvalidateWallRunCache();
	UE_LOG(LogTemp, Log, TEXT("WallGraph: Removed edge %d"), EdgeID);

	return true;
}

bool UWallGraphComponent::RemoveNode(int32 NodeID)
{
	FWallNode* Node = Nodes.Find(NodeID);
	if (!Node)
	{
		return false;
	}

	// Remove all connected edges
	TArray<int32> EdgesToRemove = Node->ConnectedEdgeIDs;
	for (int32 EdgeID : EdgesToRemove)
	{
		RemoveEdge(EdgeID);
	}

	// Remove from spatial index
	RemoveNodeFromSpatialIndex(NodeID);

	// Remove node
	Nodes.Remove(NodeID);

	UE_LOG(LogTemp, Log, TEXT("WallGraph: Removed node %d"), NodeID);

	return true;
}

void UWallGraphComponent::ClearGraph()
{
	Nodes.Empty();
	Edges.Empty();
	Intersections.Empty();
	TileToEdges.Empty();
	TileToNodes.Empty();
	EdgesByLevel.Empty();
	WallRunCache.Empty();
	NextNodeID = 0;
	NextEdgeID = 0;

	UE_LOG(LogTemp, Log, TEXT("WallGraph: Cleared graph"));
}

// ========================================
// Query Methods
// ========================================

bool UWallGraphComponent::IsWallBetweenPositions(const FVector& PosA, const FVector& PosB, int32 Level) const
{
	// This would require converting positions to grid coords
	// For now, not implemented - use IsWallBetweenTiles instead
	UE_LOG(LogTemp, Warning, TEXT("WallGraph: IsWallBetweenPositions not yet implemented, use IsWallBetweenTiles"));
	return false;
}

bool UWallGraphComponent::IsWallBetweenTiles(int32 RowA, int32 ColA, int32 RowB, int32 ColB, int32 Level) const
{
	// Calculate delta to detect diagonal movement
	int32 DeltaRow = RowB - RowA;
	int32 DeltaCol = ColB - ColA;
	bool bDiagonalMovement = (DeltaRow != 0 && DeltaCol != 0);

	// Get all edges in both tiles
	TArray<int32> TileAEdges = GetEdgesInTile(RowA, ColA, Level);
	TArray<int32> TileBEdges = GetEdgesInTile(RowB, ColB, Level);

	// DEBUG: Log for diagonal movement checks
	if (BurbArchitectDebug::IsWallDebugEnabled() && bDiagonalMovement)
	{
		UE_LOG(LogTemp, Verbose, TEXT("IsWallBetweenTiles: Checking diagonal (%d,%d) -> (%d,%d): TileA has %d edges, TileB has %d edges"),
			RowA, ColA, RowB, ColB, TileAEdges.Num(), TileBEdges.Num());
	}

	// Check if any edge appears in both tiles AND actually separates them
	for (int32 EdgeID : TileAEdges)
	{
		if (TileBEdges.Contains(EdgeID))
		{
			const FWallEdge* Edge = Edges.Find(EdgeID);
			if (!Edge)
				continue;

			// DEBUG: Log shared edges
			if (BurbArchitectDebug::IsWallDebugEnabled() && bDiagonalMovement)
			{
				UE_LOG(LogTemp, Log, TEXT("  Shared edge %d between tiles: (%d,%d)->(%d,%d), IsDiagonal=%d"),
					EdgeID, Edge->StartRow, Edge->StartColumn, Edge->EndRow, Edge->EndColumn, Edge->IsDiagonal());
			}

			// Verify this edge actually separates the tiles
			// An edge separates two tiles if:
			// 1. It's axis-aligned and lies on the shared edge between tiles
			// 2. It's diagonal and crosses the tiles

			// Must be adjacent tiles
			int32 DistSq = DeltaRow * DeltaRow + DeltaCol * DeltaCol;
			if (DistSq > 2) // 1 for orthogonal, 2 for diagonal
				continue;

			// For axis-aligned walls
			if (Edge->IsAxisAligned())
			{
				// Horizontal wall (rows differ)
				if (DeltaRow != 0 && DeltaCol == 0)
				{
					// Wall should be horizontal (same row for both endpoints)
					if (Edge->StartRow == Edge->EndRow)
					{
						// Wall should be between the two tiles
						int32 WallRow = Edge->StartRow;
						if ((WallRow == RowA && RowB == RowA + 1) || (WallRow == RowB && RowA == RowB + 1))
						{
							// Wall should span the column
							int32 MinCol = FMath::Min(Edge->StartColumn, Edge->EndColumn);
							int32 MaxCol = FMath::Max(Edge->StartColumn, Edge->EndColumn);
							if (ColA >= MinCol && ColA <= MaxCol)
							{
								return true;
							}
						}
					}
				}
				// Vertical wall (columns differ)
				else if (DeltaRow == 0 && DeltaCol != 0)
				{
					// Wall should be vertical (same column for both endpoints)
					if (Edge->StartColumn == Edge->EndColumn)
					{
						// Wall should be between the two tiles
						int32 WallCol = Edge->StartColumn;
						if ((WallCol == ColA && ColB == ColA + 1) || (WallCol == ColB && ColA == ColB + 1))
						{
							// Wall should span the row
							int32 MinRow = FMath::Min(Edge->StartRow, Edge->EndRow);
							int32 MaxRow = FMath::Max(Edge->StartRow, Edge->EndRow);
							if (RowA >= MinRow && RowA <= MaxRow)
							{
								return true;
							}
						}
					}
				}
			}
			// For diagonal walls
			else if (Edge->IsDiagonal())
			{
				// Diagonal movement between adjacent tiles
				if (DeltaRow != 0 && DeltaCol != 0)
				{
					// DEBUG: Log diagonal wall check details
					if (BurbArchitectDebug::IsWallDebugEnabled())
					{
						UE_LOG(LogTemp, Log, TEXT("    Checking diagonal edge %d: (%d,%d) -> (%d,%d)"),
							EdgeID, Edge->StartRow, Edge->StartColumn, Edge->EndRow, Edge->EndColumn);
					}

					// Use geometric separation test to check if wall separates the tiles
					if (DoesDiagonalWallSeparateTiles(*Edge, RowA, ColA, RowB, ColB))
					{
						if (BurbArchitectDebug::IsWallDebugEnabled())
						{
							UE_LOG(LogTemp, Warning, TEXT("    BLOCKS! Diagonal wall separates (%d,%d) from (%d,%d)"),
								RowA, ColA, RowB, ColB);
						}
						return true;
					}
					else if (BurbArchitectDebug::IsWallDebugEnabled())
					{
						UE_LOG(LogTemp, Log, TEXT("    Does not separate tiles"));
					}
				}
			}
		}
	}

	return false;
}

TArray<int32> UWallGraphComponent::GetEdgesAtNode(int32 NodeID) const
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (Node)
	{
		return Node->ConnectedEdgeIDs;
	}
	return TArray<int32>();
}

TArray<int32> UWallGraphComponent::GetEdgesInTile(int32 Row, int32 Column, int32 Level) const
{
	int64 SpatialKey = GetSpatialKey(Row, Column, Level);
	TArray<int32> EdgeIDs;
	TileToEdges.MultiFind(SpatialKey, EdgeIDs);

	// DEBUG: Log when we query for edges in tiles - especially if diagonal edges are present
	if (BurbArchitectDebug::IsWallDebugEnabled())
	{
		int32 DiagonalEdgeCount = 0;
		for (int32 EdgeID : EdgeIDs)
		{
			const FWallEdge* Edge = Edges.Find(EdgeID);
			if (Edge && Edge->IsDiagonal())
			{
				DiagonalEdgeCount++;
			}
		}

		if (DiagonalEdgeCount > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("GetEdgesInTile(%d,%d,%d): Found %d edges (%d diagonal)"),
				Row, Column, Level, EdgeIDs.Num(), DiagonalEdgeCount);
		}
	}

	return EdgeIDs;
}

int32 UWallGraphComponent::FindEdgeBetweenNodes(int32 NodeA, int32 NodeB) const
{
	const FWallNode* Node = Nodes.Find(NodeA);
	if (!Node)
		return -1;

	for (int32 EdgeID : Node->ConnectedEdgeIDs)
	{
		const FWallEdge* Edge = Edges.Find(EdgeID);
		if (Edge && Edge->ConnectsNodes(NodeA, NodeB))
		{
			return EdgeID;
		}
	}

	return -1;
}

bool UWallGraphComponent::DoesPathExistBetweenNodes(int32 StartNodeID, int32 EndNodeID, int32 ExcludeEdgeID) const
{
	// Early exit checks
	if (StartNodeID == EndNodeID)
	{
		return true;
	}

	const FWallNode* StartNode = Nodes.Find(StartNodeID);
	const FWallNode* EndNode = Nodes.Find(EndNodeID);
	if (!StartNode || !EndNode)
	{
		return false;
	}

	// BFS to find if path exists
	TSet<int32> Visited;
	TArray<int32> Queue;

	Queue.Add(StartNodeID);
	Visited.Add(StartNodeID);

	while (Queue.Num() > 0)
	{
		int32 CurrentNodeID = Queue[0];
		Queue.RemoveAt(0);

		const FWallNode* CurrentNode = Nodes.Find(CurrentNodeID);
		if (!CurrentNode)
		{
			continue;
		}

		// Check all edges from current node
		for (int32 EdgeID : CurrentNode->ConnectedEdgeIDs)
		{
			// Skip the excluded edge (the new edge we're checking)
			if (EdgeID == ExcludeEdgeID)
			{
				continue;
			}

			const FWallEdge* Edge = Edges.Find(EdgeID);
			if (!Edge)
			{
				continue;
			}

			// Get the node on the other end of this edge
			int32 OtherNodeID = (Edge->FromNodeID == CurrentNodeID) ? Edge->ToNodeID : Edge->FromNodeID;

			// Found the target!
			if (OtherNodeID == EndNodeID)
			{
				return true;
			}

			// Add to queue if not visited
			if (!Visited.Contains(OtherNodeID))
			{
				Visited.Add(OtherNodeID);
				Queue.Add(OtherNodeID);
			}
		}
	}

	// No path found
	return false;
}

TArray<int32> UWallGraphComponent::GetEdgesBoundingRoom(int32 RoomID) const
{
	TArray<int32> BoundingEdgeIDs;

	for (const auto& Pair : Edges)
	{
		const FWallEdge& Edge = Pair.Value;
		if (Edge.Room1 == RoomID || Edge.Room2 == RoomID)
		{
			BoundingEdgeIDs.Add(Edge.EdgeID);
		}
	}

	return BoundingEdgeIDs;
}

TArray<int32> UWallGraphComponent::GetNodesAtLevel(int32 Level) const
{
	TArray<int32> NodeIDs;
	for (const auto& Pair : Nodes)
	{
		if (Pair.Value.Level == Level)
		{
			NodeIDs.Add(Pair.Key);
		}
	}
	return NodeIDs;
}

TArray<int32> UWallGraphComponent::GetEdgesAtLevel(int32 Level) const
{
	// O(1) lookup using cached level index instead of O(N) iteration
	const TSet<int32>* LevelEdges = EdgesByLevel.Find(Level);
	if (LevelEdges)
	{
		return LevelEdges->Array();
	}
	return TArray<int32>();
}

// ========================================
// Intersection Management
// ========================================

void UWallGraphComponent::RebuildIntersections()
{
	Intersections.Empty();

	// Build intersection for each node
	for (const auto& NodePair : Nodes)
	{
		int32 NodeID = NodePair.Key;
		const FWallNode& Node = NodePair.Value;

		if (Node.ConnectedEdgeIDs.Num() > 0)
		{
			FWallIntersection Intersection(NodeID);
			Intersection.ConnectedEdgeIDs = Node.ConnectedEdgeIDs;
			Intersection.UpdateSimpleFlag();

			Intersections.Add(NodeID, Intersection);

			// Calculate extents for mitring
			CalculateIntersectionExtents(NodeID);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WallGraph: Rebuilt %d intersections"), Intersections.Num());
}

bool UWallGraphComponent::GetIntersectionAtNode(int32 NodeID, FWallIntersection& OutIntersection) const
{
	const FWallIntersection* Found = Intersections.Find(NodeID);
	if (Found)
	{
		OutIntersection = *Found;
		return true;
	}
	return false;
}

void UWallGraphComponent::CalculateIntersectionExtents(int32 NodeID)
{
	FWallIntersection* Intersection = Intersections.Find(NodeID);
	if (!Intersection)
		return;

	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node)
		return;

	// For each pair of edges meeting at this intersection, calculate the extent needed for proper mitring
	// This is based on OpenTS2's approach using angle calculations

	Intersection->WallExtents.Empty();

	for (int32 EdgeID : Intersection->ConnectedEdgeIDs)
	{
		const FWallEdge* Edge = Edges.Find(EdgeID);
		if (!Edge)
			continue;

		// Get the other node to determine wall direction
		int32 OtherNodeID = Edge->GetOtherNode(NodeID);
		const FWallNode* OtherNode = Nodes.Find(OtherNodeID);
		if (!OtherNode)
			continue;

		// Calculate wall direction vector
		FVector WallDir = (OtherNode->Position - Node->Position).GetSafeNormal2D();

		// For simple intersections (2 walls), calculate mitre extent
		if (Intersection->bIsSimple && Intersection->ConnectedEdgeIDs.Num() == 2)
		{
			// Get the other edge
			int32 OtherEdgeID = -1;
			for (int32 ID : Intersection->ConnectedEdgeIDs)
			{
				if (ID != EdgeID)
				{
					OtherEdgeID = ID;
					break;
				}
			}

			if (OtherEdgeID != -1)
			{
				const FWallEdge* OtherEdge = Edges.Find(OtherEdgeID);
				if (OtherEdge)
				{
					int32 OtherOtherNodeID = OtherEdge->GetOtherNode(NodeID);
					const FWallNode* OtherOtherNode = Nodes.Find(OtherOtherNodeID);
					if (OtherOtherNode)
					{
						FVector OtherWallDir = (OtherOtherNode->Position - Node->Position).GetSafeNormal2D();

						// Calculate angle between walls
						float DotProduct = FVector::DotProduct(WallDir, OtherWallDir);
						float Angle = FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f));

						// Calculate extent for mitring: thickness * tan(angle/2)
						float Extent = Edge->Thickness * FMath::Tan(Angle / 2.0f);

						Intersection->WallExtents.Add(EdgeID, Extent);
					}
				}
			}
		}
		else
		{
			// Complex intersection - use simple extent based on thickness
			Intersection->WallExtents.Add(EdgeID, Edge->Thickness * 0.5f);
		}
	}
}

// ========================================
// Room Assignment
// ========================================

void UWallGraphComponent::AssignRoomToBoundaryEdges(int32 RoomID, const TArray<int32>& BoundaryEdges, const FVector& RoomCentroid)
{
	if (BoundaryEdges.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallGraph::AssignRoomToBoundaryEdges: No boundary edges provided for RoomID %d"), RoomID);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WallGraph::AssignRoomToBoundaryEdges: RoomID=%d, Centroid=(%.1f, %.1f, %.1f), %d boundary edges"),
		RoomID, RoomCentroid.X, RoomCentroid.Y, RoomCentroid.Z, BoundaryEdges.Num());

	int32 Room1Assignments = 0;
	int32 Room2Assignments = 0;

	// Use geometric approach: determine which side of each wall the room centroid is on
	// This is direction-independent and always correct
	for (int32 EdgeID : BoundaryEdges)
	{
		FWallEdge* Edge = Edges.Find(EdgeID);
		if (!Edge)
		{
			UE_LOG(LogTemp, Warning, TEXT("WallGraph::AssignRoomToBoundaryEdges: Edge %d not found"), EdgeID);
			continue;
		}

		// Get wall nodes to calculate geometric normal
		const FWallNode* FromNode = Nodes.Find(Edge->FromNodeID);
		const FWallNode* ToNode = Nodes.Find(Edge->ToNodeID);

		if (!FromNode || !ToNode)
		{
			UE_LOG(LogTemp, Warning, TEXT("WallGraph::AssignRoomToBoundaryEdges: Edge %d missing nodes"), EdgeID);
			continue;
		}

		// Calculate wall direction and geometric normal (right-hand perpendicular)
		// Same calculation as UpdateAllWallRoomIDs and wall mesh generation
		FVector WallDirection = (ToNode->Position - FromNode->Position).GetSafeNormal();
		FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();
		FVector WallCenter = (FromNode->Position + ToNode->Position) * 0.5f;

		// Half-plane test: which side of the wall is the room centroid on?
		FVector ToCentroid = RoomCentroid - WallCenter;
		ToCentroid.Z = 0.0f; // 2D test only
		float DotProduct = FVector::DotProduct(ToCentroid, WallNormal);

		// Positive dot product = centroid is on +normal side (Room1)
		// Negative dot product = centroid is on -normal side (Room2)
		if (DotProduct > 0.0f)
		{
			// Room is on +normal side (Room1)
			if (Edge->Room1 == 0)
			{
				Edge->Room1 = RoomID;
				Room1Assignments++;
				UE_LOG(LogTemp, Log, TEXT("  Edge %d: Assigned Room1=%d (dot=%.2f, centroid on +normal side)"),
					EdgeID, RoomID, DotProduct);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("  Edge %d: SKIPPED Room1 (already=%d, tried=%d, dot=%.2f) - centroid on SAME side as existing room!"),
					EdgeID, Edge->Room1, RoomID, DotProduct);
			}
		}
		else
		{
			// Room is on -normal side (Room2)
			if (Edge->Room2 == 0)
			{
				Edge->Room2 = RoomID;
				Room2Assignments++;
				UE_LOG(LogTemp, Log, TEXT("  Edge %d: Assigned Room2=%d (dot=%.2f, centroid on -normal side)"),
					EdgeID, RoomID, DotProduct);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("  Edge %d: SKIPPED Room2 (already=%d, tried=%d, dot=%.2f) - centroid on SAME side as existing room!"),
					EdgeID, Edge->Room2, RoomID, DotProduct);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WallGraph::AssignRoomToBoundaryEdges: Assigned RoomID=%d to %d boundary edges (%d on Room1 side, %d on Room2 side)"),
		RoomID, Room1Assignments + Room2Assignments, Room1Assignments, Room2Assignments);
}

void UWallGraphComponent::AssignRoomIDsToNewWall(int32 EdgeID)
{
	// Get the wall edge
	FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph::AssignRoomIDsToNewWall: Edge %d not found"), EdgeID);
		return;
	}

	// Get nodes
	const FWallNode* FromNode = Nodes.Find(Edge->FromNodeID);
	const FWallNode* ToNode = Nodes.Find(Edge->ToNodeID);
	if (!FromNode || !ToNode)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph::AssignRoomIDsToNewWall: Nodes not found for edge %d"), EdgeID);
		return;
	}

	// Get LotManager to access RoomManager and grid conversion
	ALotManager* LotManager = Cast<ALotManager>(GetOwner());
	if (!LotManager || !LotManager->RoomManager)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph::AssignRoomIDsToNewWall: LotManager or RoomManager not available"));
		return;
	}

	// Calculate wall direction and geometric normal (right-hand perpendicular)
	FVector WallDirection = (ToNode->Position - FromNode->Position).GetSafeNormal();
	FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();
	FVector WallCenter = (FromNode->Position + ToNode->Position) * 0.5f;

	// Sample positions on +normal side (Room1) and -normal side (Room2)
	const float SampleDistance = 50.0f; // 50cm offset from wall center
	FVector Room1SamplePos = WallCenter + (WallNormal * SampleDistance);
	FVector Room2SamplePos = WallCenter - (WallNormal * SampleDistance);

	// Convert world positions to grid coordinates
	int32 Room1Row, Room1Col, Room2Row, Room2Col;
	if (!LotManager->LocationToTile(Room1SamplePos, Room1Row, Room1Col) ||
	    !LotManager->LocationToTile(Room2SamplePos, Room2Row, Room2Col))
	{
		UE_LOG(LogTemp, Warning, TEXT("WallGraph::AssignRoomIDsToNewWall: Failed to convert sample positions to grid coords"));
		return;
	}

	// Query RoomManager to find which rooms contain these sample tiles
	FIntVector Room1TileCoord(Room1Row, Room1Col, Edge->Level);
	FIntVector Room2TileCoord(Room2Row, Room2Col, Edge->Level);

	int32 Room1ID = LotManager->RoomManager->GetRoomAtTile(Room1TileCoord);
	int32 Room2ID = LotManager->RoomManager->GetRoomAtTile(Room2TileCoord);

	// Assign room IDs (only if not already assigned)
	bool bAssignedRoom1 = false;
	bool bAssignedRoom2 = false;

	if (Room1ID > 0 && Edge->Room1 == 0)
	{
		Edge->Room1 = Room1ID;
		bAssignedRoom1 = true;
	}

	if (Room2ID > 0 && Edge->Room2 == 0)
	{
		Edge->Room2 = Room2ID;
		bAssignedRoom2 = true;
	}

	// Log results
	if (bAssignedRoom1 || bAssignedRoom2)
	{
		UE_LOG(LogTemp, Log, TEXT("WallGraph::AssignRoomIDsToNewWall: Edge %d auto-assigned rooms: Room1=%d (%s), Room2=%d (%s)"),
			EdgeID, Edge->Room1, bAssignedRoom1 ? TEXT("assigned") : TEXT("existing"),
			Edge->Room2, bAssignedRoom2 ? TEXT("assigned") : TEXT("existing"));
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("WallGraph::AssignRoomIDsToNewWall: Edge %d - no rooms found at sample positions (Room1=%d, Room2=%d)"),
			EdgeID, Room1ID, Room2ID);
	}
}

bool UWallGraphComponent::AssignRoomToWallByGeometry(int32 EdgeID, int32 RoomID, const FVector& RoomCentroid)
{
	// Get the wall edge
	FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallGraph::AssignRoomToWallByGeometry: Edge %d not found"), EdgeID);
		return false;
	}

	// Get nodes
	const FWallNode* FromNode = Nodes.Find(Edge->FromNodeID);
	const FWallNode* ToNode = Nodes.Find(Edge->ToNodeID);
	if (!FromNode || !ToNode)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallGraph::AssignRoomToWallByGeometry: Nodes not found for edge %d"), EdgeID);
		return false;
	}

	// Calculate wall direction and geometric normal (right-hand perpendicular)
	// Same calculation as AssignRoomToBoundaryEdges for consistency
	FVector WallDirection = (ToNode->Position - FromNode->Position).GetSafeNormal();
	FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();
	FVector WallCenter = (FromNode->Position + ToNode->Position) * 0.5f;

	// Half-plane test: which side of the wall is the room centroid on?
	FVector ToCentroid = RoomCentroid - WallCenter;
	ToCentroid.Z = 0.0f; // 2D test only
	float DotProduct = FVector::DotProduct(ToCentroid, WallNormal);

	bool bAssigned = false;

	// Positive dot product = centroid is on +normal side (Room1)
	// Negative dot product = centroid is on -normal side (Room2)
	if (DotProduct > 0.0f)
	{
		// Room is on +normal side (Room1)
		if (Edge->Room1 == 0)
		{
			Edge->Room1 = RoomID;
			bAssigned = true;
			UE_LOG(LogTemp, Log, TEXT("WallGraph::AssignRoomToWallByGeometry: Edge %d assigned Room1=%d (dot=%.2f)"),
				EdgeID, RoomID, DotProduct);
		}
	}
	else
	{
		// Room is on -normal side (Room2)
		if (Edge->Room2 == 0)
		{
			Edge->Room2 = RoomID;
			bAssigned = true;
			UE_LOG(LogTemp, Log, TEXT("WallGraph::AssignRoomToWallByGeometry: Edge %d assigned Room2=%d (dot=%.2f)"),
				EdgeID, RoomID, DotProduct);
		}
	}

	return bAssigned;
}

void UWallGraphComponent::AssignRoomIDsToWalls(int32 RoomID, const TArray<FIntVector>& TileCoords)
{
	UE_LOG(LogTemp, Error, TEXT("WallGraph::AssignRoomIDsToWalls: DEPRECATED - This function is O(N^2*M) and extremely slow!"));
	UE_LOG(LogTemp, Error, TEXT("  Use AssignRoomToBoundaryEdges instead for 500-1000x performance improvement."));
	UE_LOG(LogTemp, Warning, TEXT("  Called with RoomID=%d, %d tiles - this will cause freezing on large rooms!"), RoomID, TileCoords.Num());

	int32 TotalAssignments = 0;

	// For each tile in the room, check adjacent tiles and update wall room IDs
	for (const FIntVector& TileCoord : TileCoords)
	{
		int32 Row = TileCoord.X;
		int32 Column = TileCoord.Y;
		int32 Level = TileCoord.Z;

		// Check 8 adjacent tiles (including diagonals)
		TArray<FIntVector> Neighbors = {
			FIntVector(Row - 1, Column, Level),     // Left
			FIntVector(Row + 1, Column, Level),     // Right
			FIntVector(Row, Column - 1, Level),     // Bottom
			FIntVector(Row, Column + 1, Level),     // Top
			FIntVector(Row - 1, Column - 1, Level), // Bottom-Left
			FIntVector(Row - 1, Column + 1, Level), // Top-Left
			FIntVector(Row + 1, Column - 1, Level), // Bottom-Right
			FIntVector(Row + 1, Column + 1, Level)  // Top-Right
		};

		for (const FIntVector& Neighbor : Neighbors)
		{
			// Find walls between this tile and neighbor
			TArray<int32> TileEdges = GetEdgesInTile(Row, Column, Level);
			TArray<int32> NeighborEdges = GetEdgesInTile(Neighbor.X, Neighbor.Y, Neighbor.Z);

			// Find shared edges
			for (int32 EdgeID : TileEdges)
			{
				if (NeighborEdges.Contains(EdgeID))
				{
					FWallEdge* Edge = Edges.Find(EdgeID);
					if (!Edge)
						continue;

					// Determine which side of the wall this room is on
					bool bIsRoom1Side = false;
					if (GetWallSideForTile(*Edge, Row, Column, bIsRoom1Side))
					{
						// Assign room ID to appropriate side
						if (bIsRoom1Side)
						{
							if (Edge->Room1 == 0)
							{
								Edge->Room1 = RoomID;
								TotalAssignments++;
								UE_LOG(LogTemp, Log, TEXT("WallGraph: Assigned Room1=%d to edge %d (from tile %d,%d)"), RoomID, EdgeID, Row, Column);
							}
						}
						else
						{
							if (Edge->Room2 == 0)
							{
								Edge->Room2 = RoomID;
								TotalAssignments++;
								UE_LOG(LogTemp, Log, TEXT("WallGraph: Assigned Room2=%d to edge %d (from tile %d,%d)"), RoomID, EdgeID, Row, Column);
							}
						}
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("WallGraph: AssignRoomIDsToWalls complete - made %d assignments for RoomID=%d"), TotalAssignments, RoomID);
}

bool UWallGraphComponent::GetWallSideForTile(const FWallEdge& Edge, int32 TileRow, int32 TileColumn,
                                              bool& OutIsRoom1Side) const
{
	const FWallNode* FromNode = Nodes.Find(Edge.FromNodeID);
	const FWallNode* ToNode = Nodes.Find(Edge.ToNodeID);

	if (!FromNode || !ToNode)
		return false;

	// Calculate tile center in grid coordinates
	FVector2D TileCenter(TileRow + 0.5f, TileColumn + 0.5f);

	// Calculate wall direction
	FVector2D WallDir(ToNode->Row - FromNode->Row, ToNode->Column - FromNode->Column);
	WallDir.Normalize();

	// Calculate vector from wall start to tile center
	FVector2D ToTile(TileCenter.X - FromNode->Row, TileCenter.Y - FromNode->Column);

	// Use 2D cross product to determine sidedness
	// Positive = right side (Room1), Negative = left side (Room2)
	float CrossProduct = WallDir.X * ToTile.Y - WallDir.Y * ToTile.X;

	OutIsRoom1Side = (CrossProduct > 0);

	return true;
}

// ========================================
// Spatial Indexing (Internal)
// ========================================

void UWallGraphComponent::AddToSpatialIndex(int32 EdgeID)
{
	const FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
		return;

	// Get all tiles this edge passes through
	TArray<FIntVector> TilesAlongEdge = GetTilesAlongEdge(*Edge);

	// DEBUG: Log diagonal wall indexing
	if (BurbArchitectDebug::IsWallDebugEnabled() && Edge->IsDiagonal())
	{
		UE_LOG(LogTemp, Warning, TEXT("AddToSpatialIndex: Diagonal edge %d from (%d,%d) to (%d,%d) affects %d tiles:"),
			EdgeID, Edge->StartRow, Edge->StartColumn, Edge->EndRow, Edge->EndColumn, TilesAlongEdge.Num());
		for (const FIntVector& TileCoord : TilesAlongEdge)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Tile (%d,%d,%d)"), TileCoord.X, TileCoord.Y, TileCoord.Z);
		}
	}

	for (const FIntVector& TileCoord : TilesAlongEdge)
	{
		int64 SpatialKey = GetSpatialKey(TileCoord.X, TileCoord.Y, TileCoord.Z);
		TileToEdges.Add(SpatialKey, EdgeID);
	}
}

void UWallGraphComponent::RemoveFromSpatialIndex(int32 EdgeID)
{
	const FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
		return;

	TArray<FIntVector> TilesAlongEdge = GetTilesAlongEdge(*Edge);

	for (const FIntVector& TileCoord : TilesAlongEdge)
	{
		int64 SpatialKey = GetSpatialKey(TileCoord.X, TileCoord.Y, TileCoord.Z);
		TileToEdges.RemoveSingle(SpatialKey, EdgeID);
	}
}

void UWallGraphComponent::AddNodeToSpatialIndex(int32 NodeID)
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node)
		return;

	int64 SpatialKey = GetSpatialKey(Node->Row, Node->Column, Node->Level);
	TileToNodes.Add(SpatialKey, NodeID);
}

void UWallGraphComponent::RemoveNodeFromSpatialIndex(int32 NodeID)
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node)
		return;

	int64 SpatialKey = GetSpatialKey(Node->Row, Node->Column, Node->Level);
	TileToNodes.RemoveSingle(SpatialKey, NodeID);
}

TArray<FIntVector> UWallGraphComponent::GetTilesAlongEdge(const FWallEdge& Edge) const
{
	TArray<FIntVector> Tiles;

	// Get the nodes for their world positions
	const FWallNode* StartNode = Nodes.Find(Edge.FromNodeID);
	const FWallNode* EndNode = Nodes.Find(Edge.ToNodeID);

	if (!StartNode || !EndNode)
		return Tiles;

	// Get LotManager for grid info
	ALotManager* LotManager = Cast<ALotManager>(GetOwner());
	if (!LotManager)
		return Tiles;

	const float TileSize = LotManager->GridTileSize;
	const FVector GridOrigin = LotManager->GetActorLocation();

	// Convert world positions to continuous grid coordinates
	float StartX = (StartNode->Position.X - GridOrigin.X) / TileSize;
	float StartY = (StartNode->Position.Y - GridOrigin.Y) / TileSize;
	float EndX = (EndNode->Position.X - GridOrigin.X) / TileSize;
	float EndY = (EndNode->Position.Y - GridOrigin.Y) / TileSize;

	// For diagonal walls, we want to find the tiles the wall actually passes through
	// Use a modified Bresenham that starts from the correct tile based on wall direction
	if (Edge.IsDiagonal())
	{
		// Determine direction
		bool bGoingRight = EndX > StartX;
		bool bGoingUp = EndY > StartY;

		// For diagonal walls, the corner position determines which tile to start from
		// A corner at (5.0, 5.0) is at the intersection of 4 tiles
		// We choose which tile based on the direction of the wall
		int32 X0, Y0, X1, Y1;

		if (bGoingRight && bGoingUp)
		{
			// Northeast: Start from tile to the northeast of start corner
			X0 = FMath::FloorToInt(StartX);
			Y0 = FMath::FloorToInt(StartY);
			X1 = FMath::FloorToInt(EndX) - 1;  // Don't include the final corner's tile
			Y1 = FMath::FloorToInt(EndY) - 1;
		}
		else if (!bGoingRight && bGoingUp)
		{
			// Northwest: Start from tile to the northwest of start corner
			X0 = FMath::CeilToInt(StartX) - 1;
			Y0 = FMath::FloorToInt(StartY);
			X1 = FMath::CeilToInt(EndX);
			Y1 = FMath::FloorToInt(EndY) - 1;
		}
		else if (bGoingRight && !bGoingUp)
		{
			// Southeast: Start from tile to the southeast of start corner
			X0 = FMath::FloorToInt(StartX);
			Y0 = FMath::CeilToInt(StartY) - 1;
			X1 = FMath::FloorToInt(EndX) - 1;
			Y1 = FMath::CeilToInt(EndY);
		}
		else
		{
			// Southwest: Start from tile to the southwest of start corner
			X0 = FMath::CeilToInt(StartX) - 1;
			Y0 = FMath::CeilToInt(StartY) - 1;
			X1 = FMath::CeilToInt(EndX);
			Y1 = FMath::CeilToInt(EndY);
		}

		// Now use Bresenham's to trace the diagonal
		int32 DX = FMath::Abs(X1 - X0);
		int32 DY = FMath::Abs(Y1 - Y0);
		int32 SX = (X0 < X1) ? 1 : -1;
		int32 SY = (Y0 < Y1) ? 1 : -1;
		int32 Err = DX - DY;

		int32 X = X0;
		int32 Y = Y0;

		while (true)
		{
			// Check bounds and add tile
			if (X >= 0 && X < LotManager->GridSizeX && Y >= 0 && Y < LotManager->GridSizeY)
			{
				Tiles.Add(FIntVector(Y, X, Edge.Level));
			}

			if (X == X1 && Y == Y1)
				break;

			int32 E2 = 2 * Err;
			if (E2 > -DY)
			{
				Err -= DY;
				X += SX;
			}
			if (E2 < DX)
			{
				Err += DX;
				Y += SY;
			}
		}

		if (BurbArchitectDebug::IsWallDebugEnabled())
		{
			UE_LOG(LogTemp, Warning, TEXT("GetTilesAlongEdge: Diagonal wall from (%.1f,%.1f) to (%.1f,%.1f) passes through %d tiles"),
				StartNode->Position.X, StartNode->Position.Y, EndNode->Position.X, EndNode->Position.Y, Tiles.Num());
		}
	}
	else
	{
		// For orthogonal walls, just use the edge's stored grid coordinates
		// These already represent the tiles correctly
		int32 X0 = Edge.StartColumn;
		int32 Y0 = Edge.StartRow;
		int32 X1 = Edge.EndColumn;
		int32 Y1 = Edge.EndRow;

		// Simple iteration along the wall
		if (Y0 == Y1)
		{
			// Horizontal wall - iterate along X
			int32 MinX = FMath::Min(X0, X1);
			int32 MaxX = FMath::Max(X0, X1);
			for (int32 X = MinX; X <= MaxX; X++)
			{
				if (X >= 0 && X < LotManager->GridSizeX && Y0 >= 0 && Y0 < LotManager->GridSizeY)
				{
					Tiles.Add(FIntVector(Y0, X, Edge.Level));
				}
			}
		}
		else if (X0 == X1)
		{
			// Vertical wall - iterate along Y
			int32 MinY = FMath::Min(Y0, Y1);
			int32 MaxY = FMath::Max(Y0, Y1);
			for (int32 Y = MinY; Y <= MaxY; Y++)
			{
				if (X0 >= 0 && X0 < LotManager->GridSizeX && Y >= 0 && Y < LotManager->GridSizeY)
				{
					Tiles.Add(FIntVector(Y, X0, Edge.Level));
				}
			}
		}
	}

	return Tiles;
}

bool UWallGraphComponent::DoesDiagonalWallSeparateTiles(const FWallEdge& Edge, int32 RowA, int32 ColA, int32 RowB, int32 ColB) const
{
	// For a diagonal wall from (StartRow, StartCol) to (EndRow, EndCol),
	// check if two tiles are on opposite sides of the wall line segment.

	// Calculate tile centers in grid coordinates (use 0.5 offset for center)
	FVector2D TileCenterA(RowA + 0.5, ColA + 0.5);
	FVector2D TileCenterB(RowB + 0.5, ColB + 0.5);

	// Wall endpoints in grid coordinates
	FVector2D WallStart(Edge.StartRow, Edge.StartColumn);
	FVector2D WallEnd(Edge.EndRow, Edge.EndColumn);

	// Vector along the wall
	FVector2D WallDir = WallEnd - WallStart;

	// Vectors from wall start to each tile center
	FVector2D ToTileA = TileCenterA - WallStart;
	FVector2D ToTileB = TileCenterB - WallStart;

	// Use 2D cross product to determine which side of the wall line each tile is on
	// Cross product: WallDir × ToTile = WallDir.X * ToTile.Y - WallDir.Y * ToTile.X
	float CrossA = WallDir.X * ToTileA.Y - WallDir.Y * ToTileA.X;
	float CrossB = WallDir.X * ToTileB.Y - WallDir.Y * ToTileB.X;

	// If cross products have opposite signs, tiles are on opposite sides of the wall LINE
	bool bOppositeSides = (CrossA * CrossB < 0.0f);

	if (!bOppositeSides)
	{
		// Tiles on same side or one is exactly on the line - wall doesn't separate them
		return false;
	}

	// Now check if the wall SEGMENT (not just the infinite line) actually passes between the tiles
	// We need to verify the perpendicular projection of both tiles onto the wall line
	// falls within the wall segment bounds [0, 1] (parametric form)

	float WallLengthSq = WallDir.SizeSquared();
	if (WallLengthSq < 0.0001f)
	{
		// Degenerate wall (zero length) - can't separate anything
		return false;
	}

	// Project tile centers onto wall line (parametric t values)
	float tA = FVector2D::DotProduct(ToTileA, WallDir) / WallLengthSq;
	float tB = FVector2D::DotProduct(ToTileB, WallDir) / WallLengthSq;

	// Check if the perpendicular line connecting the tiles crosses the wall segment
	// The wall segment separates the tiles if both projections overlap the segment [0, 1]
	// OR if the segment [tA, tB] contains part of [0, 1]

	float minT = FMath::Min(tA, tB);
	float maxT = FMath::Max(tA, tB);

	// Check if the segment interval [minT, maxT] overlaps with wall segment [0, 1]
	// Segments overlap if: minT < 1 AND maxT > 0
	bool bSegmentOverlaps = (minT < 1.0f) && (maxT > 0.0f);

	// Wall separates tiles if they're on opposite sides AND the segment passes between them
	return bOppositeSides && bSegmentOverlaps;
}

// ========================================
// Wall Runs (Collinear Edge Grouping)
// ========================================

void UWallGraphComponent::InvalidateWallRunCache()
{
	WallRunCache.Empty();
}

FWallRun UWallGraphComponent::GetWallRun(int32 EdgeID)
{
	// Check cache first
	if (FWallRun* Cached = WallRunCache.Find(EdgeID))
	{
		return *Cached;
	}

	FWallRun Run;

	const FWallEdge* SeedEdge = Edges.Find(EdgeID);
	if (!SeedEdge)
	{
		return Run;
	}

	// Get reference direction from seed edge
	const FWallNode* FromNode = Nodes.Find(SeedEdge->FromNodeID);
	const FWallNode* ToNode = Nodes.Find(SeedEdge->ToNodeID);
	if (!FromNode || !ToNode)
	{
		return Run;
	}

	FVector RefDir = (ToNode->Position - FromNode->Position);
	RefDir.Z = 0.f;
	float SeedLen = RefDir.Size();
	if (SeedLen < KINDA_SMALL_NUMBER)
	{
		return Run;
	}
	RefDir /= SeedLen;

	// Walk backward (through FromNode of seed) and forward (through ToNode of seed)
	TArray<int32> Backward, Forward;
	WalkRunDirection(EdgeID, true, RefDir, Backward);
	WalkRunDirection(EdgeID, false, RefDir, Forward);

	// Build ordered run: reversed backward + seed + forward
	Algo::Reverse(Backward);
	Run.EdgeIDs = MoveTemp(Backward);
	Run.EdgeIDs.Add(EdgeID);
	Run.EdgeIDs.Append(Forward);

	// Calculate lengths
	Run.TotalLength = 0.f;
	Run.Direction = RefDir;
	for (int32 EID : Run.EdgeIDs)
	{
		const FWallEdge* Edge = Edges.Find(EID);
		if (!Edge)
		{
			Run.EdgeLengths.Add(0.f);
			continue;
		}
		const FWallNode* A = Nodes.Find(Edge->FromNodeID);
		const FWallNode* B = Nodes.Find(Edge->ToNodeID);
		float Len = (A && B) ? FVector::Dist2D(A->Position, B->Position) : 0.f;
		Run.EdgeLengths.Add(Len);
		Run.TotalLength += Len;
	}

	// Cache for all edges in the run
	for (int32 EID : Run.EdgeIDs)
	{
		WallRunCache.Add(EID, Run);
	}

	return Run;
}

void UWallGraphComponent::WalkRunDirection(int32 SeedEdgeID, bool bWalkFromNode, const FVector& RefDirection, TArray<int32>& OutEdgeIDs) const
{
	const FWallEdge* SeedEdge = Edges.Find(SeedEdgeID);
	if (!SeedEdge)
	{
		return;
	}

	int32 WalkNodeID = bWalkFromNode ? SeedEdge->FromNodeID : SeedEdge->ToNodeID;
	int32 PrevEdgeID = SeedEdgeID;

	while (true)
	{
		// Check degree of walk node — only continue through degree-2 nodes
		const FWallNode* WalkNode = Nodes.Find(WalkNodeID);
		if (!WalkNode || WalkNode->ConnectedEdgeIDs.Num() != 2)
		{
			break;
		}

		// Find the other edge (not PrevEdgeID)
		int32 FoundNextEdgeID = -1;
		for (int32 EID : WalkNode->ConnectedEdgeIDs)
		{
			if (EID != PrevEdgeID)
			{
				FoundNextEdgeID = EID;
				break;
			}
		}
		if (FoundNextEdgeID < 0)
		{
			break;
		}

		// Check collinearity via dot product
		const FWallEdge* NextEdge = Edges.Find(FoundNextEdgeID);
		if (!NextEdge)
		{
			break;
		}
		const FWallNode* NA = Nodes.Find(NextEdge->FromNodeID);
		const FWallNode* NB = Nodes.Find(NextEdge->ToNodeID);
		if (!NA || !NB)
		{
			break;
		}

		FVector NextDir = (NB->Position - NA->Position);
		NextDir.Z = 0.f;
		float NextLen = NextDir.Size();
		if (NextLen < KINDA_SMALL_NUMBER)
		{
			break;
		}
		NextDir /= NextLen;

		// Must be collinear (parallel or anti-parallel)
		if (FMath::Abs(FVector::DotProduct(RefDirection, NextDir)) < 0.999f)
		{
			break;
		}

		OutEdgeIDs.Add(FoundNextEdgeID);

		// Move to the far node of next edge
		WalkNodeID = (NextEdge->FromNodeID == WalkNodeID) ? NextEdge->ToNodeID : NextEdge->FromNodeID;
		PrevEdgeID = FoundNextEdgeID;
	}
}

bool UWallGraphComponent::IsEdgeAlignedWithRun(const FWallRun& Run, int32 EdgeID) const
{
	const FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
	{
		return true;
	}
	const FWallNode* A = Nodes.Find(Edge->FromNodeID);
	const FWallNode* B = Nodes.Find(Edge->ToNodeID);
	if (!A || !B)
	{
		return true;
	}

	FVector EdgeDir = (B->Position - A->Position);
	EdgeDir.Z = 0.f;
	return FVector::DotProduct(EdgeDir, Run.Direction) > 0.f;
}

FWallRunEdgeMapping UWallGraphComponent::RunTToEdge(const FWallRun& Run, float RunT) const
{
	FWallRunEdgeMapping Result;
	if (!Run.IsValid())
	{
		return Result;
	}

	RunT = FMath::Clamp(RunT, 0.f, 1.f);
	float TargetDist = RunT * Run.TotalLength;
	float Cumulative = 0.f;

	for (int32 i = 0; i < Run.EdgeIDs.Num(); ++i)
	{
		float EdgeLen = Run.EdgeLengths[i];
		if (Cumulative + EdgeLen >= TargetDist - 0.0001f || i == Run.EdgeIDs.Num() - 1)
		{
			Result.EdgeID = Run.EdgeIDs[i];
			float LocalDist = TargetDist - Cumulative;
			float RunLocalT = (EdgeLen > 0.001f) ? FMath::Clamp(LocalDist / EdgeLen, 0.f, 1.f) : 0.f;

			// Convert from run-direction t to FromNode->ToNode t
			if (!IsEdgeAlignedWithRun(Run, Result.EdgeID))
			{
				RunLocalT = 1.f - RunLocalT;
			}
			Result.LocalT = RunLocalT;
			return Result;
		}
		Cumulative += EdgeLen;
	}

	// Fallback
	Result.EdgeID = Run.EdgeIDs.Last();
	Result.LocalT = 1.f;
	return Result;
}

float UWallGraphComponent::EdgeTToRunT(const FWallRun& Run, int32 EdgeID, float LocalT) const
{
	if (!Run.IsValid() || Run.TotalLength < 0.001f)
	{
		return 0.f;
	}

	float Cumulative = 0.f;
	for (int32 i = 0; i < Run.EdgeIDs.Num(); ++i)
	{
		if (Run.EdgeIDs[i] == EdgeID)
		{
			// Convert from FromNode->ToNode t to run-direction t
			float RunLocalT = LocalT;
			if (!IsEdgeAlignedWithRun(Run, EdgeID))
			{
				RunLocalT = 1.f - RunLocalT;
			}
			return FMath::Clamp((Cumulative + RunLocalT * Run.EdgeLengths[i]) / Run.TotalLength, 0.f, 1.f);
		}
		Cumulative += Run.EdgeLengths[i];
	}

	return 0.f;
}

// ========================================
// Debug Visualization
// ========================================
// Portal Validation
// ========================================

int32 UWallGraphComponent::GetEdgeIDFromWallIndex(const UWallComponent* WallComponent, int32 WallArrayIndex) const
{
	if (!WallComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph: GetEdgeIDFromWallIndex - WallComponent is null"));
		return -1;
	}

	if (!WallComponent->WallDataArray.IsValidIndex(WallArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph: GetEdgeIDFromWallIndex - Invalid WallArrayIndex %d"), WallArrayIndex);
		return -1;
	}

	const FWallSegmentData& WallData = WallComponent->WallDataArray[WallArrayIndex];
	return WallData.WallEdgeID;
}

TArray<int32> UWallGraphComponent::GetAllAdjacentEdgesAtNode(int32 CurrentEdgeID, int32 NodeID) const
{
	TArray<int32> AdjacentEdges;

	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node)
	{
		return AdjacentEdges; // Empty array
	}

	// Return all edges at this node EXCEPT the current edge
	// Works for any node type: simple corners (2 edges), junctions (3+ edges)
	for (int32 EdgeID : Node->ConnectedEdgeIDs)
	{
		if (EdgeID != CurrentEdgeID)
		{
			AdjacentEdges.Add(EdgeID);
		}
	}

	return AdjacentEdges;
}

bool UWallGraphComponent::CanCornerSpanToAdjacentWall(const FVector& CornerPosition, int32 AdjacentEdgeID) const
{
	const FWallEdge* AdjacentEdge = Edges.Find(AdjacentEdgeID);
	if (!AdjacentEdge)
	{
		return false;
	}

	// Get adjacent wall endpoints
	const FWallNode* AdjacentFromNode = Nodes.Find(AdjacentEdge->FromNodeID);
	const FWallNode* AdjacentToNode = Nodes.Find(AdjacentEdge->ToNodeID);

	if (!AdjacentFromNode || !AdjacentToNode)
	{
		return false;
	}

	// Calculate adjacent wall direction and length
	FVector AdjacentWallStart = AdjacentFromNode->Position;
	FVector AdjacentWallEnd = AdjacentToNode->Position;
	FVector AdjacentWallDir = (AdjacentWallEnd - AdjacentWallStart).GetSafeNormal();
	float AdjacentWallLength = FVector::Dist(AdjacentWallStart, AdjacentWallEnd);

	// Project corner onto adjacent wall line
	FVector CornerToStart = CornerPosition - AdjacentWallStart;
	float ProjectionLength = FVector::DotProduct(CornerToStart, AdjacentWallDir);

	// Check if projection falls within adjacent wall bounds
	// Use generous tolerance to allow natural corner spanning
	const float Tolerance = 10.0f; // 10cm tolerance for numerical precision
	bool bWithinBounds = (ProjectionLength >= -Tolerance && ProjectionLength <= AdjacentWallLength + Tolerance);

	if (bWithinBounds)
	{
		UE_LOG(LogTemp, Log, TEXT("WallGraph: Corner spans to adjacent wall (projection %.1f / wall length %.1f)"),
			ProjectionLength, AdjacentWallLength);
	}

	return bWithinBounds;
}

bool UWallGraphComponent::IsPositionNearJunction(int32 EdgeID, const FVector& WorldPosition, float JunctionThreshold) const
{
	const FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallGraph: IsPositionNearJunction - Invalid EdgeID %d"), EdgeID);
		return false;
	}

	// Get both endpoint nodes
	const FWallNode* FromNode = Nodes.Find(Edge->FromNodeID);
	const FWallNode* ToNode = Nodes.Find(Edge->ToNodeID);

	if (!FromNode || !ToNode)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph: IsPositionNearJunction - Missing nodes for edge %d"), EdgeID);
		return false;
	}

	// Check distance to start node
	float DistanceToStart = FVector::Dist2D(WorldPosition, FromNode->Position);
	if (DistanceToStart < JunctionThreshold)
	{
		// Check if start node is a junction (3+ connections)
		if (FromNode->ConnectedEdgeIDs.Num() >= 3)
		{
			UE_LOG(LogTemp, Log, TEXT("WallGraph: Position near junction at start node %d (%d connections)"),
				FromNode->NodeID, FromNode->ConnectedEdgeIDs.Num());
			return true;
		}
	}

	// Check distance to end node
	float DistanceToEnd = FVector::Dist2D(WorldPosition, ToNode->Position);
	if (DistanceToEnd < JunctionThreshold)
	{
		// Check if end node is a junction (3+ connections)
		if (ToNode->ConnectedEdgeIDs.Num() >= 3)
		{
			UE_LOG(LogTemp, Log, TEXT("WallGraph: Position near junction at end node %d (%d connections)"),
				ToNode->NodeID, ToNode->ConnectedEdgeIDs.Num());
			return true;
		}
	}

	return false;
}

bool UWallGraphComponent::IsPortalWithinWallBounds(int32 EdgeID, const FVector& PortalCenter, const FVector& PortalExtent, const FQuat& PortalRotation) const
{
	const FWallEdge* Edge = Edges.Find(EdgeID);
	if (!Edge)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallGraph: IsPortalWithinWallBounds - Invalid EdgeID %d"), EdgeID);
		return false;
	}

	// Get wall endpoints
	const FWallNode* FromNode = Nodes.Find(Edge->FromNodeID);
	const FWallNode* ToNode = Nodes.Find(Edge->ToNodeID);

	if (!FromNode || !ToNode)
	{
		UE_LOG(LogTemp, Error, TEXT("WallGraph: IsPortalWithinWallBounds - Missing nodes for edge %d"), EdgeID);
		return false;
	}

	// Calculate wall direction and length
	FVector WallStart = FromNode->Position;
	FVector WallEnd = ToNode->Position;
	FVector WallDir = (WallEnd - WallStart).GetSafeNormal();
	float WallLength = FVector::Dist(WallStart, WallEnd);

	// Get the portal's 4 corner points in world space
	// Portal box is typically oriented along X-axis (width) and Z-axis (height)
	// Y-axis is the depth (wall thickness direction)
	TArray<FVector> PortalCorners;
	PortalCorners.Add(PortalRotation.RotateVector(FVector(PortalExtent.X, 0, PortalExtent.Z)) + PortalCenter);   // Top-right front
	PortalCorners.Add(PortalRotation.RotateVector(FVector(-PortalExtent.X, 0, PortalExtent.Z)) + PortalCenter);  // Top-left front
	PortalCorners.Add(PortalRotation.RotateVector(FVector(PortalExtent.X, 0, -PortalExtent.Z)) + PortalCenter);  // Bottom-right front
	PortalCorners.Add(PortalRotation.RotateVector(FVector(-PortalExtent.X, 0, -PortalExtent.Z)) + PortalCenter); // Bottom-left front

	// Project all corners onto the wall line and check if they're within bounds
	// Special handling: Allow portals to extend past 2-edge corners (simple corners),
	// but maintain strict bounds at junctions (3+ edges)
	for (const FVector& Corner : PortalCorners)
	{
		// Project corner onto wall line
		FVector CornerToStart = Corner - WallStart;
		float ProjectionLength = FVector::DotProduct(CornerToStart, WallDir);

		// Check if projection is outside wall bounds [0, WallLength]
		// Add small tolerance (5cm) to account for floating-point precision
		const float Tolerance = 5.0f;

		if (ProjectionLength < -Tolerance)
		{
			// Portal extends before wall start - use graph-based validation
			// Get ALL adjacent walls at this node (works for simple corners, T-junctions, cross junctions, etc.)
			TArray<int32> AdjacentEdgeIDs = GetAllAdjacentEdgesAtNode(EdgeID, FromNode->NodeID);

			// Check if corner fits within ANY of the adjacent walls
			bool bFitsInAdjacentWall = false;
			for (int32 AdjacentEdgeID : AdjacentEdgeIDs)
			{
				if (CanCornerSpanToAdjacentWall(Corner, AdjacentEdgeID))
				{
					bFitsInAdjacentWall = true;
					break; // Found a wall that contains this corner
				}
			}

			if (bFitsInAdjacentWall)
			{
				// Corner fits in at least one adjacent wall - allow it
				continue;
			}

			// Corner doesn't fit in any adjacent wall - reject
			UE_LOG(LogTemp, Log, TEXT("WallGraph: Portal extends before wall start - corner doesn't fit in any of %d adjacent walls (projection %.1f)"),
				AdjacentEdgeIDs.Num(), ProjectionLength);
			return false;
		}
		else if (ProjectionLength > WallLength + Tolerance)
		{
			// Portal extends past wall end - use graph-based validation
			// Get ALL adjacent walls at this node (works for simple corners, T-junctions, cross junctions, etc.)
			TArray<int32> AdjacentEdgeIDs = GetAllAdjacentEdgesAtNode(EdgeID, ToNode->NodeID);

			// Check if corner fits within ANY of the adjacent walls
			bool bFitsInAdjacentWall = false;
			for (int32 AdjacentEdgeID : AdjacentEdgeIDs)
			{
				if (CanCornerSpanToAdjacentWall(Corner, AdjacentEdgeID))
				{
					bFitsInAdjacentWall = true;
					break; // Found a wall that contains this corner
				}
			}

			if (bFitsInAdjacentWall)
			{
				// Corner fits in at least one adjacent wall - allow it
				continue;
			}

			// Corner doesn't fit in any adjacent wall - reject
			UE_LOG(LogTemp, Log, TEXT("WallGraph: Portal extends past wall end - corner doesn't fit in any of %d adjacent walls (projection %.1f, wall length %.1f)"),
				AdjacentEdgeIDs.Num(), ProjectionLength, WallLength);
			return false;
		}
	}

	return true;
}

// ========================================

#if WITH_EDITOR
void UWallGraphComponent::DrawWallGraphDebug(FPrimitiveDrawInterface* PDI)
{
	if (!PDI)
		return;

	// Draw nodes as spheres
	for (const auto& Pair : Nodes)
	{
		const FWallNode& Node = Pair.Value;
		FColor NodeColor = (Node.ConnectedEdgeIDs.Num() > 2) ? FColor::Yellow : FColor::Green;
		DrawDebugSphere(GetWorld(), Node.Position, 15.0f, 8, NodeColor, false, -1.0f, 0, 2.0f);
	}

	// Draw edges as lines with room ID colors
	for (const auto& Pair : Edges)
	{
		const FWallEdge& Edge = Pair.Value;
		const FWallNode* FromNode = Nodes.Find(Edge.FromNodeID);
		const FWallNode* ToNode = Nodes.Find(Edge.ToNodeID);

		if (FromNode && ToNode)
		{
			// Color code by room IDs
			FColor EdgeColor = FColor::White;
			if (Edge.Room1 > 0 && Edge.Room2 > 0)
				EdgeColor = FColor::Cyan; // Interior wall
			else if (Edge.Room1 > 0 || Edge.Room2 > 0)
				EdgeColor = FColor::Blue; // Exterior wall
			else
				EdgeColor = FColor::Red; // No room assigned

			DrawDebugLine(GetWorld(), FromNode->Position, ToNode->Position, EdgeColor, false, -1.0f, 0, 3.0f);
		}
	}
}

void UWallGraphComponent::DrawWallGraphLabels(FPrimitiveDrawInterface* PDI)
{
	if (!PDI || !GetWorld())
		return;

	// Draw node IDs
	for (const auto& Pair : Nodes)
	{
		const FWallNode& Node = Pair.Value;
		FVector LabelPos = Node.Position + FVector(0, 0, 50);
		DrawDebugString(GetWorld(), LabelPos, FString::Printf(TEXT("N%d"), Node.NodeID),
		                nullptr, FColor::White, 0.0f, true, 1.0f);
	}

	// Draw edge IDs and room IDs
	for (const auto& Pair : Edges)
	{
		const FWallEdge& Edge = Pair.Value;
		const FWallNode* FromNode = Nodes.Find(Edge.FromNodeID);
		const FWallNode* ToNode = Nodes.Find(Edge.ToNodeID);

		if (FromNode && ToNode)
		{
			FVector MidPoint = (FromNode->Position + ToNode->Position) * 0.5f + FVector(0, 0, 30);
			DrawDebugString(GetWorld(), MidPoint,
			                FString::Printf(TEXT("E%d R%d|R%d"), Edge.EdgeID, Edge.Room1, Edge.Room2),
			                nullptr, FColor::Yellow, 0.0f, true, 0.8f);
		}
	}
}
#endif

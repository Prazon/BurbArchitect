// Fill out your copyright notice in the Description page of Project Settings.

#include "Commands/WallCommand.h"
#include "Actors/BurbPawn.h"
#include "Components/RoomManagerComponent.h"
#include "Components/WaterComponent.h"
#include "Subsystems/BuildServer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void UWallCommand::Initialize(ALotManager* Lot, int32 InLevel, const FVector& Start, const FVector& End, float Height, class UWallPattern* InPattern, UMaterialInstance* BaseMat, bool bDeferRoomGen, bool bPoolWall)
{
	LotManager = Lot;
	OperationMode = EWallOperationMode::Create;
	Level = InLevel;
	StartLoc = Start;
	EndLoc = End;
	WallHeight = Height;
	Pattern = InPattern;
	Material = BaseMat;
	bWallCreated = false;
	bDeferRoomGeneration = bDeferRoomGen;
	bIsPoolWall = bPoolWall;
}

void UWallCommand::InitializeDelete(ALotManager* Lot, const FWallSegmentData& WallToDelete)
{
	LotManager = Lot;
	OperationMode = EWallOperationMode::Delete;
	WallData = WallToDelete;
	Level = WallToDelete.Level;
	StartLoc = WallToDelete.StartLoc;
	EndLoc = WallToDelete.EndLoc;
	WallEdgeID = WallToDelete.WallEdgeID; // Must copy edge ID so Commit() can remove from wall graph
	Material = nullptr; // Could extract from WallData if needed
	bWallCreated = false;
}

void UWallCommand::Commit()
{
	if (!LotManager || !LotManager->WallComponent || !LotManager->WallGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("WallCommand: LotManager, WallComponent, or WallGraph is null"));
		return;
	}

	if (OperationMode == EWallOperationMode::Create)
	{
		// Convert world positions to grid coordinates
		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (!LotManager->LocationToTile(StartLoc, StartRow, StartColumn) ||
			!LotManager->LocationToTile(EndLoc, EndRow, EndColumn))
		{
			UE_LOG(LogTemp, Error, TEXT("WallCommand: Failed to convert locations to grid coordinates"));
			return;
		}

		// Add nodes to wall graph (or get existing nodes at these positions)
		int32 StartNodeID = LotManager->WallGraph->AddNode(StartLoc, Level, StartRow, StartColumn);
		int32 EndNodeID = LotManager->WallGraph->AddNode(EndLoc, Level, EndRow, EndColumn);

		if (StartNodeID == -1 || EndNodeID == -1)
		{
			UE_LOG(LogTemp, Error, TEXT("WallCommand: Failed to create wall graph nodes"));
			return;
		}

		// Add edge to wall graph
		WallEdgeID = LotManager->WallGraph->AddEdge(StartNodeID, EndNodeID, WallHeight, 20.0f, Pattern);

		if (WallEdgeID == -1)
		{
			UE_LOG(LogTemp, Error, TEXT("WallCommand: Failed to create wall graph edge"));
			return;
		}

		// Set pool wall flag if this is a pool wall
		if (bIsPoolWall)
		{
			FWallEdge* Edge = LotManager->WallGraph->Edges.Find(WallEdgeID);
			if (Edge)
			{
				Edge->bIsPoolWall = true;
				UE_LOG(LogTemp, Log, TEXT("WallCommand: Marked wall edge %d as pool wall"), WallEdgeID);
			}
		}

		// Auto-assign room IDs to the newly created wall by sampling adjacent tiles
		// This handles walls placed inside existing rooms (not forming new boundaries)
		LotManager->WallGraph->AssignRoomIDsToNewWall(WallEdgeID);

		// Check if this wall is placed INSIDE an existing room (potential subdivision)
		// If Room1 or Room2 is non-zero after assignment, the wall divides that room
		const FWallEdge* NewEdge = LotManager->WallGraph->Edges.Find(WallEdgeID);
		if (NewEdge && LotManager->RoomManager)
		{
			TSet<int32> RoomsToSubdivide;
			if (NewEdge->Room1 > 0)
			{
				RoomsToSubdivide.Add(NewEdge->Room1);
			}
			if (NewEdge->Room2 > 0)
			{
				RoomsToSubdivide.Add(NewEdge->Room2);
			}

			if (RoomsToSubdivide.Num() > 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("WallCommand: Wall %d placed inside existing room(s): %s - triggering subdivision detection"),
					WallEdgeID,
					*FString::JoinBy(RoomsToSubdivide.Array(), TEXT(", "), [](int32 ID) { return FString::FromInt(ID); }));

				// Trigger incremental room detection which handles subdivision
				TArray<int32> AddedWalls = { WallEdgeID };
				LotManager->RoomManager->OnWallsModified(AddedWalls, TArray<int32>());
			}
		}

		// Get the edge to read back normalized direction
		// WallGraph::AddEdge() normalizes direction so FromNode is always at lower grid coords
		const FWallEdge* CreatedEdge = LotManager->WallGraph->Edges.Find(WallEdgeID);
		if (!CreatedEdge)
		{
			UE_LOG(LogTemp, Error, TEXT("WallCommand: Failed to find created edge %d"), WallEdgeID);
			return;
		}

		// Read back the normalized node positions to use for mesh generation
		// This ensures WallSegmentData::StartLoc/EndLoc matches WallGraph edge direction
		const FWallNode* NormalizedStartNode = LotManager->WallGraph->Nodes.Find(CreatedEdge->FromNodeID);
		const FWallNode* NormalizedEndNode = LotManager->WallGraph->Nodes.Find(CreatedEdge->ToNodeID);

		if (!NormalizedStartNode || !NormalizedEndNode)
		{
			UE_LOG(LogTemp, Error, TEXT("WallCommand: Failed to find normalized edge nodes"));
			return;
		}

		// Use normalized positions for wall mesh generation
		FVector NormalizedStartLoc = NormalizedStartNode->Position;
		FVector NormalizedEndLoc = NormalizedEndNode->Position;

		// Check if this wall closes a loop (both endpoints have 2+ connections)
		// If so, trigger immediate room detection
		const FWallNode* StartNode = NormalizedStartNode;
		const FWallNode* EndNode = NormalizedEndNode;

		if (StartNode && EndNode && LotManager->RoomManager)
		{
			// PROPER LOOP DETECTION: Check if a path exists between start and end nodes
			// WITHOUT using the newly added edge. If a path exists, we've closed a loop.
			bool bClosesLoop = LotManager->WallGraph->DoesPathExistBetweenNodes(
				CreatedEdge->FromNodeID,
				CreatedEdge->ToNodeID,
				WallEdgeID  // Exclude the new edge from path search
			);

			if (bClosesLoop)
			{
				UE_LOG(LogTemp, Warning, TEXT("WallCommand: Wall closes loop (path exists between nodes %d and %d without edge %d). Running room detection..."),
					CreatedEdge->FromNodeID, CreatedEdge->ToNodeID, WallEdgeID);

				// Run incremental room detection on this edge and get detected room IDs
				TArray<int32> DetectedRoomIDs;
				int32 NewRooms = LotManager->RoomManager->DetectRoomFromNewEdgeWithIDs(WallEdgeID, DetectedRoomIDs);

				if (NewRooms > 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("WallCommand: Detected %d new room(s) from edge %d"), NewRooms, WallEdgeID);

					// Auto-generate floors and ceilings for each detected room (unless deferred)
					if (!bDeferRoomGeneration)
					{
						UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
						if (BuildServer)
						{
							for (int32 RoomID : DetectedRoomIDs)
							{
								BuildServer->AutoGenerateRoomFloorsAndCeilings(RoomID);
							}
						}
					}
					else
					{
						UE_LOG(LogTemp, Log, TEXT("WallCommand: Deferring floor/ceiling generation for %d room(s) (bDeferRoomGeneration=true)"), NewRooms);
					}
				}
			}
		}

		// Generate wall mesh for rendering (WallComponent handles presentation)
		// Use NORMALIZED positions to ensure mesh direction matches WallGraph edge direction
		// This guarantees consistent PosY/NegY face orientation regardless of user drag direction
		WallData = LotManager->WallComponent->GenerateWallSection(Level, NormalizedStartLoc, NormalizedEndLoc, WallHeight);
		WallData.WallEdgeID = WallEdgeID; // Link rendering data to graph edge
		LotManager->WallComponent->CommitWallSection(WallData, Pattern, Material);
		WallData = LotManager->WallComponent->GetWallDataByIndex(WallData.WallArrayIndex);

		// Flatten terrain along ground-level walls to prevent Z-fighting (only if terrain is uneven)
		bool bIsGroundFloor = (Level == LotManager->Basements);
		if (bIsGroundFloor && LotManager->TerrainComponent)
		{
			// Check if terrain along the wall path is uneven by sampling adjacent tiles
			bool bNeedsFlattering = false;

			// Check start and end tiles for flatness
			const bool bStartFlat = LotManager->IsTerrainFlatAtTile(Level, CreatedEdge->StartRow, CreatedEdge->StartColumn);
			const bool bEndFlat = LotManager->IsTerrainFlatAtTile(Level, CreatedEdge->EndRow, CreatedEdge->EndColumn);

			if (!bStartFlat || !bEndFlat)
			{
				bNeedsFlattering = true;
			}

			if (bNeedsFlattering)
			{
				// Calculate target height: wall base Z - small offset to prevent Z-fighting
				const FVector LotLocation = LotManager->GetActorLocation();
				const float WallBaseHeight = LotLocation.Z + (LotManager->DefaultWallHeight * (Level - LotManager->Basements));
				const float TerrainOffset = -2.0f; // 2 units below wall base (subtle flattening)
				float TargetHeight = WallBaseHeight + TerrainOffset;

				// LOWER CLAMP: Prevent terrain from extending into basement rooms (check along entire wall path)
				// Check if any tiles along the wall have basement floors below
				if (LotManager->Basements > 0 && LotManager->FloorComponent)
				{
					const int32 BasementLevel = Level - 1;
					bool bHasBasementBelow = false;

					// Sample start and end positions to check for basement floors
					FFloorTileData* StartFloor = LotManager->FloorComponent->FindFloorTile(BasementLevel, CreatedEdge->StartRow, CreatedEdge->StartColumn);
					FFloorTileData* EndFloor = LotManager->FloorComponent->FindFloorTile(BasementLevel, CreatedEdge->EndRow, CreatedEdge->EndColumn);

					if ((StartFloor && StartFloor->bCommitted) || (EndFloor && EndFloor->bCommitted))
					{
						bHasBasementBelow = true;
					}

					if (bHasBasementBelow)
					{
						// There's a basement room below this wall - clamp terrain to stay above basement ceiling
						const float BasementCeilingZ = LotLocation.Z;
						const float TerrainThickness = LotManager->TerrainComponent->TerrainThickness;
						const float MinTargetHeight = BasementCeilingZ + TerrainThickness;

						if (TargetHeight < MinTargetHeight)
						{
							TargetHeight = MinTargetHeight;
							UE_LOG(LogTemp, Warning, TEXT("WallCommand: Clamped terrain height to %.2f to prevent extending into basement room (ceiling at %.2f, thickness %.2f)"),
								TargetHeight, BasementCeilingZ, TerrainThickness);
						}
					}
				}

				// UPPER CLAMP: Prevent terrain from being raised through ground floor walls (apply AFTER basement clamp)
				// This handles the case where basement clamp would raise terrain above the wall base
				// Maximum allowable terrain height = wall base (not below, to prevent clipping through wall)
				const float MaxTargetHeight = WallBaseHeight;
				if (TargetHeight > MaxTargetHeight)
				{
					TargetHeight = MaxTargetHeight;
					UE_LOG(LogTemp, Warning, TEXT("WallCommand: Clamped terrain to %.2f to prevent raising through ground floor wall (basement conflict)"),
						TargetHeight);
				}

				// Flatten terrain corners along wall path (handles horizontal, vertical, diagonal walls)
				// Use bBypassLock=true to allow flattening even though wall now exists (prevents lock)
				LotManager->TerrainComponent->FlattenTerrainAlongWall(
					Level,
					CreatedEdge->StartRow, CreatedEdge->StartColumn,
					CreatedEdge->EndRow, CreatedEdge->EndColumn,
					TargetHeight,
					true  // bBypassLock
				);

				UE_LOG(LogTemp, Log, TEXT("WallCommand: Flattened uneven terrain along wall from (%d,%d) to (%d,%d) at height %.2f"),
					CreatedEdge->StartRow, CreatedEdge->StartColumn,
					CreatedEdge->EndRow, CreatedEdge->EndColumn,
					TargetHeight);
			}
			else
			{
				UE_LOG(LogTemp, Verbose, TEXT("WallCommand: Terrain already flat along wall from (%d,%d) to (%d,%d), skipping flattening"),
					CreatedEdge->StartRow, CreatedEdge->StartColumn,
					CreatedEdge->EndRow, CreatedEdge->EndColumn);
			}
		}

		bWallCreated = true;
		bCommitted = true;

		// Trigger immediate cutaway update for the newly created wall
		TriggerCutawayUpdate();

		UE_LOG(LogTemp, Log, TEXT("WallCommand: Created wall graph edge %d from (%s) to (%s), Grid coords: (%d,%d) to (%d,%d)"),
			WallEdgeID, *NormalizedStartLoc.ToString(), *NormalizedEndLoc.ToString(),
			CreatedEdge->StartRow, CreatedEdge->StartColumn, CreatedEdge->EndRow, CreatedEdge->EndColumn);
	}
	else if (OperationMode == EWallOperationMode::Delete)
	{
		TArray<int32> ConnectedEdgesToRegenerate;
		TSet<int32> AffectedRooms;

		// Before removing the edge, collect connected walls that need regeneration
		if (WallEdgeID != -1)
		{
			const FWallEdge* EdgeToDelete = LotManager->WallGraph->Edges.Find(WallEdgeID);
			if (EdgeToDelete)
			{
				// Store affected rooms for invalidation
				if (EdgeToDelete->Room1 > 0)
				{
					AffectedRooms.Add(EdgeToDelete->Room1);
				}
				if (EdgeToDelete->Room2 > 0)
				{
					AffectedRooms.Add(EdgeToDelete->Room2);
				}

				// BURB-4 FIX: Clean up pool water when deleting a pool wall
				// Pool water is tied to room IDs, so we must remove it before the wall is deleted
				if (EdgeToDelete->bIsPoolWall && LotManager->WaterComponent)
				{
					for (int32 RoomID : AffectedRooms)
					{
						if (LotManager->WaterComponent->RemovePoolWater(RoomID))
						{
							UE_LOG(LogTemp, Log, TEXT("WallCommand: Removed pool water for room %d (pool wall deleted)"), RoomID);
						}
					}
				}

				// Get nodes at both ends of this wall
				const FWallNode* FromNode = LotManager->WallGraph->Nodes.Find(EdgeToDelete->FromNodeID);
				const FWallNode* ToNode = LotManager->WallGraph->Nodes.Find(EdgeToDelete->ToNodeID);

				// Collect all edges connected to these nodes (except the one being deleted)
				if (FromNode)
				{
					for (int32 EdgeID : FromNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID)
						{
							ConnectedEdgesToRegenerate.AddUnique(EdgeID);
						}
					}
				}
				if (ToNode)
				{
					for (int32 EdgeID : ToNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID)
						{
							ConnectedEdgesToRegenerate.AddUnique(EdgeID);
						}
					}
				}

				UE_LOG(LogTemp, Log, TEXT("WallCommand: Deleting wall edge %d, will regenerate %d connected walls, invalidate %d rooms"),
					WallEdgeID, ConnectedEdgesToRegenerate.Num(), AffectedRooms.Num());
			}

			// Remove from wall graph
			LotManager->WallGraph->RemoveEdge(WallEdgeID);

			// CRITICAL: Rebuild intersection data AFTER removing edge but BEFORE regenerating walls
			// This ensures connected walls don't see the deleted wall in their corner mitring calculations
			LotManager->WallGraph->RebuildIntersections();
		}

		// Destroy the wall section mesh
		LotManager->WallComponent->DestroyWallSection(WallData);

		// Regenerate connected walls to update their corner mitring
		for (int32 EdgeID : ConnectedEdgesToRegenerate)
		{
			// Find the wall data for this edge
			for (FWallSegmentData& WallSegment : LotManager->WallComponent->WallDataArray)
			{
				if (WallSegment.WallEdgeID == EdgeID && WallSegment.bCommitted)
				{
					// CRITICAL: Clear connection arrays before regenerating (deleted wall is still in these arrays)
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsSections.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtStartDir.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtEndDir.Empty();

					LotManager->WallComponent->RegenerateWallSection(WallSegment, false);
					UE_LOG(LogTemp, Log, TEXT("WallCommand: Regenerated connected wall edge %d"), EdgeID);
					break;
				}
			}
		}

		// Invalidate affected rooms and reassign wall room IDs
		if (LotManager->RoomManager && LotManager->WallGraph)
		{
			for (int32 RoomID : AffectedRooms)
			{
				// Get all walls that bordered this room before invalidating
				TArray<int32> RoomBoundaryEdges = LotManager->WallGraph->GetEdgesBoundingRoom(RoomID);

				// Invalidate the room (marks it dirty, allows re-detection)
				LotManager->RoomManager->InvalidateRoom(RoomID);
				UE_LOG(LogTemp, Log, TEXT("WallCommand: Invalidated room %d (wall deletion may have opened the room)"), RoomID);

				// Reassign room IDs to all walls that bordered this room
				// This updates Room1/Room2 to reflect current state (0 if outside, or nested room ID)
				for (int32 EdgeID : RoomBoundaryEdges)
				{
					LotManager->WallGraph->AssignRoomIDsToNewWall(EdgeID);
				}

				UE_LOG(LogTemp, Log, TEXT("WallCommand: Reassigned room IDs to %d walls that bordered room %d"),
					RoomBoundaryEdges.Num(), RoomID);
			}
		}

		bWallCreated = true; // Actually deleted, but we use this flag to track if operation succeeded
		bCommitted = true;

		UE_LOG(LogTemp, Log, TEXT("WallCommand: Deleted wall from (%s) to (%s)"),
			*StartLoc.ToString(), *EndLoc.ToString());
	}
}

void UWallCommand::Undo()
{
	if (!bWallCreated || !LotManager || !LotManager->WallComponent || !LotManager->WallGraph)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallCommand: Cannot undo - wall not created or component invalid"));
		return;
	}

	if (OperationMode == EWallOperationMode::Create)
	{
		// Undo create = destroy the wall from both systems
		TArray<int32> ConnectedEdgesToRegenerate;
		TSet<int32> AffectedRooms;

		if (WallEdgeID != -1)
		{
			const FWallEdge* EdgeToDelete = LotManager->WallGraph->Edges.Find(WallEdgeID);
			if (EdgeToDelete)
			{
				// Store affected rooms
				if (EdgeToDelete->Room1 > 0) AffectedRooms.Add(EdgeToDelete->Room1);
				if (EdgeToDelete->Room2 > 0) AffectedRooms.Add(EdgeToDelete->Room2);

				// BURB-4 FIX: Clean up pool water when undoing pool wall creation
				if (EdgeToDelete->bIsPoolWall && LotManager->WaterComponent)
				{
					for (int32 RoomID : AffectedRooms)
					{
						if (LotManager->WaterComponent->RemovePoolWater(RoomID))
						{
							UE_LOG(LogTemp, Log, TEXT("WallCommand::Undo: Removed pool water for room %d (pool wall creation undone)"), RoomID);
						}
					}
				}

				// Collect connected walls
				const FWallNode* FromNode = LotManager->WallGraph->Nodes.Find(EdgeToDelete->FromNodeID);
				const FWallNode* ToNode = LotManager->WallGraph->Nodes.Find(EdgeToDelete->ToNodeID);
				if (FromNode)
				{
					for (int32 EdgeID : FromNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}
				if (ToNode)
				{
					for (int32 EdgeID : ToNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}
			}

			LotManager->WallGraph->RemoveEdge(WallEdgeID);

			// CRITICAL: Rebuild intersections after removal
			LotManager->WallGraph->RebuildIntersections();
		}
		LotManager->WallComponent->DestroyWallSection(WallData);

		// Regenerate connected walls
		for (int32 EdgeID : ConnectedEdgesToRegenerate)
		{
			for (FWallSegmentData& WallSegment : LotManager->WallComponent->WallDataArray)
			{
				if (WallSegment.WallEdgeID == EdgeID && WallSegment.bCommitted)
				{
					// CRITICAL: Clear connection arrays before regenerating
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsSections.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtStartDir.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtEndDir.Empty();

					LotManager->WallComponent->RegenerateWallSection(WallSegment, false);
					break;
				}
			}
		}

		// Invalidate affected rooms and reassign wall room IDs
		if (LotManager->RoomManager && LotManager->WallGraph)
		{
			for (int32 RoomID : AffectedRooms)
			{
				// Get all walls that bordered this room
				TArray<int32> RoomBoundaryEdges = LotManager->WallGraph->GetEdgesBoundingRoom(RoomID);

				// Invalidate the room
				LotManager->RoomManager->InvalidateRoom(RoomID);

				// Reassign room IDs to all walls that bordered this room
				for (int32 EdgeID : RoomBoundaryEdges)
				{
					LotManager->WallGraph->AssignRoomIDsToNewWall(EdgeID);
				}

				UE_LOG(LogTemp, Log, TEXT("WallCommand::Undo: Invalidated room %d, reassigned %d walls"),
					RoomID, RoomBoundaryEdges.Num());
			}
		}

		bWallCreated = false;

		UE_LOG(LogTemp, Log, TEXT("WallCommand::Undo: Destroyed wall from (%s) to (%s)"),
			*StartLoc.ToString(), *EndLoc.ToString());
	}
	else if (OperationMode == EWallOperationMode::Delete)
	{
		// Undo delete = restore the wall to both systems
		TArray<int32> ConnectedEdgesToRegenerate;

		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (LotManager->LocationToTile(StartLoc, StartRow, StartColumn) &&
			LotManager->LocationToTile(EndLoc, EndRow, EndColumn))
		{
			int32 StartNodeID = LotManager->WallGraph->AddNode(StartLoc, Level, StartRow, StartColumn);
			int32 EndNodeID = LotManager->WallGraph->AddNode(EndLoc, Level, EndRow, EndColumn);
			WallEdgeID = LotManager->WallGraph->AddEdge(StartNodeID, EndNodeID, WallHeight, 20.0f, Pattern);

			// Auto-assign room IDs to the restored wall
			if (WallEdgeID != -1)
			{
				LotManager->WallGraph->AssignRoomIDsToNewWall(WallEdgeID);

				// Get the edge to read back normalized direction
				const FWallEdge* CreatedEdge = LotManager->WallGraph->Edges.Find(WallEdgeID);
				if (CreatedEdge)
				{
					// Update WallData to use normalized positions for mesh consistency
					const FWallNode* NormalizedStartNode = LotManager->WallGraph->Nodes.Find(CreatedEdge->FromNodeID);
					const FWallNode* NormalizedEndNode = LotManager->WallGraph->Nodes.Find(CreatedEdge->ToNodeID);
					if (NormalizedStartNode && NormalizedEndNode)
					{
						WallData.StartLoc = NormalizedStartNode->Position;
						WallData.EndLoc = NormalizedEndNode->Position;
					}
				}

				// Collect connected walls for regeneration
				const FWallNode* StartNode = LotManager->WallGraph->Nodes.Find(StartNodeID);
				const FWallNode* EndNode = LotManager->WallGraph->Nodes.Find(EndNodeID);

				if (StartNode)
				{
					for (int32 EdgeID : StartNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}
				if (EndNode)
				{
					for (int32 EdgeID : EndNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}

				// Check if restoring this wall closes a loop using proper path detection
				if (StartNode && EndNode && LotManager->RoomManager)
				{
					const FWallEdge* RestoredEdge = LotManager->WallGraph->Edges.Find(WallEdgeID);
					if (RestoredEdge)
					{
						bool bClosesLoop = LotManager->WallGraph->DoesPathExistBetweenNodes(
							RestoredEdge->FromNodeID,
							RestoredEdge->ToNodeID,
							WallEdgeID  // Exclude the restored edge from path search
						);

						if (bClosesLoop)
						{
							UE_LOG(LogTemp, Warning, TEXT("WallCommand::Undo (restore): Wall closes loop. Running room detection..."));
							TArray<int32> DetectedRoomIDs;
							int32 NewRooms = LotManager->RoomManager->DetectRoomFromNewEdgeWithIDs(WallEdgeID, DetectedRoomIDs);
							if (NewRooms > 0)
							{
								UE_LOG(LogTemp, Warning, TEXT("WallCommand::Undo (restore): Detected %d new room(s)"), NewRooms);

								// Auto-generate floors and ceilings for each detected room
								UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
								if (BuildServer)
								{
									for (int32 RoomID : DetectedRoomIDs)
									{
										BuildServer->AutoGenerateRoomFloorsAndCeilings(RoomID);
									}
								}
							}
						}
					}
				}
			}
		}

		// CRITICAL: Rebuild intersections after adding wall edge
		LotManager->WallGraph->RebuildIntersections();

		LotManager->WallComponent->CommitWallSection(WallData, Pattern, Material);

		// Regenerate connected walls to update their corner mitring
		for (int32 EdgeID : ConnectedEdgesToRegenerate)
		{
			for (FWallSegmentData& WallSegment : LotManager->WallComponent->WallDataArray)
			{
				if (WallSegment.WallEdgeID == EdgeID && WallSegment.bCommitted)
				{
					// CRITICAL: Clear connection arrays before regenerating
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsSections.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtStartDir.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtEndDir.Empty();

					LotManager->WallComponent->RegenerateWallSection(WallSegment, false);
					break;
				}
			}
		}

		bWallCreated = false;

		// Trigger immediate cutaway update for the restored wall
		TriggerCutawayUpdate();

		UE_LOG(LogTemp, Log, TEXT("WallCommand::Undo: Restored wall from (%s) to (%s), regenerated %d connected walls"),
			*StartLoc.ToString(), *EndLoc.ToString(), ConnectedEdgesToRegenerate.Num());
	}
}

void UWallCommand::Redo()
{
	if (!LotManager || !LotManager->WallComponent || !LotManager->WallGraph)
	{
		UE_LOG(LogTemp, Warning, TEXT("WallCommand: Cannot redo - component invalid"));
		return;
	}

	if (OperationMode == EWallOperationMode::Create)
	{
		// Re-add to wall graph
		TArray<int32> ConnectedEdgesToRegenerate;
		FVector NormalizedStartLoc = StartLoc;
		FVector NormalizedEndLoc = EndLoc;

		int32 StartRow, StartColumn, EndRow, EndColumn;
		if (LotManager->LocationToTile(StartLoc, StartRow, StartColumn) &&
			LotManager->LocationToTile(EndLoc, EndRow, EndColumn))
		{
			int32 StartNodeID = LotManager->WallGraph->AddNode(StartLoc, Level, StartRow, StartColumn);
			int32 EndNodeID = LotManager->WallGraph->AddNode(EndLoc, Level, EndRow, EndColumn);
			WallEdgeID = LotManager->WallGraph->AddEdge(StartNodeID, EndNodeID, WallHeight, 20.0f, Pattern);

			// Auto-assign room IDs to the re-created wall
			if (WallEdgeID != -1)
			{
				LotManager->WallGraph->AssignRoomIDsToNewWall(WallEdgeID);

				// Get the edge to read back normalized direction
				const FWallEdge* CreatedEdge = LotManager->WallGraph->Edges.Find(WallEdgeID);
				if (CreatedEdge)
				{
					// Read back normalized positions for mesh generation
					const FWallNode* NormalizedStartNode = LotManager->WallGraph->Nodes.Find(CreatedEdge->FromNodeID);
					const FWallNode* NormalizedEndNode = LotManager->WallGraph->Nodes.Find(CreatedEdge->ToNodeID);
					if (NormalizedStartNode && NormalizedEndNode)
					{
						NormalizedStartLoc = NormalizedStartNode->Position;
						NormalizedEndLoc = NormalizedEndNode->Position;
					}
				}

				// Collect connected walls for regeneration
				const FWallNode* StartNode = LotManager->WallGraph->Nodes.Find(StartNodeID);
				const FWallNode* EndNode = LotManager->WallGraph->Nodes.Find(EndNodeID);

				if (StartNode)
				{
					for (int32 EdgeID : StartNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}
				if (EndNode)
				{
					for (int32 EdgeID : EndNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}

				// Check if this wall closes a loop
				if (StartNode && EndNode && LotManager->RoomManager)
				{
					bool bStartNodeHasConnections = StartNode->ConnectedEdgeIDs.Num() >= 2;
					bool bEndNodeHasConnections = EndNode->ConnectedEdgeIDs.Num() >= 2;

					if (bStartNodeHasConnections && bEndNodeHasConnections)
					{
						UE_LOG(LogTemp, Warning, TEXT("WallCommand::Redo: Wall closes loop. Running room detection..."));
						TArray<int32> DetectedRoomIDs;
						int32 NewRooms = LotManager->RoomManager->DetectRoomFromNewEdgeWithIDs(WallEdgeID, DetectedRoomIDs);
						if (NewRooms > 0)
						{
							UE_LOG(LogTemp, Warning, TEXT("WallCommand::Redo: Detected %d new room(s)"), NewRooms);

							// Auto-generate floors and ceilings for each detected room
							UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
							if (BuildServer)
							{
								for (int32 RoomID : DetectedRoomIDs)
								{
									BuildServer->AutoGenerateRoomFloorsAndCeilings(RoomID);
								}
							}
						}
					}
				}
			}
		}

		// CRITICAL: Rebuild intersections after adding wall edge
		LotManager->WallGraph->RebuildIntersections();

		// Regenerate wall section data to get fresh array index
		// Use NORMALIZED positions to ensure consistent mesh direction
		WallData = LotManager->WallComponent->GenerateWallSection(Level, NormalizedStartLoc, NormalizedEndLoc, WallHeight);
		WallData.WallEdgeID = WallEdgeID; // Link rendering data to graph edge

		// Commit the wall to the component
		LotManager->WallComponent->CommitWallSection(WallData, Pattern, Material);

		// Regenerate connected walls to update their corner mitring
		for (int32 EdgeID : ConnectedEdgesToRegenerate)
		{
			for (FWallSegmentData& WallSegment : LotManager->WallComponent->WallDataArray)
			{
				if (WallSegment.WallEdgeID == EdgeID && WallSegment.bCommitted)
				{
					// CRITICAL: Clear connection arrays before regenerating
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsSections.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtStartDir.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtEndDir.Empty();

					LotManager->WallComponent->RegenerateWallSection(WallSegment, false);
					break;
				}
			}
		}

		// Retrieve updated data with fresh WallSectionData pointer (CommitWallSection regenerates the mesh)
		WallData = LotManager->WallComponent->GetWallDataByIndex(WallData.WallArrayIndex);
		bWallCreated = true;

		// Trigger immediate cutaway update for the re-created wall
		TriggerCutawayUpdate();

		UE_LOG(LogTemp, Log, TEXT("WallCommand::Redo: Redid wall from (%s) to (%s), regenerated %d connected walls"),
			*NormalizedStartLoc.ToString(), *NormalizedEndLoc.ToString(), ConnectedEdgesToRegenerate.Num());
	}
	else if (OperationMode == EWallOperationMode::Delete)
	{
		// Re-delete the wall from both systems
		TArray<int32> ConnectedEdgesToRegenerate;
		TSet<int32> AffectedRooms;

		if (WallEdgeID != -1)
		{
			const FWallEdge* EdgeToDelete = LotManager->WallGraph->Edges.Find(WallEdgeID);
			if (EdgeToDelete)
			{
				// Store affected rooms
				if (EdgeToDelete->Room1 > 0) AffectedRooms.Add(EdgeToDelete->Room1);
				if (EdgeToDelete->Room2 > 0) AffectedRooms.Add(EdgeToDelete->Room2);

				// BURB-4 FIX: Clean up pool water when redoing pool wall deletion
				if (EdgeToDelete->bIsPoolWall && LotManager->WaterComponent)
				{
					for (int32 RoomID : AffectedRooms)
					{
						if (LotManager->WaterComponent->RemovePoolWater(RoomID))
						{
							UE_LOG(LogTemp, Log, TEXT("WallCommand::Redo: Removed pool water for room %d (pool wall deletion redone)"), RoomID);
						}
					}
				}

				// Collect connected walls
				const FWallNode* FromNode = LotManager->WallGraph->Nodes.Find(EdgeToDelete->FromNodeID);
				const FWallNode* ToNode = LotManager->WallGraph->Nodes.Find(EdgeToDelete->ToNodeID);
				if (FromNode)
				{
					for (int32 EdgeID : FromNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}
				if (ToNode)
				{
					for (int32 EdgeID : ToNode->ConnectedEdgeIDs)
					{
						if (EdgeID != WallEdgeID) ConnectedEdgesToRegenerate.AddUnique(EdgeID);
					}
				}
			}

			LotManager->WallGraph->RemoveEdge(WallEdgeID);

			// CRITICAL: Rebuild intersections after removal
			LotManager->WallGraph->RebuildIntersections();
		}
		LotManager->WallComponent->DestroyWallSection(WallData);

		// Regenerate connected walls
		for (int32 EdgeID : ConnectedEdgesToRegenerate)
		{
			for (FWallSegmentData& WallSegment : LotManager->WallComponent->WallDataArray)
			{
				if (WallSegment.WallEdgeID == EdgeID && WallSegment.bCommitted)
				{
					// CRITICAL: Clear connection arrays before regenerating
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsSections.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtStartDir.Empty();
					LotManager->WallComponent->WallDataArray[WallSegment.WallArrayIndex].ConnectedWallsAtEndDir.Empty();

					LotManager->WallComponent->RegenerateWallSection(WallSegment, false);
					break;
				}
			}
		}

		// Invalidate affected rooms and reassign wall room IDs
		if (LotManager->RoomManager && LotManager->WallGraph)
		{
			for (int32 RoomID : AffectedRooms)
			{
				// Get all walls that bordered this room
				TArray<int32> RoomBoundaryEdges = LotManager->WallGraph->GetEdgesBoundingRoom(RoomID);

				// Invalidate the room
				LotManager->RoomManager->InvalidateRoom(RoomID);

				// Reassign room IDs to all walls that bordered this room
				for (int32 EdgeID : RoomBoundaryEdges)
				{
					LotManager->WallGraph->AssignRoomIDsToNewWall(EdgeID);
				}

				UE_LOG(LogTemp, Log, TEXT("WallCommand::Redo: Invalidated room %d, reassigned %d walls"),
					RoomID, RoomBoundaryEdges.Num());
			}
		}

		bWallCreated = true;

		UE_LOG(LogTemp, Log, TEXT("WallCommand::Redo: Re-deleted wall from (%s) to (%s), regenerated %d connected walls"),
			*StartLoc.ToString(), *EndLoc.ToString(), ConnectedEdgesToRegenerate.Num());
	}
}

FString UWallCommand::GetDescription() const
{
	if (OperationMode == EWallOperationMode::Create)
	{
		return FString::Printf(TEXT("Build Wall (%.0f, %.0f) to (%.0f, %.0f)"),
			StartLoc.X, StartLoc.Y, EndLoc.X, EndLoc.Y);
	}
	else
	{
		return FString::Printf(TEXT("Delete Wall (%.0f, %.0f) to (%.0f, %.0f)"),
			StartLoc.X, StartLoc.Y, EndLoc.X, EndLoc.Y);
	}
}

bool UWallCommand::IsValid() const
{
	// Command is valid if the lot manager and wall component still exist
	return LotManager && LotManager->WallComponent;
}

void UWallCommand::TriggerCutawayUpdate()
{
	// Get the player pawn to trigger cutaway update
	if (!LotManager)
	{
		return;
	}

	UWorld* World = LotManager->GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}

	ABurbPawn* BurbPawn = Cast<ABurbPawn>(PlayerController->GetPawn());
	if (!BurbPawn)
	{
		return;
	}

	// Force immediate cutaway update and invalidate sector cache
	// This ensures partial cutaway mode works correctly for newly created walls
	BurbPawn->ForceUpdateCutaway();

	UE_LOG(LogTemp, Verbose, TEXT("WallCommand: Triggered cutaway update for newly created/modified wall"));
}

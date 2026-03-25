// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/LotSerializationSubsystem.h"
#include "Actors/LotManager.h"
#include "Actors/BurbPawn.h"
#include "Components/WallGraphComponent.h"
#include "Data/WallGraphData.h"
#include "Components/FloorComponent.h"
#include "Components/RoofComponent.h"
#include "Components/StairsComponent.h"
#include "Components/TerrainComponent.h"
#include "Components/GridComponent.h"
#include "Components/WaterComponent.h"
#include "Components/FenceComponent.h"
#include "Data/FenceItem.h"
#include "Actors/Roofs/RoofBase.h"
#include "Actors/Roofs/GableRoof.h"
#include "Actors/Roofs/HipRoof.h"
#include "Actors/Roofs/ShedRoof.h"
#include "Actors/StairsBase.h"
#include "Actors/PortalBase.h"
#include "Actors/PlaceableObject.h"
#include "Data/WallPattern.h"
#include "Data/FloorPattern.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
#include "UObject/ConstructorHelpers.h"

void ULotSerializationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("LotSerializationSubsystem initialized"));
}

void ULotSerializationSubsystem::Deinitialize()
{
	Super::Deinitialize();
	UE_LOG(LogTemp, Log, TEXT("LotSerializationSubsystem deinitialized"));
}

// ========================================
// High-Level Serialization
// ========================================

FSerializedLotData ULotSerializationSubsystem::SerializeLot(ALotManager* LotManager)
{
	FSerializedLotData Data;

	if (!LotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("SerializeLot: LotManager is null"));
		return Data;
	}

	UE_LOG(LogTemp, Log, TEXT("Serializing lot: %s"), *LotManager->GetName());

	// Serialize grid configuration
	SerializeGridConfig(LotManager, Data);

	// Serialize wall graph
	if (LotManager->WallGraph)
	{
		SerializeWallGraph(LotManager->WallGraph, Data);
	}

	// Serialize floor component
	if (LotManager->FloorComponent)
	{
		SerializeFloorComponent(LotManager->FloorComponent, Data);
	}

	// Serialize roof actors
	for (ARoofBase* RoofActor : LotManager->RoofActors)
	{
		if (RoofActor && IsValid(RoofActor))
		{
			FSerializedRoofData RoofData;
			RoofData.RoofClass = GetClassPath(RoofActor->GetClass());
			RoofData.StartLocation = RoofActor->GetActorLocation();
			RoofData.Direction = RoofActor->RoofDirection;
			RoofData.Width = RoofActor->RoofDimensions.GetWidth();
			RoofData.Length = RoofActor->RoofDimensions.GetLength();
			RoofData.Pitch = RoofActor->RoofDimensions.Pitch;
			RoofData.Height = RoofActor->RoofDimensions.Height;
			RoofData.RoofThickness = RoofActor->RoofThickness;
			RoofData.GableThickness = RoofActor->GableThickness;
			RoofData.Level = RoofActor->Level;
			RoofData.MaterialPath = GetMaterialPath(RoofActor->RoofMeshComponent ? RoofActor->RoofMeshComponent->GetMaterial(0) : nullptr);

			Data.Roofs.Add(RoofData);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized %d roof actors"), Data.Roofs.Num());

	// Serialize stairs actors
	for (AStairsBase* StairsActor : LotManager->StairsActors)
	{
		if (StairsActor && IsValid(StairsActor))
		{
			FSerializedStairsData StairsData;
			StairsData.StairsClass = GetClassPath(StairsActor->GetClass());
			StairsData.StartLocation = StairsActor->GetActorLocation();
			StairsData.Rotation = StairsActor->GetActorRotation();
			StairsData.Direction = StairsActor->StairsDirection;
			StairsData.StartLevel = StairsActor->Level;
			// Calculate end level based on number of stair modules (each tread is ~0.5 level, landing is full level)
			int32 TreadCount = 0;
			for (const FStairModuleStructure& Module : StairsActor->StairsData.Structures)
			{
				if (Module.StairType == EStairModuleType::Tread)
				{
					TreadCount++;
				}
			}
			StairsData.EndLevel = StairsActor->Level + FMath::Max(1, TreadCount / 10); // Approximate end level
			StairsData.StairsThickness = StairsActor->StairsThickness;
			StairsData.TreadMeshPath = StairsActor->StairTreadMesh ? StairsActor->StairTreadMesh->GetPathName() : FString();
			StairsData.LandingMeshPath = StairsActor->StairLandingMesh ? StairsActor->StairLandingMesh->GetPathName() : FString();

			// Serialize module structures
			for (const FStairModuleStructure& Module : StairsActor->StairsData.Structures)
			{
				FSerializedStairModule SerializedModule;
				SerializedModule.ModuleType = static_cast<uint8>(Module.StairType);
				SerializedModule.TurningSocket = static_cast<uint8>(Module.TurningSocket);
				StairsData.Modules.Add(SerializedModule);
			}

			Data.Stairs.Add(StairsData);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized %d stairs actors"), Data.Stairs.Num());

	// Serialize terrain component
	if (LotManager->TerrainComponent)
	{
		SerializeTerrainComponent(LotManager->TerrainComponent, Data);
	}

	// Serialize water component (pools)
	if (LotManager->WaterComponent)
	{
		SerializeWaterComponent(LotManager->WaterComponent, Data);
	}

	// Serialize fence component
	if (LotManager->FenceComponent)
	{
		SerializeFenceComponent(LotManager->FenceComponent, Data);
	}

	// Serialize portals (doors/windows)
	SerializePortals(LotManager, Data);

	// Serialize placed objects (furniture, decorations)
	SerializePlacedObjects(LotManager, Data);

	// Serialize room IDs
	SerializeRoomIDs(LotManager, Data);

	// Set metadata
	Data.LotName = LotManager->GetName();
	Data.SaveTimestamp = FDateTime::Now();

	UE_LOG(LogTemp, Log, TEXT("Lot serialization complete. Walls: %d, Floors: %d, Roofs: %d, Stairs: %d, Portals: %d, Objects: %d"),
		Data.WallEdges.Num(), Data.FloorTiles.Num(), Data.Roofs.Num(), Data.Stairs.Num(), Data.Portals.Num(), Data.PlacedObjects.Num());

	return Data;
}

bool ULotSerializationSubsystem::DeserializeLot(ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!LotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("DeserializeLot: LotManager is null"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("Deserializing lot: %s"), *Data.LotName);

	// Validate data
	if (!ValidateLotData(Data))
	{
		UE_LOG(LogTemp, Error, TEXT("DeserializeLot: Lot data validation failed"));
		return false;
	}

	// Clear existing lot data
	LotManager->ClearLot();

	// Apply grid configuration
	LotManager->GridSizeX = Data.GridConfig.GridSizeX;
	LotManager->GridSizeY = Data.GridConfig.GridSizeY;
	LotManager->GridTileSize = Data.GridConfig.GridTileSize;
	LotManager->Floors = Data.GridConfig.Floors;
	LotManager->Basements = Data.GridConfig.Basements;
	LotManager->GenerateGrid();

	// Recreate upper floor tiles (tiles above ground level)
	// GenerateGrid() only creates ground floor tiles, so we need to manually add upper floor tiles
	for (int32 PackedCoord : Data.UpperFloorTiles)
	{
		// Unpack coordinates: (Level << 24) | (Row << 12) | Column
		int32 Level = (PackedCoord >> 24) & 0xFF;
		int32 Row = (PackedCoord >> 12) & 0xFFF;
		int32 Column = PackedCoord & 0xFFF;

		// Create tile at this location
		FTileData NewTile;
		NewTile.TileCoord = FVector2D(Column, Row);
		NewTile.Level = Level;
		NewTile.SetRoomID(0); // Will be set later by DeserializeRoomIDs

		// Calculate world location
		FVector TileLocation;
		if (LotManager->TileToGridLocation(Level, Row, Column, true, TileLocation))
		{
			NewTile.Center = TileLocation;
		}

		// Add to grid data
		int32 TileIndex = LotManager->GridData.Add(NewTile);

		// Update spatial hash map
		FIntVector TileKey(Column, Row, Level);
		LotManager->TileIndexMap.Add(TileKey, TileIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("Recreated %d upper floor tiles"), Data.UpperFloorTiles.Num());

	// Restore current level (must be done after GenerateGrid)
	LotManager->SetCurrentLevel(Data.GridConfig.CurrentLevel);

	// Deserialize wall graph
	if (LotManager->WallGraph)
	{
		if (!DeserializeWallGraph(LotManager->WallGraph, LotManager, Data))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Wall graph deserialization failed"));
		}
	}

	// Deserialize floor component
	if (LotManager->FloorComponent)
	{
		if (!DeserializeFloorComponent(LotManager->FloorComponent, LotManager, Data))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Floor component deserialization failed"));
		}
	}

	// Clear existing roof actors
	for (ARoofBase* RoofActor : LotManager->RoofActors)
	{
		if (RoofActor && IsValid(RoofActor))
		{
			RoofActor->Destroy();
		}
	}
	LotManager->RoofActors.Empty();

	// Deserialize roof actors
	for (const FSerializedRoofData& RoofData : Data.Roofs)
	{
		// Resolve roof class
		UClass* RoofClass = ResolveClassPath(RoofData.RoofClass);
		if (!RoofClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeRoofs: Could not resolve roof class: %s, using default Gable"), *RoofData.RoofClass);
			// Use default gable roof if class can't be resolved
			RoofClass = AGableRoof::StaticClass();
		}

		// Create FRoofDimensions from serialized data
		// Note: We're storing simple dimensions (width, length, pitch) but FRoofDimensions has more detail
		// For now, we'll create a basic roof dimensions structure and let the roof actor calculate the rest
		FRoofDimensions RoofDimensions;
		RoofDimensions.Pitch = RoofData.Pitch;
		RoofDimensions.Height = RoofData.Height;
		RoofDimensions.bUsePitchInsteadOfHeight = (RoofData.Height == 0.0f);

		// Calculate distances from width/length (simplified - assumes centered roof)
		float HalfWidth = RoofData.Width / 2.0f;
		float HalfLength = RoofData.Length / 2.0f;
		RoofDimensions.LeftDistance = HalfWidth;
		RoofDimensions.RightDistance = HalfWidth;
		RoofDimensions.FrontDistance = HalfLength;
		RoofDimensions.BackDistance = HalfLength;

		// Set roof type based on class
		if (RoofClass == AGableRoof::StaticClass())
		{
			RoofDimensions.RoofType = ERoofType::Gable;
		}
		else if (RoofClass == AHipRoof::StaticClass())
		{
			RoofDimensions.RoofType = ERoofType::Hip;
		}
		else if (RoofClass == AShedRoof::StaticClass())
		{
			RoofDimensions.RoofType = ERoofType::Shed;
		}
		else
		{
			RoofDimensions.RoofType = ERoofType::Gable; // Default
		}

		// Resolve material
		UMaterialInstance* RoofMaterial = nullptr;
		if (!RoofData.MaterialPath.IsEmpty())
		{
			RoofMaterial = Cast<UMaterialInstance>(ResolveMaterialPath(RoofData.MaterialPath));
		}

		// Spawn the roof actor using LotManager's spawn method
		ARoofBase* SpawnedRoof = LotManager->SpawnRoofActor(
			RoofData.StartLocation,
			RoofData.Direction,
			RoofDimensions,
			RoofData.RoofThickness,
			RoofData.GableThickness,
			RoofMaterial
		);

		if (SpawnedRoof)
		{
			SpawnedRoof->Level = RoofData.Level;
			UE_LOG(LogTemp, Log, TEXT("Spawned roof actor at level %d: %.1fx%.1f, pitch=%.1f"),
				RoofData.Level, RoofData.Width, RoofData.Length, RoofData.Pitch);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to spawn roof actor at level %d"), RoofData.Level);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized %d roof actors"), Data.Roofs.Num());

	// Clear existing stairs actors
	for (AStairsBase* StairsActor : LotManager->StairsActors)
	{
		if (StairsActor && IsValid(StairsActor))
		{
			StairsActor->Destroy();
		}
	}
	LotManager->StairsActors.Empty();

	// Deserialize stairs actors
	for (const FSerializedStairsData& StairsData : Data.Stairs)
	{
		// Resolve stairs class
		UClass* StairsClass = ResolveClassPath(StairsData.StairsClass);
		if (!StairsClass || !StairsClass->IsChildOf(AStairsBase::StaticClass()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Invalid stairs class: %s, using default"), *StairsData.StairsClass);
			StairsClass = AStairsBase::StaticClass(); // Default fallback
		}

		// Resolve meshes
		UStaticMesh* TreadMesh = nullptr;
		if (!StairsData.TreadMeshPath.IsEmpty())
		{
			TreadMesh = LoadObject<UStaticMesh>(nullptr, *StairsData.TreadMeshPath);
			if (!TreadMesh)
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to load tread mesh: %s"), *StairsData.TreadMeshPath);
			}
		}

		UStaticMesh* LandingMesh = nullptr;
		if (!StairsData.LandingMeshPath.IsEmpty())
		{
			LandingMesh = LoadObject<UStaticMesh>(nullptr, *StairsData.LandingMeshPath);
			if (!LandingMesh)
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to load landing mesh: %s"), *StairsData.LandingMeshPath);
			}
		}

		// Reconstruct module structures from serialized data
		TArray<FStairModuleStructure> Modules;
		for (const FSerializedStairModule& SerializedModule : StairsData.Modules)
		{
			FStairModuleStructure Module;
			Module.StairType = static_cast<EStairModuleType>(SerializedModule.ModuleType);
			Module.TurningSocket = static_cast<ETurningSocket>(SerializedModule.TurningSocket);
			Modules.Add(Module);
		}

		// If no modules were serialized, create a default single tread module
		if (Modules.Num() == 0)
		{
			FStairModuleStructure DefaultModule;
			DefaultModule.StairType = EStairModuleType::Tread;
			DefaultModule.TurningSocket = ETurningSocket::Idle;
			Modules.Add(DefaultModule);
		}

		// Spawn using LotManager's spawn method
		AStairsBase* SpawnedStairs = LotManager->SpawnStairsActor(
			TSubclassOf<AStairsBase>(StairsClass),
			StairsData.StartLocation,
			StairsData.Direction,
			Modules,
			TreadMesh,
			LandingMesh,
			StairsData.StairsThickness
		);

		if (SpawnedStairs)
		{
			SpawnedStairs->Level = StairsData.StartLevel;
			UE_LOG(LogTemp, Log, TEXT("Spawned stairs actor at %s with %d modules"),
				*StairsData.StartLocation.ToString(), Modules.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to spawn stairs actor at %s"),
				*StairsData.StartLocation.ToString());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized %d stairs actors"), Data.Stairs.Num());

	// Deserialize terrain component
	if (LotManager->TerrainComponent)
	{
		if (!DeserializeTerrainComponent(LotManager->TerrainComponent, LotManager, Data))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Terrain component deserialization failed"));
		}
	}

	// Deserialize water component (pools)
	if (LotManager->WaterComponent)
	{
		if (!DeserializeWaterComponent(LotManager->WaterComponent, LotManager, Data))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Water component deserialization failed"));
		}
	}

	// Deserialize fence component
	if (LotManager->FenceComponent)
	{
		if (!DeserializeFenceComponent(LotManager->FenceComponent, LotManager, Data))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Fence component deserialization failed"));
		}
	}

	// Deserialize portals (doors/windows)
	if (!DeserializePortals(LotManager, Data))
	{
		UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Portal deserialization failed"));
	}

	// Deserialize placed objects (furniture, decorations)
	if (!DeserializePlacedObjects(LotManager, Data))
	{
		UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Placed object deserialization failed"));
	}

	// Deserialize room IDs
	if (!DeserializeRoomIDs(LotManager, Data))
	{
		UE_LOG(LogTemp, Warning, TEXT("DeserializeLot: Room ID deserialization failed"));
	}

	// Apply grid visibility based on current mode
	// In Live mode, grid and boundaries should be hidden
	// Get current mode from the player's pawn
	if (LotManager->GridComponent)
	{
		APlayerController* PC = LotManager->GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			if (ABurbPawn* BurbPawn = Cast<ABurbPawn>(PC->GetPawn()))
			{
				if (BurbPawn->CurrentMode == EBurbMode::Live)
				{
					LotManager->bShowGrid = false;
					LotManager->GridComponent->SetGridVisibility(false, true); // Hide grid, hide boundaries
					UE_LOG(LogTemp, Log, TEXT("DeserializeLot: Hidden grid and boundaries (Live mode)"));
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Lot deserialization complete"));

	return true;
}

// ========================================
// Component-Level Serialization
// ========================================

void ULotSerializationSubsystem::SerializeWallGraph(UWallGraphComponent* WallGraph, FSerializedLotData& OutData)
{
	if (!WallGraph)
	{
		return;
	}

	// Serialize nodes
	for (const auto& NodePair : WallGraph->Nodes)
	{
		const FWallNode& Node = NodePair.Value;

		FSerializedWallNode SerializedNode;
		SerializedNode.NodeID = Node.NodeID;
		SerializedNode.Row = Node.Row;
		SerializedNode.Col = Node.Column;
		SerializedNode.Level = Node.Level;

		OutData.WallNodes.Add(SerializedNode);
	}

	// Serialize edges
	for (const auto& EdgePair : WallGraph->Edges)
	{
		const FWallEdge& Edge = EdgePair.Value;

		FSerializedWallEdge SerializedEdge;
		SerializedEdge.EdgeID = Edge.EdgeID;
		SerializedEdge.StartNodeID = Edge.FromNodeID;
		SerializedEdge.EndNodeID = Edge.ToNodeID;
		SerializedEdge.Room1 = Edge.Room1;
		SerializedEdge.Room2 = Edge.Room2;
		SerializedEdge.Height = Edge.Height;
		SerializedEdge.PatternPath = Edge.Pattern ? Edge.Pattern->GetPathName() : FString();
		SerializedEdge.MaterialPath = GetMaterialPath(Edge.Pattern ? Edge.Pattern->BaseMaterial : nullptr);

		OutData.WallEdges.Add(SerializedEdge);
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized wall graph: %d nodes, %d edges"),
		OutData.WallNodes.Num(), OutData.WallEdges.Num());
}

bool ULotSerializationSubsystem::DeserializeWallGraph(UWallGraphComponent* WallGraph, ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!WallGraph || !LotManager)
	{
		return false;
	}

	// Clear existing wall data
	WallGraph->Nodes.Empty();
	WallGraph->Edges.Empty();
	WallGraph->Intersections.Empty();
	WallGraph->TileToEdges.Empty();
	WallGraph->TileToNodes.Empty();
	WallGraph->EdgesByLevel.Empty();

	// Deserialize nodes
	TMap<int32, int32> OldToNewNodeID; // Map old node IDs to new ones
	for (const FSerializedWallNode& SerializedNode : Data.WallNodes)
	{
		FVector Position;
		LotManager->TileToGridLocation(SerializedNode.Level, SerializedNode.Row, SerializedNode.Col, false, Position);

		int32 NewNodeID = WallGraph->AddNode(Position, SerializedNode.Level, SerializedNode.Row, SerializedNode.Col);
		OldToNewNodeID.Add(SerializedNode.NodeID, NewNodeID);
	}

	// Deserialize edges
	for (const FSerializedWallEdge& SerializedEdge : Data.WallEdges)
	{
		// Map old node IDs to new ones
		int32* NewStartNodeID = OldToNewNodeID.Find(SerializedEdge.StartNodeID);
		int32* NewEndNodeID = OldToNewNodeID.Find(SerializedEdge.EndNodeID);

		if (!NewStartNodeID || !NewEndNodeID)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeWallGraph: Could not find node mapping for edge %d"),
				SerializedEdge.EdgeID);
			continue;
		}

		// Get node positions
		FWallNode* StartNode = WallGraph->Nodes.Find(*NewStartNodeID);
		FWallNode* EndNode = WallGraph->Nodes.Find(*NewEndNodeID);

		if (!StartNode || !EndNode)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializeWallGraph: Could not find nodes for edge %d"),
				SerializedEdge.EdgeID);
			continue;
		}

		// Add edge with height (thickness and pattern will be set below)
		int32 NewEdgeID = WallGraph->AddEdge(*NewStartNodeID, *NewEndNodeID, SerializedEdge.Height, 10.0f, nullptr);

		if (NewEdgeID >= 0)
		{
			// Set edge properties
			FWallEdge* Edge = WallGraph->Edges.Find(NewEdgeID);
			if (Edge)
			{
				Edge->EndHeight = SerializedEdge.Height; // TODO: Support different end heights
				Edge->Room1 = SerializedEdge.Room1;
				Edge->Room2 = SerializedEdge.Room2;

				// Load wall pattern from path
				if (!SerializedEdge.PatternPath.IsEmpty())
				{
					Edge->Pattern = Cast<UWallPattern>(StaticLoadObject(UWallPattern::StaticClass(), nullptr, *SerializedEdge.PatternPath));
					if (!Edge->Pattern)
					{
						UE_LOG(LogTemp, Warning, TEXT("Failed to load wall pattern: %s"), *SerializedEdge.PatternPath);
					}
				}
			}
		}
	}

	// Rebuild intersections
	WallGraph->RebuildIntersections();

	UE_LOG(LogTemp, Log, TEXT("Deserialized wall graph: %d nodes, %d edges"),
		WallGraph->Nodes.Num(), WallGraph->Edges.Num());

	// CRITICAL: Generate wall meshes for all edges
	// The WallGraph stores logical data, but WallComponent needs to render the actual geometry
	if (LotManager->WallComponent)
	{
		for (const auto& EdgePair : WallGraph->Edges)
		{
			const FWallEdge& Edge = EdgePair.Value;
			const FWallNode* StartNode = WallGraph->Nodes.Find(Edge.FromNodeID);
			const FWallNode* EndNode = WallGraph->Nodes.Find(Edge.ToNodeID);

			if (StartNode && EndNode)
			{
				// Generate wall mesh geometry (creates vertices, triangles, etc.)
				FWallSegmentData WallData = LotManager->WallComponent->GenerateWallSection(
					Edge.Level,
					StartNode->Position,
					EndNode->Position,
					Edge.Height
				);

				// Link rendering data to graph edge
				WallData.WallEdgeID = EdgePair.Key;

				// Use default material - pattern is applied separately for PBR textures
				LotManager->WallComponent->CommitWallSection(WallData, Edge.Pattern, LotManager->DefaultWallMaterial);

				UE_LOG(LogTemp, Verbose, TEXT("Generated wall mesh for edge %d at level %d with pattern: %s"),
					EdgePair.Key, Edge.Level, Edge.Pattern ? *Edge.Pattern->GetName() : TEXT("None"));
			}
		}

		UE_LOG(LogTemp, Log, TEXT("Generated wall meshes for %d edges"), WallGraph->Edges.Num());
	}

	return true;
}

void ULotSerializationSubsystem::SerializeFloorComponent(UFloorComponent* FloorComp, FSerializedLotData& OutData)
{
	if (!FloorComp)
	{
		return;
	}

	// Serialize floor tiles
	for (const FFloorTileData& TileData : FloorComp->FloorTileDataArray)
	{
		if (!TileData.bCommitted)
		{
			continue; // Skip uncommitted tiles
		}

		FSerializedFloorTile SerializedTile;
		SerializedTile.Row = TileData.Row;
		SerializedTile.Col = TileData.Column;
		SerializedTile.Level = TileData.Level;
		SerializedTile.PatternPath = TileData.Pattern ? TileData.Pattern->GetPathName() : FString();
		SerializedTile.MaterialPath = GetMaterialPath(TileData.Pattern ? TileData.Pattern->BaseMaterial : nullptr);

		// Convert tile section state to bitfield
		uint8 Sections = 0;
		if (TileData.TileSectionState.Top) Sections |= 1;
		if (TileData.TileSectionState.Right) Sections |= 2;
		if (TileData.TileSectionState.Bottom) Sections |= 4;
		if (TileData.TileSectionState.Left) Sections |= 8;
		SerializedTile.TileSections = Sections;

		OutData.FloorTiles.Add(SerializedTile);
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized floor component: %d tiles"), OutData.FloorTiles.Num());
}

bool ULotSerializationSubsystem::DeserializeFloorComponent(UFloorComponent* FloorComp, ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!FloorComp || !LotManager)
	{
		return false;
	}

	// Clear existing floor data
	FloorComp->FloorTileDataArray.Empty();
	FloorComp->FloorSpatialMap.Empty();
	FloorComp->ClearAllMeshSections();

	// Begin batch operation to suppress per-tile rebuilds
	FloorComp->BeginBatchOperation();

	// Deserialize floor tiles
	for (const FSerializedFloorTile& SerializedTile : Data.FloorTiles)
	{
		// Create tile data
		FFloorTileData TileData;
		TileData.Row = SerializedTile.Row;
		TileData.Column = SerializedTile.Col;
		TileData.Level = SerializedTile.Level;
		TileData.bCommitted = true;

		// Convert bitfield to tile section state
		TileData.TileSectionState.Top = (SerializedTile.TileSections & 1) != 0;
		TileData.TileSectionState.Right = (SerializedTile.TileSections & 2) != 0;
		TileData.TileSectionState.Bottom = (SerializedTile.TileSections & 4) != 0;
		TileData.TileSectionState.Left = (SerializedTile.TileSections & 8) != 0;

		// Load floor pattern from path
		if (!SerializedTile.PatternPath.IsEmpty())
		{
			TileData.Pattern = Cast<UFloorPattern>(StaticLoadObject(UFloorPattern::StaticClass(), nullptr, *SerializedTile.PatternPath));
			if (!TileData.Pattern)
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to load floor pattern: %s"), *SerializedTile.PatternPath);
			}
		}

		// Add tile to floor component (uses pattern's material if set, otherwise default)
		UMaterialInstance* MaterialToUse = TileData.Pattern && TileData.Pattern->BaseMaterial
			? Cast<UMaterialInstance>(TileData.Pattern->BaseMaterial)
			: LotManager->DefaultFloorMaterial;
		FloorComp->AddFloorTile(TileData, MaterialToUse);
	}

	// End batch operation - this will rebuild all dirty levels
	FloorComp->EndBatchOperation();

	UE_LOG(LogTemp, Log, TEXT("Deserialized floor component: %d tiles"), Data.FloorTiles.Num());

	return true;
}

void ULotSerializationSubsystem::SerializeRoofComponent(URoofComponent* RoofComp, FSerializedLotData& OutData)
{
	// Note: Roofs are now ARoofBase actors, not components
	// This method is kept for backward compatibility but serialization happens via RoofActors array
	// See SerializeLot() where we iterate LotManager->RoofActors
	if (!RoofComp)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Roof component serialization - roofs are now actor-based, see RoofActors serialization"));
}

bool ULotSerializationSubsystem::DeserializeRoofComponent(URoofComponent* RoofComp, ALotManager* LotManager, const FSerializedLotData& Data)
{
	// Note: Roofs are now ARoofBase actors, not components
	// This method is kept for backward compatibility but deserialization happens when spawning RoofActors
	if (!RoofComp || !LotManager)
	{
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("Roof component deserialization - roofs are now actor-based, see RoofActors deserialization"));
	return true;
}

void ULotSerializationSubsystem::SerializeStairsComponent(UStairsComponent* StairsComp, FSerializedLotData& OutData)
{
	// Note: Stairs are now AStairsBase actors, not components
	// This method is kept for backward compatibility but serialization happens via StairsActors array
	// See SerializeLot() where we iterate LotManager->StairsActors
	if (!StairsComp)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Stairs component serialization - stairs are now actor-based, see StairsActors serialization"));
}

bool ULotSerializationSubsystem::DeserializeStairsComponent(UStairsComponent* StairsComp, ALotManager* LotManager, const FSerializedLotData& Data)
{
	// Note: Stairs are now AStairsBase actors, not components
	// This method is kept for backward compatibility but deserialization happens when spawning StairsActors
	if (!StairsComp || !LotManager)
	{
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("Stairs component deserialization - stairs are now actor-based, see StairsActors deserialization"));
	return true;
}

void ULotSerializationSubsystem::SerializeTerrainComponent(UTerrainComponent* TerrainComp, FSerializedLotData& OutData)
{
	if (!TerrainComp)
	{
		return;
	}

	// Serialize terrain tile data with positions
	OutData.Terrain.Tiles.Empty();

	// Store each terrain segment with its grid position
	for (const FTerrainSegmentData& TerrainSeg : TerrainComp->TerrainDataArray)
	{
		if (!TerrainSeg.bCommitted)
		{
			continue; // Skip uncommitted segments
		}

		FSerializedTerrainTile TileData;
		TileData.Row = TerrainSeg.Row;
		TileData.Column = TerrainSeg.Column;
		TileData.Level = TerrainSeg.Level;

		// Store the 4 corner heights
		TileData.CornerHeights.Reserve(4);
		for (const FVector& Corner : TerrainSeg.CornerLocs)
		{
			TileData.CornerHeights.Add(Corner.Z); // Store Z height
		}

		OutData.Terrain.Tiles.Add(TileData);
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized terrain component: %d tiles"), OutData.Terrain.Tiles.Num());
}

bool ULotSerializationSubsystem::DeserializeTerrainComponent(UTerrainComponent* TerrainComp, ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!TerrainComp || !LotManager)
	{
		return false;
	}

	// Check if terrain data exists
	if (Data.Terrain.Tiles.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("No terrain data to deserialize"));
		return true;
	}

	// Clear existing terrain
	TerrainComp->TerrainDataArray.Empty();
	TerrainComp->TerrainSpatialMap.Empty();
	TerrainComp->ClearAllMeshSections();
	TerrainComp->HeightMaps.Empty();

	// Get grid configuration from saved data
	const int32 GridSizeX = Data.GridConfig.GridSizeX;
	const int32 GridSizeY = Data.GridConfig.GridSizeY;
	const float GridTileSize = Data.GridConfig.GridTileSize;

	// Group tiles by level
	TMap<int32, TArray<FSerializedTerrainTile>> TilesByLevel;
	for (const FSerializedTerrainTile& Tile : Data.Terrain.Tiles)
	{
		TilesByLevel.FindOrAdd(Tile.Level).Add(Tile);
	}

	// Process each level
	for (const auto& LevelPair : TilesByLevel)
	{
		const int32 Level = LevelPair.Key;
		const TArray<FSerializedTerrainTile>& LevelTiles = LevelPair.Value;

		// Initialize height map for this level
		const float BaseZ = LotManager->GetActorLocation().Z + (Level * 300.0f); // Assuming 300 units per level
		TerrainComp->InitializeHeightMap(Level, GridSizeX, GridSizeY, GridTileSize, BaseZ);

		// Get the height map for this level
		FTerrainHeightMap* HeightMap = TerrainComp->GetOrCreateHeightMap(Level);
		if (!HeightMap)
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to create height map for level %d"), Level);
			continue;
		}

		// First pass: Set all corner heights from the serialized data
		for (const FSerializedTerrainTile& Tile : LevelTiles)
		{
			if (Tile.CornerHeights.Num() != 4)
			{
				UE_LOG(LogTemp, Warning, TEXT("Invalid corner data for tile (%d, %d): expected 4 corners, got %d"),
					Tile.Row, Tile.Column, Tile.CornerHeights.Num());
				continue;
			}

			// Set corner heights (BottomLeft, BottomRight, TopLeft, TopRight)
			// Corners are relative to BaseZ
			HeightMap->SetCornerHeight(Tile.Row, Tile.Column, Tile.CornerHeights[0] - BaseZ);           // BottomLeft
			HeightMap->SetCornerHeight(Tile.Row, Tile.Column + 1, Tile.CornerHeights[1] - BaseZ);       // BottomRight
			HeightMap->SetCornerHeight(Tile.Row + 1, Tile.Column, Tile.CornerHeights[2] - BaseZ);       // TopLeft
			HeightMap->SetCornerHeight(Tile.Row + 1, Tile.Column + 1, Tile.CornerHeights[3] - BaseZ);   // TopRight
		}

		// Begin batch operation to prevent O(N²) rebuild triggers
		TerrainComp->BeginBatchOperation();

		// Second pass: Create terrain tiles directly (NOT using GenerateTerrainSection which overwrites heights)
		for (const FSerializedTerrainTile& Tile : LevelTiles)
		{
			// Calculate tile center location
			FVector TileCenter;
			LotManager->TileToGridLocation(Tile.Level, Tile.Row, Tile.Column, true, TileCenter);

			// Create FTerrainSegmentData directly - do NOT call GenerateTerrainSection
			// because it calls LocationToAllTileCorners which returns flat corners
			// RebuildLevel() will read from HeightMap for actual corner heights
			FTerrainSegmentData TerrainData;
			TerrainData.Row = Tile.Row;
			TerrainData.Column = Tile.Column;
			TerrainData.Level = Tile.Level;  // Use serialized level, not OurLot->Basements
			TerrainData.PointLoc = TileCenter;
			TerrainData.Width = LotManager->GridTileSize;
			TerrainData.bCommitted = true;
			// CornerLocs left empty - RebuildLevel uses HeightMap when available

			// Add to terrain component
			TerrainComp->AddTerrainTile(TerrainData, TerrainComp->TerrainMaterial);
		}

		// End batch operation - this will rebuild all dirty levels
		TerrainComp->EndBatchOperation();

		UE_LOG(LogTemp, Log, TEXT("Deserialized terrain for level %d: %d tiles"), Level, LevelTiles.Num());
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized terrain component: %d tiles across %d levels"),
		Data.Terrain.Tiles.Num(), TilesByLevel.Num());

	return true;
}

void ULotSerializationSubsystem::SerializeWaterComponent(UWaterComponent* WaterComp, FSerializedLotData& OutData)
{
	if (!WaterComp)
	{
		return;
	}

	OutData.PoolWater.Empty();

	for (const FPoolWaterData& Pool : WaterComp->PoolWaterArray)
	{
		if (!Pool.bCommitted)
		{
			continue;
		}

		FSerializedPoolWater SerializedPool;
		SerializedPool.RoomID = Pool.RoomID;
		SerializedPool.Level = Pool.Level;
		SerializedPool.BoundaryVertices = Pool.BoundaryVertices;
		SerializedPool.WaterSurfaceZ = Pool.WaterSurfaceZ;
		SerializedPool.PoolFloorZ = Pool.PoolFloorZ;
		// Note: WaterMaterial is a dynamic material instance, we don't serialize it
		// Pools will recreate their materials on load

		OutData.PoolWater.Add(SerializedPool);
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized %d pool water volumes"), OutData.PoolWater.Num());
}

bool ULotSerializationSubsystem::DeserializeWaterComponent(UWaterComponent* WaterComp, ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!WaterComp || !LotManager)
	{
		return false;
	}

	// Clear existing water data
	WaterComp->DestroyAllWater();

	// Deserialize pool water volumes
	for (const FSerializedPoolWater& SerializedPool : Data.PoolWater)
	{
		// Generate pool water with the saved boundary vertices
		WaterComp->GeneratePoolWater(
			SerializedPool.RoomID,
			SerializedPool.BoundaryVertices,
			SerializedPool.WaterSurfaceZ,
			SerializedPool.PoolFloorZ,
			WaterComp->DefaultWaterMaterial
		);
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized %d pool water volumes"), Data.PoolWater.Num());
	return true;
}

void ULotSerializationSubsystem::SerializeFenceComponent(UFenceComponent* FenceComp, FSerializedLotData& OutData)
{
	if (!FenceComp)
	{
		return;
	}

	OutData.Fences.Empty();

	for (const FFenceSegmentData& Fence : FenceComp->FenceDataArray)
	{
		if (!Fence.bCommitted)
		{
			continue;
		}

		FSerializedFenceSegment SerializedFence;
		SerializedFence.StartLoc = Fence.StartLoc;
		SerializedFence.EndLoc = Fence.EndLoc;
		SerializedFence.Level = Fence.Level;
		SerializedFence.FenceItemPath = Fence.FenceItem ? Fence.FenceItem->GetPathName() : FString();

		OutData.Fences.Add(SerializedFence);
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized %d fence segments"), OutData.Fences.Num());
}

bool ULotSerializationSubsystem::DeserializeFenceComponent(UFenceComponent* FenceComp, ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!FenceComp || !LotManager)
	{
		return false;
	}

	// Clear existing fence data
	FenceComp->FenceDataArray.Empty();

	// Deserialize fence segments
	for (const FSerializedFenceSegment& SerializedFence : Data.Fences)
	{
		// Load fence item from path
		UFenceItem* FenceItem = nullptr;
		if (!SerializedFence.FenceItemPath.IsEmpty())
		{
			FenceItem = Cast<UFenceItem>(StaticLoadObject(UFenceItem::StaticClass(), nullptr, *SerializedFence.FenceItemPath));
			if (!FenceItem)
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to load fence item: %s"), *SerializedFence.FenceItemPath);
				continue;
			}
		}

		// Generate fence segment
		FenceComp->GenerateFenceSegment(
			SerializedFence.Level,
			SerializedFence.StartLoc,
			SerializedFence.EndLoc,
			FenceItem
		);
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized %d fence segments"), Data.Fences.Num());
	return true;
}

void ULotSerializationSubsystem::SerializePortals(ALotManager* LotManager, FSerializedLotData& OutData)
{
	if (!LotManager || !LotManager->GetWorld())
	{
		return;
	}

	// Find all portal actors in the world
	TArray<AActor*> FoundPortals;
	LotManager->GetAttachedActors(FoundPortals, true);

	for (AActor* Actor : FoundPortals)
	{
		if (APortalBase* Portal = Cast<APortalBase>(Actor))
		{
			FSerializedPortalData PortalData;
			PortalData.PortalClass = GetClassPath(Portal->GetClass());
			PortalData.Location = Portal->GetActorLocation();
			PortalData.Rotation = Portal->GetActorRotation();
			PortalData.Level = Portal->CurrentFloor;

			// Find which wall edge this portal is attached to via spatial query
			PortalData.AttachedWallEdgeID = -1;
			if (LotManager->WallGraph)
			{
				// Convert portal location to grid coordinates
				int32 Row, Column;
				if (LotManager->LocationToTile(PortalData.Location, Row, Column))
				{
					// Get all edges in this tile
					TArray<int32> EdgesInTile = LotManager->WallGraph->GetEdgesInTile(Row, Column, PortalData.Level);

					// Find closest edge to portal location
					float ClosestDist = MAX_FLT;
					for (int32 EdgeID : EdgesInTile)
					{
						const FWallEdge* Edge = LotManager->WallGraph->Edges.Find(EdgeID);
						if (!Edge) continue;

						const FWallNode* StartNode = LotManager->WallGraph->Nodes.Find(Edge->FromNodeID);
						const FWallNode* EndNode = LotManager->WallGraph->Nodes.Find(Edge->ToNodeID);
						if (!StartNode || !EndNode) continue;

						// Calculate distance from portal to edge line segment
						FVector ClosestPoint = FMath::ClosestPointOnSegment(PortalData.Location, StartNode->Position, EndNode->Position);
						float Dist = FVector::Dist(PortalData.Location, ClosestPoint);

						if (Dist < ClosestDist)
						{
							ClosestDist = Dist;
							PortalData.AttachedWallEdgeID = EdgeID;
						}
					}
				}
			}

			OutData.Portals.Add(PortalData);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized %d portals"), OutData.Portals.Num());
}

bool ULotSerializationSubsystem::DeserializePortals(ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!LotManager || !LotManager->GetWorld())
	{
		return false;
	}

	// Clear existing portals
	TArray<AActor*> ExistingPortals;
	LotManager->GetAttachedActors(ExistingPortals, true);
	for (AActor* Actor : ExistingPortals)
	{
		if (APortalBase* Portal = Cast<APortalBase>(Actor))
		{
			Portal->Destroy();
		}
	}

	// Spawn portals from saved data
	for (const FSerializedPortalData& PortalData : Data.Portals)
	{
		// Resolve portal class
		UClass* PortalClass = ResolveClassPath(PortalData.PortalClass);
		if (!PortalClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializePortals: Could not resolve portal class: %s"), *PortalData.PortalClass);
			continue;
		}

		// Spawn portal
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = LotManager;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		APortalBase* Portal = LotManager->GetWorld()->SpawnActor<APortalBase>(
			PortalClass,
			PortalData.Location,
			PortalData.Rotation,
			SpawnParams
		);

		if (Portal)
		{
			Portal->CurrentFloor = PortalData.Level;
			Portal->CurrentLot = LotManager;
			Portal->CurrentWallComponent = LotManager->WallComponent;
			Portal->AttachToActor(LotManager, FAttachmentTransformRules::KeepWorldTransform);

			// Re-render portal cutout in wall using stored edge ID
			if (PortalData.AttachedWallEdgeID >= 0 && LotManager->WallComponent)
			{
				LotManager->WallComponent->RenderPortals();
				UE_LOG(LogTemp, Log, TEXT("Spawned portal: %s attached to wall edge %d at level %d"),
					*PortalClass->GetName(), PortalData.AttachedWallEdgeID, PortalData.Level);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Spawned portal: %s at %s, level %d (no wall edge attachment)"),
					*PortalClass->GetName(), *PortalData.Location.ToString(), PortalData.Level);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to spawn portal: %s at %s"),
				*PortalData.PortalClass, *PortalData.Location.ToString());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized %d portals"), Data.Portals.Num());

	// CRITICAL: After spawning portals, we need to regenerate wall meshes with portal cutouts
	// Portals cut holes in walls using render targets, so walls must be regenerated
	if (Data.Portals.Num() > 0 && LotManager->WallComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("Regenerating wall meshes with portal cutouts..."));
		// The walls have already been generated above, but now we need to apply portal cutouts
		// This is typically handled by the portal's BeginPlay or by calling a refresh method
		// For now, just log that portals were spawned - the portal system should handle cutouts
	}

	return true;
}

void ULotSerializationSubsystem::SerializePlacedObjects(ALotManager* LotManager, FSerializedLotData& OutData)
{
	if (!LotManager || !LotManager->GetWorld())
	{
		return;
	}

	// Find all placeable objects in the world (excluding portals, which are handled separately)
	TArray<AActor*> AttachedActors;
	LotManager->GetAttachedActors(AttachedActors, true);

	for (AActor* Actor : AttachedActors)
	{
		// Check if it's a placeable object but NOT a portal (portals are serialized separately)
		APlaceableObject* PlaceableObj = Cast<APlaceableObject>(Actor);
		if (PlaceableObj && !Cast<APortalBase>(Actor))
		{
			FSerializedPlacedObject ObjectData;
			ObjectData.ObjectClass = GetClassPath(PlaceableObj->GetClass());
			ObjectData.Location = PlaceableObj->GetActorLocation();
			ObjectData.Rotation = PlaceableObj->GetActorRotation();
			ObjectData.Scale = PlaceableObj->GetActorScale3D();
			ObjectData.Level = PlaceableObj->CurrentFloor;

			OutData.PlacedObjects.Add(ObjectData);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized %d placed objects"), OutData.PlacedObjects.Num());
}

bool ULotSerializationSubsystem::DeserializePlacedObjects(ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!LotManager || !LotManager->GetWorld())
	{
		return false;
	}

	// Clear existing placed objects (excluding portals)
	TArray<AActor*> AttachedActors;
	LotManager->GetAttachedActors(AttachedActors, true);
	for (AActor* Actor : AttachedActors)
	{
		APlaceableObject* PlaceableObj = Cast<APlaceableObject>(Actor);
		if (PlaceableObj && !Cast<APortalBase>(Actor))
		{
			PlaceableObj->Destroy();
		}
	}

	// Spawn placed objects from saved data
	for (const FSerializedPlacedObject& ObjectData : Data.PlacedObjects)
	{
		// Resolve object class
		UClass* ObjectClass = ResolveClassPath(ObjectData.ObjectClass);
		if (!ObjectClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeserializePlacedObjects: Could not resolve object class: %s"), *ObjectData.ObjectClass);
			continue;
		}

		// Spawn object
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = LotManager;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		FTransform SpawnTransform;
		SpawnTransform.SetLocation(ObjectData.Location);
		SpawnTransform.SetRotation(ObjectData.Rotation.Quaternion());
		SpawnTransform.SetScale3D(ObjectData.Scale);

		APlaceableObject* PlacedObject = LotManager->GetWorld()->SpawnActor<APlaceableObject>(
			ObjectClass,
			SpawnTransform,
			SpawnParams
		);

		if (PlacedObject)
		{
			PlacedObject->CurrentFloor = ObjectData.Level;
			PlacedObject->AttachToActor(LotManager, FAttachmentTransformRules::KeepWorldTransform);

			UE_LOG(LogTemp, Log, TEXT("Spawned placed object: %s at level %d"), *ObjectClass->GetName(), ObjectData.Level);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to spawn placed object: %s"), *ObjectData.ObjectClass);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized %d placed objects"), Data.PlacedObjects.Num());
	return true;
}

void ULotSerializationSubsystem::SerializeGridConfig(ALotManager* LotManager, FSerializedLotData& OutData)
{
	if (!LotManager)
	{
		return;
	}

	OutData.GridConfig.GridSizeX = LotManager->GridSizeX;
	OutData.GridConfig.GridSizeY = LotManager->GridSizeY;
	OutData.GridConfig.GridTileSize = LotManager->GridTileSize;
	OutData.GridConfig.Floors = LotManager->Floors;
	OutData.GridConfig.Basements = LotManager->Basements;
	OutData.GridConfig.CurrentLevel = LotManager->CurrentLevel;
}

void ULotSerializationSubsystem::SerializeRoomIDs(ALotManager* LotManager, FSerializedLotData& OutData)
{
	if (!LotManager)
	{
		return;
	}

	// Flatten the 3D grid into a 1D array
	OutData.TileRoomIDs.Empty();
	OutData.UpperFloorTiles.Empty();

	for (const FTileData& Tile : LotManager->GridData)
	{
		OutData.TileRoomIDs.Add(Tile.GetPrimaryRoomID());

		// Save upper floor tiles (tiles above ground level)
		// These need to be explicitly recreated during deserialization
		if (Tile.Level > LotManager->Basements)
		{
			// Pack tile coordinates into single int32: (Level << 24) | (Row << 12) | Column
			// TileCoord is FVector2D (X=Column, Y=Row), cast to int32 for bitshift
			int32 Row = static_cast<int32>(Tile.TileCoord.Y);
			int32 Column = static_cast<int32>(Tile.TileCoord.X);
			int32 PackedCoord = (Tile.Level << 24) | (Row << 12) | Column;
			OutData.UpperFloorTiles.Add(PackedCoord);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Serialized %d room IDs and %d upper floor tiles"),
		OutData.TileRoomIDs.Num(), OutData.UpperFloorTiles.Num());
}

bool ULotSerializationSubsystem::DeserializeRoomIDs(ALotManager* LotManager, const FSerializedLotData& Data)
{
	if (!LotManager)
	{
		return false;
	}

	// Check if room IDs are present
	if (Data.TileRoomIDs.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("No room IDs to deserialize, will regenerate via flood fill"));
		return true;
	}

	// Validate array size
	const int32 ExpectedSize = LotManager->GridData.Num();
	if (Data.TileRoomIDs.Num() != ExpectedSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("Room ID array size mismatch. Expected %d, got %d. Will regenerate via flood fill."),
			ExpectedSize, Data.TileRoomIDs.Num());
		return false;
	}

	// Apply room IDs to grid
	for (int32 i = 0; i < Data.TileRoomIDs.Num(); ++i)
	{
		LotManager->GridData[i].SetRoomID(Data.TileRoomIDs[i]);
	}

	UE_LOG(LogTemp, Log, TEXT("Deserialized %d room IDs"), Data.TileRoomIDs.Num());

	return true;
}

// ========================================
// Validation and Versioning
// ========================================

bool ULotSerializationSubsystem::ValidateLotData(const FSerializedLotData& Data) const
{
	// Check grid dimensions
	if (Data.GridConfig.GridSizeX <= 0 || Data.GridConfig.GridSizeY <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("ValidateLotData: Invalid grid dimensions: %dx%d"),
			Data.GridConfig.GridSizeX, Data.GridConfig.GridSizeY);
		return false;
	}

	if (Data.GridConfig.GridTileSize <= 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("ValidateLotData: Invalid tile size: %f"),
			Data.GridConfig.GridTileSize);
		return false;
	}

	if (Data.GridConfig.Floors <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("ValidateLotData: Invalid floor count: %d"),
			Data.GridConfig.Floors);
		return false;
	}

	// Validate wall nodes have valid coordinates
	for (const FSerializedWallNode& Node : Data.WallNodes)
	{
		if (Node.Row < 0 || Node.Row > Data.GridConfig.GridSizeX ||
			Node.Col < 0 || Node.Col > Data.GridConfig.GridSizeY ||
			Node.Level < 0 || Node.Level >= Data.GridConfig.Floors)
		{
			UE_LOG(LogTemp, Error, TEXT("ValidateLotData: Wall node %d has invalid coordinates: (%d, %d, %d)"),
				Node.NodeID, Node.Row, Node.Col, Node.Level);
			return false;
		}
	}

	// Validate wall edges reference valid nodes
	TSet<int32> NodeIDs;
	for (const FSerializedWallNode& Node : Data.WallNodes)
	{
		NodeIDs.Add(Node.NodeID);
	}

	for (const FSerializedWallEdge& Edge : Data.WallEdges)
	{
		if (!NodeIDs.Contains(Edge.StartNodeID) || !NodeIDs.Contains(Edge.EndNodeID))
		{
			UE_LOG(LogTemp, Error, TEXT("ValidateLotData: Wall edge %d references invalid nodes: %d -> %d"),
				Edge.EdgeID, Edge.StartNodeID, Edge.EndNodeID);
			return false;
		}
	}

	// Validate floor tiles
	for (const FSerializedFloorTile& Tile : Data.FloorTiles)
	{
		if (Tile.Row < 0 || Tile.Row >= Data.GridConfig.GridSizeX ||
			Tile.Col < 0 || Tile.Col >= Data.GridConfig.GridSizeY ||
			Tile.Level < 0 || Tile.Level >= Data.GridConfig.Floors)
		{
			UE_LOG(LogTemp, Error, TEXT("ValidateLotData: Floor tile has invalid coordinates: (%d, %d, %d)"),
				Tile.Row, Tile.Col, Tile.Level);
			return false;
		}
	}

	return true;
}

// ========================================
// File I/O Helpers
// ========================================

bool ULotSerializationSubsystem::ExportToJSON(const FSerializedLotData& Data, const FString& FilePath)
{
	// Convert struct to JSON
	FString JsonString;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Data, JsonString, 0, 0))
	{
		UE_LOG(LogTemp, Error, TEXT("ExportToJSON: Failed to convert struct to JSON"));
		return false;
	}

	// Write to file
	if (!FFileHelper::SaveStringToFile(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("ExportToJSON: Failed to write file: %s"), *FilePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("Exported lot to JSON: %s"), *FilePath);
	return true;
}

bool ULotSerializationSubsystem::ImportFromJSON(const FString& FilePath, FSerializedLotData& OutData)
{
	// Read file
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("ImportFromJSON: Failed to read file: %s"), *FilePath);
		return false;
	}

	// Convert JSON to struct
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &OutData, 0, 0))
	{
		UE_LOG(LogTemp, Error, TEXT("ImportFromJSON: Failed to convert JSON to struct"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("Imported lot from JSON: %s"), *FilePath);
	return true;
}

// ========================================
// Helper Functions
// ========================================

UMaterialInterface* ULotSerializationSubsystem::ResolveMaterialPath(const FString& MaterialPath)
{
	if (MaterialPath.IsEmpty())
	{
		return nullptr;
	}

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		UE_LOG(LogTemp, Warning, TEXT("ResolveMaterialPath: Could not load material: %s"), *MaterialPath);
	}

	return Material;
}

FString ULotSerializationSubsystem::GetMaterialPath(UMaterialInterface* Material)
{
	if (!Material)
	{
		return FString();
	}

	return Material->GetPathName();
}

UClass* ULotSerializationSubsystem::ResolveClassPath(const FString& ClassPath)
{
	if (ClassPath.IsEmpty())
	{
		return nullptr;
	}

	UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
	if (!Class)
	{
		UE_LOG(LogTemp, Warning, TEXT("ResolveClassPath: Could not load class: %s"), *ClassPath);
	}

	return Class;
}

FString ULotSerializationSubsystem::GetClassPath(UClass* Class)
{
	if (!Class)
	{
		return FString();
	}

	return Class->GetPathName();
}

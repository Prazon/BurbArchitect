// Fill out your copyright notice in the Description page of Project Settings.

#include "Subsystems/BuildServer.h"
#include "Commands/BatchCommand.h"
#include "Commands/WallCommand.h"
#include "Commands/FloorCommand.h"
#include "Commands/RoofCommand.h"
#include "Commands/StairsCommand.h"
#include "Commands/TerrainCommand.h"
#include "Commands/PortalCommand.h"
#include "Commands/LoadLotCommand.h"
#include "Actors/LotManager.h"
#include "Actors/StairsBase.h"
#include "Actors/Roofs/RoofBase.h"
#include "Components/FloorComponent.h"
#include "Components/RoomManagerComponent.h"
#include "Components/WallGraphComponent.h"
#include "Data/TileTriangleData.h"  // For FTriangleCoord
#include "Interfaces/IDeletable.h"
#include "Subsystems/LotSerializationSubsystem.h"
#include "SaveGame/LotSaveGame.h"
#include "Data/LotDataAsset.h"
#include "Kismet/GameplayStatics.h"
#include "BurbArchitectDebug.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"

void UBuildServer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Initialize command history
	MaxHistorySize = 50;
	UndoStack.Empty();
	RedoStack.Empty();

	bInBatch = false;
	BatchCommands.Empty();

	UE_LOG(LogTemp, Log, TEXT("BuildServer: Initialized"));
}

void UBuildServer::Deinitialize()
{
	CurrentLot = nullptr;
	UndoStack.Empty();
	RedoStack.Empty();
	BatchCommands.Empty();

	Super::Deinitialize();
}

ALotManager* UBuildServer::GetCurrentLot()
{
	// If we have a cached lot and it's still valid, return it
	if (CurrentLot && IsValid(CurrentLot))
	{
		return CurrentLot;
	}

	// Try to find the first lot in the world
	UWorld* World = GetWorld();
	if (World)
	{
		for (TActorIterator<ALotManager> It(World); It; ++It)
		{
			CurrentLot = *It;
			return CurrentLot;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildServer: No LotManager found in world"));
	return nullptr;
}

void UBuildServer::SetCurrentLot(ALotManager* Lot)
{
	CurrentLot = Lot;
	UE_LOG(LogTemp, Log, TEXT("BuildServer: Set current lot to %s"), *Lot->GetName());
}

// ========== Wall Operations ==========

void UBuildServer::BuildWall(int32 Level, const FVector& StartLoc, const FVector& EndLoc, float Height, UWallPattern* Pattern, UMaterialInstance* BaseMaterial, bool bDeferRoomGeneration, bool bIsPoolWall)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build wall - no lot set"));
		return;
	}

	// Reduce wall height for basement levels to stop below terrain surface
	// Basement walls should align with basement ceilings (both below terrain by BasementCeilingOffset)
	float AdjustedHeight = Height;
	if (Level < Lot->Basements)
	{
		AdjustedHeight = Height - Lot->BasementCeilingOffset;
		UE_LOG(LogTemp, Log, TEXT("BuildServer::BuildWall - Basement wall at Level %d: reducing height from %.1f to %.1f (offset=%.1f)"),
			Level, Height, AdjustedHeight, Lot->BasementCeilingOffset);
	}

	UWallCommand* Command = NewObject<UWallCommand>(this);
	Command->Initialize(Lot, Level, StartLoc, EndLoc, AdjustedHeight, Pattern, BaseMaterial, bDeferRoomGeneration, bIsPoolWall);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::DeleteWall(const FWallSegmentData& WallData)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot delete wall - no lot set"));
		return;
	}

	UWallCommand* Command = NewObject<UWallCommand>(this);
	Command->InitializeDelete(Lot, WallData);
	ExecuteOrBatchCommand(Command);
}

// ========== Floor Operations ==========

void UBuildServer::BuildFloor(int32 Level, const FVector& TileCenter, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, const FTileSectionState& TileState, int32 SwatchIndex, bool bIsPool, bool bIsPoolEdge)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build floor - no lot set"));
		return;
	}

	UFloorCommand* Command = NewObject<UFloorCommand>(this);
	Command->Initialize(Lot, Level, TileCenter, Pattern, BaseMaterial, TileState, SwatchIndex, bIsPool, bIsPoolEdge);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::DeleteFloor(const FFloorSegmentData& FloorData)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot delete floor - no lot set"));
		return;
	}

	UFloorCommand* Command = NewObject<UFloorCommand>(this);
	Command->InitializeDelete(Lot, FloorData);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::UpdateFloor(const FFloorSegmentData& FloorData, const FTileSectionState& NewTileState, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, int32 SwatchIndex)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot update floor - no lot set"));
		return;
	}

	UFloorCommand* Command = NewObject<UFloorCommand>(this);
	Command->InitializeUpdate(Lot, FloorData, NewTileState, Pattern, BaseMaterial, SwatchIndex);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::UpdateFloorPattern(int32 Level, int32 Row, int32 Column, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, int32 SwatchIndex)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot update floor pattern - no lot set"));
		return;
	}

	// Find the existing floor tile
	FFloorTileData* ExistingTile = Lot->FloorComponent->FindFloorTile(Level, Row, Column);
	if (!ExistingTile)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: No floor exists at Level %d, Row %d, Column %d to update"), Level, Row, Column);
		return;
	}

	// Create FloorSegmentData from existing tile for the update command
	FFloorSegmentData FloorData;
	FloorData.Level = Level;
	FloorData.Row = Row;
	FloorData.Column = Column;
	FloorData.TileSectionState = ExistingTile->TileSectionState;

	// Convert grid coords to world location
	FVector TileCenter;
	Lot->TileToGridLocation(Level, Row, Column, true, TileCenter);
	FloorData.StartLoc = TileCenter;

	// Use existing UpdateFloor with same tile state (only pattern and swatch change)
	UFloorCommand* Command = NewObject<UFloorCommand>(this);
	Command->InitializeUpdate(Lot, FloorData, ExistingTile->TileSectionState, Pattern, BaseMaterial, SwatchIndex);
	ExecuteOrBatchCommand(Command);
}

// ========== Room Operations ==========

void UBuildServer::BuildRoom(int32 Level, const FVector& StartCorner, const FVector& EndCorner, float WallHeight)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build room - no lot set"));
		return;
	}

	// Start a batch for the room
	BeginBatch(FString::Printf(TEXT("Build Room at Level %d"), Level));

	// Calculate the four corners of the room
	FVector BottomLeft = FVector(FMath::Min(StartCorner.X, EndCorner.X), FMath::Min(StartCorner.Y, EndCorner.Y), StartCorner.Z);
	FVector TopRight = FVector(FMath::Max(StartCorner.X, EndCorner.X), FMath::Max(StartCorner.Y, EndCorner.Y), EndCorner.Z);
	FVector BottomRight = FVector(TopRight.X, BottomLeft.Y, StartCorner.Z);
	FVector TopLeft = FVector(BottomLeft.X, TopRight.Y, StartCorner.Z);

	// Build all four walls
	BuildWall(Level, BottomLeft, BottomRight, WallHeight, nullptr, nullptr);  // Bottom wall
	BuildWall(Level, BottomRight, TopRight, WallHeight, nullptr, nullptr);     // Right wall
	BuildWall(Level, TopRight, TopLeft, WallHeight, nullptr, nullptr);         // Top wall
	BuildWall(Level, TopLeft, BottomLeft, WallHeight, nullptr, nullptr);       // Left wall

	// End the batch - now all 4 walls can be undone with one command
	EndBatch();
}

// ========== Roof Operations ==========

void UBuildServer::BuildRoof(const FVector& Location, const FVector& Direction, const FRoofDimensions& Dimensions, float RoofThickness, float GableThickness, UMaterialInstance* Material)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build roof - no lot set"));
		return;
	}

	URoofCommand* Command = NewObject<URoofCommand>(this);
	Command->Initialize(Lot, Location, Direction, Dimensions, RoofThickness, GableThickness, Material);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::DeleteRoof(const FVector& Location)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot delete roof - no lot set"));
		return;
	}

	// Find roof actor at location
	ARoofBase* RoofToDelete = nullptr;
	for (ARoofBase* RoofActor : Lot->RoofActors)
	{
		if (RoofActor && RoofActor->bCommitted)
		{
			// Check if location matches (with tolerance for floating point)
			if (RoofActor->GetActorLocation().Equals(Location, 10.0f))
			{
				RoofToDelete = RoofActor;
				break;
			}
		}
	}

	if (RoofToDelete)
	{
		// Use IDeletable interface for proper cleanup (walls, floors, rooms)
		if (RoofToDelete->GetClass()->ImplementsInterface(UDeletable::StaticClass()))
		{
			IDeletable::Execute_RequestDeletion(RoofToDelete);
			UE_LOG(LogTemp, Log, TEXT("BuildServer: Deleted roof at (%s)"), *Location.ToString());
		}
		else
		{
			// Fallback: direct removal
			Lot->RemoveRoofActor(RoofToDelete);
			UE_LOG(LogTemp, Log, TEXT("BuildServer: Deleted roof at (%s) via direct removal"), *Location.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: No roof found at location (%s)"), *Location.ToString());
	}
}

// ========== Stairs Operations ==========

void UBuildServer::BuildStairs(TSubclassOf<AStairsBase> StairsClass, const FVector& StartLoc, const FVector& Direction, const TArray<FStairModuleStructure>& Structures, float StairsThickness, UStaticMesh* StairTreadMesh, UStaticMesh* StairLandingMesh)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build stairs - no lot set"));
		return;
	}

	UStairsCommand* Command = NewObject<UStairsCommand>(this);
	Command->Initialize(Lot, StairsClass, StartLoc, Direction, Structures, StairsThickness, StairTreadMesh, StairLandingMesh);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::DeleteStairs(const FVector& Location)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot delete stairs - no lot set"));
		return;
	}

	// Find stairs actor at location
	AStairsBase* StairsToDelete = nullptr;
	for (AStairsBase* StairsActor : Lot->StairsActors)
	{
		if (StairsActor && StairsActor->bCommitted)
		{
			// Check if location matches (with small tolerance for floating point)
			if (StairsActor->GetActorLocation().Equals(Location, 10.0f))
			{
				StairsToDelete = StairsActor;
				break;
			}
		}
	}

	if (StairsToDelete)
	{
		// Use IDeletable interface for proper cleanup
		if (StairsToDelete->GetClass()->ImplementsInterface(UDeletable::StaticClass()))
		{
			IDeletable::Execute_RequestDeletion(StairsToDelete);
			UE_LOG(LogTemp, Log, TEXT("BuildServer: Deleted stairs at (%s)"), *Location.ToString());
		}
		else
		{
			// Fallback: direct removal
			Lot->RemoveStairsActor(StairsToDelete);
			UE_LOG(LogTemp, Log, TEXT("BuildServer: Deleted stairs at (%s) via direct removal"), *Location.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: No stairs found at location (%s)"), *Location.ToString());
	}
}

// ========== Portal Operations ==========

void UBuildServer::BuildPortal(
	TSubclassOf<APortalBase> PortalClass,
	const FVector& Location,
	const FRotator& Rotation,
	const TArray<int32>& WallArrayIndices,
	const FVector2D& PortalSize,
	const FVector2D& PortalOffset,
	TSoftObjectPtr<UStaticMesh> WindowMesh,
	TSoftObjectPtr<UStaticMesh> DoorStaticMesh,
	TSoftObjectPtr<UStaticMesh> DoorFrameMesh
)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build portal - no lot set"));
		return;
	}

	if (!PortalClass)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build portal - PortalClass is null"));
		return;
	}

	if (WallArrayIndices.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot build portal - no wall indices provided"));
		return;
	}

	UPortalCommand* Command = NewObject<UPortalCommand>(this);
	Command->Initialize(Lot, PortalClass, Location, Rotation, WallArrayIndices, PortalSize, PortalOffset, WindowMesh, DoorStaticMesh, DoorFrameMesh);
	ExecuteOrBatchCommand(Command);
}

// ========== Terrain Operations ==========

void UBuildServer::RaiseTerrain(int32 Row, int32 Column, const TArray<float>& Spans, UMaterialInstance* Material)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot raise terrain - no lot set"));
		return;
	}

	UTerrainCommand* Command = NewObject<UTerrainCommand>(this);
	Command->InitializeRaise(Lot, Row, Column, Spans, Material);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::LowerTerrain(int32 Row, int32 Column, const TArray<float>& Spans, UMaterialInstance* Material)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot lower terrain - no lot set"));
		return;
	}

	UTerrainCommand* Command = NewObject<UTerrainCommand>(this);
	Command->InitializeLower(Lot, Row, Column, Spans, Material);
	ExecuteOrBatchCommand(Command);
}

void UBuildServer::FlattenTerrain(int32 Row, int32 Column, float TargetHeight)
{
	ALotManager* Lot = GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot flatten terrain - no lot set"));
		return;
	}

	UTerrainCommand* Command = NewObject<UTerrainCommand>(this);
	Command->InitializeFlatten(Lot, Row, Column, TargetHeight);
	ExecuteOrBatchCommand(Command);
}

// ========== Batch Operations ==========

void UBuildServer::BeginBatch(const FString& Description)
{
	if (bInBatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Already in a batch operation"));
		return;
	}

	bInBatch = true;
	BatchCommands.Empty();
	BatchDescription = Description;

	// Begin batch operation on FloorComponent and TerrainComponent to suppress per-tile/corner mesh rebuilds
	ALotManager* Lot = GetCurrentLot();
	if (Lot)
	{
		if (Lot->FloorComponent)
		{
			Lot->FloorComponent->BeginBatchOperation();
		}
		if (Lot->TerrainComponent)
		{
			Lot->TerrainComponent->BeginBatchOperation();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("BuildServer: Started batch '%s'"), *BatchDescription);
}

void UBuildServer::EndBatch()
{
	if (!bInBatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Not in a batch operation"));
		return;
	}

	if (BatchCommands.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Batch '%s' is empty, cancelling"), *BatchDescription);
		CancelBatch();
		return;
	}

	// Create a batch command containing all the commands
	UBatchCommand* BatchCmd = NewObject<UBatchCommand>(this);
	BatchCmd->SetCommands(BatchCommands, BatchDescription);

	// Trigger room detection for all walls in the batch
	// This happens after all walls are committed but before adding to undo history
	DetectRoomsForBatch(BatchCmd);

	// Rebuild wall chains after all walls are committed
	if (CurrentLot)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Rebuilding wall chains for editing"));
		CurrentLot->RebuildWallChains();

		// NOTE: BuildRoomCache() is NOT called here because DetectRoomsForBatch()
		// already handles room detection. Calling BuildRoomCache() would delete
		// the rooms we just detected and re-detect them with new IDs, causing
		// RoomID assignment failures (edges already have Room1/Room2 set).
	}

	// Don't execute the batch (commands already executed), just add to history
	UndoStack.Add(BatchCmd);

	// Clear redo stack
	RedoStack.Empty();

	// Enforce max history size
	if (UndoStack.Num() > MaxHistorySize)
	{
		UndoStack.RemoveAt(0);
	}

	// End batch operation on FloorComponent and TerrainComponent to trigger merged mesh rebuild
	if (CurrentLot)
	{
		if (CurrentLot->FloorComponent)
		{
			CurrentLot->FloorComponent->EndBatchOperation();
		}
		if (CurrentLot->TerrainComponent)
		{
			CurrentLot->TerrainComponent->EndBatchOperation();
		}
	}

	// Clean up batch state
	bInBatch = false;
	BatchCommands.Empty();
	BatchDescription = TEXT("");

	UE_LOG(LogTemp, Log, TEXT("BuildServer: Ended batch with %d commands"), BatchCmd->GetCommandCount());
}

void UBuildServer::CancelBatch()
{
	if (!bInBatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Not in a batch operation"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("BuildServer: Cancelled batch '%s'"), *BatchDescription);

	// End batch operation on FloorComponent and TerrainComponent (will rebuild any dirty levels)
	ALotManager* Lot = GetCurrentLot();
	if (Lot)
	{
		if (Lot->FloorComponent)
		{
			Lot->FloorComponent->EndBatchOperation();
		}
		if (Lot->TerrainComponent)
		{
			Lot->TerrainComponent->EndBatchOperation();
		}
	}

	bInBatch = false;
	BatchCommands.Empty();
	BatchDescription = TEXT("");
}

// ========== Undo/Redo ==========

void UBuildServer::Undo()
{
	if (UndoStack.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Nothing to undo"));
		return;
	}

	// Pop the last command from undo stack
	UBuildCommand* Command = UndoStack.Pop();
	if (Command && Command->IsValid())
	{
		// Undo the command
		Command->Undo();

		// Add to redo stack
		RedoStack.Add(Command);

		UE_LOG(LogTemp, Log, TEXT("BuildServer: Undid '%s'"), *Command->GetDescription());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Command is invalid, skipping"));
	}
}

void UBuildServer::Redo()
{
	if (RedoStack.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Nothing to redo"));
		return;
	}

	// Pop the last command from redo stack
	UBuildCommand* Command = RedoStack.Pop();
	if (Command && Command->IsValid())
	{
		// Redo the command
		Command->Redo();

		// Add back to undo stack
		UndoStack.Add(Command);

		UE_LOG(LogTemp, Log, TEXT("BuildServer: Redid '%s'"), *Command->GetDescription());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Command is invalid, skipping"));
	}
}

bool UBuildServer::CanUndo() const
{
	return UndoStack.Num() > 0;
}

bool UBuildServer::CanRedo() const
{
	return RedoStack.Num() > 0;
}

FString UBuildServer::GetUndoDescription() const
{
	if (UndoStack.Num() > 0)
	{
		UBuildCommand* Command = UndoStack.Last();
		if (Command)
		{
			return Command->GetDescription();
		}
	}
	return TEXT("No undo history");
}

FString UBuildServer::GetRedoDescription() const
{
	if (RedoStack.Num() > 0)
	{
		UBuildCommand* Command = RedoStack.Last();
		if (Command)
		{
			return Command->GetDescription();
		}
	}
	return TEXT("No redo history");
}

void UBuildServer::ClearHistory()
{
	UndoStack.Empty();
	RedoStack.Empty();
	UE_LOG(LogTemp, Log, TEXT("BuildServer: Cleared history"));
}

// ========== Private Helpers ==========

void UBuildServer::ExecuteOrBatchCommand(UBuildCommand* Command)
{
	if (!Command)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot execute null command"));
		return;
	}

	if (bInBatch)
	{
		// Add to batch
		BatchCommands.Add(Command);
		// Commit immediately so user sees the result, but don't add to history yet
		Command->Commit();
	}
	else
	{
		// Commit command
		Command->Commit();

		// Add to undo stack
		UndoStack.Add(Command);

		// Clear redo stack (new action invalidates redo history)
		RedoStack.Empty();

		// Enforce max history size
		if (UndoStack.Num() > MaxHistorySize)
		{
			UndoStack.RemoveAt(0);
		}

		UE_LOG(LogTemp, Log, TEXT("BuildServer: Committed '%s'"), *Command->GetDescription());

		// NOTE: Room detection is now handled directly in WallCommand::Commit/Undo/Redo
		// This ensures floors/ceilings are auto-generated immediately when loops close
		// The old incremental detection code here has been removed to prevent duplicate detection
	}
}

// Helper: Check if a point is inside a polygon using ray casting
bool IsPointInPolygon(const FVector& Point, const TArray<FVector>& PolygonVertices)
{
	if (PolygonVertices.Num() < 3)
		return false;

	int32 Crossings = 0;
	for (int32 i = 0; i < PolygonVertices.Num(); i++)
	{
		const FVector& V1 = PolygonVertices[i];
		const FVector& V2 = PolygonVertices[(i + 1) % PolygonVertices.Num()];

		// Ray casting along +X axis from point
		if (((V1.Y <= Point.Y) && (V2.Y > Point.Y)) || ((V1.Y > Point.Y) && (V2.Y <= Point.Y)))
		{
			float VT = (Point.Y - V1.Y) / (V2.Y - V1.Y);
			if (Point.X < V1.X + VT * (V2.X - V1.X))
			{
				Crossings++;
			}
		}
	}

	return (Crossings % 2) == 1;
}

// Calculate which tile sections (Top/Right/Bottom/Left) are inside the room polygon
FTileSectionState CalculateTileSectionsInPolygon(UWorld* World, const FVector& TileCenter, const TArray<FVector>& PolygonVertices, float TileSize)
{
	FTileSectionState SectionState;

	if (PolygonVertices.Num() < 3)
	{
		// No valid polygon - enable all sections
		return SectionState;
	}

	// Each tile section is a triangle: two corners + center
	// Triangle centroid = (V1 + V2 + V3) / 3
	// IMPORTANT: Corners are at ±0.45*TileSize (not 0.5), creating 10% tile gaps
	// For Top triangle: (TopLeft + TopRight + Center) / 3
	//   TopLeft = Center + (-0.45*T, +0.45*T)
	//   TopRight = Center + (+0.45*T, +0.45*T)
	//   Centroid = ((-0.45T + 0.45T + 0), (+0.45T + 0.45T + 0)) / 3 = (0, 0.3*T)
	float CentroidOffset = TileSize * 0.3f;  // = (2 * 0.45) / 3 = 0.3

	// Test centroid of each triangular section
	// Top triangle: TopLeft, TopRight, Center
	FVector TopCenter = TileCenter + FVector(0, CentroidOffset, 0);
	SectionState.Top = IsPointInPolygon(TopCenter, PolygonVertices);

	// Right triangle: TopRight, BottomRight, Center
	FVector RightCenter = TileCenter + FVector(CentroidOffset, 0, 0);
	SectionState.Right = IsPointInPolygon(RightCenter, PolygonVertices);

	// Bottom triangle: BottomRight, BottomLeft, Center
	FVector BottomCenter = TileCenter + FVector(0, -CentroidOffset, 0);
	SectionState.Bottom = IsPointInPolygon(BottomCenter, PolygonVertices);

	// Left triangle: BottomLeft, TopLeft, Center
	FVector LeftCenter = TileCenter + FVector(-CentroidOffset, 0, 0);
	SectionState.Left = IsPointInPolygon(LeftCenter, PolygonVertices);

	// CRITICAL FIX: If no sections are inside the polygon, enable all sections
	// This handles edge cases where a tile is in InteriorTiles but all section test points
	// fall outside the polygon boundary (can happen with certain mixed wall room shapes)
	if (!SectionState.Top && !SectionState.Right && !SectionState.Bottom && !SectionState.Left)
	{
		// The tile center is inside the room (or we wouldn't be here), but all section
		// test points are outside. This is likely a boundary tile with an unusual polygon shape.
		// Enable all sections to ensure the floor renders rather than creating an invisible tile.
		SectionState.Top = true;
		SectionState.Right = true;
		SectionState.Bottom = true;
		SectionState.Left = true;

		UE_LOG(LogTemp, Warning, TEXT("CalculateTileSectionsInPolygon: All sections were outside polygon at (%.0f,%.0f). Enabling all sections as fallback."),
			TileCenter.X, TileCenter.Y);
	}

	// Debug visualization: Draw spheres at test points
	// Green = inside polygon, Red = outside polygon, Yellow = fallback (all enabled)
	if (BurbArchitectDebug::IsTileDebugEnabled() && World)
	{
		float DebugZ = TileCenter.Z + 50.0f; // Offset upward for visibility
		bool bIsFallback = (SectionState.Top && SectionState.Right && SectionState.Bottom && SectionState.Left);

		DrawDebugSphere(World, TopCenter + FVector(0, 0, DebugZ), 10.0f, 8,
			bIsFallback ? FColor::Yellow : (SectionState.Top ? FColor::Green : FColor::Red), false, 30.0f);

		DrawDebugSphere(World, RightCenter + FVector(0, 0, DebugZ), 10.0f, 8,
			bIsFallback ? FColor::Yellow : (SectionState.Right ? FColor::Green : FColor::Red), false, 30.0f);

		DrawDebugSphere(World, BottomCenter + FVector(0, 0, DebugZ), 10.0f, 8,
			bIsFallback ? FColor::Yellow : (SectionState.Bottom ? FColor::Green : FColor::Red), false, 30.0f);

		DrawDebugSphere(World, LeftCenter + FVector(0, 0, DebugZ), 10.0f, 8,
			bIsFallback ? FColor::Yellow : (SectionState.Left ? FColor::Green : FColor::Red), false, 30.0f);

		// Log asymmetries (Top/Bottom or Left/Right mismatch)
		if (SectionState.Top != SectionState.Bottom || SectionState.Left != SectionState.Right)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Tile asymmetry at (%.1f, %.1f): T=%d R=%d B=%d L=%d"),
				TileCenter.X, TileCenter.Y,
				SectionState.Top, SectionState.Right, SectionState.Bottom, SectionState.Left);
		}
	}

	return SectionState;
}

// ========== Room Detection ==========

// Helper: Group InteriorTriangles by tile coordinate and create FTileSectionState for each tile
// This uses the authoritative triangle data from flood fill room detection
static TMap<FIntVector, FTileSectionState> GroupTrianglesToTileStates(const TArray<FTriangleCoord>& Triangles)
{
	TMap<FIntVector, FTileSectionState> TileStates;

	for (const FTriangleCoord& Tri : Triangles)
	{
		FIntVector TileCoord(Tri.Row, Tri.Column, Tri.Level);

		// Initialize with all false if this is a new tile
		if (!TileStates.Contains(TileCoord))
		{
			FTileSectionState NewState;
			NewState.Top = false;
			NewState.Right = false;
			NewState.Bottom = false;
			NewState.Left = false;
			TileStates.Add(TileCoord, NewState);
		}

		FTileSectionState& State = TileStates[TileCoord];

		// Enable the specific triangle that exists in this tile
		switch (Tri.Triangle)
		{
			case ETriangleType::Top:    State.Top = true; break;
			case ETriangleType::Right:  State.Right = true; break;
			case ETriangleType::Bottom: State.Bottom = true; break;
			case ETriangleType::Left:   State.Left = true; break;
			default: break;
		}
	}

	return TileStates;
}

void UBuildServer::AutoGenerateRoomFloorsAndCeilings(int32 RoomID)
{
	if (!CurrentLot || !CurrentLot->RoomManager)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Cannot auto-generate - CurrentLot or RoomManager is null"));
		return;
	}

	FRoomData* DetectedRoom = CurrentLot->RoomManager->Rooms.Find(RoomID);
	if (!DetectedRoom || !DetectedRoom->IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: RoomID %d not found or invalid"), RoomID);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildServer: Auto-generating floors/ceilings for Room %d with %d triangles, %d tiles, %d boundary edges"),
		DetectedRoom->RoomID, DetectedRoom->InteriorTriangles.Num(), DetectedRoom->InteriorTiles.Num(), DetectedRoom->BoundaryEdges.Num());

	// Track if WE started a batch (to avoid ending someone else's batch)
	bool bWeStartedBatch = false;
	if (!bInBatch)
	{
		BeginBatch(FString::Printf(TEXT("Auto-generate Room %d floors/ceilings"), RoomID));
		bWeStartedBatch = true;
	}

	// TRIANGLE-FIRST APPROACH: Use InteriorTriangles directly from flood fill room detection
	// This is the authoritative source - no need for geometric polygon testing
	TMap<FIntVector, FTileSectionState> TileStates = GroupTrianglesToTileStates(DetectedRoom->InteriorTriangles);

	// Auto-generate floor tiles using the grouped triangle states
	for (const auto& TilePair : TileStates)
	{
		const FIntVector& TileCoord = TilePair.Key;
		const FTileSectionState& TileState = TilePair.Value;

		// Get tile center location
		FVector TileCenter;
		if (CurrentLot->TileToGridLocation(TileCoord.Z, TileCoord.X, TileCoord.Y, true, TileCenter))
		{
			// Check if floor already exists
			FFloorSegmentData ExistingFloor;
			bool bFloorExists = CurrentLot->FloorComponent->FindExistingFloorSection(TileCoord.Z, TileCenter, ExistingFloor);

			if (!bFloorExists)
			{
				// Build floor using the triangle state from flood fill (no polygon testing needed)
				BuildFloor(TileCoord.Z, TileCenter, CurrentLot->DefaultFloorPattern, CurrentLot->DefaultFloorMaterial, TileState);
				UE_LOG(LogTemp, Log, TEXT("  Created floor at (%d,%d,%d) with sections: T=%d R=%d B=%d L=%d"),
					TileCoord.X, TileCoord.Y, TileCoord.Z,
					TileState.Top ? 1 : 0, TileState.Right ? 1 : 0, TileState.Bottom ? 1 : 0, TileState.Left ? 1 : 0);
			}
		}
	}

	// Generate ceiling tiles and floors at Level+1 if not at top floor
	if (DetectedRoom->Level + 1 < CurrentLot->Basements + CurrentLot->Floors)
	{
		// Skip ceiling generation for roof rooms (roof provides the ceiling)
		if (DetectedRoom->bIsRoofRoom)
		{
			UE_LOG(LogTemp, Log, TEXT("BuildServer: Skipping ceiling for roof room %d (roof provides ceiling)"), RoomID);
			// Skip the entire ceiling generation block - roof rooms only get floors
		}
		else
		{
			// Check if we should skip ceiling generation due to terrain (for basements only)
			bool bShouldGenerateCeiling = true;
			bool bIsBasementRoom = (DetectedRoom->Level < CurrentLot->Basements);
			int32 CeilingLevel = DetectedRoom->Level + 1;

		UE_LOG(LogTemp, Log, TEXT("BuildServer: Ceiling check for room at Level %d (Basements=%d, bIsBasement=%d, bAllowTerrainCeilings=%d)"),
			DetectedRoom->Level, CurrentLot->Basements, bIsBasementRoom, CurrentLot->bAllowTerrainCeilings);

		if (bIsBasementRoom && CurrentLot->bAllowTerrainCeilings && CurrentLot->TerrainComponent)
		{
			// Check if terrain exists at ceiling level for any tile in the room
			int32 TerrainTileCount = 0;
			for (const FIntVector& InteriorTile : DetectedRoom->InteriorTiles)
			{
				FTerrainSegmentData* TerrainTile = CurrentLot->TerrainComponent->FindTerrainTile(
					CeilingLevel, InteriorTile.X, InteriorTile.Y);

				if (TerrainTile && TerrainTile->bCommitted)
				{
					TerrainTileCount++;
					// Terrain exists above - skip ceiling generation, terrain acts as ceiling
					bShouldGenerateCeiling = false;
					UE_LOG(LogTemp, Log, TEXT("  Found terrain at ceiling level (%d,%d,%d) - terrain will act as basement ceiling"),
						InteriorTile.X, InteriorTile.Y, CeilingLevel);
					break;
				}
			}

			if (!bShouldGenerateCeiling)
			{
				UE_LOG(LogTemp, Log, TEXT("BuildServer: Using terrain as basement ceiling - skipping ceiling generation (Level %d)"), DetectedRoom->Level);
			}
		}

		if (bShouldGenerateCeiling)
		{
			// Convert FIntVector interior tiles to FTileData for GenerateTilesAboveRoom
			TArray<FTileData> RoomTiles;
			for (const FIntVector& InteriorTile : DetectedRoom->InteriorTiles)
			{
				FTileData RoomTile = CurrentLot->FindTileByGridCoords(InteriorTile.X, InteriorTile.Y, InteriorTile.Z);
				if (RoomTile.TileIndex != -1)
				{
					RoomTiles.Add(RoomTile);
				}
			}

			// Generate tiles above the room for ceiling support
			CurrentLot->GenerateTilesAboveRoom(RoomTiles, DetectedRoom->Level + 1);

			// TRIANGLE-FIRST APPROACH: Use the same TileStates we computed for floors
			// Ceilings should match the floor shape exactly
			for (const auto& TilePair : TileStates)
			{
				const FIntVector& FloorTileCoord = TilePair.Key;
				const FTileSectionState& CeilingShape = TilePair.Value;

				FVector CeilingCenter;
				if (CurrentLot->TileToGridLocation(DetectedRoom->Level + 1, FloorTileCoord.X, FloorTileCoord.Y, true, CeilingCenter))
				{
					// Check if ceiling already exists
					int32 CeilingRow, CeilingColumn;
					bool bCeilingExists = false;
					if (CurrentLot->LocationToTile(CeilingCenter, CeilingRow, CeilingColumn))
					{
						FFloorTileData* ExistingCeiling = CurrentLot->FloorComponent->FindFloorTile(DetectedRoom->Level + 1, CeilingRow, CeilingColumn);
						bCeilingExists = (ExistingCeiling != nullptr);
					}

					if (!bCeilingExists)
					{
						// Only create ceiling if at least one triangle exists (from room detection)
						if (CeilingShape.Top || CeilingShape.Right || CeilingShape.Bottom || CeilingShape.Left)
						{
							UE_LOG(LogTemp, Log, TEXT("  Creating ceiling at (%d,%d,%d) matching floor shape (T=%d R=%d B=%d L=%d)"),
								FloorTileCoord.X, FloorTileCoord.Y, DetectedRoom->Level + 1,
								CeilingShape.Top ? 1 : 0, CeilingShape.Right ? 1 : 0,
								CeilingShape.Bottom ? 1 : 0, CeilingShape.Left ? 1 : 0);

							BuildFloor(DetectedRoom->Level + 1, CeilingCenter, CurrentLot->DefaultFloorPattern, CurrentLot->DefaultFloorMaterial, CeilingShape);
						}
					}
				}
			}

			// Collect ceiling tiles for expansion generation
			TArray<FTileData> CeilingTiles;
			for (const FIntVector& InteriorTile : DetectedRoom->InteriorTiles)
			{
				FTileData CeilingTile = CurrentLot->FindTileByGridCoords(InteriorTile.X, InteriorTile.Y, DetectedRoom->Level + 1);
				if (CeilingTile.TileIndex != -1)
				{
					CeilingTiles.Add(CeilingTile);
				}
			}

			// Generate adjacent expansion tiles at Level+1 (1-tile overhang around ceiling edges)
			if (CeilingTiles.Num() > 0)
			{
				CurrentLot->GenerateAdjacentExpansionTiles(CeilingTiles, DetectedRoom->Level + 1);
				UE_LOG(LogTemp, Log, TEXT("  Generated expansion tiles around %d ceiling tiles at Level %d"), CeilingTiles.Num(), DetectedRoom->Level + 1);
			}
		} // End if (bShouldGenerateCeiling)
		} // End else (not a roof room)
	}

	// Rebuild only grid levels that received new tiles (incremental update)
	// More efficient than full regeneration - skips basements and ground floor
	CurrentLot->RebuildDirtyGridLevels();

	// Trigger floor polygon rendering by marking level dirty
	if (CurrentLot->FloorComponent)
	{
		// Set the level material to default floor material
		if (CurrentLot->DefaultFloorMaterial)
		{
			CurrentLot->FloorComponent->LevelMaterials.Add(DetectedRoom->Level, CurrentLot->DefaultFloorMaterial);
		}
		CurrentLot->FloorComponent->MarkLevelDirty(DetectedRoom->Level);
	}

	// Draw room boundary polygon and centroid for debugging
	if (GetWorld() && DetectedRoom->BoundaryVertices.Num() >= 3)
	{
		// Draw lines between consecutive vertices (CYAN = detected boundary)
		for (int32 i = 0; i < DetectedRoom->BoundaryVertices.Num(); i++)
		{
			FVector V1 = DetectedRoom->BoundaryVertices[i];
			FVector V2 = DetectedRoom->BoundaryVertices[(i + 1) % DetectedRoom->BoundaryVertices.Num()];

			// Raise slightly above floor to be visible
			V1.Z += 50.0f;
			V2.Z += 50.0f;

			if (BurbArchitectDebug::IsRoomDebugEnabled())
			{
				DrawDebugLine(GetWorld(), V1, V2, FColor::Cyan, false, 30.0f, 0, 5.0f);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Drew CYAN room boundary for Room %d with %d vertices"),
			DetectedRoom->RoomID, DetectedRoom->BoundaryVertices.Num());

		// Draw centroid
		FVector CentroidVis = DetectedRoom->Centroid;
		CentroidVis.Z += 50.0f;
		if (BurbArchitectDebug::IsRoomDebugEnabled())
		{
			DrawDebugSphere(GetWorld(), CentroidVis, 30.0f, 8, FColor::Magenta, false, 30.0f, 0, 3.0f);
			DrawDebugString(GetWorld(), CentroidVis + FVector(0, 0, 50), FString::Printf(TEXT("Room %d"), DetectedRoom->RoomID), nullptr, FColor::White, 10.0f, true, 1.5f);
		}
	}

	// Only end batch if WE started it (avoid ending someone else's batch)
	if (bWeStartedBatch)
	{
		EndBatch();
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildServer: Auto-generated floors/ceilings for Room %d (%d tiles, %d boundary edges, centroid at (%.1f,%.1f,%.1f))"),
		DetectedRoom->RoomID, DetectedRoom->InteriorTiles.Num(), DetectedRoom->BoundaryEdges.Num(),
		DetectedRoom->Centroid.X, DetectedRoom->Centroid.Y, DetectedRoom->Centroid.Z);
}

void UBuildServer::DetectRoomsForBatch(UBuildCommand* BatchCommand)
{
	UE_LOG(LogTemp, Warning, TEXT("BuildServer: DetectRoomsForBatch called"));

	if (!BatchCommand)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: BatchCommand is null"));
		return;
	}

	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: CurrentLot is null"));
		return;
	}

	// Debug: Log grid and lot info
	UE_LOG(LogTemp, Warning, TEXT("BuildServer: CurrentLot=%s, Grid has %d tiles, GridSizeX=%d, GridSizeY=%d, TileIndexMap has %d entries"),
		*CurrentLot->GetName(), CurrentLot->GridData.Num(), CurrentLot->GridSizeX, CurrentLot->GridSizeY, CurrentLot->TileIndexMap.Num());

	// Check if grid has been generated
	// Note: GridData is marked Transient, so it's cleared when PIE starts and must be regenerated in BeginPlay
	if (CurrentLot->GridData.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: LotManager grid is empty (GridData is Transient). Regenerating grid..."));
		CurrentLot->GenerateGrid();
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: After regeneration: Grid has %d tiles, TileIndexMap has %d entries"),
			CurrentLot->GridData.Num(), CurrentLot->TileIndexMap.Num());

		if (CurrentLot->GridData.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("BuildServer: Grid regeneration failed. Cannot detect rooms."));
			return;
		}
	}

	// Get batch command to access individual commands
	UBatchCommand* Batch = Cast<UBatchCommand>(BatchCommand);
	if (!Batch)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Failed to cast to UBatchCommand"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildServer: Batch has %d commands"), Batch->GetCommandCount());

	// Use incremental room detection for each wall in the batch
	// Much faster than tile-based detection
	const TArray<UBuildCommand*>& Commands = Batch->GetCommands();

	// PRE-CHECK: Verify if ANY wall in the batch closes a loop
	// If none do, exit early to avoid unnecessary processing
	bool bAnyWallClosesLoop = false;
	for (UBuildCommand* Cmd : Commands)
	{
		UWallCommand* WallCmd = Cast<UWallCommand>(Cmd);
		if (WallCmd && CurrentLot->RoomManager)
		{
			int32 WallEdgeID = WallCmd->GetWallEdgeID();
			if (WallEdgeID != -1)
			{
				const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(WallEdgeID);
				if (Edge)
				{
					bool bClosesLoop = CurrentLot->WallGraph->DoesPathExistBetweenNodes(
						Edge->FromNodeID,
						Edge->ToNodeID,
						WallEdgeID
					);
					if (bClosesLoop)
					{
						bAnyWallClosesLoop = true;
						break; // Found at least one, no need to check further
					}
				}
			}
		}
	}

	if (!bAnyWallClosesLoop)
	{
		UE_LOG(LogTemp, Log, TEXT("BuildServer: No walls in batch close loops, skipping room detection entirely"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("BuildServer: At least one wall closes a loop, proceeding with room detection"));

	// Collect all levels that have walls being placed
	TSet<int32> AffectedLevels;
	for (UBuildCommand* Cmd : Commands)
	{
		UWallCommand* WallCmd = Cast<UWallCommand>(Cmd);
		if (WallCmd && CurrentLot->WallGraph)
		{
			int32 WallEdgeID = WallCmd->GetWallEdgeID();
			if (WallEdgeID != -1)
			{
				const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(WallEdgeID);
				if (Edge)
				{
					AffectedLevels.Add(Edge->Level);
				}
			}
		}
	}

	// FLOOD FILL APPROACH: Detect all rooms ONCE per affected level
	// This replaces per-edge detection which caused room IDs to become stale
	TArray<int32> NewlyDetectedRoomIDs;

	for (int32 Level : AffectedLevels)
	{
		UE_LOG(LogTemp, Log, TEXT("BuildServer: Running flood fill room detection for level %d"), Level);

		// DetectAllRooms returns all rooms on the level
		int32 RoomsOnLevel = CurrentLot->RoomManager->DetectAllRooms(Level);

		// Collect all room IDs on this level for floor/ceiling generation
		for (const auto& RoomPair : CurrentLot->RoomManager->Rooms)
		{
			if (RoomPair.Value.Level == Level)
			{
				NewlyDetectedRoomIDs.AddUnique(RoomPair.Key);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("BuildServer: Detected %d rooms on level %d"), RoomsOnLevel, Level);
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildServer: Auto-generating floors/ceilings for %d rooms"), NewlyDetectedRoomIDs.Num());

	// Process each detected room - rooms now exist because detection just ran
	for (int32 RoomID : NewlyDetectedRoomIDs)
	{
		AutoGenerateRoomFloorsAndCeilings(RoomID);
	}

	// Log all wall edge room assignments BEFORE regenerating
	if (CurrentLot && CurrentLot->WallGraph)
	{
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Wall Edge Room Assignments BEFORE Wall Regeneration"));
		UE_LOG(LogTemp, Warning, TEXT("========================================"));

		for (const auto& EdgePair : CurrentLot->WallGraph->Edges)
		{
			const FWallEdge& Edge = EdgePair.Value;
			UE_LOG(LogTemp, Warning, TEXT("  Edge %d: Room1=%d, Room2=%d"),
				EdgePair.Key, Edge.Room1, Edge.Room2);
		}

		UE_LOG(LogTemp, Warning, TEXT("========================================"));
	}

	// OPTIMIZED: Only regenerate walls that border newly detected rooms
	// This preserves wall trim/mitring and dramatically improves performance
	// RegenerateWallSection() repopulates ConnectedWallsAtStartDir/EndDir before regenerating mesh
	if (CurrentLot && CurrentLot->WallComponent && CurrentLot->RoomManager)
	{
		TSet<int32> WallsToRegenerate; // Use set to avoid duplicates (walls border multiple rooms)

		// Collect all wall edge IDs from newly detected rooms
		for (int32 RoomID : NewlyDetectedRoomIDs)
		{
			if (CurrentLot->RoomManager->Rooms.Contains(RoomID))
			{
				const FRoomData& Room = CurrentLot->RoomManager->Rooms[RoomID];

				// Add all boundary edge IDs for this room
				for (int32 EdgeID : Room.BoundaryEdges)
				{
					// Find the wall in WallDataArray by matching WallEdgeID
					for (int32 i = 0; i < CurrentLot->WallComponent->WallDataArray.Num(); i++)
					{
						if (CurrentLot->WallComponent->WallDataArray[i].WallEdgeID == EdgeID)
						{
							WallsToRegenerate.Add(i);
							break;
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("BuildServer: Regenerating %d walls (from %d new rooms) instead of all %d walls"),
			WallsToRegenerate.Num(), NewlyDetectedRoomIDs.Num(), CurrentLot->WallComponent->WallDataArray.Num());

		// Regenerate only affected walls using the existing working logic
		// RegenerateWallSection() populates ConnectedWalls arrays before calling GenerateWallMeshSection()
		for (int32 WallArrayIndex : WallsToRegenerate)
		{
			if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallArrayIndex))
			{
				// Clear existing connection data to force fresh calculation
				CurrentLot->WallComponent->WallDataArray[WallArrayIndex].ConnectedWallsAtStartDir.Empty();
				CurrentLot->WallComponent->WallDataArray[WallArrayIndex].ConnectedWallsAtEndDir.Empty();
				CurrentLot->WallComponent->WallDataArray[WallArrayIndex].ConnectedWallsSections.Empty();

				// Regenerate with recursive=true to update neighboring walls
				CurrentLot->WallComponent->RegenerateWallSection(
					CurrentLot->WallComponent->WallDataArray[WallArrayIndex],
					true  // bRecursive - update connected walls too
				);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildServer: DetectRoomsForBatch complete"));
}

void UBuildServer::DetectRoomsForWall(int32 Level, const FVector& StartLoc, const FVector& EndLoc)
{
	if (!CurrentLot)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildServer: DetectRoomsForWall called for single wall at Level %d"), Level);

	// Convert to grid coordinates
	int32 StartRow, StartCol, EndRow, EndCol;
	if (!CurrentLot->LocationToTile(StartLoc, StartRow, StartCol) ||
		!CurrentLot->LocationToTile(EndLoc, EndRow, EndCol))
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer: Failed to convert wall locations to grid coordinates"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("BuildServer: Wall corners: (%d,%d) to (%d,%d)"), StartRow, StartCol, EndRow, EndCol);

	// Get tiles adjacent using grid coordinates
	// Wall corners are at grid intersections, tiles are bounded by corners
	TSet<FIntVector> AffectedTileCoords;

	// Add all valid tiles adjacent to both wall endpoints
	// This works for both orthogonal and diagonal walls

	// Tiles adjacent to start corner (up to 4 tiles)
	if (StartRow > 0 && StartCol > 0)
		AffectedTileCoords.Add(FIntVector(StartRow - 1, StartCol - 1, Level));
	if (StartRow > 0 && StartCol < CurrentLot->GridSizeY)
		AffectedTileCoords.Add(FIntVector(StartRow - 1, StartCol, Level));
	if (StartRow < CurrentLot->GridSizeX && StartCol > 0)
		AffectedTileCoords.Add(FIntVector(StartRow, StartCol - 1, Level));
	if (StartRow < CurrentLot->GridSizeX && StartCol < CurrentLot->GridSizeY)
		AffectedTileCoords.Add(FIntVector(StartRow, StartCol, Level));

	// Tiles adjacent to end corner (up to 4 tiles)
	if (EndRow > 0 && EndCol > 0)
		AffectedTileCoords.Add(FIntVector(EndRow - 1, EndCol - 1, Level));
	if (EndRow > 0 && EndCol < CurrentLot->GridSizeY)
		AffectedTileCoords.Add(FIntVector(EndRow - 1, EndCol, Level));
	if (EndRow < CurrentLot->GridSizeX && EndCol > 0)
		AffectedTileCoords.Add(FIntVector(EndRow, EndCol - 1, Level));
	if (EndRow < CurrentLot->GridSizeX && EndCol < CurrentLot->GridSizeY)
		AffectedTileCoords.Add(FIntVector(EndRow, EndCol, Level));

	UE_LOG(LogTemp, Log, TEXT("BuildServer: Found %d affected tiles"), AffectedTileCoords.Num());

	// Track processed tiles to avoid redundant room detection
	TSet<FIntVector> ProcessedTiles;

	// Run room detection on each affected tile
	for (const FIntVector& TileCoord : AffectedTileCoords)
	{
		if (ProcessedTiles.Contains(TileCoord))
		{
			UE_LOG(LogTemp, Log, TEXT("BuildServer: Tile (%d,%d,%d) already processed, skipping"), TileCoord.X, TileCoord.Y, TileCoord.Z);
			continue;
		}

		FTileData Tile = CurrentLot->FindTileByGridCoords(TileCoord.X, TileCoord.Y, TileCoord.Z);

		if (Tile.TileIndex == -1 || Tile.bOutOfBounds)
		{
			UE_LOG(LogTemp, Log, TEXT("BuildServer: Tile (%d,%d,%d) is invalid or out of bounds"), TileCoord.X, TileCoord.Y, TileCoord.Z);
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("BuildServer: Detecting room from tile (%d,%d,%d)"), TileCoord.X, TileCoord.Y, TileCoord.Z);

		// Use RoomManager polygon-based room detection
		FRoomData DetectedRoom = CurrentLot->RoomManager->DetectRoomFromTile(TileCoord);

		if (DetectedRoom.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("BuildServer: Detected room (RoomID: %d) with %d interior tiles"),
				DetectedRoom.RoomID, DetectedRoom.InteriorTiles.Num());

			// Mark all interior tiles as processed
			for (const FIntVector& InteriorTile : DetectedRoom.InteriorTiles)
			{
				ProcessedTiles.Add(InteriorTile);
			}

			// DISABLED: Rooms now render as polygons in FloorComponent::RebuildLevel
			// Auto-generate floors for all interior tiles
			/*
			for (const FIntVector& InteriorTile : DetectedRoom.InteriorTiles)
			{
				FVector TileCenter;
				if (CurrentLot->TileToGridLocation(InteriorTile.Z, InteriorTile.X, InteriorTile.Y, true, TileCenter))
				{
					FFloorSegmentData ExistingFloor;
					bool bFloorExists = CurrentLot->FloorComponent->FindExistingFloorSection(InteriorTile.Z, TileCenter, ExistingFloor);

					if (!bFloorExists)
					{
						BuildFloor(InteriorTile.Z, TileCenter, CurrentLot->DefaultFloorPattern, CurrentLot->DefaultFloorMaterial, FTileSectionState());
						UE_LOG(LogTemp, Log, TEXT("  Created floor at (%d,%d,%d)"), InteriorTile.X, InteriorTile.Y, InteriorTile.Z);
					}
				}
			}
			*/

			// Cache the room in RoomManager (DetectRoomFromTile doesn't cache it)
			CurrentLot->RoomManager->Rooms.Add(DetectedRoom.RoomID, DetectedRoom);

			// Trigger floor polygon rendering by marking level dirty
			if (CurrentLot->FloorComponent)
			{
				// Set the level material to default floor material
				if (CurrentLot->DefaultFloorMaterial)
				{
					CurrentLot->FloorComponent->LevelMaterials.Add(DetectedRoom.Level, CurrentLot->DefaultFloorMaterial);
				}
				CurrentLot->FloorComponent->MarkLevelDirty(DetectedRoom.Level);
			}

			// Draw room boundary polygon and centroid for debugging
			if (GetWorld() && DetectedRoom.BoundaryVertices.Num() >= 3)
			{
				// Draw lines between consecutive vertices (CYAN = detected boundary)
				for (int32 i = 0; i < DetectedRoom.BoundaryVertices.Num(); i++)
				{
					FVector V1 = DetectedRoom.BoundaryVertices[i];
					FVector V2 = DetectedRoom.BoundaryVertices[(i + 1) % DetectedRoom.BoundaryVertices.Num()];

					// Raise slightly above floor to be visible
					V1.Z += 50.0f;
					V2.Z += 50.0f;

					if (BurbArchitectDebug::IsRoomDebugEnabled())
					{
						DrawDebugLine(GetWorld(), V1, V2, FColor::Cyan, false, 30.0f, 0, 5.0f);
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("BuildServer: Drew CYAN room boundary for Room %d with %d vertices"),
					DetectedRoom.RoomID, DetectedRoom.BoundaryVertices.Num());

				// Draw centroid
				FVector CentroidVis = DetectedRoom.Centroid;
				CentroidVis.Z += 50.0f;
				if (BurbArchitectDebug::IsRoomDebugEnabled())
				{
					DrawDebugSphere(GetWorld(), CentroidVis, 30.0f, 8, FColor::Magenta, false, 30.0f, 0, 3.0f);
					DrawDebugString(GetWorld(), CentroidVis + FVector(0, 0, 50),FString::Printf(TEXT("Room %d"), DetectedRoom.RoomID), nullptr, FColor::White, 10.0f, true, 1.5f);
				}
			}

			UE_LOG(LogTemp, Warning, TEXT("BuildServer: Room created and cached with RoomID=%d, %d tiles, %d boundary edges, centroid at (%.1f,%.1f,%.1f)"),
				DetectedRoom.RoomID, DetectedRoom.InteriorTiles.Num(), DetectedRoom.BoundaryEdges.Num(),
				DetectedRoom.Centroid.X, DetectedRoom.Centroid.Y, DetectedRoom.Centroid.Z);

			// OPTIMIZED: Only regenerate walls that border this newly detected room
			// This preserves wall trim/mitring and dramatically improves performance
			if (CurrentLot->WallComponent)
			{
				TSet<int32> WallsToRegenerate;

				// Collect all wall edge IDs from the detected room
				for (int32 EdgeID : DetectedRoom.BoundaryEdges)
				{
					// Find the wall in WallDataArray by matching WallEdgeID
					for (int32 i = 0; i < CurrentLot->WallComponent->WallDataArray.Num(); i++)
					{
						if (CurrentLot->WallComponent->WallDataArray[i].WallEdgeID == EdgeID)
						{
							WallsToRegenerate.Add(i);
							break;
						}
					}
				}

				UE_LOG(LogTemp, Warning, TEXT("BuildServer: Regenerating %d walls (from Room %d) instead of all %d walls"),
					WallsToRegenerate.Num(), DetectedRoom.RoomID, CurrentLot->WallComponent->WallDataArray.Num());

				// Regenerate only affected walls using the existing working logic
				for (int32 WallArrayIndex : WallsToRegenerate)
				{
					if (CurrentLot->WallComponent->WallDataArray.IsValidIndex(WallArrayIndex))
					{
						// Clear existing connection data to force fresh calculation
						CurrentLot->WallComponent->WallDataArray[WallArrayIndex].ConnectedWallsAtStartDir.Empty();
						CurrentLot->WallComponent->WallDataArray[WallArrayIndex].ConnectedWallsAtEndDir.Empty();
						CurrentLot->WallComponent->WallDataArray[WallArrayIndex].ConnectedWallsSections.Empty();

						// Regenerate with recursive=true to update neighboring walls
						CurrentLot->WallComponent->RegenerateWallSection(
							CurrentLot->WallComponent->WallDataArray[WallArrayIndex],
							true  // bRecursive - update connected walls too
						);
					}
				}
			}

			break; // Only need to detect once per wall operation
		}
	}
}

// ========================================
// Lot Save/Load Operations
// ========================================

bool UBuildServer::LoadLotFromSlot(const FString& SlotName)
{
	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::LoadLotFromSlot: No current lot set"));
		return false;
	}

	// Check if save exists
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildServer::LoadLotFromSlot: Save slot does not exist: %s"), *SlotName);
		return false;
	}

	// Load save game
	ULotSaveGame* LoadedSave = Cast<ULotSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!LoadedSave)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::LoadLotFromSlot: Failed to load save game from slot: %s"), *SlotName);
		return false;
	}

	// Create and execute load command
	ULoadLotCommand* Command = NewObject<ULoadLotCommand>(this);
	Command->Initialize(CurrentLot, LoadedSave->LotData);
	ExecuteOrBatchCommand(Command);

	UE_LOG(LogTemp, Log, TEXT("BuildServer::LoadLotFromSlot: Successfully loaded lot from slot: %s"), *SlotName);
	return true;
}

bool UBuildServer::ImportLotFromFile(const FString& FilePath)
{
	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::ImportLotFromFile: No current lot set"));
		return false;
	}

	// Get serialization subsystem
	ULotSerializationSubsystem* SerializationSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::ImportLotFromFile: LotSerializationSubsystem not found"));
		return false;
	}

	// Import from JSON
	FSerializedLotData LotData;
	if (!SerializationSubsystem->ImportFromJSON(FilePath, LotData))
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::ImportLotFromFile: Failed to import JSON from: %s"), *FilePath);
		return false;
	}

	// Create and execute load command
	ULoadLotCommand* Command = NewObject<ULoadLotCommand>(this);
	Command->Initialize(CurrentLot, LotData);
	ExecuteOrBatchCommand(Command);

	UE_LOG(LogTemp, Log, TEXT("BuildServer::ImportLotFromFile: Successfully imported lot from file: %s"), *FilePath);
	return true;
}

bool UBuildServer::LoadDefaultLot(ULotDataAsset* LotAsset)
{
	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::LoadDefaultLot: No current lot set"));
		return false;
	}

	if (!LotAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::LoadDefaultLot: LotAsset is null"));
		return false;
	}

	// Validate lot data
	if (!LotAsset->ValidateLotData())
	{
		UE_LOG(LogTemp, Error, TEXT("BuildServer::LoadDefaultLot: Lot data validation failed for asset: %s"), *LotAsset->GetName());
		return false;
	}

	// Create and execute load command
	ULoadLotCommand* Command = NewObject<ULoadLotCommand>(this);
	Command->Initialize(CurrentLot, LotAsset->LotData);
	ExecuteOrBatchCommand(Command);

	UE_LOG(LogTemp, Log, TEXT("BuildServer::LoadDefaultLot: Successfully loaded default lot: %s"), *LotAsset->GetLotName());
	return true;
}

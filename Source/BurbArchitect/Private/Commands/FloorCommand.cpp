// Fill out your copyright notice in the Description page of Project Settings.

#include "Commands/FloorCommand.h"

void UFloorCommand::Initialize(ALotManager* Lot, int32 InLevel, const FVector& InTileCenter, class UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, const FTileSectionState& TileState, int32 InSwatchIndex, bool bInIsPool, bool bInIsPoolEdge)
{
	LotManager = Lot;
	OperationMode = EFloorOperationMode::Create;
	Level = InLevel;
	TileCenter = InTileCenter;
	FloorPattern = Pattern;
	FloorMaterial = BaseMaterial;
	TileSectionState = TileState;
	SwatchIndex = InSwatchIndex;
	bFloorCreated = false;
	bIsCeiling = false;
	bTerrainWasRemoved = false;
	bIsPool = bInIsPool;
	bIsPoolEdge = bInIsPoolEdge;
}

void UFloorCommand::InitializeForExistingFloor(ALotManager* Lot, const FFloorSegmentData& ExistingFloor, class UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, bool bIsCeilingFloor, int32 InSwatchIndex)
{
	LotManager = Lot;
	OperationMode = EFloorOperationMode::Create;
	FloorData = ExistingFloor;
	Level = ExistingFloor.Level;
	TileCenter = ExistingFloor.StartLoc;
	FloorPattern = Pattern;
	FloorMaterial = BaseMaterial;
	TileSectionState = ExistingFloor.TileSectionState;
	SwatchIndex = InSwatchIndex;
	bIsCeiling = bIsCeilingFloor;
	// Mark as already created so Commit() won't create it again
	bFloorCreated = true;
	bCommitted = true;
}

void UFloorCommand::InitializeDelete(ALotManager* Lot, const FFloorSegmentData& FloorToDelete)
{
	LotManager = Lot;
	OperationMode = EFloorOperationMode::Delete;
	FloorData = FloorToDelete;
	Level = FloorToDelete.Level;
	TileCenter = FloorToDelete.StartLoc;
	FloorMaterial = nullptr;
	bFloorCreated = false;
	bIsCeiling = false;
	bTerrainWasRemoved = false;
}

void UFloorCommand::InitializeUpdate(ALotManager* Lot, const FFloorSegmentData& FloorToUpdate, const FTileSectionState& NewTileState, class UFloorPattern* Pattern, UMaterialInstance* BaseMaterial, int32 InSwatchIndex)
{
	LotManager = Lot;
	OperationMode = EFloorOperationMode::Update;
	FloorData = FloorToUpdate;
	Level = FloorToUpdate.Level;
	TileCenter = FloorToUpdate.StartLoc;
	OldTileSectionState = FloorToUpdate.TileSectionState;
	TileSectionState = NewTileState;
	FloorPattern = Pattern;
	FloorMaterial = BaseMaterial;
	SwatchIndex = InSwatchIndex;
	bFloorCreated = false;
	bIsCeiling = false;
	bTerrainWasRemoved = false;

	// Store old patterns for each existing triangle (for undo)
	OldPatterns.Empty();
	int32 Row = FloorToUpdate.Row;
	int32 Column = FloorToUpdate.Column;

	if (Row != -1 && Column != -1 && LotManager && LotManager->FloorComponent)
	{
		// Query each triangle type and store its current pattern
		if (FFloorTileData* TopTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Top))
		{
			OldPatterns.Add(ETriangleType::Top, TopTriangle->Pattern);
		}
		if (FFloorTileData* RightTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Right))
		{
			OldPatterns.Add(ETriangleType::Right, RightTriangle->Pattern);
		}
		if (FFloorTileData* BottomTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Bottom))
		{
			OldPatterns.Add(ETriangleType::Bottom, BottomTriangle->Pattern);
		}
		if (FFloorTileData* LeftTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Left))
		{
			OldPatterns.Add(ETriangleType::Left, LeftTriangle->Pattern);
		}
	}
}

void UFloorCommand::Commit()
{
	if (!LotManager || !LotManager->FloorComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("FloorCommand: LotManager or FloorComponent is null"));
		return;
	}

	if (OperationMode == EFloorOperationMode::Create)
	{
		// Skip if already created (for InitializeForExistingFloor)
		if (bFloorCreated && bCommitted)
		{
			UE_LOG(LogTemp, Log, TEXT("FloorCommand: Floor already created, skipping commit"));
			return;
		}

		// Get grid coordinates from tile center
		int32 Row, Column;
		if (!LotManager->LocationToTile(TileCenter, Row, Column))
		{
			UE_LOG(LogTemp, Error, TEXT("FloorCommand: Invalid tile center location"));
			return;
		}

		// Create separate triangle entries for each enabled quadrant
		// Each triangle is now an independent entity with its own pattern
		if (TileSectionState.Top)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Top;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.SwatchIndex = SwatchIndex;
			NewTriangle.bCommitted = true;
			NewTriangle.bIsPool = bIsPool;
			NewTriangle.bIsPoolEdge = bIsPoolEdge;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		if (TileSectionState.Right)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Right;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.SwatchIndex = SwatchIndex;
			NewTriangle.bCommitted = true;
			NewTriangle.bIsPool = bIsPool;
			NewTriangle.bIsPoolEdge = bIsPoolEdge;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		if (TileSectionState.Bottom)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Bottom;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.SwatchIndex = SwatchIndex;
			NewTriangle.bCommitted = true;
			NewTriangle.bIsPool = bIsPool;
			NewTriangle.bIsPoolEdge = bIsPoolEdge;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		if (TileSectionState.Left)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Left;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.SwatchIndex = SwatchIndex;
			NewTriangle.bCommitted = true;
			NewTriangle.bIsPool = bIsPool;
			NewTriangle.bIsPoolEdge = bIsPoolEdge;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		// Flatten terrain underneath ground-level floors to prevent Z-fighting
		bool bIsGroundFloor = (Level == LotManager->Basements);

		UE_LOG(LogTemp, Warning, TEXT("FloorCommand::Commit - Terrain flattening check: Level=%d, Basements=%d, bIsGroundFloor=%d, bRemoveTerrainUnderFloors=%d, TerrainComponent=%s"),
			Level, LotManager->Basements, bIsGroundFloor, LotManager->bRemoveTerrainUnderFloors, LotManager->TerrainComponent ? TEXT("Valid") : TEXT("NULL"));

		if (bIsGroundFloor && LotManager->bRemoveTerrainUnderFloors && LotManager->TerrainComponent)
		{
			// Check if this tile is at the lot boundary (perimeter of the grid)
			// Perimeter tiles don't flatten terrain to preserve lot edge appearance
			bool bIsLotPerimeterTile = false;

			// Check if tile is on any edge of the lot grid
			if (Row == 0 || Row == LotManager->GridSizeY - 1 ||
				Column == 0 || Column == LotManager->GridSizeX - 1)
			{
				bIsLotPerimeterTile = true;
			}

			UE_LOG(LogTemp, Warning, TEXT("FloorCommand::Commit - Lot perimeter check at (%d,%d): GridSize=(%d,%d) -> LotPerimeter=%s"),
				Row, Column, LotManager->GridSizeX, LotManager->GridSizeY,
				bIsLotPerimeterTile ? TEXT("YES") : TEXT("NO"));

			if (!bIsLotPerimeterTile)
			{
				// Interior tile - flatten terrain underneath floor
				// Check if terrain exists at this location
				FTerrainSegmentData* ExistingTerrain = LotManager->TerrainComponent->FindTerrainTile(Level, Row, Column);

				UE_LOG(LogTemp, Warning, TEXT("FloorCommand::Commit - Terrain lookup at (%d,%d) Level %d: Found=%s, Committed=%d"),
					Row, Column, Level, ExistingTerrain ? TEXT("YES") : TEXT("NO"),
					ExistingTerrain ? ExistingTerrain->bCommitted : false);

				if (ExistingTerrain && ExistingTerrain->bCommitted)
				{
					// Only flatten terrain if it's actually uneven at this tile
					const bool bIsTerrainFlat = LotManager->IsTerrainFlatAtTile(Level, Row, Column);

					if (!bIsTerrainFlat)
					{
						// Calculate target height: floor Z - small offset to prevent Z-fighting
						const FVector LotLocation = LotManager->GetActorLocation();
						const float FloorHeight = LotLocation.Z + (LotManager->DefaultWallHeight * (Level - LotManager->Basements));
						const float TerrainOffset = -2.0f; // 2 units below floor surface (subtle flattening)
						float TargetHeight = FloorHeight + TerrainOffset;

						// LOWER CLAMP: Prevent terrain from extending into basement rooms (only where basement floor exists below)
						// Check if there's a basement floor directly below this tile
						if (LotManager->Basements > 0 && LotManager->FloorComponent)
						{
							const int32 BasementLevel = Level - 1;
							bool bHasBasementFloor = LotManager->FloorComponent->HasAnyFloorTile(BasementLevel, Row, Column);

							if (bHasBasementFloor)
							{
								// There's a basement room below - clamp terrain to stay above basement ceiling
								const float BasementCeilingZ = LotLocation.Z;
								const float TerrainThickness = LotManager->TerrainComponent->TerrainThickness;
								const float MinTargetHeight = BasementCeilingZ + TerrainThickness;

								if (TargetHeight < MinTargetHeight)
								{
									TargetHeight = MinTargetHeight;
									UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Clamped terrain height to %.2f to prevent extending into basement room at (%d,%d) (ceiling at %.2f, thickness %.2f)"),
										TargetHeight, Row, Column, BasementCeilingZ, TerrainThickness);
								}
							}
						}

						// UPPER CLAMP: Prevent terrain from being raised through ground floor (apply AFTER basement clamp)
						// This handles the case where basement clamp would raise terrain above the floor surface
						// Maximum allowable terrain height = floor surface (not below, to prevent clipping through floor)
						const float MaxTargetHeight = FloorHeight;
						if (TargetHeight > MaxTargetHeight)
						{
							TargetHeight = MaxTargetHeight;
							UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Clamped terrain to %.2f to prevent raising through ground floor at (%d,%d) (basement conflict)"),
								TargetHeight, Row, Column);
						}

						// Flatten only the corners covered by floor quadrants (handles diagonal/partial floors)
						// Use bBypassLock=true to allow flattening even though floor now exists (prevents lock)
						LotManager->TerrainComponent->FlattenTerrainUnderFloor(Level, Row, Column, TileSectionState, TargetHeight, true);

						UE_LOG(LogTemp, Log, TEXT("FloorCommand: Flattened uneven terrain under floor at (%d,%d) to height %.2f"),
							Row, Column, TargetHeight);
					}
					else
					{
						UE_LOG(LogTemp, Verbose, TEXT("FloorCommand: Terrain already flat at (%d,%d), skipping flattening"),
							Row, Column);
					}

					// Terrain remains visible but is flattened underneath
					bTerrainWasRemoved = false;
				}
				else
				{
					bTerrainWasRemoved = false;
					UE_LOG(LogTemp, Warning, TEXT("FloorCommand: No terrain to flatten at Level %d, Row %d, Column %d"),
						Level, Row, Column);
				}
			}
			else
			{
				// Lot perimeter tile - preserve terrain at lot edges
				bTerrainWasRemoved = false;
				UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Preserving terrain for lot perimeter tile at Level %d, Row %d, Column %d"),
					Level, Row, Column);
			}
		}
		else
		{
			bTerrainWasRemoved = false;
			UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Skipping terrain flattening (conditions not met)"));
		}

		bFloorCreated = true;
		bCommitted = true;

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Created floor at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
	else if (OperationMode == EFloorOperationMode::Delete)
	{
		// Get grid coordinates from floor data
		int32 Row = FloorData.Row;
		int32 Column = FloorData.Column;

		if (Row == -1 || Column == -1)
		{
			// Fallback: try to get from location
			if (!LotManager->LocationToTile(TileCenter, Row, Column))
			{
				UE_LOG(LogTemp, Error, TEXT("FloorCommand: Cannot determine grid coordinates for deletion"));
				return;
			}
		}

		// Remove all triangles at this tile position (based on TileSectionState)
		if (TileSectionState.Top)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Top);
		}
		if (TileSectionState.Right)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Right);
		}
		if (TileSectionState.Bottom)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Bottom);
		}
		if (TileSectionState.Left)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Left);
		}

		bFloorCreated = false;
		bCommitted = true;

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Deleted floor triangles at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
	else if (OperationMode == EFloorOperationMode::Update)
	{
		// Get grid coordinates
		int32 Row = FloorData.Row;
		int32 Column = FloorData.Column;

		if (Row == -1 || Column == -1)
		{
			if (!LotManager->LocationToTile(TileCenter, Row, Column))
			{
				UE_LOG(LogTemp, Error, TEXT("FloorCommand: Cannot determine grid coordinates for update"));
				return;
			}
		}

		// Update operation: Add new triangles and remove old ones
		// Add new triangles
		if (TileSectionState.Top)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Top);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Top;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			// Remove if it exists but shouldn't
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Top);
		}

		if (TileSectionState.Right)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Right);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Right;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Right);
		}

		if (TileSectionState.Bottom)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Bottom);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Bottom;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Bottom);
		}

		if (TileSectionState.Left)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Left);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Left;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Left);
		}

		// Mark level dirty for rebuild
		LotManager->FloorComponent->MarkLevelDirty(Level);

		bFloorCreated = true;
		bCommitted = true;

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Updated floor triangles at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
}

void UFloorCommand::Undo()
{
	if (!bCommitted || !LotManager || !LotManager->FloorComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Cannot undo - command not committed or component invalid"));
		return;
	}

	// Get grid coordinates
	int32 Row = FloorData.Row;
	int32 Column = FloorData.Column;

	if (Row == -1 || Column == -1)
	{
		if (!LotManager->LocationToTile(TileCenter, Row, Column))
		{
			UE_LOG(LogTemp, Error, TEXT("FloorCommand: Cannot determine grid coordinates for undo"));
			return;
		}
	}

	if (OperationMode == EFloorOperationMode::Create)
	{
		if (!bFloorCreated)
		{
			UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Cannot undo create - floor doesn't exist"));
			return;
		}

		// Undo create = remove all triangles that were created (based on TileSectionState)
		if (TileSectionState.Top)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Top);
		}
		if (TileSectionState.Right)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Right);
		}
		if (TileSectionState.Bottom)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Bottom);
		}
		if (TileSectionState.Left)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Left);
		}

		// Restore terrain if it was modified
		if (LotManager->bRestoreTerrainOnFloorRemoval && LotManager->TerrainComponent)
		{
			if (bTerrainWasRemoved)
			{
				// Full removal case - restore the entire terrain tile
				LotManager->TerrainComponent->AddTerrainTile(RemovedTerrainData, RemovedTerrainData.Material);

				UE_LOG(LogTemp, Log, TEXT("FloorCommand: Restored fully removed terrain at Level %d, Row %d, Column %d"),
					RemovedTerrainData.Level, RemovedTerrainData.Row, RemovedTerrainData.Column);

				bTerrainWasRemoved = false;
			}
			else
			{
				// Partial cutout case - restore original TileSectionState
				FTerrainSegmentData* ExistingTerrain = LotManager->TerrainComponent->FindTerrainTile(Level, Row, Column);
				if (ExistingTerrain && ExistingTerrain->bCommitted)
				{
					// Restore original terrain state from stored data
					ExistingTerrain->TileSectionState = RemovedTerrainData.TileSectionState;
					LotManager->TerrainComponent->MarkLevelDirty(Level);

					UE_LOG(LogTemp, Log, TEXT("FloorCommand: Restored partial terrain cutout at Level %d, Row %d, Column %d to T=%d R=%d B=%d L=%d"),
						Level, Row, Column,
						ExistingTerrain->TileSectionState.Top ? 1 : 0,
						ExistingTerrain->TileSectionState.Right ? 1 : 0,
						ExistingTerrain->TileSectionState.Bottom ? 1 : 0,
						ExistingTerrain->TileSectionState.Left ? 1 : 0);
				}
			}
		}

		bFloorCreated = false;

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Undid floor creation at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
	else if (OperationMode == EFloorOperationMode::Delete)
	{
		if (bFloorCreated)
		{
			UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Cannot undo delete - floor already exists"));
			return;
		}

		// Undo delete = restore all triangles that were deleted
		if (TileSectionState.Top)
		{
			FFloorTileData RestoredTriangle;
			RestoredTriangle.Row = Row;
			RestoredTriangle.Column = Column;
			RestoredTriangle.Level = Level;
			RestoredTriangle.Triangle = ETriangleType::Top;
			RestoredTriangle.Pattern = FloorPattern;
			RestoredTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
		}

		if (TileSectionState.Right)
		{
			FFloorTileData RestoredTriangle;
			RestoredTriangle.Row = Row;
			RestoredTriangle.Column = Column;
			RestoredTriangle.Level = Level;
			RestoredTriangle.Triangle = ETriangleType::Right;
			RestoredTriangle.Pattern = FloorPattern;
			RestoredTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
		}

		if (TileSectionState.Bottom)
		{
			FFloorTileData RestoredTriangle;
			RestoredTriangle.Row = Row;
			RestoredTriangle.Column = Column;
			RestoredTriangle.Level = Level;
			RestoredTriangle.Triangle = ETriangleType::Bottom;
			RestoredTriangle.Pattern = FloorPattern;
			RestoredTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
		}

		if (TileSectionState.Left)
		{
			FFloorTileData RestoredTriangle;
			RestoredTriangle.Row = Row;
			RestoredTriangle.Column = Column;
			RestoredTriangle.Level = Level;
			RestoredTriangle.Triangle = ETriangleType::Left;
			RestoredTriangle.Pattern = FloorPattern;
			RestoredTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
		}

		bFloorCreated = true;

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Undid floor deletion at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
	else if (OperationMode == EFloorOperationMode::Update)
	{
		// Undo update = restore old tile state (use OldTileSectionState and OldPatterns to restore)
		// Remove triangles that exist in new state but not in old state
		// Add triangles that exist in old state but not in new state
		if (OldTileSectionState.Top)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Top);
			UFloorPattern* OldPattern = OldPatterns.FindRef(ETriangleType::Top);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = OldPattern; // Restore old pattern
			}
			else
			{
				FFloorTileData RestoredTriangle;
				RestoredTriangle.Row = Row;
				RestoredTriangle.Column = Column;
				RestoredTriangle.Level = Level;
				RestoredTriangle.Triangle = ETriangleType::Top;
				RestoredTriangle.Pattern = OldPattern;
				RestoredTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Top);
		}

		if (OldTileSectionState.Right)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Right);
			UFloorPattern* OldPattern = OldPatterns.FindRef(ETriangleType::Right);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = OldPattern; // Restore old pattern
			}
			else
			{
				FFloorTileData RestoredTriangle;
				RestoredTriangle.Row = Row;
				RestoredTriangle.Column = Column;
				RestoredTriangle.Level = Level;
				RestoredTriangle.Triangle = ETriangleType::Right;
				RestoredTriangle.Pattern = OldPattern;
				RestoredTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Right);
		}

		if (OldTileSectionState.Bottom)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Bottom);
			UFloorPattern* OldPattern = OldPatterns.FindRef(ETriangleType::Bottom);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = OldPattern; // Restore old pattern
			}
			else
			{
				FFloorTileData RestoredTriangle;
				RestoredTriangle.Row = Row;
				RestoredTriangle.Column = Column;
				RestoredTriangle.Level = Level;
				RestoredTriangle.Triangle = ETriangleType::Bottom;
				RestoredTriangle.Pattern = OldPattern;
				RestoredTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Bottom);
		}

		if (OldTileSectionState.Left)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Left);
			UFloorPattern* OldPattern = OldPatterns.FindRef(ETriangleType::Left);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = OldPattern; // Restore old pattern
			}
			else
			{
				FFloorTileData RestoredTriangle;
				RestoredTriangle.Row = Row;
				RestoredTriangle.Column = Column;
				RestoredTriangle.Level = Level;
				RestoredTriangle.Triangle = ETriangleType::Left;
				RestoredTriangle.Pattern = OldPattern;
				RestoredTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(RestoredTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Left);
		}

		LotManager->FloorComponent->MarkLevelDirty(Level);

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Undid floor update at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
}

void UFloorCommand::Redo()
{
	if (!bCommitted || !LotManager || !LotManager->FloorComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Cannot redo - command not committed or component invalid"));
		return;
	}

	// Get grid coordinates
	int32 Row = FloorData.Row;
	int32 Column = FloorData.Column;

	if (Row == -1 || Column == -1)
	{
		if (!LotManager->LocationToTile(TileCenter, Row, Column))
		{
			UE_LOG(LogTemp, Error, TEXT("FloorCommand: Cannot determine grid coordinates for redo"));
			return;
		}
	}

	if (OperationMode == EFloorOperationMode::Create)
	{
		if (bFloorCreated)
		{
			UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Cannot redo create - floor already exists"));
			return;
		}

		// Redo create = add all triangles back
		if (TileSectionState.Top)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Top;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		if (TileSectionState.Right)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Right;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		if (TileSectionState.Bottom)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Bottom;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		if (TileSectionState.Left)
		{
			FFloorTileData NewTriangle;
			NewTriangle.Row = Row;
			NewTriangle.Column = Column;
			NewTriangle.Level = Level;
			NewTriangle.Triangle = ETriangleType::Left;
			NewTriangle.Pattern = FloorPattern;
			NewTriangle.bCommitted = true;
			LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
		}

		// Re-apply terrain cutout if it was modified originally
		bool bIsGroundFloor = (Level == LotManager->Basements);
		if (bIsGroundFloor && LotManager->bRemoveTerrainUnderFloors && LotManager->TerrainComponent)
		{
			FTerrainSegmentData* ExistingTerrain = LotManager->TerrainComponent->FindTerrainTile(Level, Row, Column);
			if (ExistingTerrain && ExistingTerrain->bCommitted)
			{
				if (bTerrainWasRemoved)
				{
					// Full removal case - remove terrain completely
					LotManager->TerrainComponent->RemoveTerrainTile(
						RemovedTerrainData.Level,
						RemovedTerrainData.Row,
						RemovedTerrainData.Column);

					UE_LOG(LogTemp, Log, TEXT("FloorCommand: Re-cut terrain (full) at Level %d, Row %d, Column %d"),
						RemovedTerrainData.Level, RemovedTerrainData.Row, RemovedTerrainData.Column);
				}
				else
				{
					// Partial cutout case - reapply the inverted TileSectionState
					FTileSectionState NewTerrainState;
					NewTerrainState.Top = !TileSectionState.Top;
					NewTerrainState.Right = !TileSectionState.Right;
					NewTerrainState.Bottom = !TileSectionState.Bottom;
					NewTerrainState.Left = !TileSectionState.Left;

					ExistingTerrain->TileSectionState = NewTerrainState;
					LotManager->TerrainComponent->MarkLevelDirty(Level);

					UE_LOG(LogTemp, Log, TEXT("FloorCommand: Re-cut terrain (partial) at Level %d, Row %d, Column %d to T=%d R=%d B=%d L=%d"),
						Level, Row, Column,
						NewTerrainState.Top ? 1 : 0,
						NewTerrainState.Right ? 1 : 0,
						NewTerrainState.Bottom ? 1 : 0,
						NewTerrainState.Left ? 1 : 0);
				}
			}
		}

		bFloorCreated = true;

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Redid floor creation at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
	else if (OperationMode == EFloorOperationMode::Delete)
	{
		if (!bFloorCreated)
		{
			UE_LOG(LogTemp, Warning, TEXT("FloorCommand: Cannot redo delete - floor doesn't exist"));
			return;
		}

		// Redo delete = remove all triangles
		if (TileSectionState.Top)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Top);
		}
		if (TileSectionState.Right)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Right);
		}
		if (TileSectionState.Bottom)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Bottom);
		}
		if (TileSectionState.Left)
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Left);
		}

		bFloorCreated = false;

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Redid floor deletion at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
	else if (OperationMode == EFloorOperationMode::Update)
	{
		// Redo update = apply the new tile state (same logic as Commit Update)
		if (TileSectionState.Top)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Top);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Top;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Top);
		}

		if (TileSectionState.Right)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Right);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Right;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Right);
		}

		if (TileSectionState.Bottom)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Bottom);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Bottom;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Bottom);
		}

		if (TileSectionState.Left)
		{
			FFloorTileData* ExistingTriangle = LotManager->FloorComponent->FindFloorTile(Level, Row, Column, ETriangleType::Left);
			if (ExistingTriangle)
			{
				ExistingTriangle->Pattern = FloorPattern;
			}
			else
			{
				FFloorTileData NewTriangle;
				NewTriangle.Row = Row;
				NewTriangle.Column = Column;
				NewTriangle.Level = Level;
				NewTriangle.Triangle = ETriangleType::Left;
				NewTriangle.Pattern = FloorPattern;
				NewTriangle.bCommitted = true;
				LotManager->FloorComponent->AddFloorTile(NewTriangle, FloorMaterial);
			}
		}
		else
		{
			LotManager->FloorComponent->RemoveFloorTile(Level, Row, Column, ETriangleType::Left);
		}

		LotManager->FloorComponent->MarkLevelDirty(Level);

		UE_LOG(LogTemp, Log, TEXT("FloorCommand: Redid floor update at level %d, Row %d, Column %d (merged mesh)"),
			Level, Row, Column);
	}
}

FString UFloorCommand::GetDescription() const
{
	if (OperationMode == EFloorOperationMode::Create)
	{
		return FString::Printf(TEXT("Build Floor at Level %d (%.0f, %.0f)"),
			Level, TileCenter.X, TileCenter.Y);
	}
	else if (OperationMode == EFloorOperationMode::Delete)
	{
		return FString::Printf(TEXT("Delete Floor at Level %d (%.0f, %.0f)"),
			Level, TileCenter.X, TileCenter.Y);
	}
	else
	{
		return FString::Printf(TEXT("Update Floor at Level %d (%.0f, %.0f)"),
			Level, TileCenter.X, TileCenter.Y);
	}
}

bool UFloorCommand::IsValid() const
{
	// Command is valid if the lot manager and floor component still exist
	return LotManager && LotManager->FloorComponent;
}

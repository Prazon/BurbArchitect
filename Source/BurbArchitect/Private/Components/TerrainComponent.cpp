// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/TerrainComponent.h"

#include "Actors/LotManager.h"
#include "Components/TerrainComponent.h"
#include "Components/TerrainHeightMap.h"

UTerrainComponent::UTerrainComponent(const FObjectInitializer &ObjectInitializer) : UProceduralMeshComponent(ObjectInitializer)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	bCommitted = false;

	// Ensure component is visible
	SetVisibility(true);
	SetHiddenInGame(false);

	// Disable custom depth - terrain is permanent geometry
	bRenderCustomDepth = false;

	// Configure collision for terrain - must respond to Tile trace channel for brush tools AND support AI navigation
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionObjectType(ECC_WorldStatic);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Block); // Tile trace channel - block so brush tools can hit terrain
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Block); // Pawn channel - block so AI can walk on terrain
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block); // Visibility channel - for camera traces
}

void UTerrainComponent::PostLoad()
{
	Super::PostLoad();

	// Clean up any legacy terrain tiles that exist on levels other than ground floor
	// This handles old serialized data from before the ground-level-only restriction was added
	if (TerrainDataArray.Num() > 0)
	{
		const int32 RemovedCount = RemoveInvalidTerrainTiles();
		if (RemovedCount > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::PostLoad - Cleaned up %d invalid terrain tiles from non-ground levels"), RemovedCount);
		}
	}
}

void UTerrainComponent::BeginPlay()
{
	Super::BeginPlay();

	// Ensure terrain material is applied after Blueprint recompile
	// Use DefaultTerrainMaterial from LotManager if no material is set
	if (TerrainDataArray.Num() > 0)
	{
		ALotManager* OurLot = Cast<ALotManager>(GetOwner());
		UMaterialInstance* MaterialToApply = TerrainMaterial;

		if (!MaterialToApply && OurLot)
		{
			MaterialToApply = OurLot->DefaultTerrainMaterial;
		}

		if (MaterialToApply)
		{
			ApplySharedTerrainMaterial(MaterialToApply);
		}
	}
}

void UTerrainComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


void UTerrainComponent::DestroyTerrain()
{
	// Component removal handled by LotManager's singular component reference
	for (int i = 0; i<5 ; i++)
	{
		ClearMeshSection(i);
	}
	DestroyComponent();
}

void UTerrainComponent::DestroyTerrainSection(FTerrainSegmentData TerrainSection)
{
	// Use merged mesh system - remove tile and rebuild level
	RemoveTerrainTile(TerrainSection.Level, TerrainSection.Row, TerrainSection.Column);
}

void UTerrainComponent::CommitTerrain(UMaterialInstance* DefaultTerrainMaterial)
{
	bCommitted = true;

	// Set material on Terrain to default - use provided or fall back to LotManager's default
	if (DefaultTerrainMaterial)
	{
		TerrainMaterial = DefaultTerrainMaterial;
	}
	else if (!TerrainMaterial)
	{
		ALotManager* OurLot = Cast<ALotManager>(GetOwner());
		if (OurLot && OurLot->DefaultTerrainMaterial)
		{
			TerrainMaterial = OurLot->DefaultTerrainMaterial;
		}
	}

	// Update material for all levels in the merged mesh system
	if (TerrainMaterial)
	{
		// Update material for each level
		for (auto& LevelMaterialPair : LevelMaterials)
		{
			LevelMaterials[LevelMaterialPair.Key] = TerrainMaterial;
			SetMaterial(LevelMaterialPair.Key, TerrainMaterial);
		}
	}
}

void UTerrainComponent::CommitTerrainSection(FTerrainSegmentData InTerrainData, UMaterialInstance* DefaultTerrainMaterial)
{
	TerrainDataArray[InTerrainData.ArrayIndex].bCommitted = true;

	// Set material on terrain section - use provided default or fall back to LotManager's default
	if (DefaultTerrainMaterial)
	{
		TerrainDataArray[InTerrainData.ArrayIndex].Material = DefaultTerrainMaterial;
	}
	else if (!TerrainDataArray[InTerrainData.ArrayIndex].Material)
	{
		ALotManager* OurLot = Cast<ALotManager>(GetOwner());
		if (OurLot && OurLot->DefaultTerrainMaterial)
		{
			TerrainDataArray[InTerrainData.ArrayIndex].Material = OurLot->DefaultTerrainMaterial;
		}
	}

	// Regenerate terrain mesh with new material
	GenerateTerrainMeshSection(TerrainDataArray[InTerrainData.ArrayIndex]);
	bRenderCustomDepth = true;
}

bool UTerrainComponent::FindExistingTerrainSection(const int32 Row, const int32 Column,
	FTerrainSegmentData OutTerrain)
{
	return false;
}


FTerrainSegmentData UTerrainComponent::GenerateTerrainSection(const int32 Row, const int32 Column,
	const FVector& PointLoc, UMaterialInstance* OptionalMaterial)
{
	FTerrainSegmentData NewTerrainData;

	ALotManager* OurLot = Cast<ALotManager>(GetOwner());

	NewTerrainData.SectionIndex = -1;
	NewTerrainData.Row = Row;
	NewTerrainData.Column = Column;
	// Terrain is at ground floor level (Basements), not hardcoded 0
	NewTerrainData.Level = OurLot->Basements;

	// Terrain should be at grid level (no offset)
	// If you want terrain below grid, adjust this offset value
	NewTerrainData.PointLoc = PointLoc; // Was: PointLoc + FVector(0, 0, -2.0f)

	NewTerrainData.Width = OurLot->GridTileSize;
	// Use ground floor level (Basements) for corner calculation to get correct Z height
	// LevelHeight = DefaultWallHeight * (Level - Basements) = 300 * (Basements - Basements) = 0
	NewTerrainData.CornerLocs = OurLot->LocationToAllTileCorners(PointLoc, OurLot->Basements);
	NewTerrainData.Material = OptionalMaterial;

	return NewTerrainData;
}

FTerrainSegmentData UTerrainComponent::GenerateTerrainMeshSection(FTerrainSegmentData InTerrainData)
{
	// MERGED MESH SYSTEM: Add tile to spatial map and mark level dirty
	// The actual mesh generation is deferred to RebuildLevel()

	// Get material for this tile
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	UMaterialInstance* MaterialToUse = InTerrainData.Material;
	if (!MaterialToUse && OurLot)
	{
		MaterialToUse = OurLot->DefaultTerrainMaterial;
	}

	// Mark tile as committed
	InTerrainData.bCommitted = true;

	// Add terrain tile to spatial map (this automatically marks level dirty and triggers rebuild)
	AddTerrainTile(InTerrainData, MaterialToUse);

	// Find the tile we just added and return it
	FTerrainSegmentData* AddedTile = FindTerrainTile(InTerrainData.Level, InTerrainData.Row, InTerrainData.Column);
	if (AddedTile)
	{
		return *AddedTile;
	}

	// Fallback: return the input data (shouldn't happen)
	UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::GenerateTerrainMeshSection - Failed to find added tile at Level %d, Row %d, Column %d"),
		InTerrainData.Level, InTerrainData.Row, InTerrainData.Column);
	return InTerrainData;
}

void UTerrainComponent::FlattenTerrainOn(FTerrainSegmentData InTerrainData, const int32 Level)
{
	int32 NewSectionIndex = InTerrainData.SectionIndex;
	if(NewSectionIndex == -1)
	{
		UE_LOG(LogTemp, Error, TEXT("TheBrubs Plugin : Couldn't find Terrain Data in Memory"));
		return;
	}

	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		return;
	}

	// Calculate target flatten height for this level
	const float TargetHeight = OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Level - OurLot->Basements));

	InTerrainData.bFlatten = true;

	// NEW: Use grid-based height system to flatten all 4 corners
	// This automatically updates neighboring tiles that share these corners
	const int32 Row = InTerrainData.Row;
	const int32 Column = InTerrainData.Column;

	// Update each corner to the flatten height using the new height map system
	// Corner coordinates for this tile:
	// BottomLeft: (Row, Column)
	// BottomRight: (Row, Column+1)
	// TopLeft: (Row+1, Column)
	// TopRight: (Row+1, Column+1)

	UpdateCornerHeight(Level, Row, Column, TargetHeight);             // BottomLeft
	UpdateCornerHeight(Level, Row, Column + 1, TargetHeight);         // BottomRight
	UpdateCornerHeight(Level, Row + 1, Column, TargetHeight);         // TopLeft
	UpdateCornerHeight(Level, Row + 1, Column + 1, TargetHeight);     // TopRight

	// Update terrain data
	InTerrainData.PointLoc.Z = TargetHeight;
	TerrainDataArray[InTerrainData.ArrayIndex] = InTerrainData;

	// Note: UpdateCornerHeight already regenerates affected tiles including this one
	// No need to manually call ForceUpdateCorner or GenerateTerrainMeshSection
}

void UTerrainComponent::ForceUpdateCorner(FVector Corner)
{
	// OLD O(N×4) approach - DEPRECATED
	// This method has been replaced by UpdateCornerHeight() which uses O(1) grid lookups
	// Keeping old code commented for reference
	/*
	for (FTerrainSegmentData& Data : TerrainDataArray)
    {
		for (FVector& corner : Data.CornerLocs){
			if (Corner.X == corner.X && Corner.Y == corner.Y)
			{
				corner = Corner;
				GenerateTerrainMeshSection(Data);
			}
		}
	}
	*/

	// NEW grid-based O(1) approach using height map
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		return;
	}

	// Convert world position to grid coordinates
	int32 Row, Column;
	if (!OurLot->LocationToTile(Corner, Row, Column))
	{
		return;
	}

	// Determine corner position within the grid
	// Corners are at grid intersections, so we need to find which corner this is
	FVector TileCenter = OurLot->LocationToTileCenter(Corner);

	// Calculate corner row/column (corners are offset from tile row/column)
	// This is a simplified approach - may need refinement based on exact corner determination logic
	int32 CornerRow = Row;
	int32 CornerColumn = Column;

	// Determine level from Z position
	const float BaseZ = OurLot->GetActorLocation().Z;
	const float RelativeZ = Corner.Z - BaseZ;
	const int32 Level = FMath::RoundToInt(RelativeZ / OurLot->DefaultWallHeight) + OurLot->Basements;

	// Extract height from corner position
	const float CornerHeight = Corner.Z;

	// Use the new grid-based update method (O(1) lookup + only affects 4 tiles maximum)
	UpdateCornerHeight(Level, CornerRow, CornerColumn, CornerHeight);
}


// void UTerrainComponent::ModifyTerrainOn(FTerrainSegmentData InTerrainData)
// {
// 	int32 NewSectionIndex = InTerrainData.SectionIndex;
// 	if(NewSectionIndex == -1)
// 	{
// 		UE_LOG(LogTemp, Error, TEXT("TheBrubs Plugin : Couldn't find Terrain Data in Memory"));
// 		return;
// 	}

// 	// All required Vertices 
// 	FVector Center = InTerrainData.PointLoc;
// 	FVector BottomLeft = GetComponentTransform().InverseTransformPosition(AdjustCornerZ(InTerrainData.CornerLocs[0]));
// 	FVector BottomRight = GetComponentTransform().InverseTransformPosition(AdjustCornerZ(InTerrainData.CornerLocs[1]));
// 	FVector TopLeft = GetComponentTransform().InverseTransformPosition(AdjustCornerZ(InTerrainData.CornerLocs[2]));
// 	FVector TopRight = GetComponentTransform().InverseTransformPosition(AdjustCornerZ(InTerrainData.CornerLocs[3]));

// 	//TerrainTile
// 	TArray<FVector> TerrainVerts;
// 	TArray<int32> TerrainTriangles;
// 	TArray<FVector> TerrainNormals;
// 	TArray<FVector2D> TerrainUVs;
// 	TArray<FColor> TerrainVertexColors;
// 	TArray<FProcMeshTangent> TerrainTangents;
	
// 	TerrainVerts.Add(TopLeft);
// 	TerrainVerts.Add(TopRight);
// 	TerrainVerts.Add(Center);
// 	TerrainVerts.Add(BottomRight);
// 	TerrainVerts.Add(BottomLeft);
	
// 	TerrainUVs.Add(FVector2D(0, 1));
// 	TerrainUVs.Add(FVector2D(1, 1));
// 	TerrainUVs.Add(FVector2D(0.5, 0.5));
// 	TerrainUVs.Add(FVector2D(1, 0));
// 	TerrainUVs.Add(FVector2D(0, 0));

// 	TerrainNormals.Add(FVector(0, 0, 1));
// 	TerrainNormals.Add(FVector(0, 0, 1));
// 	TerrainNormals.Add(FVector(0, 0, 1));
// 	TerrainNormals.Add(FVector(0, 0, 1));
// 	TerrainNormals.Add(FVector(0, 0, 1));

// 	TerrainTriangles.Add(0); // Top Left
// 	TerrainTriangles.Add(1); // Top Right
// 	TerrainTriangles.Add(2); // Center

// 	TerrainTriangles.Add(1); // Top Right
// 	TerrainTriangles.Add(3); // Bottom Right
// 	TerrainTriangles.Add(2); // Center
	
// 	TerrainTriangles.Add(3); // Bottom Right
// 	TerrainTriangles.Add(4); // Bottom Left
// 	TerrainTriangles.Add(2); // Center
	
// 	TerrainTriangles.Add(4); // Bottom Left
// 	TerrainTriangles.Add(0); // Top Left
// 	TerrainTriangles.Add(2); // Center
	
// 	UpdateMeshSection(NewSectionIndex, TerrainVerts, TerrainNormals, TerrainUVs, TerrainVertexColors, TerrainTangents);
	
// 	TerrainDataArray[InTerrainData.ArrayIndex] = InTerrainData;

// 	ReGenerateTerrainsNearOfCorner(BottomLeft);
// 	ReGenerateTerrainsNearOfCorner(BottomRight);
// 	ReGenerateTerrainsNearOfCorner(TopLeft);
// 	ReGenerateTerrainsNearOfCorner(TopRight);
// }


// FVector UTerrainComponent::AdjustCornerZ(FVector Corner)
// {

//     float GridTileSize = 100.0f;
//     float Offset = GridTileSize * 0.45f;

//     ALotManager* OurLot = Cast<ALotManager>(GetOwner());

//     FVector TileCenters[4] = {
//         FVector(Corner + FVector(-Offset, -Offset, 0.0f)),  // Bottom Left
//         FVector(Corner + FVector( Offset, -Offset, 0.0f)),  // Bottom Right
//         FVector(Corner + FVector(-Offset,  Offset, 0.0f)),  // Top Left
//         FVector(Corner + FVector( Offset,  Offset, 0.0f))   // Top Right
//     };

// 	FTerrainSegmentData* TerrainSegments[4];

//     if (!OurLot->FindTerrainWithVector(TileCenters[0], TerrainSegments[0]) ||
// 	    !OurLot->FindTerrainWithVector(TileCenters[1], TerrainSegments[1]) ||
// 		!OurLot->FindTerrainWithVector(TileCenters[2], TerrainSegments[2]) ||
// 		!OurLot->FindTerrainWithVector(TileCenters[3], TerrainSegments[3]))
//     {
//         return Corner;
//     }

// 	// Calculate the average Z value
//     float TotalZ = 0.0f;

//     for (const FTerrainSegmentData* TerrainSegment : TerrainSegments)
//     {
// 		if (TerrainSegment->bFlatten)
//    	 	{
//         	Corner.Z = TerrainSegment->PointLoc.Z;
//         	return Corner;
//    		}

//         TotalZ += TerrainSegment->PointLoc.Z;
//     }

//     Corner.Z = FMath::FloorToFloat(TotalZ / 4.0f);

//     return Corner;
// }

// ========== Grid-Based Corner Height Storage (OpenTS2-inspired) ==========

void UTerrainComponent::InitializeHeightMap(int32 Level, int32 GridWidth, int32 GridHeight, float TileSize, float BaseZ)
{
	FTerrainHeightMap& HeightMap = HeightMaps.FindOrAdd(Level);
	HeightMap.Initialize(GridWidth, GridHeight, TileSize, BaseZ);
}

FTerrainHeightMap* UTerrainComponent::GetOrCreateHeightMap(int32 Level)
{
	if (!HeightMaps.Contains(Level))
	{
		ALotManager* OurLot = Cast<ALotManager>(GetOwner());
		if (OurLot)
		{
			// VALIDATION: Terrain should ONLY exist on ground floor (Basements level)
			const int32 GroundLevel = OurLot->Basements;
			if (Level != GroundLevel)
			{
				UE_LOG(LogTemp, Error, TEXT("TerrainComponent::GetOrCreateHeightMap - REJECTED: Terrain height map can only exist for ground level (Level %d). Requested Level %d."),
					GroundLevel, Level);
				return nullptr;
			}

			// Initialize with base Z height (flat terrain at level height)
			// Height map should normally be initialized during GenerateTerrainComponents()
			// This is a fallback for safety
			InitializeHeightMap(Level, OurLot->GridSizeX, OurLot->GridSizeY, OurLot->GridTileSize, GetOwner()->GetActorLocation().Z);

			UE_LOG(LogTemp, Warning, TEXT("TerrainComponent: Height map for level %d was not pre-initialized, creating fallback"), Level);
		}
	}

	return HeightMaps.Find(Level);
}

const FTerrainHeightMap* UTerrainComponent::GetHeightMap(int32 Level) const
{
	return HeightMaps.Find(Level);
}

void UTerrainComponent::UpdateCornerHeight(int32 Level, int32 CornerRow, int32 CornerColumn, float NewHeight, bool bBypassLock)
{
	FTerrainHeightMap* HeightMap = GetOrCreateHeightMap(Level);
	if (!HeightMap)
	{
		return;
	}

	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		return;
	}

	// Lock perimeter corners at their original height (BaseZ + 0.0f)
	// This allows terrain to seamlessly connect to surrounding static/fake terrain
	const bool bIsPerimeterCorner = (CornerRow == 0 || CornerRow == OurLot->GridSizeY ||
	                                  CornerColumn == 0 || CornerColumn == OurLot->GridSizeX);
	if (bIsPerimeterCorner)
	{
		// Skip height update for perimeter corners
		// Still mark dirty so adjacent interior tiles update properly
		MarkCornerDirty(Level, CornerRow, CornerColumn);
		return;
	}

	// Calculate lot base height for this level
	const float LotBaseHeight = OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Level - OurLot->Basements));

	// ROOM-BASED CLAMPING: Prevent manual terrain editing through rooms with floors
	// Check all tiles that share this corner (up to 4) and apply floor-based constraints
	float RoomMinHeight = LotBaseHeight + MinTerrainHeight;  // Start with configured min
	float RoomMaxHeight = LotBaseHeight + MaxTerrainHeight;  // Start with configured max

	// Terrain only exists on ground floor (Level == Basements)
	// The ground floor terrain serves dual purpose:
	// - Ground surface under ground floor rooms
	// - Ceiling above basement rooms
	if (OurLot->FloorComponent && Level == OurLot->Basements)
	{
		// Get the 4 tiles that potentially share this corner
		const TArray<FIntVector2> TileOffsets = {
			FIntVector2(0, 0),      // Tile with corner as BottomLeft
			FIntVector2(0, -1),     // Tile with corner as BottomRight
			FIntVector2(-1, 0),     // Tile with corner as TopLeft
			FIntVector2(-1, -1)     // Tile with corner as TopRight
		};

		// Check if this corner should be locked from editing
		// Corners are FULLY LOCKED if:
		// 1. Flattened by a ground floor room
		// 2. Part of a wall node
		bool bCornerIsLocked = false;

		for (const FIntVector2& Offset : TileOffsets)
		{
			const int32 TileRow = CornerRow + Offset.X;
			const int32 TileColumn = CornerColumn + Offset.Y;

			// Check if there's a ground floor at this position
			FFloorTileData* GroundFloor = OurLot->FloorComponent->FindFloorTile(Level, TileRow, TileColumn);
			if (GroundFloor && GroundFloor->bCommitted)
			{
				// Ground floor exists - this corner has been flattened and is LOCKED
				bCornerIsLocked = true;
				break;
			}
		}

		// Also check if this corner is part of a wall (walls flatten terrain along their path)
		if (!bCornerIsLocked && OurLot->WallGraph)
		{
			// Check if any wall nodes exist at this corner position
			// Nodes is a TMap<int32, FWallNode>, so iterate over key-value pairs
			for (const auto& NodePair : OurLot->WallGraph->Nodes)
			{
				const FWallNode& Node = NodePair.Value;
				if (Node.Level == Level && Node.Row == CornerRow && Node.Column == CornerColumn)
				{
					// This corner is part of a wall - LOCKED
					bCornerIsLocked = true;
					break;
				}
			}
		}

		// If corner is locked, reject manual terrain editing entirely (unless bypassing lock)
		if (bCornerIsLocked && !bBypassLock)
		{
			// Corner has been flattened by ground floor or wall - do NOT allow manual editing
			UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent: Rejecting manual edit at locked corner (%d,%d) - flattened by floor/wall"),
				CornerRow, CornerColumn);
			return;
		}

		// BASEMENT CEILING PROTECTION: Apply minimum clamp (but don't lock)
		// Basement ceilings can be raised but not lowered below ceiling height
		for (const FIntVector2& Offset : TileOffsets)
		{
			const int32 TileRow = CornerRow + Offset.X;
			const int32 TileColumn = CornerColumn + Offset.Y;

			// Check if there's a basement floor BELOW this ground floor position
			// Ground floor terrain IS the basement ceiling when basement rooms exist below
			if (OurLot->Basements > 0)
			{
				const int32 BasementLevel = Level - 1; // One level below ground floor
				FFloorTileData* BasementFloor = OurLot->FloorComponent->FindFloorTile(BasementLevel, TileRow, TileColumn);

				if (BasementFloor && BasementFloor->bCommitted)
				{
					// Basement floor exists below - this terrain IS the basement ceiling
					// Apply MIN clamp to prevent lowering below ceiling level (but allow raising)
					// Basement ceiling is at ground floor base level (LotBaseHeight)
					const float BasementCeilingZ = LotBaseHeight;
					RoomMinHeight = FMath::Max(RoomMinHeight, BasementCeilingZ);
				}
			}
		}
	}

	// Apply floor-based clamping first, then global min/max constraints
	const float ClampedHeight = FMath::Clamp(NewHeight, RoomMinHeight, RoomMaxHeight);

	// Update the height in the shared grid
	HeightMap->SetCornerHeight(CornerRow, CornerColumn, ClampedHeight);

	// MERGED MESH SYSTEM: Use batch operation to regenerate all affected tiles at once
	// This prevents multiple rebuilds of the same level mesh
	bool bWasInBatch = bInBatchOperation;
	if (!bWasInBatch)
	{
		BeginBatchOperation();
	}

	// Get all terrain tiles affected by this corner and add them back to trigger rebuild
	TArray<FTerrainSegmentData*> AffectedTiles = GetTilesAffectedByCorner(Level, CornerRow, CornerColumn);

	// Calculate BaseZ for this level (matches RebuildLevel calculation)
	const float BaseZ = OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Level - OurLot->Basements));

	for (FTerrainSegmentData* TileData : AffectedTiles)
	{
		if (TileData)
		{
			// Update the tile's corner locations FROM HEIGHT MAP (not from LocationToAllTileCorners which returns flat terrain)
			// Calculate corner positions based on grid coordinates and height map data
			const int32 TileRow = TileData->Row;
			const int32 TileCol = TileData->Column;
			const float BaseX = OurLot->GetActorLocation().X;
			const float BaseY = OurLot->GetActorLocation().Y;
			const float TileSize = OurLot->GridTileSize;

			// Read corner heights from height map
			const float BLHeight = HeightMap->GetCornerHeight(TileRow, TileCol);
			const float BRHeight = HeightMap->GetCornerHeight(TileRow, TileCol + 1);
			const float TLHeight = HeightMap->GetCornerHeight(TileRow + 1, TileCol);
			const float TRHeight = HeightMap->GetCornerHeight(TileRow + 1, TileCol + 1);

			// Calculate corner positions (in world space)
			// CornerLocs order: [0]=BottomLeft, [1]=BottomRight, [2]=TopLeft, [3]=TopRight
			TileData->CornerLocs[0] = FVector(BaseX + TileCol * TileSize, BaseY + TileRow * TileSize, BaseZ + BLHeight);
			TileData->CornerLocs[1] = FVector(BaseX + (TileCol + 1) * TileSize, BaseY + TileRow * TileSize, BaseZ + BRHeight);
			TileData->CornerLocs[2] = FVector(BaseX + TileCol * TileSize, BaseY + (TileRow + 1) * TileSize, BaseZ + TLHeight);
			TileData->CornerLocs[3] = FVector(BaseX + (TileCol + 1) * TileSize, BaseY + (TileRow + 1) * TileSize, BaseZ + TRHeight);

			}
	}

	// Mark corner as dirty for incremental update
	MarkCornerDirty(Level, CornerRow, CornerColumn);

	// If not in batch operation, update dirty vertices immediately
	if (!bWasInBatch)
	{
		UpdateDirtyVertices();
		EndBatchOperation();
	}
}

TArray<FTerrainSegmentData*> UTerrainComponent::GetTilesAffectedByCorner(int32 Level, int32 CornerRow, int32 CornerColumn)
{
	TArray<FTerrainSegmentData*> AffectedTiles;

	// A corner at (CornerRow, CornerColumn) affects up to 4 tiles:
	// Tile (CornerRow, CornerColumn)     - corner is BottomLeft
	// Tile (CornerRow, CornerColumn-1)   - corner is BottomRight
	// Tile (CornerRow-1, CornerColumn)   - corner is TopLeft
	// Tile (CornerRow-1, CornerColumn-1) - corner is TopRight

	// OPTIMIZED: Use spatial hash map for O(1) lookups instead of O(N) iteration
	const TArray<FIntVector2> TileOffsets = {
		FIntVector2(0, 0),      // Tile with corner as BottomLeft
		FIntVector2(0, -1),     // Tile with corner as BottomRight
		FIntVector2(-1, 0),     // Tile with corner as TopLeft
		FIntVector2(-1, -1)     // Tile with corner as TopRight
	};

	for (const FIntVector2& Offset : TileOffsets)
	{
		const int32 TileRow = CornerRow + Offset.X;
		const int32 TileColumn = CornerColumn + Offset.Y;

		// Use spatial map for O(1) lookup
		FTerrainSegmentData* TileData = FindTerrainTile(Level, TileRow, TileColumn);
		if (TileData && TileData->bCommitted)
		{
			AffectedTiles.Add(TileData);
		}
	}

	return AffectedTiles;
}

uint8 UTerrainComponent::CalculateFilledMask(int32 Row, int32 Column, int32 Level) const
{
	uint8 Mask = 0;

	// Check each of the 4 neighbors and set corresponding bit if neighbor exists
	// Bit 0: Top neighbor (Row+1, Column)
	// Bit 1: Right neighbor (Row, Column+1)
	// Bit 2: Bottom neighbor (Row-1, Column)
	// Bit 3: Left neighbor (Row, Column-1)

	// OPTIMIZED: Use spatial hash map for O(1) lookups instead of O(N) iteration
	const TArray<FIntVector2> NeighborOffsets = {
		FIntVector2(1, 0),   // Top neighbor (Bit 0)
		FIntVector2(0, 1),   // Right neighbor (Bit 1)
		FIntVector2(-1, 0),  // Bottom neighbor (Bit 2)
		FIntVector2(0, -1)   // Left neighbor (Bit 3)
	};

	for (int32 i = 0; i < NeighborOffsets.Num(); i++)
	{
		const int32 NeighborRow = Row + NeighborOffsets[i].X;
		const int32 NeighborColumn = Column + NeighborOffsets[i].Y;

		// Use spatial map for O(1) lookup
		int32 GridKey = MakeGridKey(Level, NeighborRow, NeighborColumn);
		if (const int32* IndexPtr = TerrainSpatialMap.Find(GridKey))
		{
			if (TerrainDataArray.IsValidIndex(*IndexPtr) && TerrainDataArray[*IndexPtr].bCommitted)
			{
				Mask |= (1 << i);
			}
		}
	}

	return Mask;
}

void UTerrainComponent::DestroyAllTerrain()
{
	// Clear all mesh sections
	for (int32 i = 0; i < GetNumSections(); i++)
	{
		ClearMeshSection(i);
	}

	// Clear terrain data array
	TerrainDataArray.Empty();

	// Clear height maps
	HeightMaps.Empty();

	// Clear merged mesh system data structures
	TerrainSpatialMap.Empty();
	DirtyLevels.Empty();
	LevelMaterials.Empty();
	bInBatchOperation = false;

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent: Destroyed all terrain meshes and data (merged mesh system cleared)"));
}

int32 UTerrainComponent::RemoveInvalidTerrainTiles()
{
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::RemoveInvalidTerrainTiles - No LotManager found, cannot determine ground level"));
		return 0;
	}

	const int32 GroundLevel = OurLot->Basements;
	int32 RemovedCount = 0;

	UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::RemoveInvalidTerrainTiles - Scanning %d terrain tiles (Ground level: %d)"),
		TerrainDataArray.Num(), GroundLevel);
	UE_LOG(LogTemp, Warning, TEXT("  Current mesh sections: %d"), GetNumSections());

	// Collect tiles to remove (can't modify array while iterating)
	TArray<FIntVector> TilesToRemove; // Store as (Level, Row, Column)
	TSet<int32> InvalidLevels; // Track unique invalid levels

	for (const FTerrainSegmentData& Tile : TerrainDataArray)
	{
		if (Tile.Level != GroundLevel)
		{
			TilesToRemove.Add(FIntVector(Tile.Level, Tile.Row, Tile.Column));
			InvalidLevels.Add(Tile.Level);
			UE_LOG(LogTemp, Warning, TEXT("  - Found invalid terrain tile at Level %d, Row %d, Column %d (should be Level %d)"),
				Tile.Level, Tile.Row, Tile.Column, GroundLevel);
		}
	}

	// Remove invalid tiles
	for (const FIntVector& TileCoords : TilesToRemove)
	{
		RemoveTerrainTile(TileCoords.X, TileCoords.Y, TileCoords.Z);
		RemovedCount++;
	}

	// Clean up ALL traces of invalid levels from data structures
	for (int32 InvalidLevel : InvalidLevels)
	{
		// Remove heightmaps for invalid levels
		if (HeightMaps.Remove(InvalidLevel) > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  - Removed HeightMap for invalid level %d"), InvalidLevel);
		}

		// Remove level materials for invalid levels
		if (LevelMaterials.Remove(InvalidLevel) > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  - Removed LevelMaterial for invalid level %d"), InvalidLevel);
		}

		// Remove from dirty levels set
		if (DirtyLevels.Remove(InvalidLevel) > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  - Removed level %d from DirtyLevels"), InvalidLevel);
		}

		// Clear mesh sections for invalid level (2 sections per level: top + sides)
		const int32 TopSection = GetTopSectionIndex(InvalidLevel);
		const int32 SidesSection = GetSidesAndBottomSectionIndex(InvalidLevel);

		ClearMeshSection(TopSection);
		ClearMeshSection(SidesSection);
		UE_LOG(LogTemp, Warning, TEXT("  - Cleared mesh sections %d and %d for invalid level %d"),
			TopSection, SidesSection, InvalidLevel);
	}

	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::RemoveInvalidTerrainTiles - Removed %d invalid terrain tiles from %d levels"),
			RemovedCount, InvalidLevels.Num());
		UE_LOG(LogTemp, Warning, TEXT("  Remaining tiles: %d"), TerrainDataArray.Num());
		UE_LOG(LogTemp, Warning, TEXT("  Mesh sections after cleanup: %d (should be 2)"), GetNumSections());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RemoveInvalidTerrainTiles - No invalid terrain tiles found"));
	}

	return RemovedCount;
}

void UTerrainComponent::ApplySharedTerrainMaterial(UMaterialInstance* Material)
{
	if (!Material)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent: Cannot apply null material"));
		return;
	}

	// Store for future reference
	TerrainMaterial = Material;

	// Apply material to all existing levels in the merged mesh system
	for (auto& LevelMaterialPair : LevelMaterials)
	{
		LevelMaterials[LevelMaterialPair.Key] = Material;
		SetMaterial(LevelMaterialPair.Key, Material);
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent: Applied shared terrain material to %d levels"), LevelMaterials.Num());
}

// ========== Merged Mesh System Implementation ==========

FTerrainSegmentData* UTerrainComponent::FindTerrainTile(int32 Level, int32 Row, int32 Column)
{
	int32 GridKey = MakeGridKey(Level, Row, Column);

	if (int32* IndexPtr = TerrainSpatialMap.Find(GridKey))
	{
		if (TerrainDataArray.IsValidIndex(*IndexPtr))
		{
			return &TerrainDataArray[*IndexPtr];
		}
		else
		{
			// Invalid index - clean up spatial map
			UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::FindTerrainTile - Invalid index %d in spatial map"), *IndexPtr);
			TerrainSpatialMap.Remove(GridKey);
		}
	}

	return nullptr;
}

void UTerrainComponent::AddTerrainTile(const FTerrainSegmentData& TileData, UMaterialInstance* Material)
{
	// Validate coordinates
	if (TileData.Row == -1 || TileData.Column == -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::AddTerrainTile - Invalid grid coordinates (Row: %d, Column: %d)"),
			TileData.Row, TileData.Column);
		return;
	}

	// VALIDATION: Terrain should ONLY exist on ground floor (Basements level)
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (OurLot)
	{
		const int32 GroundLevel = OurLot->Basements;
		if (TileData.Level != GroundLevel)
		{
			UE_LOG(LogTemp, Error, TEXT("TerrainComponent::AddTerrainTile - REJECTED: Terrain can only exist on ground level (Level %d). Attempted to add at Level %d, Row %d, Column %d"),
				GroundLevel, TileData.Level, TileData.Row, TileData.Column);
			return;
		}
	}

	int32 GridKey = MakeGridKey(TileData.Level, TileData.Row, TileData.Column);

	// Check if tile already exists
	if (int32* ExistingIndexPtr = TerrainSpatialMap.Find(GridKey))
	{
		// Update existing tile
		if (TerrainDataArray.IsValidIndex(*ExistingIndexPtr))
		{
			TerrainDataArray[*ExistingIndexPtr] = TileData;
			TerrainDataArray[*ExistingIndexPtr].ArrayIndex = *ExistingIndexPtr;
			UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent::AddTerrainTile - Updated existing tile at Level %d, Row %d, Column %d"),
				TileData.Level, TileData.Row, TileData.Column);
		}
	}
	else
	{
		// Add new tile
		int32 NewIndex = TerrainDataArray.Add(TileData);
		TerrainDataArray[NewIndex].ArrayIndex = NewIndex;
		TerrainSpatialMap.Add(GridKey, NewIndex);

		UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent::AddTerrainTile - Added new tile at Level %d, Row %d, Column %d (Index: %d)"),
			TileData.Level, TileData.Row, TileData.Column, NewIndex);
	}

	// Set material for this level if provided
	if (Material)
	{
		UMaterialInstance** ExistingMaterial = LevelMaterials.Find(TileData.Level);
		if (!ExistingMaterial)
		{
			LevelMaterials.Add(TileData.Level, Material);
			UE_LOG(LogTemp, Log, TEXT("TerrainComponent::AddTerrainTile - Set material for Level %d"), TileData.Level);
		}
		else if (*ExistingMaterial != Material)
		{
			// Material changed - update it and log warning
			LevelMaterials[TileData.Level] = Material;
			UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::AddTerrainTile - Material changed for Level %d (merged mesh uses single material per level)"), TileData.Level);
		}
	}

	// Mark level dirty for rebuild
	MarkLevelDirty(TileData.Level);
}

void UTerrainComponent::RemoveTerrainTile(int32 Level, int32 Row, int32 Column)
{
	// Validate coordinates
	if (Row == -1 || Column == -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::RemoveTerrainTile - Invalid grid coordinates (Row: %d, Column: %d)"),
			Row, Column);
		return;
	}

	int32 GridKey = MakeGridKey(Level, Row, Column);

	if (int32* IndexPtr = TerrainSpatialMap.Find(GridKey))
	{
		if (TerrainDataArray.IsValidIndex(*IndexPtr))
		{
			// Mark as not committed (lazy deletion)
			TerrainDataArray[*IndexPtr].bCommitted = false;

			// Remove from spatial map
			TerrainSpatialMap.Remove(GridKey);

			// Reset height map corners to base level (0.0f) for this tile
			FTerrainHeightMap* HeightMap = GetOrCreateHeightMap(Level);
			if (HeightMap)
			{
				// Reset all 4 corners of this tile to base height
				HeightMap->SetCornerHeight(Row, Column, 0.0f);           // BottomLeft
				HeightMap->SetCornerHeight(Row, Column + 1, 0.0f);       // BottomRight
				HeightMap->SetCornerHeight(Row + 1, Column, 0.0f);       // TopLeft
				HeightMap->SetCornerHeight(Row + 1, Column + 1, 0.0f);   // TopRight
			}

			UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent::RemoveTerrainTile - Removed tile at Level %d, Row %d, Column %d"),
				Level, Row, Column);

			// Mark level dirty for rebuild
			MarkLevelDirty(Level);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::RemoveTerrainTile - Invalid index %d in spatial map"), *IndexPtr);
			TerrainSpatialMap.Remove(GridKey);
		}
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent::RemoveTerrainTile - No tile found at Level %d, Row %d, Column %d"),
			Level, Row, Column);
	}
}

void UTerrainComponent::RemoveTerrainRegion(int32 Level, int32 FromRow, int32 FromCol, int32 ToRow, int32 ToCol)
{
	// Validate coordinates
	if (FromRow > ToRow || FromCol > ToCol)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::RemoveTerrainRegion - Invalid range (FromRow: %d, FromCol: %d, ToRow: %d, ToCol: %d)"),
			FromRow, FromCol, ToRow, ToCol);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RemoveTerrainRegion - Removing tiles from (%d,%d) to (%d,%d) at Level %d"),
		FromRow, FromCol, ToRow, ToCol, Level);

	// Use batch operation to suppress intermediate rebuilds
	bool bWasInBatch = bInBatchOperation;
	if (!bWasInBatch)
	{
		BeginBatchOperation();
	}

	int32 RemovedCount = 0;

	// Remove all tiles in the rectangular region
	for (int32 Row = FromRow; Row <= ToRow; ++Row)
	{
		for (int32 Col = FromCol; Col <= ToCol; ++Col)
		{
			int32 GridKey = MakeGridKey(Level, Row, Col);

			if (int32* IndexPtr = TerrainSpatialMap.Find(GridKey))
			{
				if (TerrainDataArray.IsValidIndex(*IndexPtr))
				{
					// Mark as not committed (lazy deletion)
					TerrainDataArray[*IndexPtr].bCommitted = false;

					// Remove from spatial map
					TerrainSpatialMap.Remove(GridKey);

					RemovedCount++;
				}
			}
		}
	}

	// Reset height map corners for the removed region
	if (RemovedCount > 0)
	{
		FTerrainHeightMap* HeightMap = GetOrCreateHeightMap(Level);
		if (HeightMap)
		{
			// Reset all corners in the region to base height (0.0f)
			// Corners extend from FromRow to ToRow+1 and FromCol to ToCol+1
			for (int32 CornerRow = FromRow; CornerRow <= ToRow + 1; ++CornerRow)
			{
				for (int32 CornerCol = FromCol; CornerCol <= ToCol + 1; ++CornerCol)
				{
					HeightMap->SetCornerHeight(CornerRow, CornerCol, 0.0f);
				}
			}
		}

		// Mark level dirty for rebuild (only once for entire region)
		MarkLevelDirty(Level);
	}

	// End batch operation if we started it (triggers single rebuild)
	if (!bWasInBatch)
	{
		EndBatchOperation();
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RemoveTerrainRegion - Removed %d tiles from Level %d"), RemovedCount, Level);
}

void UTerrainComponent::MarkLevelDirty(int32 Level)
{
	DirtyLevels.Add(Level);

	// If not in batch operation, rebuild immediately
	if (!bInBatchOperation)
	{
		RebuildLevel(Level);
		// Clear from dirty set after immediate rebuild to prevent duplicate rebuilds
		DirtyLevels.Remove(Level);
	}
}

void UTerrainComponent::BeginBatchOperation()
{
	bInBatchOperation = true;
}

void UTerrainComponent::EndBatchOperation()
{
	bInBatchOperation = false;

	// Use incremental updates if we have dirty corners, otherwise fall back to full rebuild
	if (DirtyCorners.Num() > 0)
	{
		UpdateDirtyVertices();
	}
	else if (DirtyLevels.Num() > 0)
	{
		// Copy dirty levels to avoid iteration issues
		TSet<int32> LevelsToRebuild = DirtyLevels;
		DirtyLevels.Empty();

		// Rebuild all dirty levels
		for (int32 Level : LevelsToRebuild)
		{
			RebuildLevel(Level);
		}
	}
}

void UTerrainComponent::MigrateToMergedMesh()
{
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::MigrateToMergedMesh - Starting migration"));
	UE_LOG(LogTemp, Warning, TEXT("  Existing terrain tiles: %d"), TerrainDataArray.Num());
	UE_LOG(LogTemp, Warning, TEXT("  Existing mesh sections: %d"), GetNumSections());
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	if (TerrainDataArray.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::MigrateToMergedMesh - No terrain data to migrate"));
		return;
	}

	// Step 1: Clear ALL existing mesh sections (old per-tile sections)
	const int32 OldSectionCount = GetNumSections();
	UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::MigrateToMergedMesh - Clearing %d old mesh sections..."), OldSectionCount);

	// Clear all sections by resetting the entire mesh
	ClearAllMeshSections();

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::MigrateToMergedMesh - Cleared all sections, now have %d sections"), GetNumSections());

	// Step 2: Clear and rebuild spatial map from existing TerrainDataArray
	TerrainSpatialMap.Empty();
	TSet<int32> UniqueLevels;

	for (int32 i = 0; i < TerrainDataArray.Num(); i++)
	{
		FTerrainSegmentData& Tile = TerrainDataArray[i];

		// Skip uncommitted tiles
		if (!Tile.bCommitted)
		{
			continue;
		}

		// Update array index
		Tile.ArrayIndex = i;

		// Clear old section index (no longer used in merged mesh system)
		Tile.SectionIndex = -1;

		// Add to spatial map
		int32 GridKey = MakeGridKey(Tile.Level, Tile.Row, Tile.Column);
		TerrainSpatialMap.Add(GridKey, i);

		// Track unique levels
		UniqueLevels.Add(Tile.Level);

		// Set level material if available
		if (Tile.Material && !LevelMaterials.Contains(Tile.Level))
		{
			LevelMaterials.Add(Tile.Level, Tile.Material);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::MigrateToMergedMesh - Rebuilt spatial map with %d tiles"), TerrainSpatialMap.Num());
	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::MigrateToMergedMesh - Found %d unique levels"), UniqueLevels.Num());

	// Step 3: Initialize height maps and populate with existing corner heights
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (OurLot)
	{
		for (int32 Level : UniqueLevels)
		{
			// Initialize height map for this level
			InitializeHeightMap(Level, OurLot->GridSizeX, OurLot->GridSizeY, OurLot->GridTileSize,
				OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Level - OurLot->Basements)));

			UE_LOG(LogTemp, Log, TEXT("TerrainComponent::MigrateToMergedMesh - Initialized height map for level %d"), Level);
		}

		// Populate height maps from existing CornerLocs
		for (int32 i = 0; i < TerrainDataArray.Num(); i++)
		{
			FTerrainSegmentData& Tile = TerrainDataArray[i];
			if (!Tile.bCommitted)
			{
				continue;
			}

			FTerrainHeightMap* HeightMap = GetOrCreateHeightMap(Tile.Level);
			if (HeightMap)
			{
				// Extract heights from CornerLocs (world space) and store in height map
				// CornerLocs order: [0]=BottomLeft, [1]=BottomRight, [2]=TopLeft, [3]=TopRight
				const float BaseZ = OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Tile.Level - OurLot->Basements));

				// Set corner heights (relative to BaseZ)
				HeightMap->SetCornerHeight(Tile.Row, Tile.Column, Tile.CornerLocs[0].Z - BaseZ);               // BottomLeft
				HeightMap->SetCornerHeight(Tile.Row, Tile.Column + 1, Tile.CornerLocs[1].Z - BaseZ);           // BottomRight
				HeightMap->SetCornerHeight(Tile.Row + 1, Tile.Column, Tile.CornerLocs[2].Z - BaseZ);           // TopLeft
				HeightMap->SetCornerHeight(Tile.Row + 1, Tile.Column + 1, Tile.CornerLocs[3].Z - BaseZ);       // TopRight
			}
		}

		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::MigrateToMergedMesh - Populated height maps from %d tiles"), TerrainDataArray.Num());
	}

	// Step 4: Rebuild merged mesh for each unique level
	BeginBatchOperation();
	for (int32 Level : UniqueLevels)
	{
		MarkLevelDirty(Level);
	}
	EndBatchOperation();

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::MigrateToMergedMesh - Migration COMPLETE"));
	UE_LOG(LogTemp, Warning, TEXT("  New mesh sections: %d (one per level)"), GetNumSections());
	UE_LOG(LogTemp, Warning, TEXT("  Spatial map entries: %d"), TerrainSpatialMap.Num());
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void UTerrainComponent::RebuildLevel(int32 Level)
{
	UE_LOG(LogTemp, Log, TEXT("========================================"));
	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - Level %d"), Level);
	UE_LOG(LogTemp, Log, TEXT("  Rendering Method: MERGED MESH (all tiles combined)"));
	UE_LOG(LogTemp, Log, TEXT("  TerrainDataArray size: %d"), TerrainDataArray.Num());
	UE_LOG(LogTemp, Log, TEXT("========================================"));

	// Clear existing mesh sections for this level (2 sections: top, sides+bottom)
	ClearMeshSection(GetTopSectionIndex(Level));
	ClearMeshSection(GetSidesAndBottomSectionIndex(Level));

	// Separate arrays for 2 mesh sections
	// TOP SURFACE: Per-tile with quadrant support and cross-tile normal smoothing
	TArray<FVector> TopVertices;
	TArray<int32> TopTriangles;
	TArray<FVector> TopNormals;
	TArray<FVector2D> TopUVs;
	TArray<FColor> TopVertexColors;
	TArray<FProcMeshTangent> TopTangents;

	// SIDES + BOTTOM: Static geometry (side faces + bottom slab combined)
	TArray<FVector> SideVertices;
	TArray<int32> SideTriangles;
	TArray<FVector> SideNormals;
	TArray<FVector2D> SideUVs;
	TArray<FColor> SideVertexColors;
	TArray<FProcMeshTangent> SideTangents;

	// Get LotManager for corner calculations (optional - NeighbourhoodManager can also own this)
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - No LotManager found, using direct tile positions (NeighbourhoodManager mode)"));
	}

	// Safety check for empty array
	if (TerrainDataArray.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - No tiles to rebuild for level %d"), Level);
		return;
	}

	// Count committed tiles at this level
	int32 EstimatedTileCount = 0;
	for (int32 i = 0; i < TerrainDataArray.Num(); i++)
	{
		const FTerrainSegmentData& Tile = TerrainDataArray[i];
		if (Tile.bCommitted && Tile.Level == Level)
		{
			EstimatedTileCount++;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - Found %d committed tiles at level %d"), EstimatedTileCount, Level);

	if (EstimatedTileCount == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - No committed tiles at level %d, exiting"), Level);
		return;
	}

	// Reserve approximate space for efficiency
	// TOP: 9 vertices per tile (4 corners + 4 edge midpoints + 1 center)
	// TOP: 8 triangles per tile (but conditional on quadrant visibility)
	TopVertices.Reserve(EstimatedTileCount * 9);
	TopTriangles.Reserve(EstimatedTileCount * 24); // 8 triangles × 3 indices (upper bound)
	TopNormals.Reserve(EstimatedTileCount * 9);
	TopUVs.Reserve(EstimatedTileCount * 9);
	TopVertexColors.Reserve(EstimatedTileCount * 9);

	// SIDE: ~4 vertices per edge, estimate 2 exterior edges per tile on average
	SideVertices.Reserve(EstimatedTileCount * 8);
	SideTriangles.Reserve(EstimatedTileCount * 12); // 2 edges × 2 triangles × 3 indices
	SideNormals.Reserve(EstimatedTileCount * 8);
	SideUVs.Reserve(EstimatedTileCount * 8 + 4);  // +4 for bottom slab
	SideVertexColors.Reserve(EstimatedTileCount * 8 + 4);

	// Get height map for this level
	const FTerrainHeightMap* HeightMap = GetHeightMap(Level);

	// ==== PASS 1: Accumulate normals at shared corners for cross-tile smoothing ====
	// Map from corner grid coordinates to accumulated normals from all touching triangles
	TMap<FIntVector, TArray<FVector>> CornerNormalAccumulator;

	// First pass: Calculate normals for each tile and accumulate at corners
	for (int32 i = 0; i < TerrainDataArray.Num(); i++)
	{
		const FTerrainSegmentData& Tile = TerrainDataArray[i];
		if (!Tile.bCommitted || Tile.Level != Level)
		{
			continue;
		}

		// Get corner heights and positions
		FVector BottomLeft, BottomRight, TopLeft, TopRight;

		if (HeightMap && OurLot)
		{
			// LotManager mode: Use height map for variable terrain heights
			const float BaseZ = OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Level - OurLot->Basements));
			const float BLHeight = HeightMap->GetCornerHeight(Tile.Row, Tile.Column);
			const float BRHeight = HeightMap->GetCornerHeight(Tile.Row, Tile.Column + 1);
			const float TLHeight = HeightMap->GetCornerHeight(Tile.Row + 1, Tile.Column);
			const float TRHeight = HeightMap->GetCornerHeight(Tile.Row + 1, Tile.Column + 1);

			const float TileSize = OurLot->GridTileSize;
			const float BaseX = OurLot->GetActorLocation().X;
			const float BaseY = OurLot->GetActorLocation().Y;

			BottomLeft = FVector(BaseX + Tile.Column * TileSize, BaseY + Tile.Row * TileSize, BaseZ + BLHeight);
			BottomRight = FVector(BaseX + (Tile.Column + 1) * TileSize, BaseY + Tile.Row * TileSize, BaseZ + BRHeight);
			TopLeft = FVector(BaseX + Tile.Column * TileSize, BaseY + (Tile.Row + 1) * TileSize, BaseZ + TLHeight);
			TopRight = FVector(BaseX + (Tile.Column + 1) * TileSize, BaseY + (Tile.Row + 1) * TileSize, BaseZ + TRHeight);
		}
		else if (Tile.CornerLocs.Num() >= 4)
		{
			// Use precomputed corner locations
			BottomLeft = Tile.CornerLocs[0];
			BottomRight = Tile.CornerLocs[1];
			TopLeft = Tile.CornerLocs[2];
			TopRight = Tile.CornerLocs[3];
		}
		else
		{
			// NeighbourhoodManager mode: Calculate flat corners from center point and width
			// For a flat terrain cell centered at PointLoc with size Width×Width
			const float HalfWidth = Tile.Width / 2.0f;
			const FVector Center = Tile.PointLoc;

			BottomLeft = Center + FVector(-HalfWidth, -HalfWidth, 0.0f);
			BottomRight = Center + FVector(HalfWidth, -HalfWidth, 0.0f);
			TopLeft = Center + FVector(-HalfWidth, HalfWidth, 0.0f);
			TopRight = Center + FVector(HalfWidth, HalfWidth, 0.0f);
		}

		// Transform to local space
		BottomLeft = GetComponentTransform().InverseTransformPosition(BottomLeft);
		BottomRight = GetComponentTransform().InverseTransformPosition(BottomRight);
		TopLeft = GetComponentTransform().InverseTransformPosition(TopLeft);
		TopRight = GetComponentTransform().InverseTransformPosition(TopRight);

		// Calculate edge midpoints (OpenTS2 smoothing - average of 2 corners)
		FVector MidTop = (TopLeft + TopRight) / 2.0f;
		FVector MidRight = (TopRight + BottomRight) / 2.0f;
		FVector MidBottom = (BottomRight + BottomLeft) / 2.0f;
		FVector MidLeft = (BottomLeft + TopLeft) / 2.0f;

		// Calculate center by averaging all 4 corners (OpenTS2 approach)
		const float AverageZ = (BottomLeft.Z + BottomRight.Z + TopLeft.Z + TopRight.Z) / 4.0f;
		FVector Center = GetComponentTransform().InverseTransformPosition(Tile.PointLoc);
		Center.Z = AverageZ;

		// Calculate normals for all 8 triangles and accumulate at corners
		// Triangle 1: TopLeft → MidTop → Center
		{
			FVector Normal = FVector::CrossProduct(Center - TopLeft, MidTop - TopLeft).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row + 1, Tile.Column, Level)).Add(Normal);
		}

		// Triangle 2: MidTop → TopRight → Center
		{
			FVector Normal = FVector::CrossProduct(Center - MidTop, TopRight - MidTop).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row + 1, Tile.Column + 1, Level)).Add(Normal);
		}

		// Triangle 3: TopRight → MidRight → Center
		{
			FVector Normal = FVector::CrossProduct(Center - TopRight, MidRight - TopRight).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row + 1, Tile.Column + 1, Level)).Add(Normal);
		}

		// Triangle 4: MidRight → BottomRight → Center
		{
			FVector Normal = FVector::CrossProduct(Center - MidRight, BottomRight - MidRight).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row, Tile.Column + 1, Level)).Add(Normal);
		}

		// Triangle 5: BottomRight → MidBottom → Center
		{
			FVector Normal = FVector::CrossProduct(Center - BottomRight, MidBottom - BottomRight).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row, Tile.Column + 1, Level)).Add(Normal);
		}

		// Triangle 6: MidBottom → BottomLeft → Center
		{
			FVector Normal = FVector::CrossProduct(Center - MidBottom, BottomLeft - MidBottom).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row, Tile.Column, Level)).Add(Normal);
		}

		// Triangle 7: BottomLeft → MidLeft → Center
		{
			FVector Normal = FVector::CrossProduct(Center - BottomLeft, MidLeft - BottomLeft).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row, Tile.Column, Level)).Add(Normal);
		}

		// Triangle 8: MidLeft → TopLeft → Center
		{
			FVector Normal = FVector::CrossProduct(Center - MidLeft, TopLeft - MidLeft).GetSafeNormal();
			CornerNormalAccumulator.FindOrAdd(FIntVector(Tile.Row + 1, Tile.Column, Level)).Add(Normal);
		}
	}

	// ==== PASS 2: Generate mesh with averaged normals ====
	for (int32 i = 0; i < TerrainDataArray.Num(); i++)
	{
		FTerrainSegmentData& Tile = TerrainDataArray[i];
		if (!Tile.bCommitted || Tile.Level != Level)
		{
			continue;
		}

		// Recalculate corner positions (same as pass 1)
		FVector BottomLeft, BottomRight, TopLeft, TopRight;

		if (HeightMap && OurLot)
		{
			// LotManager mode: Use height map for variable terrain heights
			const float BaseZ = OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Level - OurLot->Basements));
			const float BLHeight = HeightMap->GetCornerHeight(Tile.Row, Tile.Column);
			const float BRHeight = HeightMap->GetCornerHeight(Tile.Row, Tile.Column + 1);
			const float TLHeight = HeightMap->GetCornerHeight(Tile.Row + 1, Tile.Column);
			const float TRHeight = HeightMap->GetCornerHeight(Tile.Row + 1, Tile.Column + 1);

			const float TileSize = OurLot->GridTileSize;
			const float BaseX = OurLot->GetActorLocation().X;
			const float BaseY = OurLot->GetActorLocation().Y;

			BottomLeft = FVector(BaseX + Tile.Column * TileSize, BaseY + Tile.Row * TileSize, BaseZ + BLHeight);
			BottomRight = FVector(BaseX + (Tile.Column + 1) * TileSize, BaseY + Tile.Row * TileSize, BaseZ + BRHeight);
			TopLeft = FVector(BaseX + Tile.Column * TileSize, BaseY + (Tile.Row + 1) * TileSize, BaseZ + TLHeight);
			TopRight = FVector(BaseX + (Tile.Column + 1) * TileSize, BaseY + (Tile.Row + 1) * TileSize, BaseZ + TRHeight);
		}
		else if (Tile.CornerLocs.Num() >= 4)
		{
			// Use precomputed corner locations
			BottomLeft = Tile.CornerLocs[0];
			BottomRight = Tile.CornerLocs[1];
			TopLeft = Tile.CornerLocs[2];
			TopRight = Tile.CornerLocs[3];
		}
		else
		{
			// NeighbourhoodManager mode: Calculate flat corners from center point and width
			const float HalfWidth = Tile.Width / 2.0f;
			const FVector Center = Tile.PointLoc;

			BottomLeft = Center + FVector(-HalfWidth, -HalfWidth, 0.0f);
			BottomRight = Center + FVector(HalfWidth, -HalfWidth, 0.0f);
			TopLeft = Center + FVector(-HalfWidth, HalfWidth, 0.0f);
			TopRight = Center + FVector(HalfWidth, HalfWidth, 0.0f);
		}

		// Transform to local space
		BottomLeft = GetComponentTransform().InverseTransformPosition(BottomLeft);
		BottomRight = GetComponentTransform().InverseTransformPosition(BottomRight);
		TopLeft = GetComponentTransform().InverseTransformPosition(TopLeft);
		TopRight = GetComponentTransform().InverseTransformPosition(TopRight);

		// Recalculate edge midpoints and center
		FVector MidTop = (TopLeft + TopRight) / 2.0f;
		FVector MidRight = (TopRight + BottomRight) / 2.0f;
		FVector MidBottom = (BottomRight + BottomLeft) / 2.0f;
		FVector MidLeft = (BottomLeft + TopLeft) / 2.0f;

		const float AverageZ = (BottomLeft.Z + BottomRight.Z + TopLeft.Z + TopRight.Z) / 4.0f;
		FVector Center = GetComponentTransform().InverseTransformPosition(Tile.PointLoc);
		Center.Z = AverageZ;

		// Base vertex index for this tile in TOP surface array
		int32 BaseVertexIndex = TopVertices.Num();

		// Cache the vertex index for O(1) lookup during incremental updates
		Tile.MeshVertexIndex = BaseVertexIndex;

		// Add TOP surface vertices ONLY (9 per tile: 4 corners + 4 edge midpoints + 1 center)
		// Bottom vertices are no longer needed - bottom slab is a single quad
		TopVertices.Add(TopLeft);      // 0
		TopVertices.Add(MidTop);        // 1
		TopVertices.Add(TopRight);      // 2
		TopVertices.Add(MidRight);      // 3
		TopVertices.Add(BottomRight);   // 4
		TopVertices.Add(MidBottom);     // 5
		TopVertices.Add(BottomLeft);    // 6
		TopVertices.Add(MidLeft);       // 7
		TopVertices.Add(Center);        // 8

		// Add UVs for TOP surface only
		TopUVs.Add(FVector2D(0, 1));    // TopLeft
		TopUVs.Add(FVector2D(0.5, 1));  // MidTop
		TopUVs.Add(FVector2D(1, 1));    // TopRight
		TopUVs.Add(FVector2D(1, 0.5));  // MidRight
		TopUVs.Add(FVector2D(1, 0));    // BottomRight
		TopUVs.Add(FVector2D(0.5, 0));  // MidBottom
		TopUVs.Add(FVector2D(0, 0));    // BottomLeft
		TopUVs.Add(FVector2D(0, 0.5));  // MidLeft
		TopUVs.Add(FVector2D(0.5, 0.5)); // Center

		// Look up averaged normals for corners (cross-tile smoothing)
		FVector NormalTL = FVector(0, 0, 1);
		FVector NormalTR = FVector(0, 0, 1);
		FVector NormalBR = FVector(0, 0, 1);
		FVector NormalBL = FVector(0, 0, 1);

		// Average accumulated normals from pass 1
		if (TArray<FVector>* AccumTL = CornerNormalAccumulator.Find(FIntVector(Tile.Row + 1, Tile.Column, Level)))
		{
			FVector Sum = FVector::ZeroVector;
			for (const FVector& N : *AccumTL) Sum += N;
			NormalTL = (Sum / AccumTL->Num()).GetSafeNormal();
		}
		if (TArray<FVector>* AccumTR = CornerNormalAccumulator.Find(FIntVector(Tile.Row + 1, Tile.Column + 1, Level)))
		{
			FVector Sum = FVector::ZeroVector;
			for (const FVector& N : *AccumTR) Sum += N;
			NormalTR = (Sum / AccumTR->Num()).GetSafeNormal();
		}
		if (TArray<FVector>* AccumBR = CornerNormalAccumulator.Find(FIntVector(Tile.Row, Tile.Column + 1, Level)))
		{
			FVector Sum = FVector::ZeroVector;
			for (const FVector& N : *AccumBR) Sum += N;
			NormalBR = (Sum / AccumBR->Num()).GetSafeNormal();
		}
		if (TArray<FVector>* AccumBL = CornerNormalAccumulator.Find(FIntVector(Tile.Row, Tile.Column, Level)))
		{
			FVector Sum = FVector::ZeroVector;
			for (const FVector& N : *AccumBL) Sum += N;
			NormalBL = (Sum / AccumBL->Num()).GetSafeNormal();
		}

		// Calculate normals for edge midpoints (average of adjacent corners)
		FVector NormalMidTop = ((NormalTL + NormalTR) / 2.0f).GetSafeNormal();
		FVector NormalMidRight = ((NormalTR + NormalBR) / 2.0f).GetSafeNormal();
		FVector NormalMidBottom = ((NormalBR + NormalBL) / 2.0f).GetSafeNormal();
		FVector NormalMidLeft = ((NormalBL + NormalTL) / 2.0f).GetSafeNormal();

		// Calculate center normal (average of all 4 corners)
		FVector NormalCenter = ((NormalTL + NormalTR + NormalBR + NormalBL) / 4.0f).GetSafeNormal();

		// Add TOP surface normals only
		TopNormals.Add(NormalTL);
		TopNormals.Add(NormalMidTop);
		TopNormals.Add(NormalTR);
		TopNormals.Add(NormalMidRight);
		TopNormals.Add(NormalBR);
		TopNormals.Add(NormalMidBottom);
		TopNormals.Add(NormalBL);
		TopNormals.Add(NormalMidLeft);
		TopNormals.Add(NormalCenter);

		// Calculate slope-based vertex colors for texture blending (OpenTS2 approach)
		// Only for TOP surface vertices (9 vertices)
		const FVector UpVector(0, 0, 1);
		TArray<FVector> AllNormals = {NormalTL, NormalMidTop, NormalTR, NormalMidRight,
		                               NormalBR, NormalMidBottom, NormalBL, NormalMidLeft, NormalCenter};

		for (int32 VertIdx = 0; VertIdx < 9; VertIdx++) // 9 vertices (top surface only)
		{
			const FVector& Normal = AllNormals[VertIdx];
			const float DotProduct = FVector::DotProduct(Normal, UpVector);
			const float SlopeFactor = 1.0f - DotProduct; // 0 = flat, 1 = vertical

			const float GrassThreshold = 0.1f;   // ~6 degrees
			const float DirtThreshold = 0.5f;    // ~30 degrees

			uint8 GrassAmount = 255;
			uint8 DirtAmount = 0;
			uint8 SandAmount = 0;

			if (SlopeFactor < GrassThreshold)
			{
				// Flat terrain - pure grass
				GrassAmount = 255;
				DirtAmount = 0;
				SandAmount = 0;
			}
			else if (SlopeFactor < DirtThreshold)
			{
				// Moderate slope - blend from grass to dirt
				const float BlendFactor = (SlopeFactor - GrassThreshold) / (DirtThreshold - GrassThreshold);
				GrassAmount = static_cast<uint8>(FMath::Lerp(255.0f, 0.0f, BlendFactor));
				DirtAmount = static_cast<uint8>(FMath::Lerp(0.0f, 255.0f, BlendFactor));
				SandAmount = 0;
			}
			else
			{
				// Steep slope - blend from dirt to sand/rock
				const float BlendFactor = FMath::Clamp((SlopeFactor - DirtThreshold) / (1.0f - DirtThreshold), 0.0f, 1.0f);
				GrassAmount = 0;
				DirtAmount = static_cast<uint8>(FMath::Lerp(255.0f, 0.0f, BlendFactor));
				SandAmount = static_cast<uint8>(FMath::Lerp(0.0f, 255.0f, BlendFactor));
			}

			TopVertexColors.Add(FColor(GrassAmount, DirtAmount, SandAmount, 255));
		}

		// Add TOP surface triangles (8 triangles per tile, all using center as hub)
		// Vertex layout: 0=TL, 1=MidTop, 2=TR, 3=MidRight, 4=BR, 5=MidBottom, 6=BL, 7=MidLeft, 8=Center
		// CONDITIONAL RENDERING: Only render quadrants where TileSectionState is true

		// Top quadrant (triangles 1 & 2)
		if (Tile.TileSectionState.Top)
		{
			// Triangle 1: TopLeft → MidTop → Center
			TopTriangles.Add(BaseVertexIndex + 0);
			TopTriangles.Add(BaseVertexIndex + 1);
			TopTriangles.Add(BaseVertexIndex + 8);

			// Triangle 2: MidTop → TopRight → Center
			TopTriangles.Add(BaseVertexIndex + 1);
			TopTriangles.Add(BaseVertexIndex + 2);
			TopTriangles.Add(BaseVertexIndex + 8);
		}

		// Right quadrant (triangles 3 & 4)
		if (Tile.TileSectionState.Right)
		{
			// Triangle 3: TopRight → MidRight → Center
			TopTriangles.Add(BaseVertexIndex + 2);
			TopTriangles.Add(BaseVertexIndex + 3);
			TopTriangles.Add(BaseVertexIndex + 8);

			// Triangle 4: MidRight → BottomRight → Center
			TopTriangles.Add(BaseVertexIndex + 3);
			TopTriangles.Add(BaseVertexIndex + 4);
			TopTriangles.Add(BaseVertexIndex + 8);
		}

		// Bottom quadrant (triangles 5 & 6)
		if (Tile.TileSectionState.Bottom)
		{
			// Triangle 5: BottomRight → MidBottom → Center
			TopTriangles.Add(BaseVertexIndex + 4);
			TopTriangles.Add(BaseVertexIndex + 5);
			TopTriangles.Add(BaseVertexIndex + 8);

			// Triangle 6: MidBottom → BottomLeft → Center
			TopTriangles.Add(BaseVertexIndex + 5);
			TopTriangles.Add(BaseVertexIndex + 6);
			TopTriangles.Add(BaseVertexIndex + 8);
		}

		// Left quadrant (triangles 7 & 8)
		if (Tile.TileSectionState.Left)
		{
			// Triangle 7: BottomLeft → MidLeft → Center
			TopTriangles.Add(BaseVertexIndex + 6);
			TopTriangles.Add(BaseVertexIndex + 7);
			TopTriangles.Add(BaseVertexIndex + 8);

			// Triangle 8: MidLeft → TopLeft → Center
			TopTriangles.Add(BaseVertexIndex + 7);
			TopTriangles.Add(BaseVertexIndex + 0);
			TopTriangles.Add(BaseVertexIndex + 8);
		}

		// ========== SIDE FACES GENERATION ==========
		// Generate side faces ONLY at lot perimeter boundaries
		// Interior cutouts (pools, stairs) should NOT have side faces - terrain is a solid cube
		// Side faces auto-adjust to terrain height changes

		// Thickness offset extends downward from top surface
		const FVector ThicknessOffset = FVector(0, 0, -TerrainThickness);

		// Lot boundary detection - only generate side faces at lot edges, not interior holes
		// Row increases in +Y direction, Column increases in +X direction
		const bool bIsTopBoundary = OurLot ? (Tile.Row == OurLot->GridSizeY - 1) : false;
		const bool bIsRightBoundary = OurLot ? (Tile.Column == OurLot->GridSizeX - 1) : false;
		const bool bIsBottomBoundary = (Tile.Row == 0);
		const bool bIsLeftBoundary = (Tile.Column == 0);

		// UV scaling for side faces - use world units to prevent stretching
		// 100 world units = 1 UV unit (standard tiling scale)
		const float UVScale = 100.0f;
		const float EdgeWidth = FVector::Dist(TopLeft, TopRight); // Horizontal width of tile edge
		const float EdgeHeight = TerrainThickness; // Vertical height of side face
		const float UVWidth = EdgeWidth / UVScale;
		const float UVHeight = EdgeHeight / UVScale;

		// Top edge (+Y direction) - Single quad from corner to corner
		// Only render at lot perimeter boundary
		if (bIsTopBoundary)
		{
			FVector EdgeNormal = FVector(0, 1, 0); // Points outward (+Y)

			// Single quad: TopLeft → TopRight spanning entire edge
			int32 EdgeBaseIdx = SideVertices.Num();
			SideVertices.Add(TopRight);                    // Top surface right
			SideVertices.Add(TopLeft);                     // Top surface left
			SideVertices.Add(TopLeft + ThicknessOffset);   // Bottom left
			SideVertices.Add(TopRight + ThicknessOffset);  // Bottom right

			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);

			// UV coordinates scaled to world dimensions to prevent stretching
			SideUVs.Add(FVector2D(0, UVHeight));           // Top right
			SideUVs.Add(FVector2D(UVWidth, UVHeight));     // Top left
			SideUVs.Add(FVector2D(UVWidth, 0));            // Bottom left
			SideUVs.Add(FVector2D(0, 0));                  // Bottom right

			SideVertexColors.Add(FColor(139, 90, 43, 255)); // Brown/dirt color for side faces
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));

			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 1);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 3);
		}

		// Right edge (+X direction) - Single quad from corner to corner
		// Only render at lot perimeter boundary
		if (bIsRightBoundary)
		{
			FVector EdgeNormal = FVector(1, 0, 0); // Points outward (+X)

			// Single quad: TopRight → BottomRight spanning entire edge
			int32 EdgeBaseIdx = SideVertices.Num();
			SideVertices.Add(BottomRight);                    // Top surface bottom corner
			SideVertices.Add(TopRight);                       // Top surface top corner
			SideVertices.Add(TopRight + ThicknessOffset);     // Bottom top corner
			SideVertices.Add(BottomRight + ThicknessOffset);  // Bottom bottom corner

			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);

			// UV coordinates scaled to world dimensions
			SideUVs.Add(FVector2D(0, UVHeight));
			SideUVs.Add(FVector2D(UVWidth, UVHeight));
			SideUVs.Add(FVector2D(UVWidth, 0));
			SideUVs.Add(FVector2D(0, 0));

			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));

			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 1);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 3);
		}

		// Bottom edge (-Y direction) - Single quad from corner to corner
		// Only render at lot perimeter boundary
		if (bIsBottomBoundary)
		{
			FVector EdgeNormal = FVector(0, -1, 0); // Points outward (-Y)

			int32 EdgeBaseIdx = SideVertices.Num();
			SideVertices.Add(BottomLeft);                     // Top surface left
			SideVertices.Add(BottomRight);                    // Top surface right
			SideVertices.Add(BottomRight + ThicknessOffset);  // Bottom right
			SideVertices.Add(BottomLeft + ThicknessOffset);   // Bottom left

			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);

			// UV coordinates scaled to world dimensions
			SideUVs.Add(FVector2D(0, UVHeight));
			SideUVs.Add(FVector2D(UVWidth, UVHeight));
			SideUVs.Add(FVector2D(UVWidth, 0));
			SideUVs.Add(FVector2D(0, 0));

			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));

			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 1);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 3);
		}

		// Left edge (-X direction) - Single quad from corner to corner
		// Only render at lot perimeter boundary
		if (bIsLeftBoundary)
		{
			FVector EdgeNormal = FVector(-1, 0, 0); // Points outward (-X)

			int32 EdgeBaseIdx = SideVertices.Num();
			SideVertices.Add(TopLeft);                        // Top surface top
			SideVertices.Add(BottomLeft);                     // Top surface bottom
			SideVertices.Add(BottomLeft + ThicknessOffset);   // Bottom bottom
			SideVertices.Add(TopLeft + ThicknessOffset);      // Bottom top

			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);
			SideNormals.Add(EdgeNormal);

			// UV coordinates scaled to world dimensions
			SideUVs.Add(FVector2D(0, UVHeight));
			SideUVs.Add(FVector2D(UVWidth, UVHeight));
			SideUVs.Add(FVector2D(UVWidth, 0));
			SideUVs.Add(FVector2D(0, 0));

			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));
			SideVertexColors.Add(FColor(139, 90, 43, 255));

			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 1);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 0);
			SideTriangles.Add(EdgeBaseIdx + 2);
			SideTriangles.Add(EdgeBaseIdx + 3);
		}
	}

	// ========== BOTTOM SLAB GENERATION ==========
	// Generate single quad spanning bounding box of all tiles at this level
	// Bottom face is static and never modified during terrain editing

	if (EstimatedTileCount > 0 && OurLot)
	{
		// Find bounding box of all terrain tiles at this level
		float MinX = FLT_MAX, MinY = FLT_MAX, MaxX = -FLT_MAX, MaxY = -FLT_MAX;
		float BottomZ = 0.0f;

		for (int32 i = 0; i < TerrainDataArray.Num(); i++)
		{
			const FTerrainSegmentData& Tile = TerrainDataArray[i];
			if (!Tile.bCommitted || Tile.Level != Level)
			{
				continue;
			}

			const float TileSize = OurLot->GridTileSize;
			const float BaseX = OurLot->GetActorLocation().X;
			const float BaseY = OurLot->GetActorLocation().Y;
			const float BaseZ = OurLot->GetActorLocation().Z + (OurLot->DefaultWallHeight * (Level - OurLot->Basements));

			// Calculate tile bounds
			const float TileMinX = BaseX + (Tile.Column * TileSize);
			const float TileMaxX = BaseX + ((Tile.Column + 1) * TileSize);
			const float TileMinY = BaseY + (Tile.Row * TileSize);
			const float TileMaxY = BaseY + ((Tile.Row + 1) * TileSize);

			MinX = FMath::Min(MinX, TileMinX);
			MaxX = FMath::Max(MaxX, TileMaxX);
			MinY = FMath::Min(MinY, TileMinY);
			MaxY = FMath::Max(MaxY, TileMaxY);

			// Bottom Z is constant: BaseZ - TerrainThickness
			BottomZ = BaseZ - TerrainThickness;
		}

		// Convert to local space
		FVector BL_World = FVector(MinX, MinY, BottomZ);
		FVector BR_World = FVector(MaxX, MinY, BottomZ);
		FVector TL_World = FVector(MinX, MaxY, BottomZ);
		FVector TR_World = FVector(MaxX, MaxY, BottomZ);

		FVector BL = GetComponentTransform().InverseTransformPosition(BL_World);
		FVector BR = GetComponentTransform().InverseTransformPosition(BR_World);
		FVector TL = GetComponentTransform().InverseTransformPosition(TL_World);
		FVector TR = GetComponentTransform().InverseTransformPosition(TR_World);

		// Add bottom slab to the SideVertices array (combining sides + bottom into one section)
		int32 BottomBaseIdx = SideVertices.Num();

		SideVertices.Add(BL);
		SideVertices.Add(BR);
		SideVertices.Add(TR);
		SideVertices.Add(TL);

		// Inverted normals (faces down)
		const FVector DownNormal = FVector(0, 0, -1);
		SideNormals.Add(DownNormal);
		SideNormals.Add(DownNormal);
		SideNormals.Add(DownNormal);
		SideNormals.Add(DownNormal);

		// UVs
		SideUVs.Add(FVector2D(0, 0));
		SideUVs.Add(FVector2D(1, 0));
		SideUVs.Add(FVector2D(1, 1));
		SideUVs.Add(FVector2D(0, 1));

		// Simple gray color
		SideVertexColors.Add(FColor(100, 100, 100, 255));
		SideVertexColors.Add(FColor(100, 100, 100, 255));
		SideVertexColors.Add(FColor(100, 100, 100, 255));
		SideVertexColors.Add(FColor(100, 100, 100, 255));

		// 2 triangles (reversed winding for downward visibility)
		SideTriangles.Add(BottomBaseIdx + 0);
		SideTriangles.Add(BottomBaseIdx + 2);
		SideTriangles.Add(BottomBaseIdx + 1);
		SideTriangles.Add(BottomBaseIdx + 0);
		SideTriangles.Add(BottomBaseIdx + 3);
		SideTriangles.Add(BottomBaseIdx + 2);
	}

	// ========== CREATE MESH SECTIONS ==========

	// TOP SURFACE - Grass/terrain material
	if (TopVertices.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - Creating TOP mesh section with %d vertices, %d triangles"),
			TopVertices.Num(), TopTriangles.Num() / 3);

		CreateMeshSection(GetTopSectionIndex(Level), TopVertices, TopTriangles, TopNormals, TopUVs, TopVertexColors, TopTangents, bCreateCollision);

		// Set grass/terrain material for top
		UMaterialInstance* LevelMaterial = LevelMaterials.FindRef(Level);
		if (!LevelMaterial)
		{
			LevelMaterial = TerrainMaterial;
			if (!LevelMaterial && OurLot)
			{
				LevelMaterial = OurLot->DefaultTerrainMaterial;
			}
		}

		if (LevelMaterial)
		{
			SetMaterial(GetTopSectionIndex(Level), LevelMaterial);

			// Restore ShowGrid parameter
			if (OurLot)
			{
				bool bShouldShowGrid = OurLot->bShowGrid && (Level == OurLot->CurrentLevel);
				UMaterialInterface* CurrentMat = GetMaterial(GetTopSectionIndex(Level));
				UMaterialInstanceDynamic* DynamicMat = Cast<UMaterialInstanceDynamic>(CurrentMat);
				if (!DynamicMat && CurrentMat)
				{
					DynamicMat = UMaterialInstanceDynamic::Create(CurrentMat, this);
					SetMaterial(GetTopSectionIndex(Level), DynamicMat);
				}
				if (DynamicMat)
				{
					DynamicMat->SetScalarParameterValue(FName("ShowGrid"), bShouldShowGrid ? 1.0f : 0.0f);
				}
			}
		}
	}

	// SIDES + BOTTOM - Combined static geometry (dirt/cliff material)
	if (SideVertices.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - Creating SIDES+BOTTOM mesh section with %d vertices, %d triangles"),
			SideVertices.Num(), SideTriangles.Num() / 3);

		CreateMeshSection(GetSidesAndBottomSectionIndex(Level), SideVertices, SideTriangles, SideNormals, SideUVs, SideVertexColors, SideTangents, bCreateCollision);

		// Set side material (dirt/cliff)
		UMaterialInstance* SideMat = SideMaterial;
		if (!SideMat)
		{
			// Fallback chain: DefaultSideMaterial -> TerrainMaterial -> LotManager's default
			SideMat = DefaultSideMaterial;
			if (!SideMat)
			{
				SideMat = TerrainMaterial;
				if (!SideMat && OurLot)
				{
					SideMat = OurLot->DefaultTerrainMaterial;
				}
			}
		}

		if (SideMat)
		{
			SetMaterial(GetSidesAndBottomSectionIndex(Level), SideMat);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::RebuildLevel - Level %d rebuild complete"), Level);
}

float UTerrainComponent::SampleTerrainElevation(int32 Level, int32 Row, int32 Column)
{
	// Get height map for this level
	const FTerrainHeightMap* HeightMap = GetHeightMap(Level);
	if (!HeightMap)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::SampleTerrainElevation - No height map for level %d"), Level);
		return 0.0f;
	}

	// Sample the average height of the tile's four corners
	// This gives a more accurate representation of the tile's elevation
	float BottomLeft = HeightMap->GetCornerHeight(Row, Column);
	float BottomRight = HeightMap->GetCornerHeight(Row, Column + 1);
	float TopLeft = HeightMap->GetCornerHeight(Row + 1, Column);
	float TopRight = HeightMap->GetCornerHeight(Row + 1, Column + 1);

	// Return average of four corners
	float AverageHeight = (BottomLeft + BottomRight + TopLeft + TopRight) / 4.0f;

	return AverageHeight;
}

void UTerrainComponent::FlattenRegion(int32 FromRow, int32 FromCol, int32 ToRow, int32 ToCol, float TargetHeight, int32 Level)
{
	// OpenTS2-style terrain flattening: set all corners in region to same height
	// This ensures floors placed on top will have a flat surface

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::FlattenRegion - Flattening Level %d from (%d,%d) to (%d,%d) at height %.2f"),
		Level, FromRow, FromCol, ToRow, ToCol, TargetHeight);

	// Ensure valid range
	if (FromRow > ToRow || FromCol > ToCol)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::FlattenRegion - Invalid range"));
		return;
	}

	// Flatten all corners in the rectangular region
	// Need to update corners from FromRow to ToRow+1 (inclusive) because corners are shared
	for (int32 CornerRow = FromRow; CornerRow <= ToRow; ++CornerRow)
	{
		for (int32 CornerCol = FromCol; CornerCol <= ToCol; ++CornerCol)
		{
			// Use UpdateCornerHeight which automatically regenerates affected tiles
			UpdateCornerHeight(Level, CornerRow, CornerCol, TargetHeight);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::FlattenRegion - Flattened %d corners"),
		(ToRow - FromRow + 1) * (ToCol - FromCol + 1));
}

void UTerrainComponent::SmoothCornerRegion(int32 Level, int32 CenterRow, int32 CenterCol, float Radius, float Strength)
{
	// Laplacian smoothing: blend each corner toward average of neighbors
	// This creates smoother terrain transitions

	// Get height map for this level
	FTerrainHeightMap* HeightMap = GetOrCreateHeightMap(Level);
	if (!HeightMap)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::SmoothCornerRegion - No height map for level %d"), Level);
		return;
	}

	// Clamp strength to valid range
	Strength = FMath::Clamp(Strength, 0.0f, 1.0f);

	// Calculate bounding box of affected corners
	int32 RadiusInt = FMath::CeilToInt(Radius);
	int32 MinRow = FMath::Max(0, CenterRow - RadiusInt);
	int32 MaxRow = FMath::Min(HeightMap->GetGridHeight() - 1, CenterRow + RadiusInt);
	int32 MinCol = FMath::Max(0, CenterCol - RadiusInt);
	int32 MaxCol = FMath::Min(HeightMap->GetGridWidth() - 1, CenterCol + RadiusInt);

	// Store original heights (we need to read from original values, not partially-updated ones)
	TMap<FIntPoint, float> OriginalHeights;
	for (int32 Row = MinRow; Row <= MaxRow; ++Row)
	{
		for (int32 Col = MinCol; Col <= MaxCol; ++Col)
		{
			// Check if corner is within circular radius
			float DistSq = FVector2D(Row - CenterRow, Col - CenterCol).SizeSquared();
			if (DistSq <= Radius * Radius)
			{
				OriginalHeights.Add(FIntPoint(Row, Col), HeightMap->GetCornerHeight(Row, Col));
			}
		}
	}

	// Begin batch operation to suppress per-corner rebuilds
	BeginBatchOperation();

	// Apply smoothing to each corner in the region
	for (const TPair<FIntPoint, float>& Pair : OriginalHeights)
	{
		int32 Row = Pair.Key.X;
		int32 Col = Pair.Key.Y;
		float OriginalHeight = Pair.Value;

		// Calculate average height of neighbors (Laplacian smoothing kernel)
		TArray<float> NeighborHeights;

		// Check 4-connected neighbors (cardinal directions)
		if (Row > 0)
		{
			NeighborHeights.Add(HeightMap->GetCornerHeight(Row - 1, Col));
		}
		if (Row < HeightMap->GetGridHeight() - 1)
		{
			NeighborHeights.Add(HeightMap->GetCornerHeight(Row + 1, Col));
		}
		if (Col > 0)
		{
			NeighborHeights.Add(HeightMap->GetCornerHeight(Row, Col - 1));
		}
		if (Col < HeightMap->GetGridWidth() - 1)
		{
			NeighborHeights.Add(HeightMap->GetCornerHeight(Row, Col + 1));
		}

		// Calculate average of neighbors
		if (NeighborHeights.Num() > 0)
		{
			float NeighborAvg = 0.0f;
			for (float Height : NeighborHeights)
			{
				NeighborAvg += Height;
			}
			NeighborAvg /= NeighborHeights.Num();

			// Blend original height toward neighbor average based on strength
			float NewHeight = FMath::Lerp(OriginalHeight, NeighborAvg, Strength);

			// Update corner height
			UpdateCornerHeight(Level, Row, Col, NewHeight);
		}
	}

	// End batch operation (rebuilds all dirty levels)
	EndBatchOperation();

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::SmoothCornerRegion - Smoothed %d corners at Level %d"),
		OriginalHeights.Num(), Level);
}

void UTerrainComponent::FlattenTerrainUnderFloor(int32 Level, int32 Row, int32 Column, const FTileSectionState& TileSectionState, float TargetHeight, bool bBypassLock)
{
	// Selectively flatten terrain corners based on which floor quadrants are present
	// This handles diagonal/partial floors correctly by only flattening covered corners

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::FlattenTerrainUnderFloor - Level %d, Tile (%d,%d), TargetHeight %.2f, BypassLock=%d, Quadrants: T=%d R=%d B=%d L=%d"),
		Level, Row, Column, TargetHeight, bBypassLock ? 1 : 0,
		TileSectionState.Top ? 1 : 0,
		TileSectionState.Right ? 1 : 0,
		TileSectionState.Bottom ? 1 : 0,
		TileSectionState.Left ? 1 : 0);

	// Build set of corners to flatten based on active quadrants
	// Use TSet to avoid duplicating corners that are shared by multiple quadrants
	TSet<FIntPoint> CornersToFlatten;

	// Corner coordinates in grid space (relative to tile at Row, Column):
	// - BottomLeft: (Row, Column)
	// - BottomRight: (Row, Column+1)
	// - TopLeft: (Row+1, Column)
	// - TopRight: (Row+1, Column+1)

	// Top quadrant uses TopLeft + TopRight corners
	if (TileSectionState.Top)
	{
		CornersToFlatten.Add(FIntPoint(Row + 1, Column));     // TopLeft
		CornersToFlatten.Add(FIntPoint(Row + 1, Column + 1)); // TopRight
	}

	// Right quadrant uses TopRight + BottomRight corners
	if (TileSectionState.Right)
	{
		CornersToFlatten.Add(FIntPoint(Row + 1, Column + 1)); // TopRight
		CornersToFlatten.Add(FIntPoint(Row, Column + 1));     // BottomRight
	}

	// Bottom quadrant uses BottomRight + BottomLeft corners
	if (TileSectionState.Bottom)
	{
		CornersToFlatten.Add(FIntPoint(Row, Column + 1)); // BottomRight
		CornersToFlatten.Add(FIntPoint(Row, Column));     // BottomLeft
	}

	// Left quadrant uses BottomLeft + TopLeft corners
	if (TileSectionState.Left)
	{
		CornersToFlatten.Add(FIntPoint(Row, Column));     // BottomLeft
		CornersToFlatten.Add(FIntPoint(Row + 1, Column)); // TopLeft
	}

	// Early exit if no corners to flatten (empty TileSectionState)
	if (CornersToFlatten.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::FlattenTerrainUnderFloor - No quadrants active, skipping"));
		return;
	}

	// Use batch operation to suppress intermediate rebuilds
	bool bWasInBatch = bInBatchOperation;
	if (!bWasInBatch)
	{
		BeginBatchOperation();
	}

	// Flatten each unique corner
	for (const FIntPoint& Corner : CornersToFlatten)
	{
		UpdateCornerHeight(Level, Corner.X, Corner.Y, TargetHeight, bBypassLock);
	}

	// End batch operation if we started it (triggers single rebuild)
	if (!bWasInBatch)
	{
		EndBatchOperation();
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::FlattenTerrainUnderFloor - Flattened %d unique corners at Level %d"),
		CornersToFlatten.Num(), Level);
}

void UTerrainComponent::FlattenTerrainAlongWall(int32 Level, int32 StartRow, int32 StartColumn, int32 EndRow, int32 EndColumn, float TargetHeight, bool bBypassLock)
{
	// Flatten all terrain corners along a wall segment to prevent Z-fighting
	// Handles horizontal, vertical, and diagonal walls

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::FlattenTerrainAlongWall - Level %d, from (%d,%d) to (%d,%d), TargetHeight %.2f, BypassLock=%d"),
		Level, StartRow, StartColumn, EndRow, EndColumn, TargetHeight, bBypassLock ? 1 : 0);

	// Build set of corners to flatten along the wall path
	TSet<FIntPoint> CornersToFlatten;

	// Determine wall orientation
	const int32 DeltaRow = EndRow - StartRow;
	const int32 DeltaColumn = EndColumn - StartColumn;

	if (DeltaRow == 0)
	{
		// Horizontal wall (same row, different columns)
		const int32 MinColumn = FMath::Min(StartColumn, EndColumn);
		const int32 MaxColumn = FMath::Max(StartColumn, EndColumn);

		for (int32 Col = MinColumn; Col <= MaxColumn; ++Col)
		{
			CornersToFlatten.Add(FIntPoint(StartRow, Col));
		}

		UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent::FlattenTerrainAlongWall - Horizontal wall: flattening %d corners"),
			CornersToFlatten.Num());
	}
	else if (DeltaColumn == 0)
	{
		// Vertical wall (same column, different rows)
		const int32 MinRow = FMath::Min(StartRow, EndRow);
		const int32 MaxRow = FMath::Max(StartRow, EndRow);

		for (int32 Row = MinRow; Row <= MaxRow; ++Row)
		{
			CornersToFlatten.Add(FIntPoint(Row, StartColumn));
		}

		UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent::FlattenTerrainAlongWall - Vertical wall: flattening %d corners"),
			CornersToFlatten.Num());
	}
	else
	{
		// Diagonal wall - use Bresenham's line algorithm to find corners along path
		const int32 AbsDeltaRow = FMath::Abs(DeltaRow);
		const int32 AbsDeltaColumn = FMath::Abs(DeltaColumn);
		const int32 StepRow = (DeltaRow > 0) ? 1 : -1;
		const int32 StepColumn = (DeltaColumn > 0) ? 1 : -1;

		int32 CurrentRow = StartRow;
		int32 CurrentColumn = StartColumn;

		if (AbsDeltaRow > AbsDeltaColumn)
		{
			// More vertical than horizontal
			int32 Error = AbsDeltaRow / 2;
			while (CurrentRow != EndRow)
			{
				CornersToFlatten.Add(FIntPoint(CurrentRow, CurrentColumn));

				Error -= AbsDeltaColumn;
				if (Error < 0)
				{
					CurrentColumn += StepColumn;
					Error += AbsDeltaRow;
				}
				CurrentRow += StepRow;
			}
		}
		else
		{
			// More horizontal than vertical
			int32 Error = AbsDeltaColumn / 2;
			while (CurrentColumn != EndColumn)
			{
				CornersToFlatten.Add(FIntPoint(CurrentRow, CurrentColumn));

				Error -= AbsDeltaRow;
				if (Error < 0)
				{
					CurrentRow += StepRow;
					Error += AbsDeltaColumn;
				}
				CurrentColumn += StepColumn;
			}
		}

		// Add the end point
		CornersToFlatten.Add(FIntPoint(EndRow, EndColumn));

		UE_LOG(LogTemp, Verbose, TEXT("TerrainComponent::FlattenTerrainAlongWall - Diagonal wall: flattening %d corners"),
			CornersToFlatten.Num());
	}

	// Early exit if no corners to flatten
	if (CornersToFlatten.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::FlattenTerrainAlongWall - No corners to flatten"));
		return;
	}

	// Use batch operation to suppress intermediate rebuilds
	bool bWasInBatch = bInBatchOperation;
	if (!bWasInBatch)
	{
		BeginBatchOperation();
	}

	// Flatten each unique corner
	for (const FIntPoint& Corner : CornersToFlatten)
	{
		UpdateCornerHeight(Level, Corner.X, Corner.Y, TargetHeight, bBypassLock);
	}

	// End batch operation if we started it (triggers single rebuild)
	if (!bWasInBatch)
	{
		EndBatchOperation();
	}

	UE_LOG(LogTemp, Log, TEXT("TerrainComponent::FlattenTerrainAlongWall - Flattened %d unique corners at Level %d"),
		CornersToFlatten.Num(), Level);
}

// ========== Incremental Update System ==========

void UTerrainComponent::MarkCornerDirty(int32 Level, int32 CornerRow, int32 CornerColumn)
{
	int32 CornerKey = MakeCornerKey(Level, CornerRow, CornerColumn);
	DirtyCorners.Add(CornerKey);
}

void UTerrainComponent::ClearDirtyCorners()
{
	DirtyCorners.Empty();
}

void UTerrainComponent::UpdateDirtyVertices()
{
	if (DirtyCorners.Num() == 0)
	{
		return; // No dirty corners, nothing to update
	}

	// Get LotManager for calculations
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		UE_LOG(LogTemp, Error, TEXT("TerrainComponent::UpdateDirtyVertices - No LotManager found"));
		return;
	}

	// Group dirty corners by level for batch processing
	TMap<int32, TSet<int32>> CornersByLevel; // Level -> Set of corner keys

	for (int32 CornerKey : DirtyCorners)
	{
		int32 Level = (CornerKey >> 24) & 0xFF;
		CornersByLevel.FindOrAdd(Level).Add(CornerKey);
	}

	// Process each level separately
	for (const TPair<int32, TSet<int32>>& LevelPair : CornersByLevel)
	{
		int32 Level = LevelPair.Key;
		const TSet<int32>& LevelCorners = LevelPair.Value;

		// Get height map for this level
		const FTerrainHeightMap* HeightMap = GetHeightMap(Level);
		if (!HeightMap)
		{
			UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::UpdateDirtyVertices - No height map for level %d"), Level);
			continue;
		}

		// Get existing mesh section data (TOP section only, since we only update top vertices)
		FProcMeshSection* MeshSection = GetProcMeshSection(GetTopSectionIndex(Level));
		if (!MeshSection)
		{
			UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::UpdateDirtyVertices - No mesh section for level %d"), Level);
			continue;
		}

		// Use the HeightMap's stored BaseZ instead of recalculating it
		// This ensures consistency with how the terrain was originally created
		const float BaseZ = HeightMap->GetBaseZ();
		const float TileSize = OurLot->GridTileSize;
		const float BaseX = OurLot->GetActorLocation().X;
		const float BaseY = OurLot->GetActorLocation().Y;
		FVector ThicknessOffset = FVector(0, 0, -TerrainThickness);

		// Collect all tiles affected by dirty corners
		TSet<FTerrainSegmentData*> AffectedTiles;
		for (int32 CornerKey : LevelCorners)
		{
			int32 CornerRow = (CornerKey >> 12) & 0xFFF;
			int32 CornerColumn = CornerKey & 0xFFF;

			// Get all tiles affected by this corner
			TArray<FTerrainSegmentData*> TilesForCorner = GetTilesAffectedByCorner(Level, CornerRow, CornerColumn);
			for (FTerrainSegmentData* Tile : TilesForCorner)
			{
				AffectedTiles.Add(Tile);
			}
		}

		if (AffectedTiles.Num() == 0)
		{
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("TerrainComponent::UpdateDirtyVertices - Updating %d tiles on level %d"),
			AffectedTiles.Num(), Level);

		// For each affected tile, update its vertices in the mesh
		bool bMeshModified = false;

		for (FTerrainSegmentData* TileData : AffectedTiles)
		{
			// Read corner heights from height map
			const int32 TileRow = TileData->Row;
			const int32 TileCol = TileData->Column;

			const float BLHeight = HeightMap->GetCornerHeight(TileRow, TileCol);
			const float BRHeight = HeightMap->GetCornerHeight(TileRow, TileCol + 1);
			const float TLHeight = HeightMap->GetCornerHeight(TileRow + 1, TileCol);
			const float TRHeight = HeightMap->GetCornerHeight(TileRow + 1, TileCol + 1);

			// Calculate target Z coordinates in local space
			// Build world positions
			FVector WorldBL = FVector(BaseX + TileCol * TileSize, BaseY + TileRow * TileSize, BaseZ + BLHeight);
			FVector WorldBR = FVector(BaseX + (TileCol + 1) * TileSize, BaseY + TileRow * TileSize, BaseZ + BRHeight);
			FVector WorldTL = FVector(BaseX + TileCol * TileSize, BaseY + (TileRow + 1) * TileSize, BaseZ + TLHeight);
			FVector WorldTR = FVector(BaseX + (TileCol + 1) * TileSize, BaseY + (TileRow + 1) * TileSize, BaseZ + TRHeight);

			// Transform to local space
			FVector LocalBL = GetComponentTransform().InverseTransformPosition(WorldBL);
			FVector LocalBR = GetComponentTransform().InverseTransformPosition(WorldBR);
			FVector LocalTL = GetComponentTransform().InverseTransformPosition(WorldTL);
			FVector LocalTR = GetComponentTransform().InverseTransformPosition(WorldTR);

			// Calculate edge midpoints (OpenTS2 smoothing approach)
			FVector LocalMidTop = (LocalTL + LocalTR) / 2.0f;
			FVector LocalMidRight = (LocalTR + LocalBR) / 2.0f;
			FVector LocalMidBottom = (LocalBR + LocalBL) / 2.0f;
			FVector LocalMidLeft = (LocalBL + LocalTL) / 2.0f;

			// Calculate center position (averaged from corners)
			const float CenterZ = (LocalBL.Z + LocalBR.Z + LocalTL.Z + LocalTR.Z) / 4.0f;
			const FVector LocalCenter = FVector(
				(LocalBL.X + LocalBR.X + LocalTL.X + LocalTR.X) / 4.0f,
				(LocalBL.Y + LocalBR.Y + LocalTL.Y + LocalTR.Y) / 4.0f,
				CenterZ
			);

			// Get cached base vertex index for O(1) lookup (set during RebuildLevel)
			int32 BaseVertexIndex = TileData->MeshVertexIndex;

			// Validate cached index
			if (BaseVertexIndex < 0 || BaseVertexIndex >= MeshSection->ProcVertexBuffer.Num())
			{
				UE_LOG(LogTemp, Warning, TEXT("TerrainComponent::UpdateDirtyVertices - Invalid cached vertex index %d for tile (%d, %d), skipping"),
					BaseVertexIndex, TileRow, TileCol);
				continue;
			}

			// Update vertex positions - TOP surface (0-8) and BOTTOM surface (9-17)
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 0].Position = LocalTL;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 1].Position = LocalMidTop;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 2].Position = LocalTR;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 3].Position = LocalMidRight;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 4].Position = LocalBR;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 5].Position = LocalMidBottom;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 6].Position = LocalBL;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 7].Position = LocalMidLeft;
			MeshSection->ProcVertexBuffer[BaseVertexIndex + 8].Position = LocalCenter;

			bMeshModified = true;
		}

		// Update the mesh section on the GPU if we modified anything
		if (bMeshModified)
		{
			MarkRenderStateDirty();
			UE_LOG(LogTemp, Log, TEXT("TerrainComponent::UpdateDirtyVertices - Updated %d tiles on level %d"),
				AffectedTiles.Num(), Level);
		}
	}

	// Clear dirty corners after update
	ClearDirtyCorners();
}

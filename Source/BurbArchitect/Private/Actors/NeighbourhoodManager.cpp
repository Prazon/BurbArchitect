// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/NeighbourhoodManager.h"
#include "Actors/LotManager.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
ANeighbourhoodManager::ANeighbourhoodManager()
{
	// Set this actor to call Tick() every frame
	PrimaryActorTick.bCanEverTick = false;

	// Create root component
	USceneComponent* RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;

	// Create neighbourhood terrain component (coarse grid)
	TerrainComponent = CreateDefaultSubobject<UTerrainComponent>(TEXT("TerrainComponent"));
	TerrainComponent->SetupAttachment(RootComponent);
	TerrainComponent->SetCastHiddenShadow(true);
}

// Called when the game starts or when spawned
void ANeighbourhoodManager::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void ANeighbourhoodManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ANeighbourhoodManager::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Terrain is generated via "Generate Neighbourhood Terrain" button
	// No auto-generation on construction
}

#if WITH_EDITOR
void ANeighbourhoodManager::GenerateNeighbourhoodTerrain()
{
	if (!TerrainComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("TerrainComponent is null!"));
		return;
	}

	// Use 1:1 tile scale (same as lots)
	int32 TerrainSizeX = NeighbourhoodSizeX;
	int32 TerrainSizeY = NeighbourhoodSizeY;

	UE_LOG(LogTemp, Warning, TEXT("Generating neighbourhood terrain:"));
	UE_LOG(LogTemp, Warning, TEXT("  - Neighbourhood size: %dx%d tiles"), NeighbourhoodSizeX, NeighbourhoodSizeY);
	UE_LOG(LogTemp, Warning, TEXT("  - Terrain grid: %dx%d tiles (1:1 scale)"), TerrainSizeX, TerrainSizeY);
	UE_LOG(LogTemp, Warning, TEXT("  - Tile size: %.0f units"), LotTileSize);

	// Clear existing terrain
	TerrainComponent->TerrainDataArray.Empty();
	TerrainComponent->TerrainSpatialMap.Empty();
	TerrainComponent->ClearAllMeshSections();

	// Generate 1:1 terrain grid
	for (int32 Row = 0; Row < TerrainSizeX; Row++)
	{
		for (int32 Col = 0; Col < TerrainSizeY; Col++)
		{
			FTerrainSegmentData TerrainTile;
			TerrainTile.Row = Row;
			TerrainTile.Column = Col;
			TerrainTile.Level = 0;
			TerrainTile.Width = LotTileSize;  // Same as lot tiles

			// Calculate world position (centered at neighbourhood origin)
			FVector TileCenter;
			TileCenter.X = (Row - TerrainSizeX / 2.0f) * LotTileSize;
			TileCenter.Y = (Col - TerrainSizeY / 2.0f) * LotTileSize;
			TileCenter.Z = 0.0f;

			TerrainTile.PointLoc = TileCenter;

			// All quadrants visible by default
			TerrainTile.TileSectionState.Top = true;
			TerrainTile.TileSectionState.Right = true;
			TerrainTile.TileSectionState.Bottom = true;
			TerrainTile.TileSectionState.Left = true;

			TerrainTile.Material = NeighbourhoodTerrainMaterial;
			TerrainTile.bCommitted = true;

			// Add to terrain component
			TerrainTile.ArrayIndex = TerrainComponent->TerrainDataArray.Add(TerrainTile);

			// Add to spatial map (packed key = Row << 16 | Col)
			int32 PackedKey = (Row << 16) | Col;
			TerrainComponent->TerrainSpatialMap.Add(PackedKey, TerrainTile.ArrayIndex);
		}
	}

	// Build the mesh
	TerrainComponent->RebuildLevel(0);

	UE_LOG(LogTemp, Warning, TEXT("Neighbourhood terrain complete! Generated %d terrain tiles"),
		TerrainSizeX * TerrainSizeY);
}
#endif

bool ANeighbourhoodManager::ClaimTiles(ALotManager* Lot, int32 Row, int32 Column, int32 SizeX, int32 SizeY)
{
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("NeighbourhoodManager::ClaimTiles - Lot is null!"));
		return false;
	}

	// Validate placement
	if (!CanPlaceLot(Row, Column, SizeX, SizeY))
	{
		UE_LOG(LogTemp, Error, TEXT("NeighbourhoodManager::ClaimTiles - Cannot place lot at (%d, %d) - overlaps or out of bounds!"), Row, Column);
		return false;
	}

	// Claim tiles in occupancy map
	for (int32 R = Row; R < Row + SizeX; R++)
	{
		for (int32 C = Column; C < Column + SizeY; C++)
		{
			int32 PackedKey = (R << 16) | C;
			TileClaimMap.Add(PackedKey, Lot);
		}
	}

	// Add to lots array
	PlacedLots.AddUnique(Lot);

	UE_LOG(LogTemp, Warning, TEXT("NeighbourhoodManager: Claimed %dx%d tiles at (%d, %d) for lot '%s'"),
		SizeX, SizeY, Row, Column, *Lot->GetName());

	return true;
}

void ANeighbourhoodManager::ReleaseTiles(ALotManager* Lot)
{
	if (!Lot)
	{
		UE_LOG(LogTemp, Error, TEXT("NeighbourhoodManager::ReleaseTiles - Lot is null!"));
		return;
	}

	// Find and remove all tiles claimed by this lot
	TArray<int32> KeysToRemove;
	for (auto& Elem : TileClaimMap)
	{
		if (Elem.Value == Lot)
		{
			KeysToRemove.Add(Elem.Key);
		}
	}

	for (int32 Key : KeysToRemove)
	{
		TileClaimMap.Remove(Key);
	}

	// Remove from lots array
	PlacedLots.Remove(Lot);

	UE_LOG(LogTemp, Warning, TEXT("NeighbourhoodManager: Released %d tiles for lot '%s'"),
		KeysToRemove.Num(), *Lot->GetName());
}

bool ANeighbourhoodManager::CanPlaceLot(int32 Row, int32 Column, int32 SizeX, int32 SizeY) const
{
	// Check if lot is within neighbourhood bounds
	if (Row < 0 || Column < 0 ||
		Row + SizeX > NeighbourhoodSizeX ||
		Column + SizeY > NeighbourhoodSizeY)
	{
		UE_LOG(LogTemp, Warning, TEXT("NeighbourhoodManager::CanPlaceLot - Lot at (%d, %d) size (%d, %d) exceeds neighbourhood bounds (%d, %d)"),
			Row, Column, SizeX, SizeY, NeighbourhoodSizeX, NeighbourhoodSizeY);
		return false;
	}

	// Check if any tiles are already claimed
	for (int32 R = Row; R < Row + SizeX; R++)
	{
		for (int32 C = Column; C < Column + SizeY; C++)
		{
			int32 PackedKey = (R << 16) | C;
			if (TileClaimMap.Contains(PackedKey))
			{
				UE_LOG(LogTemp, Warning, TEXT("NeighbourhoodManager::CanPlaceLot - Tile (%d, %d) already claimed!"), R, C);
				return false;
			}
		}
	}

	return true;
}

void ANeighbourhoodManager::CreateTerrainHole(int32 Row, int32 Column, int32 SizeX, int32 SizeY)
{
	if (!TerrainComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("NeighbourhoodManager::CreateTerrainHole - TerrainComponent is null!"));
		return;
	}

	// 1:1 tile scale - lot tiles match terrain tiles exactly
	UE_LOG(LogTemp, Warning, TEXT("NeighbourhoodManager: Creating terrain hole at tiles (%d, %d) size (%d, %d)"),
		Row, Column, SizeX, SizeY);

	// Hide all terrain tiles within the lot bounds
	int32 HiddenTiles = 0;
	for (int32 TileRow = Row; TileRow < Row + SizeX; TileRow++)
	{
		for (int32 TileCol = Column; TileCol < Column + SizeY; TileCol++)
		{
			// Find terrain tile in spatial map
			int32 PackedKey = (TileRow << 16) | TileCol;
			int32* TileIndexPtr = TerrainComponent->TerrainSpatialMap.Find(PackedKey);

			if (TileIndexPtr && TerrainComponent->TerrainDataArray.IsValidIndex(*TileIndexPtr))
			{
				FTerrainSegmentData& TerrainTile = TerrainComponent->TerrainDataArray[*TileIndexPtr];

				// Hide all quadrants of this tile
				TerrainTile.TileSectionState.Top = false;
				TerrainTile.TileSectionState.Right = false;
				TerrainTile.TileSectionState.Bottom = false;
				TerrainTile.TileSectionState.Left = false;

				HiddenTiles++;
			}
		}
	}

	// Rebuild terrain mesh to apply changes
	if (HiddenTiles > 0)
	{
		TerrainComponent->RebuildLevel(0);
		UE_LOG(LogTemp, Warning, TEXT("  - Hidden %d terrain tiles"), HiddenTiles);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("  - No terrain tiles found to hide!"));
	}
}

void ANeighbourhoodManager::FillTerrainHole(int32 Row, int32 Column, int32 SizeX, int32 SizeY)
{
	if (!TerrainComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("NeighbourhoodManager::FillTerrainHole - TerrainComponent is null!"));
		return;
	}

	// Check if any other lot still claims tiles in this area
	bool bStillClaimed = false;
	for (int32 R = Row; R < Row + SizeX && !bStillClaimed; R++)
	{
		for (int32 C = Column; C < Column + SizeY && !bStillClaimed; C++)
		{
			int32 PackedKey = (R << 16) | C;
			if (TileClaimMap.Contains(PackedKey))
			{
				bStillClaimed = true;
			}
		}
	}

	// Only fill hole if no other lot claims this area
	if (!bStillClaimed)
	{
		// 1:1 tile scale - lot tiles match terrain tiles exactly
		UE_LOG(LogTemp, Warning, TEXT("NeighbourhoodManager: Filling terrain hole at tiles (%d, %d) size (%d, %d)"),
			Row, Column, SizeX, SizeY);

		// Show all terrain tiles within the lot bounds
		int32 RestoredTiles = 0;
		for (int32 TileRow = Row; TileRow < Row + SizeX; TileRow++)
		{
			for (int32 TileCol = Column; TileCol < Column + SizeY; TileCol++)
			{
				// Find terrain tile in spatial map
				int32 PackedKey = (TileRow << 16) | TileCol;
				int32* TileIndexPtr = TerrainComponent->TerrainSpatialMap.Find(PackedKey);

				if (TileIndexPtr && TerrainComponent->TerrainDataArray.IsValidIndex(*TileIndexPtr))
				{
					FTerrainSegmentData& TerrainTile = TerrainComponent->TerrainDataArray[*TileIndexPtr];

					// Show all quadrants of this tile
					TerrainTile.TileSectionState.Top = true;
					TerrainTile.TileSectionState.Right = true;
					TerrainTile.TileSectionState.Bottom = true;
					TerrainTile.TileSectionState.Left = true;

					RestoredTiles++;
				}
			}
		}

		// Rebuild terrain mesh to apply changes
		if (RestoredTiles > 0)
		{
			TerrainComponent->RebuildLevel(0);
			UE_LOG(LogTemp, Warning, TEXT("  - Restored %d terrain tiles"), RestoredTiles);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("NeighbourhoodManager: Cannot fill hole at (%d, %d) - still claimed by another lot"),
			Row, Column);
	}
}

float ANeighbourhoodManager::GetTerrainHeightAt(FVector WorldLocation) const
{
	if (!TerrainComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("NeighbourhoodManager::GetTerrainHeightAt - TerrainComponent is null!"));
		return 0.0f;
	}

	// Convert world location to neighbourhood tile coordinates
	int32 TileRow, TileCol;
	if (!WorldToNeighbourhoodTile(WorldLocation, TileRow, TileCol))
	{
		// Location is outside neighbourhood bounds
		return 0.0f;
	}

	// 1:1 scale - tile coordinates match terrain tile coordinates directly
	int32 PackedKey = (TileRow << 16) | TileCol;
	const int32* TileIndexPtr = TerrainComponent->TerrainSpatialMap.Find(PackedKey);

	if (TileIndexPtr && TerrainComponent->TerrainDataArray.IsValidIndex(*TileIndexPtr))
	{
		const FTerrainSegmentData& TerrainTile = TerrainComponent->TerrainDataArray[*TileIndexPtr];

		// Return the Z height of the terrain tile center
		// Note: PointLoc is relative to neighbourhood manager, so add actor Z
		return TerrainTile.PointLoc.Z + GetActorLocation().Z;
	}

	// Fallback: return neighbourhood manager Z location
	return GetActorLocation().Z;
}

float ANeighbourhoodManager::GetTerrainHeightAtTile(int32 Row, int32 Column) const
{
	FVector TileCenter = NeighbourhoodTileToWorld(Row, Column);
	return GetTerrainHeightAt(TileCenter);
}

FVector ANeighbourhoodManager::NeighbourhoodTileToWorld(int32 Row, int32 Column) const
{
	FVector WorldLocation;

	// Calculate world position from tile coordinates
	// Center the grid around the neighbourhood manager's location
	WorldLocation.X = (Row - NeighbourhoodSizeX / 2.0f) * LotTileSize;
	WorldLocation.Y = (Column - NeighbourhoodSizeY / 2.0f) * LotTileSize;
	WorldLocation.Z = 0.0f;

	// Transform by neighbourhood manager's transform
	WorldLocation = GetActorTransform().TransformPosition(WorldLocation);

	return WorldLocation;
}

bool ANeighbourhoodManager::WorldToNeighbourhoodTile(FVector Location, int32& OutRow, int32& OutColumn) const
{
	// Transform world location to neighbourhood-local space
	FVector LocalLocation = GetActorTransform().InverseTransformPosition(Location);

	// Convert to tile coordinates
	OutRow = FMath::RoundToInt(LocalLocation.X / LotTileSize + NeighbourhoodSizeX / 2.0f);
	OutColumn = FMath::RoundToInt(LocalLocation.Y / LotTileSize + NeighbourhoodSizeY / 2.0f);

	// Validate bounds
	if (OutRow < 0 || OutRow >= NeighbourhoodSizeX ||
		OutColumn < 0 || OutColumn >= NeighbourhoodSizeY)
	{
		return false;
	}

	return true;
}

ALotManager* ANeighbourhoodManager::GetLotAtTile(int32 Row, int32 Column) const
{
	int32 PackedKey = (Row << 16) | Column;

	if (const ALotManager* const* FoundLot = TileClaimMap.Find(PackedKey))
	{
		return const_cast<ALotManager*>(*FoundLot);
	}

	return nullptr;
}

bool ANeighbourhoodManager::IsTileClaimed(int32 Row, int32 Column) const
{
	int32 PackedKey = (Row << 16) | Column;
	return TileClaimMap.Contains(PackedKey);
}

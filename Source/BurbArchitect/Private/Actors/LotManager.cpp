// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/LotManager.h"
#include "Actors/NeighbourhoodManager.h"
#include "Algo/MinElement.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Components/GableRoofComponent.h"
#include "Components/HipRoofComponent.h"
#include "Components/ShedRoofComponent.h"
#include "Components/RoomManagerComponent.h"
#include "Components/TerrainComponent.h"
#include "Components/WaterComponent.h"
#include "Components/FenceComponent.h"
#include "Actors/StairsBase.h"
#include "Actors/PortalBase.h"
#include "Actors/Roofs/RoofBase.h"
#include "Actors/Roofs/GableRoof.h"
#include "Actors/Roofs/HipRoof.h"
#include "Actors/Roofs/ShedRoof.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Actors/BurbPawn.h"
#include "Subsystems/BurbBasementViewSubsystem.h"
#include "Subsystems/BuildServer.h"
#include "Subsystems/LotSerializationSubsystem.h"
#include "SaveGame/LotSaveGame.h"
#include "Data/LotDataAsset.h"
#include "BurbArchitectDebug.h"
#include "DrawDebugHelpers.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#endif

// Sets default values
ALotManager::ALotManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true; // Enabled for debug drawing
	bReplicates = true;
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;

	// Create the grid component
	GridComponent = CreateDefaultSubobject<UGridComponent>(TEXT("GridComponent"));
	GridComponent->SetupAttachment(RootComponent);
	GridComponent->SetCastShadow(false);

	// Create wall component with basement view support
	WallComponent = CreateDefaultSubobject<UWallComponent>(TEXT("WallComponent"));
	WallComponent->SetupAttachment(RootComponent);
	WallComponent->SetCastHiddenShadow(true);

	// Create wall graph component for graph-based wall management
	WallGraph = CreateDefaultSubobject<UWallGraphComponent>(TEXT("WallGraph"));
	WallGraph->SetupAttachment(RootComponent);

	// Create room manager component for room detection and caching
	RoomManager = CreateDefaultSubobject<URoomManagerComponent>(TEXT("RoomManager"));

	// Create floor component with basement view support
	FloorComponent = CreateDefaultSubobject<UFloorComponent>(TEXT("FloorComponent"));
	FloorComponent->SetupAttachment(RootComponent);
	FloorComponent->SetCastHiddenShadow(true);

	// Create terrain component (ProceduralMesh)
	TerrainComponent = CreateDefaultSubobject<UTerrainComponent>(TEXT("TerrainComponent"));
	TerrainComponent->SetupAttachment(RootComponent);
	TerrainComponent->SetCastHiddenShadow(true);
	// Terrain is not visible in basement view (no custom depth stencil)

	// Create water component (ProceduralMesh for pool water volumes)
	WaterComponent = CreateDefaultSubobject<UWaterComponent>(TEXT("WaterComponent"));
	WaterComponent->SetupAttachment(RootComponent);

	// Create fence component (manages all fence segments on lot)
	FenceComponent = CreateDefaultSubobject<UFenceComponent>(TEXT("FenceComponent"));
	FenceComponent->SetupAttachment(RootComponent);

	// Roof components array is created on-demand when tools place roofs
	// No default components needed since each roof type gets its own component

	// Stairs actors array is managed dynamically when stairs are placed
	// No default components needed

	LotName = FText::FromString("Vacant Lot");
	GridTileSize = 100.0f;
	GridSizeX = 16;
	GridSizeY = 16;
	RoomCounter = 1;
	Floors = 3; // Temporarily set to 1 for debugging room IDs
	Basements = 1; // Default: Level 0 is basement, Level 1 is ground floor
	CurrentLevel = 1; // Default to ground floor (Level 1 when Basements = 1)

	// Initialize cached mode to Build (will be updated by OnBurbModeChanged when pawn spawns)
	CachedBurbMode = EBurbMode::Build;
}

void ALotManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALotManager, LotName);
	DOREPLIFETIME(ALotManager, GridSizeY);
	DOREPLIFETIME(ALotManager, GridSizeX);
	DOREPLIFETIME(ALotManager, Floors);
	DOREPLIFETIME(ALotManager, Basements);
	DOREPLIFETIME(ALotManager, CurrentLevel);
}

void ALotManager::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Auto-discover neighbourhood if not set
	if (!ParentNeighbourhood)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANeighbourhoodManager::StaticClass(), FoundActors);
		if (FoundActors.Num() > 0)
		{
			ParentNeighbourhood = Cast<ANeighbourhoodManager>(FoundActors[0]);
		}
	}

	// Auto-snap when dragged in editor (even if not placed yet)
	if (ParentNeighbourhood)
	{
		SnapToNeighbourhoodGrid();
	}

	// Terrain switching is now handled in PostEditChangeProperty
	// This just proceeds with whatever component exists

	// Check if we need to regenerate
	const bool bGridSizeChanged = (LastGeneratedGridSizeX != GridSizeX || LastGeneratedGridSizeY != GridSizeY);

	// Skip terrain/grid generation if lot is placed on neighbourhood
	// Terrain is generated via "Place Lot on Neighbourhood" button instead
	if (!bIsPlacedOnNeighbourhood)
	{
		// Check if terrain/grid actually exists (mesh sections aren't serialized, so they're lost on project load)
		bool bTerrainMissing = false;
		// For ProceduralMesh, terrain is missing if component is null OR no sections exist
		bTerrainMissing = !TerrainComponent || TerrainComponent->GetNumSections() == 0;

		const bool bGridDataMissing = GridData.Num() == 0;
		const bool bGridMeshMissing = GridComponent && GridComponent->GetNumSections() == 0;

		if (bGridSizeChanged || bTerrainMissing || bGridDataMissing || bGridMeshMissing)
		{
			//Generate Terrain (when grid size changes or terrain missing)
			GenerateTerrainComponents();

			//Generate GridData and GridMesh (when grid size changes or data missing)
			// GridMesh depends on GridData, so both must be called together
			GenerateGridData();
			GenerateGridMesh();

			// Update tracking variables after all components have regenerated
			LastGeneratedGridSizeX = GridSizeX;
			LastGeneratedGridSizeY = GridSizeY;
		}
	}
}


// Called when the game starts or when spawned
void ALotManager::BeginPlay()
{
	Super::BeginPlay();

	// Initialize to ground floor (Level == Basements)
	// This ensures we start at ground level, not basement
	SetCurrentLevel(Basements);

	// Check if we need to regenerate (same checks as OnConstruction)
	const bool bGridSizeChanged = (LastGeneratedGridSizeX != GridSizeX || LastGeneratedGridSizeY != GridSizeY);

	// Check terrain missing - works with both implementations
	bool bTerrainMissing = false;
	// For ProceduralMesh, terrain is missing if component is null OR no sections exist
	bTerrainMissing = !TerrainComponent || TerrainComponent->GetNumSections() == 0;
	
	const bool bGridDataMissing = GridData.Num() == 0;
	const bool bGridMeshMissing = GridComponent && GridComponent->GetNumSections() == 0;

	UE_LOG(LogTemp, Warning, TEXT("LotManager BeginPlay: Regeneration checks - GridSizeChanged=%d, TerrainMissing=%d, GridDataMissing=%d, GridMeshMissing=%d"),
		bGridSizeChanged, bTerrainMissing, bGridDataMissing, bGridMeshMissing);

	if (bGridSizeChanged || bTerrainMissing || bGridDataMissing || bGridMeshMissing)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotManager BeginPlay: Generating terrain and grid..."));

		// Generate terrain at runtime (mesh sections aren't serialized)
		GenerateTerrainComponents();

		// Generate grid data and mesh
		GenerateGridData();
		GenerateGridMesh();

		// Update tracking variables
		LastGeneratedGridSizeX = GridSizeX;
		LastGeneratedGridSizeY = GridSizeY;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("LotManager BeginPlay: Skipping terrain generation (already exists)"));
	}

	// Bind to local player's mode changes (client-side only)
	// Grid visibility is a client-side presentation concern, not replicated
	if (GetWorld() && GetWorld()->GetFirstLocalPlayerFromController())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->GetPawn())
		{
			if (ABurbPawn* BurbPawn = Cast<ABurbPawn>(PC->GetPawn()))
			{
				// Bind to mode changes
				BurbPawn->OnModeChanged.AddDynamic(this, &ALotManager::OnBurbModeChanged);

				// Apply initial mode state
				// This handles: 1) Pawns placed in level with default mode
				//               2) Pawns spawned via RestartPlayer (mode will be updated shortly after and delegate will fire again)
				OnBurbModeChanged(BurbPawn->CurrentMode, BurbPawn->CurrentMode);

				UE_LOG(LogTemp, Log, TEXT("LotManager: Bound to BurbPawn mode changes, initial mode: %d"), static_cast<int32>(BurbPawn->CurrentMode));
			}
		}
	}
}

void ALotManager::PostLoad()
{
	Super::PostLoad();

	if (!bEnableRoofGeneration && RoofComponents.Num() > 0)
	{
		for (URoofComponent* RoofComp : RoofComponents)
		{
			if (RoofComp)
			{
				RoofComp->DestroyComponent();
			}
		}
		RoofComponents.Empty();
	}

	// Regenerate terrain/grid if missing after load (procedural mesh sections aren't serialized)
	bool bTerrainMissing = false;
	
	// For ProceduralMesh, terrain is missing if component is null OR no sections exist
	bTerrainMissing = !TerrainComponent || TerrainComponent->GetNumSections() == 0;
	
	const bool bGridMeshMissing = GridComponent && GridComponent->GetNumSections() == 0;

	if (bTerrainMissing || bGridMeshMissing)
	{
		// Mark for regeneration in OnConstruction
		LastGeneratedGridSizeX = -1;
		LastGeneratedGridSizeY = -1;
	}
}

#if WITH_EDITOR
void ALotManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Get the name of the property that changed
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Check if GridSizeX or GridSizeY changed
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ALotManager, GridSizeX) ||
	    PropertyName == GET_MEMBER_NAME_CHECKED(ALotManager, GridSizeY))
	{
		UE_LOG(LogTemp, Warning, TEXT("LotManager: Grid size changed to %dx%d, forcing terrain regeneration"), GridSizeX, GridSizeY);

		// Force immediate regeneration to prevent coordinate mismatch
		// This ensures HeightMap dimensions match the new GridSizeX/Y BEFORE any rendering occurs
		GenerateTerrainComponents();
		GenerateGridData();
		GenerateGridMesh();

		// Update tracking variables to prevent duplicate regeneration in OnConstruction
		LastGeneratedGridSizeX = GridSizeX;
		LastGeneratedGridSizeY = GridSizeY;
	}
}

void ALotManager::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Only redraw on finished move to avoid performance issues during drag
	if (bFinished && GridComponent)
	{
		// Redraw boundary lines at the new location
		GridComponent->DrawBoundaryLines(GridWidth(), GridHeight(), GetActorLocation());
	}
}
#endif

// Called every frame
void ALotManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Debug draw room IDs continuously
	//DebugDrawRoomIDs(0.0f);
}

void ALotManager::GenerateGrid()
{
	GenerateGridData();
	GenerateGridMesh();
}

void ALotManager::GenerateGridData()
{
	// Iterative grid generation - only add/remove tiles as needed

	// Step 1: Remove tiles that are outside new bounds (iterate backwards to avoid index shifting)
	int32 RemovedCount = 0;
	for (int32 idx = GridData.Num() - 1; idx >= 0; idx--)
	{
		const FTileData& Tile = GridData[idx];

		// Remove if outside new bounds or above basement levels
		// Valid range: x in [-1, GridSizeX], y in [-1, GridSizeY], level in [0, Basements]
		if (Tile.Level > Basements ||
		    Tile.TileCoord.X < -1 || Tile.TileCoord.X > GridSizeX ||
		    Tile.TileCoord.Y < -1 || Tile.TileCoord.Y > GridSizeY)
		{
			GridData.RemoveAt(idx);
			RemovedCount++;
		}
	}

	// Step 2: Reindex remaining tiles and rebuild hash map
	for (int32 idx = 0; idx < GridData.Num(); idx++)
	{
		GridData[idx].TileIndex = idx;
	}
	RebuildTileIndexMap();

	// Step 3: Add new tiles that don't exist yet
	int32 AddedCount = 0;
	for (int32 i = 0; i <= Basements; i++)
	{
		for (int32 x = -1; x <= GridSizeX; x++)
		{
			for (int32 y = -1; y <= GridSizeY; y++)
			{
				// Check if tile already exists using hash map
				FIntVector Key(x, y, i);
				if (TileIndexMap.Contains(Key))
				{
					// Tile exists, update its position in case grid moved
					int32 ExistingIdx = TileIndexMap[Key];
					if (GridData.IsValidIndex(ExistingIdx))
					{
						FVector LocalCenter;
						TileToGridLocation(i, x, y, true, LocalCenter);
						GridData[ExistingIdx].Center = LocalCenter;
						GridData[ExistingIdx].CornerLocs = LocationToAllTileCorners(LocalCenter, i);
					}
					continue; // Skip to next tile
				}

				// Tile doesn't exist - create it
				FTileData Tile;
				Tile.TileCoord = {static_cast<double>(x), static_cast<double>(y)};
				Tile.Level = i;
				FVector LocalCenter;
				TileToGridLocation(i, Tile.TileCoord.X, Tile.TileCoord.Y, true, LocalCenter);
				Tile.Center = LocalCenter;

				// Calculate the tile's corners
				TArray<FVector> TempCorners = LocationToAllTileCorners(LocalCenter, i);
				Tile.CornerLocs = TempCorners;

				// Mark the 1 tile safe layer around our grid as out of bounds
				if (x == -1 || y == -1 || x == GridSizeX || y == GridSizeY)
				{
					Tile.bOutOfBounds = true;
					// Debug visualization: Draw red boxes at out-of-bounds tiles
				// Controlled by console command: burb.debug.tiles
				if (BurbArchitectDebug::IsTileDebugEnabled())
				{
					DrawDebugBox(GetWorld(), Tile.Center, {24,24,24}, FColor::Red, false, 1, 0, 3);
				}
				}

				// Save tile index for getter/setters
				Tile.TileIndex = GridData.Add(Tile);
				GridData[Tile.TileIndex].TileIndex = Tile.TileIndex;

				// Add to hash map
				TileIndexMap.Add(Key, Tile.TileIndex);
				AddedCount++;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("LotManager: Iterative grid update - Added %d tiles, Removed %d tiles, Total %d tiles (levels 0 to %d)"),
	       AddedCount, RemovedCount, GridData.Num(), Basements);
}

void ALotManager::GenerateGridMesh()
{
	if (!GridComponent)
		return;

	// GridData must exist for mesh generation (GenerateGridData should be called first)
	if (GridData.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotManager::GenerateGridMesh: Cannot generate mesh - GridData is empty. Call GenerateGridData first."));
		return;
	}

	// Generate the grid mesh using the GridComponent
	// Exclude ground floor (Basements level) from grid mesh generation
	GridComponent->GenerateGridMesh(GridData, GridSizeX, GridSizeY, GridTileSize, GridMaterial, Basements);

	// Draw boundary lines around the lot
	GridComponent->DrawBoundaryLines(GridWidth(), GridHeight(), GetActorLocation());

	// Hide all level sections initially, then show only the current level
	// Much more efficient than calling SetTileVisibility for every tile
	for (int32 Level = 0; Level < Floors; ++Level)
	{
		bool bShouldShow = (Level == CurrentLevel);
		GridComponent->SetLevelVisibility(Level, bShouldShow);
	}
}

void ALotManager::RebuildDirtyGridLevels()
{
	if (!GridComponent || DirtyGridLevels.Num() == 0)
		return;

	UE_LOG(LogTemp, Log, TEXT("LotManager::RebuildDirtyGridLevels - Rebuilding %d dirty levels"), DirtyGridLevels.Num());

	for (int32 Level : DirtyGridLevels)
	{
		// Skip basement and ground floor (they never receive new tiles during gameplay)
		if (Level <= Basements)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Skipping Level %d (basement/ground floor should never be dirty)"), Level);
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("  Rebuilding grid mesh for Level %d"), Level);
		GridComponent->RebuildGridLevel(Level, GridData, GridMaterial, Basements);

		// Restore grid visibility based on global state and current level
		// Grid visible = (bShowGrid == true) AND (Level == CurrentLevel)
		bool bShouldShow = bShowGrid && (Level == CurrentLevel);
		GridComponent->SetLevelVisibility(Level, bShouldShow);
	}

	DirtyGridLevels.Empty();
}

void ALotManager::RebuildTileIndexMap()
{
	TileIndexMap.Empty(GridData.Num());

	for (int32 i = 0; i < GridData.Num(); ++i)
	{
		const FTileData& Tile = GridData[i];
		FIntVector Key(Tile.TileCoord.X, Tile.TileCoord.Y, Tile.Level);
		TileIndexMap.Add(Key, i);
	}
}

void ALotManager::GenerateTilesAboveRoom(const TArray<FTileData>& RoomTiles, int32 TargetLevel)
{
	// For each tile in the room, create corresponding tile at TargetLevel
	// Ceilings provide support for tiles directly above

	for (const FTileData& RoomTile : RoomTiles)
	{
		// Check if tile already exists
		FTileData ExistingTile = FindTileByGridCoords(RoomTile.TileCoord.X, RoomTile.TileCoord.Y, TargetLevel);
		if (ExistingTile.TileIndex >= 0)
		{
			continue; // Already exists
		}

		// Create new tile at TargetLevel
		FTileData NewTile;
		NewTile.TileCoord = RoomTile.TileCoord;
		NewTile.Level = TargetLevel;
		NewTile.SetRoomID(0); // Default to outside
		NewTile.bSeeded = false;
		NewTile.bOutOfBounds = RoomTile.bOutOfBounds;

		// Calculate center and corners
		FVector LocalCenter;
		TileToGridLocation(TargetLevel, NewTile.TileCoord.X, NewTile.TileCoord.Y, true, LocalCenter);
		NewTile.Center = LocalCenter;
		NewTile.CornerLocs = LocationToAllTileCorners(LocalCenter, TargetLevel);

		// Add to GridData
		NewTile.TileIndex = GridData.Add(NewTile);
		GridData[NewTile.TileIndex].TileIndex = NewTile.TileIndex;

		// Update spatial hash map
		FIntVector Key(NewTile.TileCoord.X, NewTile.TileCoord.Y, TargetLevel);
		TileIndexMap.Add(Key, NewTile.TileIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("Generated %d tiles at Level %d (ceiling support from level below)"), RoomTiles.Num(), TargetLevel);
}

void ALotManager::GenerateAdjacentExpansionTiles(const TArray<FTileData>& RoomTiles, int32 Level)
{
	// For each tile in the room, generate tiles 1 cell adjacent
	// This allows players to expand by building walls/rooms next to existing rooms

	TSet<FIntVector> TilesToCreate; // Use set to avoid duplicates

	for (const FTileData& RoomTile : RoomTiles)
	{
		int32 Row = RoomTile.TileCoord.X;
		int32 Column = RoomTile.TileCoord.Y;

		// Add 8 adjacent tiles (including diagonals)
		TArray<FIntVector> AdjacentCoords = {
			FIntVector(Row-1, Column,   Level),  // Left
			FIntVector(Row+1, Column,   Level),  // Right
			FIntVector(Row,   Column-1, Level),  // Bottom
			FIntVector(Row,   Column+1, Level),  // Top
			FIntVector(Row-1, Column-1, Level),  // Bottom-Left
			FIntVector(Row-1, Column+1, Level),  // Top-Left
			FIntVector(Row+1, Column-1, Level),  // Bottom-Right
			FIntVector(Row+1, Column+1, Level),  // Top-Right
		};

		for (const FIntVector& Coord : AdjacentCoords)
		{
			// Check bounds
			if (Coord.X < -1 || Coord.X > GridSizeX || Coord.Y < -1 || Coord.Y > GridSizeY)
				continue;

			// Check if tile already exists
			FTileData ExistingTile = FindTileByGridCoords(Coord.X, Coord.Y, Level);
			if (ExistingTile.TileIndex >= 0)
				continue; // Already exists

			TilesToCreate.Add(Coord);
		}
	}

	// Create all unique adjacent tiles
	for (const FIntVector& Coord : TilesToCreate)
	{
		FTileData NewTile;
		NewTile.TileCoord = FVector2D(Coord.X, Coord.Y);
		NewTile.Level = Coord.Z;
		NewTile.SetRoomID(0); // Outside
		NewTile.bSeeded = false;
		NewTile.bOutOfBounds = (Coord.X == -1 || Coord.X == GridSizeX || Coord.Y == -1 || Coord.Y == GridSizeY);

		// Calculate center and corners
		FVector LocalCenter;
		TileToGridLocation(Coord.Z, Coord.X, Coord.Y, true, LocalCenter);
		NewTile.Center = LocalCenter;
		NewTile.CornerLocs = LocationToAllTileCorners(LocalCenter, Coord.Z);

		// Add to GridData
		NewTile.TileIndex = GridData.Add(NewTile);
		GridData[NewTile.TileIndex].TileIndex = NewTile.TileIndex;

		// Update spatial hash map
		FIntVector Key(Coord.X, Coord.Y, Coord.Z);
		TileIndexMap.Add(Key, NewTile.TileIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("Generated %d adjacent expansion tiles at Level %d"), TilesToCreate.Num(), Level);

	// Mark this level's grid as needing rebuild (only if tiles were actually added)
	if (TilesToCreate.Num() > 0)
	{
		DirtyGridLevels.Add(Level);
	}
}

float ALotManager::GridWidth() const
{
	return GridSizeY * GridTileSize;
}

float ALotManager::GridHeight() const
{
	return GridSizeX * GridTileSize;
}

FVector ALotManager::GridCenter() const
{
	FVector GridCenter;
	// Use Basements level to get ground floor center
	TileToGridLocation(Basements, GridSizeX/2, GridSizeY/2, true, GridCenter);
	return GridCenter;
}

void ALotManager::GenerateTerrainComponents()
{
	// Clear existing terrain and regenerate
	TerrainComponent->DestroyAllTerrain();

	// Initialize height map for ground level (Basements level = 0)
	const int32 GroundLevel = Basements;
	TerrainComponent->InitializeHeightMap(GroundLevel, GridSizeX, GridSizeY, GridTileSize, GetActorLocation().Z);
	UE_LOG(LogTemp, Log, TEXT("LotManager: Initialized ProceduralTerrain height map for level %d (grid %dx%d)"), GroundLevel, GridSizeX, GridSizeY);

	// Ensure the array has enough memory allocated
	TerrainComponent->TerrainDataArray.Reserve(GridSizeX * GridSizeY);

	// Use batch operation to prevent O(N²) rebuild triggers
	// Without this, each tile addition would trigger a full level rebuild
	TerrainComponent->BeginBatchOperation();

	for (int32 Row = 0; Row < GridSizeX; ++Row)
	{
		for (int32 Column = 0; Column < GridSizeY; ++Column)
		{
			FVector TerrainLoc;
			TileToGridLocation(Basements, Row, Column, true, TerrainLoc);

			// Generate terrain section
			FTerrainSegmentData NewTerrainSegment = TerrainComponent->GenerateTerrainSection(Row, Column, TerrainLoc, DefaultTerrainMaterial);

			// Generate mesh (marks level dirty but doesn't rebuild due to batch mode)
			TerrainComponent->GenerateTerrainMeshSection(NewTerrainSegment);
		}
	}

	// Rebuild all dirty levels once at the end (O(N) instead of O(N²))
	TerrainComponent->EndBatchOperation();
}


UMaterialInstanceDynamic* ALotManager::CreateMaterialInstance(const FLinearColor Colour, const float Opacity, FName Name)
{
	UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(GridMaterial, this, Name);
	DynamicMaterial->SetVectorParameterValue("Colour", Colour);
	DynamicMaterial->SetScalarParameterValue("Opacity", Opacity);
	return DynamicMaterial;
}

bool ALotManager::LocationToTile(const FVector Location, int32& Row, int32& Column) const
{
	const FVector ActorLocation = GetActorLocation();
	// Fixed: World X maps to Column, World Y maps to Row (consistent with TileToGridLocation)
	Column = UE4::SSE::FloorToInt32(GridSizeX * ((Location.X - ActorLocation.X) / GridWidth()));
	Row = UE4::SSE::FloorToInt32(GridSizeY * ((Location.Y - ActorLocation.Y) / GridHeight()));
	return TILE_VALID(Row, Column);
}

bool ALotManager::LocationToTileCorner(int32 Level, const FVector Location, FVector& NearestCorner)
{
	const FVector ActorLocation = GetActorLocation();

	// Calculate the closest row and column based on the location
	// Fixed: Use GridTileSize directly instead of incorrect dimensional calculation
	const float RelativeX = Location.X - ActorLocation.X;
	const float RelativeY = Location.Y - ActorLocation.Y;

	// Calculate exact grid coordinates (can be fractional)
	const float ExactRow = RelativeX / GridTileSize;
	const float ExactColumn = RelativeY / GridTileSize;

	// Add hysteresis: if we're very close to the current snap point, don't change
	// This prevents flickering when cursor is near tile boundaries (2can you .5% deadzone)
	const float HysteresisThreshold = 0.025f;

	int32 ClosestRow;
	int32 ClosestColumn;

	// Check if we have a previous corner to apply hysteresis to
	static FVector LastCorner = FVector::ZeroVector;
	const bool bHasLastCorner = !LastCorner.Equals(FVector::ZeroVector, 0.01f);

	if (bHasLastCorner)
	{
		const float LastRow = (LastCorner.X - ActorLocation.X) / GridTileSize;
		const float LastColumn = (LastCorner.Y - ActorLocation.Y) / GridTileSize;

		// If we're within hysteresis range of last snap point, keep it
		if (FMath::Abs(ExactRow - LastRow) < HysteresisThreshold &&
			FMath::Abs(ExactColumn - LastColumn) < HysteresisThreshold)
		{
			ClosestRow = FMath::RoundToInt32(LastRow);
			ClosestColumn = FMath::RoundToInt32(LastColumn);
		}
		else
		{
			ClosestRow = FMath::RoundToInt32(ExactRow);
			ClosestColumn = FMath::RoundToInt32(ExactColumn);
		}
	}
	else
	{
		ClosestRow = FMath::RoundToInt32(ExactRow);
		ClosestColumn = FMath::RoundToInt32(ExactColumn);
	}

	// Clamp ClosestRow and ClosestColumn to the boundaries
	ClosestRow = FMath::Clamp(ClosestRow, 0, GridSizeX);
	ClosestColumn = FMath::Clamp(ClosestColumn, 0, GridSizeY);

	// Now calculate the exact position of the nearest corner
	NearestCorner.X = ActorLocation.X + ClosestRow * GridTileSize;
	NearestCorner.Y = ActorLocation.Y + ClosestColumn * GridTileSize;

	// Calculate base Z from level
	float BaseZ = ActorLocation.Z + (DefaultWallHeight * (Level - Basements));

	// Adjust for terrain on ground level (use tile average - keeps walls level)
	// Note: Corner coordinates are at tile intersections, we use the tile to the bottom-left of the corner
	// For corner at (ClosestRow, ClosestColumn), the tile is at (ClosestRow-1, ClosestColumn-1) if valid
	int32 TileRow = FMath::Max(0, ClosestRow - 1);
	int32 TileColumn = FMath::Max(0, ClosestColumn - 1);
	NearestCorner.Z = GetTerrainAdjustedZ(Level, TileRow, TileColumn, BaseZ);

	// Cache this corner for hysteresis on next call
	LastCorner = NearestCorner;

	// Ensure the calculated corner is within the bounds of the grid
	// Fixed: X should be bounded by GridWidth(), Y by GridHeight()
	if (NearestCorner.X >= ActorLocation.X &&
		NearestCorner.X <= ActorLocation.X + GridWidth() &&
		NearestCorner.Y >= ActorLocation.Y &&
		NearestCorner.Y <= ActorLocation.Y + GridHeight())
	{
		return true;
	}

	return false;
}

//ouputs the corners in this order: bottom left, bottom right, top left, top right
TArray<FVector> ALotManager::LocationToAllTileCorners(const FVector Location, int32 Level)
{
	// Define the offsets for each corner from the center.
	TArray<FVector> Corners;
	float LevelHeight = DefaultWallHeight * (Level - Basements);

	// CRITICAL FIX: Always return 4 corners, even if bounds check fails
	// The old code would return empty array if LocationToTileCorner failed,
	// causing crashes when accessing CornerLocs[0]

	// Calculate corners directly without bounds checking
	const FVector ActorLocation = GetActorLocation();
	const float HalfTile = GridTileSize * 0.5f;
	const float Z = ActorLocation.Z + LevelHeight;

	// Always add exactly 4 corners
	Corners.Add(FVector(Location.X - HalfTile, Location.Y - HalfTile, Z)); // Bottom Left
	Corners.Add(FVector(Location.X + HalfTile, Location.Y - HalfTile, Z)); // Bottom Right
	Corners.Add(FVector(Location.X - HalfTile, Location.Y + HalfTile, Z)); // Top Left
	Corners.Add(FVector(Location.X + HalfTile, Location.Y + HalfTile, Z)); // Top Right

	return Corners;
}

FVector ALotManager::LocationToTileCenter(const FVector& Location)
{
	// Calculate the tile indices for the given location
	int32 TileRow = FMath::FloorToInt(Location.X / GridTileSize);
	int32 TileColumn = FMath::FloorToInt(Location.Y / GridTileSize);

	// Calculate the center of the tile
	FVector TileCenter(
		(TileRow + 0.5f) * GridTileSize,
		(TileColumn + 0.5f) * GridTileSize,
		Location.Z // Assuming the Z-axis remains unchanged
	);

	return TileCenter;
}

bool ALotManager::LocationToTileEdge(const FVector Location, FVector& TileEdge) const
{
	const FVector ActorLocation = GetActorLocation();
	int32 Row, Column;
	// Fixed: World X maps to Column, World Y maps to Row (consistent with TileToGridLocation)
	Column = UE4::SSE::FloorToInt32(GridSizeX * ((Location.X - ActorLocation.X) / GridWidth()));
	Row = UE4::SSE::FloorToInt32(GridSizeY * ((Location.Y - ActorLocation.Y) / GridHeight()));

	if (TILE_VALID(Row, Column))
	{
		// Calculate the edge of the grid tile (Column → X, Row → Y)
		TileEdge.X = ActorLocation.X + (Column * GridTileSize);
		TileEdge.Y = ActorLocation.Y + (Row * GridTileSize);
		TileEdge.Z = Location.Z;
		return true;
	}

	return false;
}

bool ALotManager::TileToGridLocation(int32 Level, const int32 Row, const int32 Column,bool bCenter, FVector& GridLocation) const
{
	const FVector Location = GetActorLocation();

	// Calculate base Z from level
	float BaseZ = Location.Z + (DefaultWallHeight * (Level - Basements));

	// Adjust for terrain on ground level (uses tile average - keeps floors level)
	float AdjustedZ = GetTerrainAdjustedZ(Level, Row, Column, BaseZ);

	if (bCenter)
	{
		// Fixed: Column → World X, Row → World Y
		GridLocation.X = GridTileSize / 2.f + Location.X + Column * GridTileSize;
		GridLocation.Y = GridTileSize / 2.f + Location.Y + Row * GridTileSize;
		GridLocation.Z = AdjustedZ;
	}
	else
	{
		// Fixed: Column → World X, Row → World Y
		GridLocation.X = Location.X + Column * GridTileSize;
		GridLocation.Y = Location.Y + Row * GridTileSize;
		GridLocation.Z = AdjustedZ;
	}
	return TILE_VALID(Row, Column);
}

bool ALotManager::IsOverlappingExistingWallOrActor(FWallSegmentData WallData)
{
	// Grid-based check: detect crossing diagonal walls on the same tile
	// This is more reliable than sphere traces for diagonal wall validation
	if (IsCrossingDiagonalWall(WallData.StartLoc, WallData.EndLoc, WallData.Level))
	{
		return true;
	}

	return false;
}

bool ALotManager::IsCrossingDiagonalWall(const FVector& StartLoc, const FVector& EndLoc, int32 Level) const
{
	// Only check diagonal walls (both X and Y differ)
	int32 NewStartRow, NewStartCol, NewEndRow, NewEndCol;
	if (!const_cast<ALotManager*>(this)->LocationToTile(StartLoc, NewStartRow, NewStartCol) ||
		!const_cast<ALotManager*>(this)->LocationToTile(EndLoc, NewEndRow, NewEndCol))
	{
		return false;
	}

	// Check if the new wall is diagonal
	const bool bNewIsDiagonal = (NewStartRow != NewEndRow) && (NewStartCol != NewEndCol);
	if (!bNewIsDiagonal)
	{
		return false; // Only diagonal walls can cross other diagonals
	}

	// Find the tile this diagonal occupies (use min row/col)
	const int32 TileRow = FMath::Min(NewStartRow, NewEndRow);
	const int32 TileCol = FMath::Min(NewStartCol, NewEndCol);

	// Determine the diagonal type of the new wall:
	// Type A (main diagonal): bottom-left to top-right, i.e., (Row,Col) <-> (Row+1,Col+1)
	// Type B (anti diagonal): top-left to bottom-right, i.e., (Row+1,Col) <-> (Row,Col+1)
	//
	// For Type A: StartRow < EndRow implies StartCol < EndCol (or vice versa)
	// For Type B: StartRow < EndRow implies StartCol > EndCol (or vice versa)
	const bool bNewIsMainDiagonal = ((NewStartRow < NewEndRow) == (NewStartCol < NewEndCol));

	// Query WallGraph for existing edges in this tile
	if (!WallGraph)
	{
		return false;
	}

	TArray<int32> EdgesInTile = WallGraph->GetEdgesInTile(TileRow, TileCol, Level);

	for (int32 EdgeID : EdgesInTile)
	{
		const FWallEdge* Edge = WallGraph->Edges.Find(EdgeID);
		if (!Edge || !Edge->bCommitted)
		{
			continue;
		}

		// Check if this existing edge is also diagonal
		if (!Edge->IsDiagonal())
		{
			continue;
		}

		// Check if it occupies the same tile
		const int32 ExistingTileRow = FMath::Min(Edge->StartRow, Edge->EndRow);
		const int32 ExistingTileCol = FMath::Min(Edge->StartColumn, Edge->EndColumn);

		if (ExistingTileRow != TileRow || ExistingTileCol != TileCol)
		{
			continue; // Different tile
		}

		// Determine the diagonal type of the existing wall
		const bool bExistingIsMainDiagonal = ((Edge->StartRow < Edge->EndRow) == (Edge->StartColumn < Edge->EndColumn));

		// If they're different diagonal types, they cross!
		if (bNewIsMainDiagonal != bExistingIsMainDiagonal)
		{
			return true; // Crossing detected
		}
	}

	return false;
}

bool ALotManager::ContainsVectorWithZInRange(const TArray<FVector>& VectorArray, const FVector& TargetVector, float MinZ, float MaxZ)
{
	for (const FVector& Vec : VectorArray)
	{
		if (Vec.Z >= MinZ && Vec.Z <= MaxZ)
		{
			if(Vec.Y == TargetVector.Y && Vec.X == TargetVector.X)
			{
				return true;
			}
		}
	}
	return false;
}

//Get the 2 tiles adjacent to a wall using grid coordinates (optimized)
TArray<FTileData> ALotManager::GetTilesAdjacentToWall(const FVector& StartCorner, const FVector& EndCorner, int32 Level) const
{
	TArray<FTileData> Tiles;
	Tiles.Reserve(2); // Walls typically touch 2 tiles (or 1 for diagonal)

	// Convert world positions to grid coordinates
	int32 StartRow, StartCol, EndRow, EndCol;
	if (!const_cast<ALotManager*>(this)->LocationToTile(StartCorner, StartRow, StartCol) ||
		!const_cast<ALotManager*>(this)->LocationToTile(EndCorner, EndRow, EndCol))
	{
		return Tiles;
	}

	// For axis-aligned walls (horizontal or vertical)
	if (StartCorner.X == EndCorner.X || StartCorner.Y == EndCorner.Y)
	{
		// Check tiles on both sides of the wall
		// Horizontal wall (along Y axis)
		if (StartCorner.X == EndCorner.X)
		{
			const int32 Col = FMath::Min(StartCol, EndCol);
			Tiles.Add(FindTileByGridCoords(StartRow, Col, Level));
			Tiles.Add(FindTileByGridCoords(StartRow, Col + 1, Level));
		}
		// Vertical wall (along X axis)
		else
		{
			const int32 Row = FMath::Min(StartRow, EndRow);
			Tiles.Add(FindTileByGridCoords(Row, StartCol, Level));
			Tiles.Add(FindTileByGridCoords(Row + 1, StartCol, Level));
		}
	}
	// For diagonal walls
	else
	{
		// Diagonal walls occupy a single tile
		const int32 Row = FMath::Min(StartRow, EndRow);
		const int32 Col = FMath::Min(StartCol, EndCol);
		Tiles.Add(FindTileByGridCoords(Row, Col, Level));
	}

	// Remove invalid tiles
	Tiles.RemoveAll([](const FTileData& Tile) { return Tile.TileIndex == -1; });

	return Tiles;
}

//Get the 2 tiles adjacent to a wall, if it bridges on an outside and inside tile return true
bool ALotManager::DoesBridgeOutsideInside(const FVector& StartCorner, const FVector& EndCorner) const
{
	TArray<FTileData> Tiles;
	FVector CenterLocation = (StartCorner + EndCorner) / 2; // Calculates the midpoint
	//Vertical & Horizontal Wall
	if (StartCorner.X == EndCorner.X || StartCorner.Y == EndCorner.Y)
	{
		Tiles.Empty();
		//For each grid tile
		for (int i = 0; i<GridData.Num(); i++)
		{
			//If the tile contains both the start and end loc in its corners then it contains the wall
			if(GridData[i].CornerLocs.Contains(StartCorner) && GridData[i].CornerLocs.Contains(EndCorner))
			{
				Tiles.Add(GridData[i]);
			}
		}
	}
	//Diagonal wall
	else
	{
		Tiles.Empty();
		//For each grid tile
		for (int i = 0; i<GridData.Num(); i++)
		{
			//If the tile center is the wall center then it contains the diagonal wall
			if(GridData[i].Center == CenterLocation)
			{
				Tiles.Add(GridData[i]);
			}
		}
	}
	//If the wall bridges both Outside and Inside tiles
	if(Tiles.Num()<2)
	{
		return false;
	}
	if((Tiles[0].GetPrimaryRoomID() == 0 && Tiles[1].GetPrimaryRoomID() != 0) || (Tiles[0].GetPrimaryRoomID() != 0 && Tiles[1].GetPrimaryRoomID() == 0))
	{
		return true;
	}
	return false;
}

//Check if a wall is between two given tiles.
bool ALotManager::IsWallBetweenTiles(const FTileData& TileA,const FTileData& TileB)
{
	// NEW: Use wall graph for deterministic grid-based queries
	if (WallGraph)
	{
		return WallGraph->IsWallBetweenTiles(
			TileA.TileCoord.X, TileA.TileCoord.Y,
			TileB.TileCoord.X, TileB.TileCoord.Y,
			TileA.Level
		);
	}

	// FALLBACK: Legacy vector-based implementation (deprecated)
	// This will be removed once wall graph migration is complete
	UE_LOG(LogTemp, Warning, TEXT("IsWallBetweenTiles: WallGraph not available, using legacy method"));

	// Check if tiles are on the same level
	if (TileA.Level != TileB.Level)
		return false;

	// Calculate movement direction
	int32 DeltaRow = TileB.TileCoord.X - TileA.TileCoord.X;
	int32 DeltaCol = TileB.TileCoord.Y - TileA.TileCoord.Y;

	for (int i = 0; i < WallComponent->WallDataArray.Num(); i++)
	{
		const FWallSegmentData& Wall = WallComponent->WallDataArray[i];

		if (Wall.Level != TileA.Level)
			continue;

		FVector StartCorner = Wall.StartLoc;
		FVector EndCorner = Wall.EndLoc;

		bool TileAContainsStart = ContainsVectorWithZInRange(TileA.CornerLocs, StartCorner, StartCorner.Z-6, StartCorner.Z+6);
		bool TileAContainsEnd = ContainsVectorWithZInRange(TileA.CornerLocs, EndCorner, EndCorner.Z-6, EndCorner.Z+6);
		bool TileBContainsStart = ContainsVectorWithZInRange(TileB.CornerLocs, StartCorner, StartCorner.Z-6, StartCorner.Z+6);
		bool TileBContainsEnd = ContainsVectorWithZInRange(TileB.CornerLocs, EndCorner, EndCorner.Z-6, EndCorner.Z+6);

		// Check for axis-aligned walls between tiles
		if ((TileAContainsStart && TileBContainsEnd) || (TileAContainsEnd && TileBContainsStart))
		{
			return true;
		}

		// Check for diagonal walls blocking diagonal movement
		bool bIsDiagonal = (Wall.StartLoc.X != Wall.EndLoc.X) && (Wall.StartLoc.Y != Wall.EndLoc.Y);
		bool bDiagonalMovement = (DeltaRow != 0 && DeltaCol != 0);

		if (bIsDiagonal && bDiagonalMovement)
		{
			// Check if diagonal wall in TileA blocks movement to TileB
			if (TileAContainsStart && TileAContainsEnd)
			{
				// Determine diagonal direction: ascending (/) or descending (\)
				FVector WallDir = EndCorner - StartCorner;
				bool bAscendingDiagonal = ((WallDir.X > 0) == (WallDir.Y > 0));

				// Check if movement crosses this diagonal
				bool bMovementAscending = ((DeltaRow > 0) == (DeltaCol > 0));

				// Movement crosses diagonal if trying to move in the opposite diagonal direction
				if (bAscendingDiagonal != bMovementAscending)
				{
					return true;
				}
			}

			// Check if diagonal wall in TileB blocks movement from TileA
			if (TileBContainsStart && TileBContainsEnd)
			{
				// Determine diagonal direction
				FVector WallDir = EndCorner - StartCorner;
				bool bAscendingDiagonal = ((WallDir.X > 0) == (WallDir.Y > 0));

				// Check if movement crosses this diagonal
				bool bMovementAscending = ((DeltaRow > 0) == (DeltaCol > 0));

				// Movement crosses diagonal if trying to move in the opposite diagonal direction
				if (bAscendingDiagonal != bMovementAscending)
				{
					return true;
				}
			}
		}
	}
	return false;
}

//Check if a tile contains a diagonal wall section across it
bool ALotManager::HasDiagonalWall(FTileData TileA)
{
	// Check committed walls in the main WallComponent
	for (const FWallSegmentData& WallData : WallComponent->WallDataArray)
	{
		if (!WallData.bCommitted || WallData.Level != TileA.Level)
		{
			continue;
		}

		bool TileAContainsStart = TileA.CornerLocs.Contains(WallData.StartLoc);
		bool TileAContainsEnd = TileA.CornerLocs.Contains(WallData.EndLoc);

		// Check if this is a diagonal wall (X and Y both change)
		bool bIsDiagonal = (WallData.StartLoc.X != WallData.EndLoc.X) && (WallData.StartLoc.Y != WallData.EndLoc.Y);

		// If tile contains both start and end of a diagonal wall
		if (TileAContainsStart && TileAContainsEnd && bIsDiagonal)
		{
			return true;
		}
	}
	return false;
}

FBox ALotManager::CreateBoundingBox(const FVector& Start, const FVector& End) const
{
	FVector MinPoint(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y), 0);
	FVector MaxPoint(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y), 0);
	return FBox(MinPoint, MaxPoint);
}

FVector ALotManager::SnapToPrimaryDirection(const FVector& Vec) const
{
	// Initialize the result vector with Z explicitly set to 0.0f
	FVector Result(0.0f, 0.0f, 0.0f);
	// Snap X and Y components to the nearest integer value within the range of -1 to 1
	
	Result.X = FMath::Clamp(FMath::RoundToInt(Vec.X), -1, 1);
	Result.Y = FMath::Clamp(FMath::RoundToInt(Vec.Y), -1, 1);
	// Conditionally reset Y to zero if no clear primary direction in the XY plane
	if (FMath::Abs(Result.X + Result.Y) != 1)
	{
		Result.Y = 0;
	}
	 
	return Result;
}

bool ALotManager::FindTerrain(const int32 Row, const int32 Column, FTerrainSegmentData*& OutTerrainSegmentData)
{
	if (!TerrainComponent)
	{
		return false;
	}

	for (int i = 0; i < TerrainComponent->TerrainDataArray.Num(); i++)
	{
		if(TerrainComponent->TerrainDataArray[i].Row == Row && TerrainComponent->TerrainDataArray[i].Column == Column)
		{
			OutTerrainSegmentData = &TerrainComponent->TerrainDataArray[i];
			return true;
		}
	}
	return false;
}

bool ALotManager::FindTerrainWithVector(FVector &Location, FTerrainSegmentData*& OutTerrainSegmentData)
{
	Location = LocationToTileCenter(Location);
	
	if (!TerrainComponent)
	{
		return false;
	}

	for (int i = 0; i < TerrainComponent->TerrainDataArray.Num(); i++)
	{
		if(TerrainComponent->TerrainDataArray[i].PointLoc.X == Location.X && TerrainComponent->TerrainDataArray[i].PointLoc.Y == Location.Y)
		{
			Location.Z = TerrainComponent->TerrainDataArray[i].PointLoc.Z;
			OutTerrainSegmentData = &TerrainComponent->TerrainDataArray[i];
			return true;
		}
	}

	return false;
}

float ALotManager::CalculateCenterSpan(int32 outRow, int32 outColumn, int32 x, int32 y, float Radius) const
{
	// Calculate the Euclidean distance once
	float Distance = FMath::Sqrt(FMath::Pow((outRow - x) * GridTileSize, 2) + FMath::Pow((outColumn - y) * GridTileSize, 2));

	if (Distance >= Radius)
	{
		return 0.0f;
	}

    // Normalize the Distance by the Radius (reuse calculated distance instead of recalculating)
    float NormalizedSpan = Distance / Radius;

    // Perform linear interpolation (Lerp) to get the CenterSpan
    const float CenterSpan = FMath::Lerp(10.0f, 0.0f, NormalizedSpan);

    return FMath::RoundToFloat(CenterSpan);
}

FVector ALotManager::GetMouseWorldPosition(float Distance) const
{
	if (const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		FVector WorldLocation, WorldDirection;

		PlayerController->DeprojectMousePositionToWorld(WorldLocation, WorldDirection);
		
		return WorldLocation + (WorldDirection * Distance);
	}
	return FVector::ZeroVector;
}

FVector ALotManager::MagnetizeToTileCenter(const FVector& Location, FVector& TargetLocation)
{
	const float Speed = 15.0f;
	int32 outRow=0;
	int32 outColumn=0;
	if (LocationToTile(Location, outRow, outColumn))
	{
		// Use Basements level to magnetize to ground floor
		TileToGridLocation(Basements, outRow, outColumn,true, TargetLocation);
	
		const FVector Direction = (TargetLocation - Location).GetSafeNormal();
		const FVector NewLocation = Location + Direction * Speed;

		if (FVector::Dist(NewLocation, TargetLocation) <= Speed)
		{
			return TargetLocation; // Snap to the target location when close enough
		}

		return NewLocation;
	}
	return FVector::ZeroVector;
}

bool ALotManager::AreDirectionsAligned(const FVector& DirA, const FVector& DirB)
{
	return FMath::Abs(FVector::DotProduct(DirA, DirB)) > (1.0f - KINDA_SMALL_NUMBER);
}

bool ALotManager::AreLinesIntersecting2D(const FVector& Start1, const FVector& End1, const FVector& Start2, const FVector& End2)
{
	// Project points onto 2D plane (ignore Z component)
	FVector2D A(Start1.X, Start1.Y);
	FVector2D B(End1.X, End1.Y);
	FVector2D C(Start2.X, Start2.Y);
	FVector2D D(End2.X, End2.Y);

	// Calculate the direction of the lines
	FVector2D AB = B - A;
	FVector2D CD = D - C;

	// Compute determinants to check if lines are collinear
	float Denominator = (AB.X * CD.Y - AB.Y * CD.X);

	// If denominator is zero, lines are parallel (or collinear) and do not intersect
	if (FMath::IsNearlyZero(Denominator, KINDA_SMALL_NUMBER))
	{
		return false;
	}

	// Calculate intersection factors
	FVector2D AC = C - A;
	float T1 = (AC.X * CD.Y - AC.Y * CD.X) / Denominator;
	float T2 = (AC.X * AB.Y - AC.Y * AB.X) / Denominator;

	// The lines intersect if T1 and T2 are in range [0,1]
	return (T1 >= 0.0f && T1 <= 1.0f) && (T2 >= 0.0f && T2 <= 1.0f);
}

bool ALotManager::IsPointOnLineSegment(const FVector& A, const FVector& B, const FVector& Z, const float Tolerance = 1e-4f)
{
	// Direction vectors
	FVector ABDir = (B - A).GetSafeNormal();
	FVector AZDir = (Z - A).GetSafeNormal();
	
	if (A == Z || B == Z)
	{
		return true;
	}
	
	// Check if Z lies on the same line as A and B
	if (!ABDir.Equals(AZDir, Tolerance) && !ABDir.Equals(-AZDir, Tolerance))
	{
		return false; // Z is not on the line defined by A and B
	}

	// Check if Z is within the segment range
	float MinX = FMath::Min(A.X, B.X);
	float MaxX = FMath::Max(A.X, B.X);
	float MinY = FMath::Min(A.Y, B.Y);
	float MaxY = FMath::Max(A.Y, B.Y);
	float MinZ = FMath::Min(A.Z, B.Z);
	float MaxZ = FMath::Max(A.Z, B.Z);

	if (Z.X >= MinX && Z.X <= MaxX &&
		Z.Y >= MinY && Z.Y <= MaxY &&
		Z.Z >= MinZ && Z.Z <= MaxZ)
	{
		return true; // Z is within the bounds of the segment
	}
	
	return false; // Z is outside the segment bounds
}

bool ALotManager::IsPointInArea2D(const FVector& A, const FVector& B, const FVector& Point)
{
	FVector Center = (A + B) * 0.5f;
	FVector AB = B - A;
	FVector AP = Point - A;

	float DotAB = FVector::DotProduct(AP, AB);
	float LengthSquaredAB = AB.SizeSquared();

	// Check if projection falls within the segment length
	if (DotAB < 0.0f || DotAB > LengthSquaredAB)
	{
		return false;
	}

	// Perpendicular check
	FVector AB_Normal = FVector(-AB.Y, AB.X, 0.0f); // Perpendicular vector
	FVector PC = Point - Center;
	float Distance = FVector::DotProduct(PC, AB_Normal.GetSafeNormal());

	// Check if point is within half the diameter on the perpendicular axis
	return FMath::Abs(Distance) <= AB.Size() * 0.5f;
}

FVector ALotManager::GetWallFacingDirection(const FVector& WallStart, const FVector& WallEnd)
{
	// Get wall direction vector
	FVector WallDirection = (WallEnd - WallStart).GetSafeNormal();

	// Calculate perpendicular normal (rotate 90 degrees right in 2D)
	// This gives us the "right-hand" normal of the wall
	FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();

	return WallNormal;
}

void ALotManager::RebuildWallChains()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALotManager::RebuildWallChains);

	WallChains.Empty();

	if (!WallComponent)
		return;

	TArray<int32> ProcessedWalls;

	for (int32 i = 0; i < WallComponent->WallDataArray.Num(); i++)
	{
		if (ProcessedWalls.Contains(i))
			continue;

		FWallSegmentData& StartWall = WallComponent->WallDataArray[i];
		if (!StartWall.bCommitted)
			continue;

		// Start new chain
		FWallChains NewChain;
		NewChain.Level = StartWall.Level;
		NewChain.Direction = (StartWall.EndLoc - StartWall.StartLoc).GetSafeNormal();
		NewChain.WallIndices.Add(i);
		ProcessedWalls.Add(i);

		// Find all connected, aligned walls
		TArray<int32> ToProcess = {i};

		while (ToProcess.Num() > 0)
		{
			int32 CurrentIdx = ToProcess.Pop();
			FWallSegmentData& CurrentWall = WallComponent->WallDataArray[CurrentIdx];

			// Check all walls for connections
			for (int32 j = 0; j < WallComponent->WallDataArray.Num(); j++)
			{
				if (ProcessedWalls.Contains(j))
					continue;

				FWallSegmentData& TestWall = WallComponent->WallDataArray[j];
				if (!TestWall.bCommitted || TestWall.Level != NewChain.Level)
					continue;

				// Check if aligned (same direction within tolerance)
				FVector TestDir = (TestWall.EndLoc - TestWall.StartLoc).GetSafeNormal();
				if (!TestDir.Equals(NewChain.Direction, 0.01f) && !TestDir.Equals(-NewChain.Direction, 0.01f))
					continue;

				// Check if connected (shares endpoint)
				bool bConnected =
					CurrentWall.StartLoc.Equals(TestWall.StartLoc, 1.0f) ||
					CurrentWall.StartLoc.Equals(TestWall.EndLoc, 1.0f) ||
					CurrentWall.EndLoc.Equals(TestWall.StartLoc, 1.0f) ||
					CurrentWall.EndLoc.Equals(TestWall.EndLoc, 1.0f);

				if (bConnected)
				{
					NewChain.WallIndices.Add(j);
					ProcessedWalls.Add(j);
					ToProcess.Add(j);
				}
			}
		}

		// Calculate chain endpoints by projecting along direction
		float MinProj = TNumericLimits<float>::Max();
		float MaxProj = TNumericLimits<float>::Lowest();

		for (int32 WallIdx : NewChain.WallIndices)
		{
			FWallSegmentData& Wall = WallComponent->WallDataArray[WallIdx];

			float StartProj = FVector::DotProduct(Wall.StartLoc, NewChain.Direction);
			float EndProj = FVector::DotProduct(Wall.EndLoc, NewChain.Direction);

			MinProj = FMath::Min(MinProj, FMath::Min(StartProj, EndProj));
			MaxProj = FMath::Max(MaxProj, FMath::Max(StartProj, EndProj));
		}

		NewChain.StartLoc = NewChain.Direction * MinProj;
		NewChain.EndLoc = NewChain.Direction * MaxProj;

		WallChains.Add(NewChain);
	}

	UE_LOG(LogTemp, Log, TEXT("RebuildWallChains: Built %d wall chains from %d committed walls"),
		WallChains.Num(), WallComponent->WallDataArray.Num());
}

int32 ALotManager::FindWallChainForSegment(int32 WallSegmentIndex) const
{
	for (int32 i = 0; i < WallChains.Num(); i++)
	{
		if (WallChains[i].WallIndices.Contains(WallSegmentIndex))
		{
			return i;
		}
	}

	return -1; // Not found
}

// Remake this in Research
float ALotManager::CalculatePointOnLineDistance(const FVector& CameraLocation, const FVector& WidgetLocation, const FVector& MovementLocation)
{
	const float Distance = FVector::Dist(CameraLocation, WidgetLocation);

	const FVector Direction = (MovementLocation - CameraLocation).GetSafeNormal();

	const FVector D = CameraLocation + (Direction * Distance);

	return FVector::Dist(D, WidgetLocation);
}

TArray<FTileData> ALotManager::GetNeighboringTiles(const FTileData StartTile, int32 Level)
{
	TArray<FTileData> Neighbors;

	// Add 4 orthogonal neighbors (for axis-aligned walls)
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X - 1, StartTile.TileCoord.Y), Level));     // Left
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X + 1, StartTile.TileCoord.Y), Level));     // Right
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X, StartTile.TileCoord.Y - 1), Level));     // Bottom
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X, StartTile.TileCoord.Y + 1), Level));     // Top

	// Add 4 diagonal neighbors (for diagonal walls and rooms)
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X - 1, StartTile.TileCoord.Y - 1), Level)); // Bottom-Left
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X - 1, StartTile.TileCoord.Y + 1), Level)); // Top-Left
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X + 1, StartTile.TileCoord.Y - 1), Level)); // Bottom-Right
	Neighbors.Add(FindTileByCoord(FVector2D(StartTile.TileCoord.X + 1, StartTile.TileCoord.Y + 1), Level)); // Top-Right

	return Neighbors;
}

FTileData ALotManager::FindTileByCoord(FVector2D TileCoord, int32 Level)
{
	// Use optimized grid coordinate lookup
	return FindTileByGridCoords(TileCoord.X, TileCoord.Y, Level);
}

// Optimized O(1) lookup using hash map
FTileData ALotManager::FindTileByGridCoords(int32 Row, int32 Column, int32 Level) const
{
	FIntVector Key(Row, Column, Level);
	const int32* IndexPtr = TileIndexMap.Find(Key);

	if (IndexPtr && GridData.IsValidIndex(*IndexPtr))
	{
		return GridData[*IndexPtr];
	}

	return FTileData(); // Return default if not found
}

int32 ALotManager::GetRoomAtTile(int32 Level, int32 Row, int32 Column) const
{
	// Use the optimized grid coordinate lookup
	FTileData Tile = FindTileByGridCoords(Row, Column, Level);

	// Return the room ID (0 = outside/no room, >0 = inside a room)
	// Works with diagonal walls because flood-fill assigns RoomIDs to all enclosed tiles
	return Tile.GetPrimaryRoomID();
}

FTileData ALotManager::GetTileDataAtIndex(int32 Index)
{
	return GridData[Index];
}

bool ALotManager::SetTileDataAtIndex(int32 Index, FTileData NewTileData)
{
	if(GridData.IsValidIndex(Index))
	{
		GridData[Index] = NewTileData;
		return true;
	}
	return false;
}

//This uses a depth first algo, if we detect a boundary tile we cancel the search and assume we're outside (no room)
void ALotManager::DebugDrawRoomIDs(float Duration)
{
	if (!GetWorld())
		return;

	for (const FTileData& Tile : GridData)
	{
		// Skip out of bounds tiles
		if (Tile.bOutOfBounds)
			continue;

		// Only draw level 0 for now
		if (Tile.Level != 0)
			continue;

		// Draw room ID above tile center
		if (BurbArchitectDebug::IsLabelDebugEnabled())
		{
			FVector TextLocation = Tile.Center + FVector(0, 0, 100.0f);
			FString RoomText = FString::Printf(TEXT("%d"), Tile.GetPrimaryRoomID());

			// Color code: 0 = Red (outside), >0 = Green (rooms)
			FColor TextColor = (Tile.GetPrimaryRoomID() == 0) ? FColor::Red : FColor::Green;

			DrawDebugString(GetWorld(), TextLocation, RoomText, nullptr, TextColor, Duration, true, 2.0f);
		}
	}
}

// Creates a temporary preview wall component for build tools
// Note: This is a transient component that should be destroyed when the tool completes
UWallComponent* ALotManager::GenerateWallSegment(int32 Level, const FVector& TileCornerStart, const FVector& TileCornerEnd, bool& bValidPlacement, float InWallHeight)
{
	UWallComponent* NewWallComponent;
	NewWallComponent = NewObject<UWallComponent>(this, UWallComponent::StaticClass(), NAME_None, RF_Transient);
	NewWallComponent->SetupAttachment(GetRootComponent());
	NewWallComponent->RegisterComponent();
	AddInstanceComponent(NewWallComponent);
	NewWallComponent->SetWorldLocation(TileCornerStart);
	NewWallComponent->SetCastShadow(false);

	FWallSegmentData NewWallData;
	NewWallData.StartLoc = TileCornerStart;
	NewWallData.EndLoc = TileCornerEnd;
	NewWallData.Height = InWallHeight;
	NewWallData.Level = Level;

	// Grid coordinates are no longer stored in FWallSegmentData
	// They are managed by WallGraphComponent instead

	// Validate placement - check overlap and height compatibility
	// Note: Use NewWallData here, not NewWallComponent->WallData (which hasn't been assigned yet)
	bValidPlacement = !IsOverlappingExistingWallOrActor(NewWallData);

	// Check terrain flatness on ground level (allows up to 5 units height difference)
	if (bValidPlacement && Level == Basements)
	{
		bValidPlacement = IsTerrainFlatForWall(Level, TileCornerStart, TileCornerEnd);
	}

	// Check wall height compatibility (prevent walls at different elevations from connecting)
	if (bValidPlacement && Level == Basements)
	{
		bValidPlacement = IsWallHeightValidForConnection(TileCornerStart, TileCornerEnd, Level);
	}

	NewWallComponent->WallData = NewWallData;
	NewWallComponent->WallMaterial = !bValidPlacement ? InvalidPreviewMaterial : ValidPreviewMaterial;
	NewWallComponent->bValidPlacement = bValidPlacement;
	NewWallComponent->GenerateWallMesh();

	return NewWallComponent;
}

// Creates a temporary preview roof component for build tools
// Note: This is a transient component that should be destroyed when the tool completes
// Creates specialized component based on roof type and adds it to the RoofComponents array
URoofComponent* ALotManager::GenerateRoofSegment(const FVector& Location, const FVector& Direction, const FRoofDimensions &RoofDimensions, const float RoofThickness, const float GableThickness, UMaterialInstance* ValidMaterial)
{
    URoofComponent* NewRoofComponent = nullptr;

	// Create specialized roof component based on roof type
	switch (RoofDimensions.RoofType)
	{
		case ERoofType::Gable:
			NewRoofComponent = NewObject<UGableRoofComponent>(this, UGableRoofComponent::StaticClass(), NAME_None, RF_Transient);
			break;
		case ERoofType::Hip:
			NewRoofComponent = NewObject<UHipRoofComponent>(this, UHipRoofComponent::StaticClass(), NAME_None, RF_Transient);
			break;
		case ERoofType::Shed:
			NewRoofComponent = NewObject<UShedRoofComponent>(this, UShedRoofComponent::StaticClass(), NAME_None, RF_Transient);
			break;
		default:
			// Fallback to base Gable roof component if type is unknown
			NewRoofComponent = NewObject<UGableRoofComponent>(this, UGableRoofComponent::StaticClass(), NAME_None, RF_Transient);
			break;
	}

	// Setup and register the component
	NewRoofComponent->SetupAttachment(GetRootComponent());
	NewRoofComponent->RegisterComponent();
	AddInstanceComponent(NewRoofComponent);
	NewRoofComponent->SetWorldLocation(Location + (FVector::UpVector * 2.0f));

	// Setup roof data
	FRoofSegmentData NewRoofData;
	NewRoofData.Location = Location;
	NewRoofData.Direction = Direction;
	NewRoofData.Dimensions = RoofDimensions;
    NewRoofData.RoofThickness = RoofThickness;
    NewRoofData.GableThickness = GableThickness;
	NewRoofData.SectionIndex = -1;
	NewRoofData.LotManager = this; // Pass LotManager reference for wall generation

	NewRoofComponent->RoofData = NewRoofData;
	NewRoofComponent->RoofMaterial = ValidMaterial;
	NewRoofComponent->LotManager = this; // Set LotManager on component for wall cleanup
	// Use GenerateRoofMeshSection which calls the virtual override for each roof type
	NewRoofComponent->RoofData = NewRoofComponent->GenerateRoofMeshSection(NewRoofData);

	// Add to the roof components array
	RoofComponents.Add(NewRoofComponent);

	return NewRoofComponent;
}

// Removes a specific roof component from the array and destroys it
void ALotManager::RemoveRoofComponent(URoofComponent* RoofComp)
{
	if (RoofComp)
	{
		RoofComponents.Remove(RoofComp);
		RoofComp->DestroyComponent();
	}
}

// Clears all roof components from the array and destroys them
void ALotManager::ClearAllRoofComponents()
{
	for (URoofComponent* RoofComp : RoofComponents)
	{
		if (RoofComp)
		{
			RoofComp->DestroyComponent();
		}
	}
	RoofComponents.Empty();
}

// Finds the first roof component of a specific type
URoofComponent* ALotManager::GetRoofComponentByType(ERoofType RoofType) const
{
	for (URoofComponent* RoofComp : RoofComponents)
	{
		if (RoofComp && RoofComp->RoofData.Dimensions.RoofType == RoofType)
		{
			return RoofComp;
		}
	}
	return nullptr;
}

// Returns the number of roof components currently in the array
int32 ALotManager::GetRoofComponentCount() const
{
	return RoofComponents.Num();
}

// NEW ROOF ACTOR METHODS

// Spawns a roof actor based on roof type and initializes it
ARoofBase* ALotManager::SpawnRoofActor(const FVector& Location, const FVector& Direction,
	const FRoofDimensions& RoofDimensions, const float RoofThickness,
	const float GableThickness, UMaterialInstance* ValidMaterial)
{
	// Get spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;
	SpawnParams.Owner = this;

	// Determine which roof actor class to spawn based on type
	TSubclassOf<ARoofBase> RoofClass = nullptr;
	switch (RoofDimensions.RoofType)
	{
	case ERoofType::Gable:
		RoofClass = AGableRoof::StaticClass();
		break;
	case ERoofType::Hip:
		RoofClass = AHipRoof::StaticClass();
		break;
	case ERoofType::Shed:
		RoofClass = AShedRoof::StaticClass();
		break;
	default:
		RoofClass = AGableRoof::StaticClass(); // Default to gable
		break;
	}

	if (!RoofClass)
	{
		UE_LOG(LogTemp, Error, TEXT("LotManager: Failed to determine roof actor class"));
		return nullptr;
	}

	// Spawn the roof actor
	ARoofBase* NewRoofActor = GetWorld()->SpawnActor<ARoofBase>(
		RoofClass,
		Location,
		FRotator::ZeroRotator,
		SpawnParams
	);

	if (NewRoofActor)
	{
		// Initialize the roof
		NewRoofActor->InitializeRoof(
			this,
			Location,
			Direction,
			RoofDimensions,
			RoofThickness,
			GableThickness
		);

		// Apply material
		if (ValidMaterial)
		{
			NewRoofActor->RoofMaterial = ValidMaterial;
			if (NewRoofActor->RoofMeshComponent)
			{
				NewRoofActor->RoofMeshComponent->SetMaterial(0, ValidMaterial);
			}
		}

		// Add to the roof actors array
		RoofActors.Add(NewRoofActor);

		UE_LOG(LogTemp, Log, TEXT("LotManager: Spawned %s roof actor at location (%s)"),
			*UEnum::GetValueAsString(RoofDimensions.RoofType),
			*Location.ToString());
	}

	return NewRoofActor;
}

// Removes a specific roof actor from the array and destroys it
void ALotManager::RemoveRoofActor(ARoofBase* RoofActor)
{
	if (RoofActor)
	{
		RoofActors.Remove(RoofActor);
		RoofActor->Destroy();
	}
}

// Clears all roof actors from the array and destroys them
void ALotManager::ClearAllRoofActors()
{
	for (ARoofBase* RoofActor : RoofActors)
	{
		if (RoofActor)
		{
			RoofActor->Destroy();
		}
	}
	RoofActors.Empty();
}

// Finds the first roof actor of a specific type
ARoofBase* ALotManager::GetRoofActorByType(ERoofType RoofType) const
{
	for (ARoofBase* RoofActor : RoofActors)
	{
		if (RoofActor && RoofActor->GetRoofType() == RoofType)
		{
			return RoofActor;
		}
	}
	return nullptr;
}

// Returns the number of roof actors currently in the array
int32 ALotManager::GetRoofActorCount() const
{
	return RoofActors.Num();
}

// NEW STAIRS ACTOR METHODS

// Spawns a stairs actor and initializes it
AStairsBase* ALotManager::SpawnStairsActor(TSubclassOf<AStairsBase> StairsClass, const FVector& StartLocation, const FVector& Direction,
	const TArray<FStairModuleStructure>& Structures, UStaticMesh* StairTreadMesh, UStaticMesh* StairLandingMesh,
	float StairsThickness)
{
	// Validate the stairs class
	if (!StairsClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotManager: No stairs class specified, using default AStairsBase"));
		StairsClass = AStairsBase::StaticClass();
	}

	// Get spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;
	SpawnParams.Owner = this;

	// Spawn the stairs actor using the specified class (allows Blueprint child classes)
	AStairsBase* NewStairsActor = GetWorld()->SpawnActor<AStairsBase>(
		StairsClass,
		StartLocation,
		Direction.Rotation(),
		SpawnParams
	);

	if (NewStairsActor)
	{
		// Initialize the stairs actor with the provided parameters
		NewStairsActor->InitializeStairs(
			this,
			StartLocation,
			Direction,
			Structures,
			StairTreadMesh,
			StairLandingMesh,
			StairsThickness
		);

		// Add to the stairs actors array
		StairsActors.Add(NewStairsActor);

		UE_LOG(LogTemp, Log, TEXT("LotManager: Spawned stairs actor at location (%s)"), *StartLocation.ToString());
	}

	return NewStairsActor;
}

// Removes a specific stairs actor from the array and destroys it
void ALotManager::RemoveStairsActor(AStairsBase* StairsActor)
{
	if (StairsActor)
	{
		StairsActors.Remove(StairsActor);
		StairsActor->Destroy();
	}
}

// Clears all stairs actors from the array and destroys them
void ALotManager::ClearAllStairsActors()
{
	for (AStairsBase* StairsActor : StairsActors)
	{
		if (StairsActor)
		{
			StairsActor->Destroy();
		}
	}
	StairsActors.Empty();
}

// Returns the number of stairs actors currently in the array
int32 ALotManager::GetStairsActorCount() const
{
	return StairsActors.Num();
}

// Finds a stairs actor at a specific location with matching structure
AStairsBase* ALotManager::FindStairsActorAtLocation(const FVector& Location, const TArray<FStairModuleStructure>& Structures)
{
	for (AStairsBase* StairsActor : StairsActors)
	{
		if (StairsActor && StairsActor->bCommitted)
		{
			// Check if location and structures match
			// Use actor's world location since StairsData.StartLoc is now in local space
			if (StairsActor->GetActorLocation().Equals(Location, 1.0f) &&
				StairsActor->StairsData.Structures == Structures)
			{
				return StairsActor;
			}
		}
	}
	return nullptr;
}

// Creates a temporary preview floor component for build tools
// Note: This is a transient component that should be destroyed when the tool completes
UFloorComponent* ALotManager::GenerateFloorSegment(int32 Level, const FVector& TileCenter, UMaterialInstance* OptionalMaterial, const FTileSectionState& TileSectionState)
{
	UFloorComponent* NewFloorComponent;
	NewFloorComponent = NewObject<UFloorComponent>(this, UFloorComponent::StaticClass(), NAME_None, RF_Transient);
	NewFloorComponent->SetupAttachment(GetRootComponent());
	NewFloorComponent->RegisterComponent();
	AddInstanceComponent(NewFloorComponent);

	NewFloorComponent->FloorData.Level = Level;
	NewFloorComponent->FloorData.StartLoc = TileCenter;
	NewFloorComponent->FloorData.CornerLocs = LocationToAllTileCorners(TileCenter, Level);

	// Apply Z offset to all corners to match tile center offset (for preview Z-fighting prevention)
	// Calculate expected grid Z for this tile center
	FVector ExpectedGridCenter;
	int32 Row, Column;
	if (LocationToTile(TileCenter, Row, Column))
	{
		TileToGridLocation(Level, Row, Column, true, ExpectedGridCenter);
		float ZOffset = TileCenter.Z - ExpectedGridCenter.Z;
		for (FVector& Corner : NewFloorComponent->FloorData.CornerLocs)
		{
			Corner.Z += ZOffset;
		}
	}

	NewFloorComponent->FloorData.TileSectionState = TileSectionState;

	// Store grid coordinates for reliable lookups
	LocationToTile(TileCenter, NewFloorComponent->FloorData.Row, NewFloorComponent->FloorData.Column);

	NewFloorComponent->SetWorldLocation(TileCenter);
	NewFloorComponent->FloorData.Width = GridTileSize;

	// Check terrain flatness on ground level (allows up to 5 units height difference)
	bool bValidPlacement = true;
	if (Level == Basements)
	{
		bValidPlacement = IsTerrainFlatAtTile(Level, NewFloorComponent->FloorData.Row, NewFloorComponent->FloorData.Column);
	}

	NewFloorComponent->FloorMaterial = bValidPlacement ?
		(IsValid(OptionalMaterial) ? OptionalMaterial : ValidPreviewMaterial) :
		InvalidPreviewMaterial;
	NewFloorComponent->bValidPlacement = bValidPlacement;
	NewFloorComponent->GenerateFloorMesh();

	return NewFloorComponent;
}

void ALotManager::BrushTerrainRaise(const int32 Row, const int32 Column, const TArray<float> Spans, UMaterialInstance* OptionalMaterial)
{
	FTerrainSegmentData* Terrain = nullptr;

	if(!FindTerrain(Row, Column, Terrain))
	{
		return;
	}

	// Reset flatten flag since terrain is being modified
	Terrain->bFlatten = false;

	// Calculate raise amounts for each corner based on neighboring spans
	float CornerSpan1 = FMath::RoundToFloat(pow(Spans[3]*Spans[4]*Spans[6]*Spans[7], 0.25f)); // BottomRight (index 1)
	float CornerSpan2 = FMath::RoundToFloat(pow(Spans[0]*Spans[1]*Spans[3]*Spans[4], 0.25f)); // TopRight (index 3)
	float CornerSpan3 = FMath::RoundToFloat(pow(Spans[4]*Spans[5]*Spans[7]*Spans[8], 0.25f)); // BottomLeft (index 0)
	float CornerSpan4 = FMath::RoundToFloat(pow(Spans[1]*Spans[2]*Spans[4]*Spans[5], 0.25f)); // TopLeft (index 2)

	const int32 Level = Terrain->Level;

	// Get or create the height map to read current corner heights (source of truth)
	FTerrainHeightMap* HeightMap = TerrainComponent->GetOrCreateHeightMap(Level);
	if (!HeightMap)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushTerrainRaise: Failed to get or create height map for level %d"), Level);
		return;
	}

	// Update each corner using the height map system
	// Read current heights from height map (not stale CornerLocs!)
	if (CornerSpan3 > 0.0f) // BottomLeft
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row, Column);
		TerrainComponent->UpdateCornerHeight(Level, Row, Column, CurrentHeight + CornerSpan3);
	}

	if (CornerSpan1 > 0.0f) // BottomRight
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row, Column + 1);
		TerrainComponent->UpdateCornerHeight(Level, Row, Column + 1, CurrentHeight + CornerSpan1);
	}

	if (CornerSpan4 > 0.0f) // TopLeft
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row + 1, Column);
		TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column, CurrentHeight + CornerSpan4);
	}

	if (CornerSpan2 > 0.0f) // TopRight
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row + 1, Column + 1);
		TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column + 1, CurrentHeight + CornerSpan2);
	}

	// Clear flatten flag
	Terrain->bFlatten = false;

	// UpdateCornerHeight automatically handles incremental mesh updates via UpdateDirtyVertices
	// No need to call GenerateTerrainMeshSection - it would trigger a full RebuildLevel!
}

void ALotManager::BrushTerrainLower(const int32 Row, const int32 Column, TArray<float> Spans, UMaterialInstance *OptionalMaterial)
{
	FTerrainSegmentData* Terrain = nullptr;

	if(!FindTerrain(Row, Column, Terrain))
	{
		return;
	}

	// Reset flatten flag since terrain is being modified
	Terrain->bFlatten = false;

	// Calculate lower amounts for each corner based on neighboring spans
	float CornerSpan1 = FMath::RoundToFloat(pow(Spans[3]*Spans[4]*Spans[6]*Spans[7], 0.25f)); // BottomRight (index 1)
	float CornerSpan2 = FMath::RoundToFloat(pow(Spans[0]*Spans[1]*Spans[3]*Spans[4], 0.25f)); // TopRight (index 3)
	float CornerSpan3 = FMath::RoundToFloat(pow(Spans[4]*Spans[5]*Spans[7]*Spans[8], 0.25f)); // BottomLeft (index 0)
	float CornerSpan4 = FMath::RoundToFloat(pow(Spans[1]*Spans[2]*Spans[4]*Spans[5], 0.25f)); // TopLeft (index 2)

	const int32 Level = Terrain->Level;

	// Get or create the height map to read current corner heights (source of truth)
	FTerrainHeightMap* HeightMap = TerrainComponent->GetOrCreateHeightMap(Level);
	if (!HeightMap)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushTerrainLower: Failed to get or create height map for level %d"), Level);
		return;
	}

	// Update each corner using the height map system (subtract for lowering)
	// Read current heights from height map (not stale CornerLocs!)
	if (CornerSpan3 > 0.0f) // BottomLeft
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row, Column);
		TerrainComponent->UpdateCornerHeight(Level, Row, Column, CurrentHeight - CornerSpan3);
	}

	if (CornerSpan1 > 0.0f) // BottomRight
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row, Column + 1);
		TerrainComponent->UpdateCornerHeight(Level, Row, Column + 1, CurrentHeight - CornerSpan1);
	}

	if (CornerSpan4 > 0.0f) // TopLeft
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row + 1, Column);
		TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column, CurrentHeight - CornerSpan4);
	}

	if (CornerSpan2 > 0.0f) // TopRight
	{
		float CurrentHeight = HeightMap->GetCornerHeight(Row + 1, Column + 1);
		TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column + 1, CurrentHeight - CornerSpan2);
	}

	// Clear flatten flag
	Terrain->bFlatten = false;

	// UpdateCornerHeight automatically handles incremental mesh updates via UpdateDirtyVertices
	// No need to call GenerateTerrainMeshSection - it would trigger a full RebuildLevel!
}

void ALotManager::BrushTerrainFlatten(const int32 Row, const int32 Column, const float TargetHeight)
{
	FTerrainSegmentData* Terrain = nullptr;

	if(!FindTerrain(Row, Column, Terrain))
	{
		return;
	}

	if (!TerrainComponent)
	{
		return;
	}

	const int32 Level = Terrain->Level;

	// Get or create the height map to update corner heights
	FTerrainHeightMap* HeightMap = TerrainComponent->GetOrCreateHeightMap(Level);
	if (!HeightMap)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushTerrainFlatten: Failed to get or create height map for level %d"), Level);
		return;
	}

	// Set all 4 corners of this tile to the target height
	// This will automatically update neighboring tiles that share these corners
	TerrainComponent->UpdateCornerHeight(Level, Row, Column, TargetHeight);             // BottomLeft
	TerrainComponent->UpdateCornerHeight(Level, Row, Column + 1, TargetHeight);         // BottomRight
	TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column, TargetHeight);         // TopLeft
	TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column + 1, TargetHeight);     // TopRight

	// Mark terrain as flattened
	Terrain->bFlatten = true;
}

bool ALotManager::IsTerrainFlatAtTile(int32 Level, int32 Row, int32 Column) const
{
	// Only validate terrain on ground level (Basements)
	// Upper levels don't have terrain, so they're always "flat" for this check
	if (Level != Basements)
	{
		return true;
	}

	// Check if TerrainComponent exists
	if (!TerrainComponent)
	{
		return true; // No terrain component = no terrain = flat
	}

	// Get the height map for this level
	const FTerrainHeightMap* HeightMap = TerrainComponent->GetHeightMap(Level);
	if (!HeightMap)
	{
		return true; // No height map = no terrain = flat
	}

	// Get all 4 corner heights for this tile
	// Corner coordinates:
	// BottomLeft: (Row, Column)
	// BottomRight: (Row, Column+1)
	// TopLeft: (Row+1, Column)
	// TopRight: (Row+1, Column+1)
	const float BLHeight = HeightMap->GetCornerHeight(Row, Column);
	const float BRHeight = HeightMap->GetCornerHeight(Row, Column + 1);
	const float TLHeight = HeightMap->GetCornerHeight(Row + 1, Column);
	const float TRHeight = HeightMap->GetCornerHeight(Row + 1, Column + 1);

	// Check for acceptable flatness - allow up to 5 units height difference
	// This provides leniency for slightly uneven terrain while blocking very sloped areas
	const float MaxHeightDifference = 5.0f;

	// Find min and max corner heights
	const float MinHeight = FMath::Min(FMath::Min(BLHeight, BRHeight), FMath::Min(TLHeight, TRHeight));
	const float MaxHeight = FMath::Max(FMath::Max(BLHeight, BRHeight), FMath::Max(TLHeight, TRHeight));

	// Check if height difference is within tolerance
	const bool bIsFlat = (MaxHeight - MinHeight) <= MaxHeightDifference;

	return bIsFlat;
}

bool ALotManager::IsTerrainFlatForWall(int32 Level, const FVector& StartLoc, const FVector& EndLoc) const
{
	// Only validate terrain on ground level (Basements)
	if (Level != Basements)
	{
		return true;
	}

	// Get the tiles adjacent to this wall segment
	TArray<FTileData> AdjacentTiles = GetTilesAdjacentToWall(StartLoc, EndLoc, Level);

	// A wall requires flat terrain on BOTH adjacent tiles
	// If there's only one adjacent tile (edge of lot), just check that one
	// If there are no adjacent tiles, return true (shouldn't happen, but be safe)
	if (AdjacentTiles.Num() == 0)
	{
		return true;
	}

	// Check flatness for each adjacent tile
	for (const FTileData& Tile : AdjacentTiles)
	{
		// FTileData uses TileCoord (FVector2D) where X = Row, Y = Column
		if (!IsTerrainFlatAtTile(Level, static_cast<int32>(Tile.TileCoord.X), static_cast<int32>(Tile.TileCoord.Y)))
		{
			return false; // At least one adjacent tile has sloped terrain
		}
	}

	return true; // All adjacent tiles have flat terrain
}

float ALotManager::GetTerrainAdjustedZ(int32 Level, int32 Row, int32 Column, float DefaultZ) const
{
	// Only adjust for ground level (Basements)
	if (Level != Basements)
	{
		return DefaultZ;
	}

	// Check if terrain component exists
	if (!TerrainComponent)
	{
		return DefaultZ; // No terrain = use default
	}

	// Only adjust if terrain is flat at this tile (required for placement)
	if (!IsTerrainFlatAtTile(Level, Row, Column))
	{
		return DefaultZ; // Not flat = use default
	}

	// Get average terrain height at this tile's 4 corners
	float TerrainHeight = TerrainComponent->SampleTerrainElevation(Level, Row, Column);

	// Return lot base Z + terrain elevation offset
	return GetActorLocation().Z + TerrainHeight;
}

bool ALotManager::IsWallHeightValidForConnection(const FVector& StartLoc, const FVector& EndLoc, int32 Level) const
{
	// If WallGraph doesn't exist, allow placement (no existing walls to conflict with)
	if (!WallGraph)
	{
		return true;
	}

	// Height tolerance for comparison (small epsilon for floating-point errors)
	const float HeightTolerance = 1.0f;

	// Check both start and end positions for existing nodes at different heights
	TArray<FVector> PositionsToCheck = {StartLoc, EndLoc};

	for (const FVector& Position : PositionsToCheck)
	{
		// Convert world position to grid coordinates for spatial lookup
		int32 Row, Column;
		if (!LocationToTile(Position, Row, Column))
		{
			continue; // Invalid position, skip
		}

		// Query WallGraph for nodes at this grid position and level
		int64 TileKey = (static_cast<int64>(Level) << 32) | (static_cast<int64>(Row) << 16) | static_cast<int64>(Column);
		TArray<int32> NodeIDsAtTile;
		WallGraph->TileToNodes.MultiFind(TileKey, NodeIDsAtTile);

		// Check each node at this position
		for (int32 NodeID : NodeIDsAtTile)
		{
			const FWallNode* Node = WallGraph->Nodes.Find(NodeID);
			if (!Node || Node->Level != Level)
			{
				continue; // Node doesn't exist or wrong level
			}

			// Check if this node is close to our proposed position (within tolerance)
			if (FVector::Dist2D(Node->Position, Position) > GridTileSize * 0.1f)
			{
				continue; // Too far away, not the same connection point
			}

			// Found an existing node at this position - check its Z height
			if (!FMath::IsNearlyEqual(Node->Position.Z, Position.Z, HeightTolerance))
			{
				// Existing node has different Z height - wall cannot connect
				return false;
			}
		}
	}

	return true; // No height conflicts found
}

void ALotManager::ToggleGrid(bool bVisible)
{
	// Set global grid visibility state
	bShowGrid = bVisible;

	// Apply visibility to all levels
	// Rule: Grid visible = (bShowGrid == true) AND (Level == CurrentLevel)
	for (int32 Level = 0; Level < Floors; Level++)
	{
		bool bShouldShow = bVisible && (Level == CurrentLevel);

		// Update GridComponent mesh section visibility
		if (GridComponent)
		{
			GridComponent->SetLevelVisibility(Level, bShouldShow);
		}

		// Update FloorComponent ShowGrid parameter (per-pattern mesh sections)
		if (FloorComponent)
		{
			// Use actual section indices from LevelToSectionIndices map
			if (TArray<int32>* SectionIndices = FloorComponent->LevelToSectionIndices.Find(Level))
			{
				for (int32 SectionIndex : *SectionIndices)
				{
					UMaterialInterface* FloorMat = FloorComponent->GetMaterial(SectionIndex);
					if (FloorMat)
					{
						UMaterialInstanceDynamic* DynamicFloorMat = Cast<UMaterialInstanceDynamic>(FloorMat);
						if (DynamicFloorMat)
						{
							DynamicFloorMat->SetScalarParameterValue(FName("ShowGrid"), bShouldShow ? 1.0f : 0.0f);
						}
					}
				}
			}
		}

		// Update TerrainComponent ShowGrid parameter
		// Terrain ONLY exists on ground floor (Basements), so only update when we're on that level
		if (TerrainComponent && Level == Basements)
		{
			// Terrain always uses section 0 (top surface), regardless of level number
			int32 TopSectionIndex = UTerrainComponent::GetTopSectionIndex(Level);
			UMaterialInterface* TerrainMat = TerrainComponent->GetMaterial(TopSectionIndex);
			if (TerrainMat)
			{
				// Check if already dynamic, otherwise create dynamic instance
				UMaterialInstanceDynamic* DynamicTerrainMat = Cast<UMaterialInstanceDynamic>(TerrainMat);
				if (!DynamicTerrainMat)
				{
					// Create dynamic instance from static material
					if (UMaterialInstance* StaticMat = Cast<UMaterialInstance>(TerrainMat))
					{
						DynamicTerrainMat = UMaterialInstanceDynamic::Create(StaticMat, TerrainComponent);
						TerrainComponent->SetMaterial(TopSectionIndex, DynamicTerrainMat);
					}
				}

				if (DynamicTerrainMat)
				{
					DynamicTerrainMat->SetScalarParameterValue(FName("ShowGrid"), bShouldShow ? 1.0f : 0.0f);
				}
			}
		}
	}
}

void ALotManager::ToggleAllGrid(bool bVisible)
{
	// Don't allow toggling grid in Live mode - grid should always be hidden
	if (CachedBurbMode == EBurbMode::Live)
	{
		UE_LOG(LogTemp, Warning, TEXT("LotManager: Cannot toggle grid in Live mode"));
		return;
	}

	if (GridComponent)
	{
		GridComponent->SetGridVisibility(bVisible);
	}
}

void ALotManager::OnBurbModeChanged(EBurbMode OldMode, EBurbMode NewMode)
{
	// Cache the new mode for later checks (e.g., ToggleAllGrid)
	CachedBurbMode = NewMode;

	// Update grid visibility based on mode
	// Live mode: Hide grid and boundary lines
	// Build/Buy modes: Show grid per normal logic
	const bool bShowGridInNewMode = (NewMode != EBurbMode::Live);
	const bool bHideBoundaries = (NewMode == EBurbMode::Live);

	// Update global grid visibility state
	bShowGrid = bShowGridInNewMode;

	// Update grid component visibility (includes boundary lines)
	if (GridComponent)
	{
		GridComponent->SetGridVisibility(bShowGrid, bHideBoundaries);

		// Refresh per-level visibility based on new bShowGrid state
		for (int32 Level = 0; Level < Basements + Floors; ++Level)
		{
			bool bShouldShow = bShowGrid && (Level == CurrentLevel);
			GridComponent->SetLevelVisibility(Level, bShouldShow);
		}

		// When exiting Live mode, redraw boundary lines
		if (OldMode == EBurbMode::Live && NewMode != EBurbMode::Live)
		{
			GridComponent->DrawBoundaryLines(GridWidth(), GridHeight(), GetActorLocation());
		}
	}

	// Update floor grid line visibility via material parameters (per-pattern mesh sections)
	if (FloorComponent)
	{
		for (int32 Level = 0; Level < Basements + Floors; Level++)
		{
			// Use actual section indices from LevelToSectionIndices map
			if (TArray<int32>* SectionIndices = FloorComponent->LevelToSectionIndices.Find(Level))
			{
				for (int32 SectionIndex : *SectionIndices)
				{
					UMaterialInterface* SectionMaterial = FloorComponent->GetMaterial(SectionIndex);
					if (SectionMaterial)
					{
						UMaterialInstanceDynamic* DynamicMat = Cast<UMaterialInstanceDynamic>(SectionMaterial);
						if (DynamicMat)
						{
							// Update ShowGrid parameter: only current level should show grid (if globally enabled)
							bool bShowGridForLevel = bShowGrid && (Level == CurrentLevel);
							DynamicMat->SetScalarParameterValue(FName("ShowGrid"), bShowGridForLevel ? 1.0f : 0.0f);
						}
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("LotManager: Mode changed from %d to %d, grid visibility now: %s"),
		static_cast<int32>(OldMode), static_cast<int32>(NewMode), bShowGrid ? TEXT("ON") : TEXT("OFF"));
}

void ALotManager::SetCurrentLevel(int32 NewLevel)
{
	// Store the current level
	CurrentLevel = NewLevel;

	// Sync all player pawns to the new level
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Iterator->Get();
		if (PC)
		{
			APawn* Pawn = PC->GetPawn();
			if (ABurbPawn* BurbPawn = Cast<ABurbPawn>(Pawn))
			{
				BurbPawn->CurrentLevel = NewLevel;
			}
		}
	}

	// Auto-enable basement view mode when viewing basement levels
	// Level < Basements means we're in a basement
	SetBasementViewMode(NewLevel < Basements);

	// Hide terrain when viewing basement levels (terrain is always on ground floor)
	if (TerrainComponent)
	{
		bool bShowTerrain = (NewLevel >= Basements); // Only show terrain on ground floor and above
		TerrainComponent->SetVisibility(bShowTerrain);
	}

	// Update grid visibility per level
	// Grid visible = (bShowGrid == true) AND (Level == CurrentLevel)
	// Uses efficient per-level visibility instead of per-tile
	if (GridComponent)
	{
		for (int32 Level = 0; Level < Basements + Floors; ++Level)
		{
			bool bShouldShow = bShowGrid && (Level == NewLevel);
			GridComponent->SetLevelVisibility(Level, bShouldShow);
		}
	}

	// Update floor visibility (per-pattern mesh sections - multiple materials per level)
	if (FloorComponent)
	{
		for (int32 Level = 0; Level < Basements + Floors; Level++)
		{
			// Use actual section indices from LevelToSectionIndices map
			if (TArray<int32>* SectionIndices = FloorComponent->LevelToSectionIndices.Find(Level))
			{
				for (int32 SectionIndex : *SectionIndices)
				{
					UMaterialInterface* SectionMaterial = FloorComponent->GetMaterial(SectionIndex);
					if (SectionMaterial)
					{
						UMaterialInstanceDynamic* DynamicMat = Cast<UMaterialInstanceDynamic>(SectionMaterial);
						if (DynamicMat)
						{
							// Set visibility parameter: 0.0 if floor is above current level, 1.0 otherwise
							float VisibilityValue = (Level > NewLevel) ? 0.0f : 1.0f;
							DynamicMat->SetScalarParameterValue(FName("VisibilityLevel"), VisibilityValue);

							// Update ShowGrid parameter: only current level should show grid (if globally enabled)
							bool bShowGridForLevel = bShowGrid && (Level == NewLevel);
							DynamicMat->SetScalarParameterValue(FName("ShowGrid"), bShowGridForLevel ? 1.0f : 0.0f);
						}
					}
				}
			}
		}

		// Disable visibility collision for floors above current level
		// This allows clicking through hidden floors to interact with objects inside rooms
		// Note: ProceduralMeshComponent doesn't support per-section collision, so this is an
		// all-or-nothing approach. We disable ECC_Visibility when viewing lower levels to allow
		// clicking through upper floors. This works because:
		// - Tools trace with ECC_Visibility to detect floors
		// - When viewing lower levels, we want to trace through upper floors
		// - Upper floors are visually hidden via material VisibilityLevel parameter

		// Check if there are any floors above current level that would be hidden
		bool bHasHiddenFloorsAbove = false;
		for (const FFloorTileData& TileData : FloorComponent->FloorTileDataArray)
		{
			if (TileData.Level > NewLevel)
			{
				bHasHiddenFloorsAbove = true;
				break;
			}
		}

		// If we have hidden floors above, disable visibility collision to allow clicking through
		if (bHasHiddenFloorsAbove)
		{
			FloorComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			UE_LOG(LogTemp, Log, TEXT("LotManager: Disabled floor visibility collision (level %d has floors above)"), NewLevel);
		}
		else
		{
			// No floors above, enable normal collision
			FloorComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			UE_LOG(LogTemp, Log, TEXT("LotManager: Enabled floor visibility collision (level %d, no floors above)"), NewLevel);
		}
	}

	// Update terrain grid visibility
	// Terrain ONLY exists on ground floor (Basements level), so only update when viewing that level
	if (TerrainComponent)
	{
		// Terrain uses section index 0 for top surface, regardless of level number
		int32 TopSectionIndex = UTerrainComponent::GetTopSectionIndex(Basements);
		UMaterialInterface* TerrainMat = TerrainComponent->GetMaterial(TopSectionIndex);
		if (TerrainMat)
		{
			UMaterialInstanceDynamic* DynamicMat = Cast<UMaterialInstanceDynamic>(TerrainMat);
			if (!DynamicMat)
			{
				// Create dynamic instance if needed
				if (UMaterialInstance* StaticMat = Cast<UMaterialInstance>(TerrainMat))
				{
					DynamicMat = UMaterialInstanceDynamic::Create(StaticMat, TerrainComponent);
					TerrainComponent->SetMaterial(TopSectionIndex, DynamicMat);
				}
			}

			if (DynamicMat)
			{
				// Show terrain grid only when on ground floor AND grid is globally enabled
				bool bShowTerrainGrid = bShowGrid && (NewLevel == Basements);
				DynamicMat->SetScalarParameterValue(FName("ShowGrid"), bShowTerrainGrid ? 1.0f : 0.0f);
			}
		}
	}

	// Update wall visibility
	if (WallComponent)
	{
		for (FWallSegmentData& WallData : WallComponent->WallDataArray)
		{
			if (WallData.WallMaterial)
			{
				// Set visibility parameter: 0.0 if wall is above current level, 1.0 otherwise
				float VisibilityValue = (WallData.Level > NewLevel) ? 0.0f : 1.0f;
				WallData.WallMaterial->SetScalarParameterValue(FName("VisibilityLevel"), VisibilityValue);
				WallComponent->SetMaterial(WallData.SectionIndex, WallData.WallMaterial);
			}
		}
	}

	// Update portal (doors/windows) visibility based on level
	// Portals are tracked per-wall in WallComponent->WallDataArray[].PortalArray
	// Each portal should be hidden if its wall's level is above the current viewing level
	if (WallComponent)
	{
		for (FWallSegmentData& WallData : WallComponent->WallDataArray)
		{
			bool bShouldHidePortals = (WallData.Level > NewLevel);

			for (APortalBase* Portal : WallData.PortalArray)
			{
				if (Portal)
				{
					Portal->SetActorHiddenInGame(bShouldHidePortals);
				}
			}
		}
	}

	// Force update cutaway mode to refresh roof and portal visibility based on new level
	// ApplyCutawayMode will handle all roof visibility logic (both mode and level checks)
	// and also update portal visibility based on cutaway state
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PC = Iterator->Get();
		if (PC)
		{
			APawn* Pawn = PC->GetPawn();
			if (ABurbPawn* BurbPawn = Cast<ABurbPawn>(Pawn))
			{
				BurbPawn->ForceUpdateCutaway();
			}
		}
	}
}


void ALotManager::SetBasementViewMode(bool bEnabled)
{
	// Enable/disable the basement view rendering system via subsystem
	if (UWorld* World = GetWorld())
	{
		if (UBurbBasementViewSubsystem* Subsystem = World->GetSubsystem<UBurbBasementViewSubsystem>())
		{
			Subsystem->SetBasementViewEnabled(bEnabled);
		}
	}

	// Get the first local player controller
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!PlayerController)
		return;

	// Get the player pawn
	APawn* PlayerPawn = PlayerController->GetPawn();
	if (!PlayerPawn)
		return;

	// Get the camera component from the pawn
	UCameraComponent* CameraComponent = PlayerPawn->FindComponentByClass<UCameraComponent>();
	if (!CameraComponent)
		return;

	if (bEnabled && PostProcessBasementMaterial)
	{
		// Add the post-process material to the camera's post-process settings
		FWeightedBlendable Blendable(1.0f, PostProcessBasementMaterial);
		CameraComponent->PostProcessSettings.WeightedBlendables.Array.Add(Blendable);

		UE_LOG(LogTemp, Log, TEXT("Basement view mode ENABLED - Post-process material applied"));
	}
	else
	{
		// Remove the post-process material from the camera
		if (PostProcessBasementMaterial)
		{
			// Find and remove the blendable with our material
			for (int32 i = CameraComponent->PostProcessSettings.WeightedBlendables.Array.Num() - 1; i >= 0; i--)
			{
				if (CameraComponent->PostProcessSettings.WeightedBlendables.Array[i].Object == PostProcessBasementMaterial)
				{
					CameraComponent->PostProcessSettings.WeightedBlendables.Array.RemoveAt(i);
					break;
				}
			}
		}

		UE_LOG(LogTemp, Log, TEXT("Basement view mode DISABLED - Post-process material removed"));
	}
}

/*** START Room Cache Implementation ***/

void ALotManager::BuildRoomCache()
{
	// Detect all rooms on all levels using RoomManager
	if (!RoomManager)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildRoomCache: RoomManager not available"));
		return;
	}

	// Don't clear cache - DetectAllRooms will handle per-level clearing
	// This allows incremental updates and preserves rooms on unaffected levels

	// Detect rooms on each level
	int32 TotalRooms = 0;
	for (int32 Level = 0; Level < Basements + Floors; Level++)
	{
		int32 RoomsOnLevel = RoomManager->DetectAllRooms(Level);
		TotalRooms += RoomsOnLevel;
		UE_LOG(LogTemp, Log, TEXT("BuildRoomCache: Detected %d rooms on Level %d"), RoomsOnLevel, Level);
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildRoomCache: Detected total of %d rooms across all levels"), TotalRooms);
}

void ALotManager::InvalidateRoom(int32 RoomID)
{
	// Invalidate room in RoomManager
	if (RoomManager)
	{
		RoomManager->InvalidateRoom(RoomID);
	}
}

void ALotManager::InvalidateAllRooms()
{
	// Invalidate all rooms in RoomManager
	if (RoomManager)
	{
		RoomManager->InvalidateAllRooms();
	}
}

void ALotManager::ClearRoomCache()
{
	// Clear room cache in RoomManager
	if (RoomManager)
	{
		RoomManager->ClearRoomCache();
	}
}

/*** END Room Cache Implementation ***/

// ========================================
// Save/Load System Implementation
// ========================================

bool ALotManager::SaveLotToSlot(const FString& SlotName)
{
	// Only the host can save in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveLotToSlot: Only the host can save"));
		return false;
	}

	ULotSerializationSubsystem* SerializationSubsystem = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveLotToSlot: LotSerializationSubsystem not found"));
		return false;
	}

	// Serialize lot data
	FSerializedLotData LotData = SerializationSubsystem->SerializeLot(this);

	// Create save game object
	ULotSaveGame* SaveGame = Cast<ULotSaveGame>(UGameplayStatics::CreateSaveGameObject(ULotSaveGame::StaticClass()));
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveLotToSlot: Failed to create save game object"));
		return false;
	}

	// Populate save game
	SaveGame->LotData = LotData;
	SaveGame->SlotName = SlotName;

	// Save to disk
	if (UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully saved lot to slot: %s"), *SlotName);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save lot to slot: %s"), *SlotName);
		return false;
	}
}

bool ALotManager::LoadLotFromSlot(const FString& SlotName)
{
	// Only the host can load in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadLotFromSlot: Only the host can load"));
		return false;
	}

	// Check if save exists
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadLotFromSlot: Save slot does not exist: %s"), *SlotName);
		return false;
	}

	// Load save game
	ULotSaveGame* LoadedSave = Cast<ULotSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!LoadedSave)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotFromSlot: Failed to load save game from slot: %s"), *SlotName);
		return false;
	}

	// Get serialization subsystem
	ULotSerializationSubsystem* SerializationSubsystem = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotFromSlot: LotSerializationSubsystem not found"));
		return false;
	}

	// Deserialize lot data
	if (SerializationSubsystem->DeserializeLot(this, LoadedSave->LotData))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully loaded lot from slot: %s"), *SlotName);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to deserialize lot from slot: %s"), *SlotName);
		return false;
	}
}

bool ALotManager::ExportLotToFile(const FString& FilePath)
{
	// Only the host can export in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("ExportLotToFile: Only the host can export"));
		return false;
	}

	ULotSerializationSubsystem* SerializationSubsystem = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("ExportLotToFile: LotSerializationSubsystem not found"));
		return false;
	}

	// Serialize lot data
	FSerializedLotData LotData = SerializationSubsystem->SerializeLot(this);

	// Export to JSON
	if (SerializationSubsystem->ExportToJSON(LotData, FilePath))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully exported lot to file: %s"), *FilePath);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to export lot to file: %s"), *FilePath);
		return false;
	}
}

bool ALotManager::ImportLotFromFile(const FString& FilePath)
{
	// Only the host can import in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("ImportLotFromFile: Only the host can import"));
		return false;
	}

	ULotSerializationSubsystem* SerializationSubsystem = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("ImportLotFromFile: LotSerializationSubsystem not found"));
		return false;
	}

	// Import from JSON
	FSerializedLotData LotData;
	if (!SerializationSubsystem->ImportFromJSON(FilePath, LotData))
	{
		UE_LOG(LogTemp, Error, TEXT("ImportLotFromFile: Failed to import JSON from: %s"), *FilePath);
		return false;
	}

	// Deserialize lot data
	if (SerializationSubsystem->DeserializeLot(this, LotData))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully imported lot from file: %s"), *FilePath);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to deserialize lot from file: %s"), *FilePath);
		return false;
	}
}

bool ALotManager::LoadDefaultLot(ULotDataAsset* LotAsset)
{
	// Only the host can load in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadDefaultLot: Only the host can load"));
		return false;
	}

	if (!LotAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadDefaultLot: LotAsset is null"));
		return false;
	}

	// Validate lot data
	if (!LotAsset->ValidateLotData())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadDefaultLot: Lot data validation failed for asset: %s"), *LotAsset->GetName());
		return false;
	}

	// Get serialization subsystem
	ULotSerializationSubsystem* SerializationSubsystem = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadDefaultLot: LotSerializationSubsystem not found"));
		return false;
	}

	// Deserialize lot data
	if (SerializationSubsystem->DeserializeLot(this, LotAsset->LotData))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully loaded default lot: %s"), *LotAsset->GetLotName());
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load default lot: %s"), *LotAsset->GetLotName());
		return false;
	}
}

#if WITH_EDITOR
ULotDataAsset* ALotManager::SaveAsDataAsset(const FString& AssetName, const FString& PackagePath)
{
	ULotSerializationSubsystem* SerializationSubsystem = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveAsDataAsset: LotSerializationSubsystem not found"));
		return nullptr;
	}

	// Serialize lot data
	FSerializedLotData LotData = SerializationSubsystem->SerializeLot(this);

	// Create package path
	FString PackageName = FString::Printf(TEXT("%s%s"), *PackagePath, *AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveAsDataAsset: Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	// Create data asset
	ULotDataAsset* NewAsset = NewObject<ULotDataAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveAsDataAsset: Failed to create data asset"));
		return nullptr;
	}

	// Populate data asset
	NewAsset->LotData = LotData;

	// Mark package as dirty
	Package->MarkPackageDirty();

	// Save package
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (UPackage::SavePackage(Package, NewAsset, *PackageFileName, SaveArgs))
	{
		// Notify asset registry
		FAssetRegistryModule::AssetCreated(NewAsset);

		UE_LOG(LogTemp, Log, TEXT("Successfully saved lot as data asset: %s"), *PackageName);
		return NewAsset;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save data asset package: %s"), *PackageFileName);
		return nullptr;
	}
}
#endif

void ALotManager::ClearLot()
{
	UE_LOG(LogTemp, Log, TEXT("Clearing lot..."));

	// Clear wall graph
	if (WallGraph)
	{
		WallGraph->Nodes.Empty();
		WallGraph->Edges.Empty();
		WallGraph->Intersections.Empty();
		WallGraph->TileToEdges.Empty();
		WallGraph->TileToNodes.Empty();
		WallGraph->EdgesByLevel.Empty();
		WallGraph->NextNodeID = 0;
		WallGraph->NextEdgeID = 0;
	}

	// Clear wall component
	if (WallComponent)
	{
		WallComponent->WallDataArray.Empty();
		WallComponent->ClearAllMeshSections();
	}

	// Clear floor component
	if (FloorComponent)
	{
		FloorComponent->FloorTileDataArray.Empty();
		FloorComponent->FloorSpatialMap.Empty();
		FloorComponent->ClearAllMeshSections();
	}

	// Clear roof actors
	ClearAllRoofActors();

	// Clear stairs actors
	ClearAllStairsActors();

	// Clear portals (doors/windows)
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors, true);
	for (AActor* Actor : AttachedActors)
	{
		if (APortalBase* Portal = Cast<APortalBase>(Actor))
		{
			Portal->Destroy();
		}
	}

	// Clear placed objects (furniture, decorations)
	for (AActor* Actor : AttachedActors)
	{
		APlaceableObject* PlaceableObj = Cast<APlaceableObject>(Actor);
		if (PlaceableObj && !Cast<APortalBase>(Actor)) // Exclude portals (already cleared)
		{
			PlaceableObj->Destroy();
		}
	}

	// Clear terrain component
	if (TerrainComponent)
	{
		TerrainComponent->TerrainDataArray.Empty();
		TerrainComponent->TerrainSpatialMap.Empty();
		TerrainComponent->ClearAllMeshSections();
	}

	// Clear room cache
	ClearRoomCache();

	// Reset grid data (keep room IDs at 0)
	for (FTileData& Tile : GridData)
	{
		Tile.SetRoomID(0);
	}

	UE_LOG(LogTemp, Log, TEXT("Lot cleared successfully"));
}

/*** END Save/Load System Implementation ***/

/*** START Neighbourhood Integration ***/

#if WITH_EDITOR
void ALotManager::PlaceLotOnNeighbourhood()
{
	// Auto-discover neighbourhood if not set
	if (!ParentNeighbourhood)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANeighbourhoodManager::StaticClass(), FoundActors);
		if (FoundActors.Num() > 0)
		{
			ParentNeighbourhood = Cast<ANeighbourhoodManager>(FoundActors[0]);
			UE_LOG(LogTemp, Warning, TEXT("Auto-discovered neighbourhood: %s"), *ParentNeighbourhood->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("No NeighbourhoodManager found in level! Place one first."));
			return;
		}
	}

	if (!ParentNeighbourhood->TerrainComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("Neighbourhood has no TerrainComponent! Generate neighbourhood terrain first."));
		return;
	}

	// Snap to neighbourhood grid
	SnapToNeighbourhoodGrid();

	// Validate placement
	if (!ParentNeighbourhood->CanPlaceLot(NeighbourhoodOffsetRow, NeighbourhoodOffsetColumn, GridSizeX, GridSizeY))
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot place lot at (%d, %d) - overlaps with another lot or out of bounds!"),
			NeighbourhoodOffsetRow, NeighbourhoodOffsetColumn);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Placing lot '%s' at neighbourhood position (%d, %d)..."),
		*GetName(), NeighbourhoodOffsetRow, NeighbourhoodOffsetColumn);

	// 1. Claim tiles in neighbourhood
	bool bSuccess = ParentNeighbourhood->ClaimTiles(
		this,
		NeighbourhoodOffsetRow,
		NeighbourhoodOffsetColumn,
		GridSizeX,
		GridSizeY
	);

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to claim neighbourhood tiles!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Claimed %dx%d tiles in neighbourhood"), GridSizeX, GridSizeY);

	// 2. Punch hole in neighbourhood terrain
	ParentNeighbourhood->CreateTerrainHole(
		NeighbourhoodOffsetRow,
		NeighbourhoodOffsetColumn,
		GridSizeX,
		GridSizeY
	);

	UE_LOG(LogTemp, Warning, TEXT("Punched hole in neighbourhood terrain"));

	// 3. Generate lot's own terrain
	GenerateTerrainComponents();

	UE_LOG(LogTemp, Warning, TEXT("Generated lot terrain"));

	// 4. Stitch lot terrain edges to neighbourhood landscape
	StitchTerrainToNeighbourhood();

	UE_LOG(LogTemp, Warning, TEXT("Stitched lot terrain to neighbourhood edges"));

	// 5. Generate grid
	GenerateGridData();
	GenerateGridMesh();

	UE_LOG(LogTemp, Warning, TEXT("Generated lot grid"));

	// Mark as placed
	bIsPlacedOnNeighbourhood = true;

	UE_LOG(LogTemp, Warning, TEXT("Lot placement complete! ✓"));
}

void ALotManager::UnplaceLotFromNeighbourhood()
{
	if (!bIsPlacedOnNeighbourhood)
	{
		UE_LOG(LogTemp, Warning, TEXT("Lot is not currently placed on neighbourhood"));
		return;
	}

	if (!ParentNeighbourhood)
	{
		UE_LOG(LogTemp, Error, TEXT("ParentNeighbourhood is null!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Unplacing lot '%s' from neighbourhood..."), *GetName());

	// 1. Release claimed tiles
	ParentNeighbourhood->ReleaseTiles(this);

	UE_LOG(LogTemp, Warning, TEXT("Released neighbourhood tiles"));

	// 2. Restore neighbourhood terrain
	ParentNeighbourhood->FillTerrainHole(
		NeighbourhoodOffsetRow,
		NeighbourhoodOffsetColumn,
		GridSizeX,
		GridSizeY
	);

	UE_LOG(LogTemp, Warning, TEXT("Restored neighbourhood terrain"));

	// 3. Destroy lot terrain
	if (TerrainComponent)
	{
		TerrainComponent->TerrainDataArray.Empty();
		TerrainComponent->TerrainSpatialMap.Empty();
		TerrainComponent->ClearAllMeshSections();
		UE_LOG(LogTemp, Warning, TEXT("Destroyed lot terrain"));
	}

	// 4. Clear grid
	GridData.Empty();
	if (GridComponent)
	{
		GridComponent->ClearAllMeshSections();
		UE_LOG(LogTemp, Warning, TEXT("Cleared lot grid"));
	}

	// Mark as unplaced
	bIsPlacedOnNeighbourhood = false;

	UE_LOG(LogTemp, Warning, TEXT("Lot unplacement complete! ✓"));
}
#endif

void ALotManager::SnapToNeighbourhoodGrid()
{
	if (!ParentNeighbourhood) return;

	// Convert current world position to neighbourhood tile
	int32 SnapRow, SnapCol;
	ParentNeighbourhood->WorldToNeighbourhoodTile(GetActorLocation(), SnapRow, SnapCol);

	// Update offset
	NeighbourhoodOffsetRow = SnapRow;
	NeighbourhoodOffsetColumn = SnapCol;

	// Snap actor position to exact tile center
	FVector SnappedLocation = ParentNeighbourhood->NeighbourhoodTileToWorld(SnapRow, SnapCol);
	SetActorLocation(SnappedLocation);
}

void ALotManager::StitchTerrainToNeighbourhood()
{
	if (!ParentNeighbourhood || !TerrainComponent) return;

	UE_LOG(LogTemp, Warning, TEXT("Stitching lot terrain edges to neighbourhood landscape..."));

	// Sample landscape height at lot boundaries and apply to lot terrain edges
	// This creates seamless height transitions between neighbourhood landscape and lot terrain

	// Note: The actual implementation depends on how TerrainComponent stores heights
	// This is a placeholder that demonstrates the concept

	// For each edge of the lot:
	// - Left edge (Column = 0)
	// - Right edge (Column = GridSizeY - 1)
	// - Top edge (Row = 0)
	// - Bottom edge (Row = GridSizeX - 1)

	// Sample landscape height at each edge tile and apply to terrain
	// Left edge
	for (int32 Row = 0; Row < GridSizeX; Row++)
	{
		FVector EdgeWorldPos = ParentNeighbourhood->NeighbourhoodTileToWorld(
			NeighbourhoodOffsetRow + Row,
			NeighbourhoodOffsetColumn
		);

		float TerrainHeight = ParentNeighbourhood->GetTerrainHeightAt(EdgeWorldPos);

		// TODO: Apply height to lot terrain left edge
		// This requires knowledge of how TerrainComponent stores corner heights
	}

	// Right edge
	for (int32 Row = 0; Row < GridSizeX; Row++)
	{
		FVector EdgeWorldPos = ParentNeighbourhood->NeighbourhoodTileToWorld(
			NeighbourhoodOffsetRow + Row,
			NeighbourhoodOffsetColumn + GridSizeY
		);

		float TerrainHeight = ParentNeighbourhood->GetTerrainHeightAt(EdgeWorldPos);

		// TODO: Apply height to lot terrain right edge
	}

	// Top edge
	for (int32 Col = 0; Col < GridSizeY; Col++)
	{
		FVector EdgeWorldPos = ParentNeighbourhood->NeighbourhoodTileToWorld(
			NeighbourhoodOffsetRow,
			NeighbourhoodOffsetColumn + Col
		);

		float TerrainHeight = ParentNeighbourhood->GetTerrainHeightAt(EdgeWorldPos);

		// TODO: Apply height to lot terrain top edge
	}

	// Bottom edge
	for (int32 Col = 0; Col < GridSizeY; Col++)
	{
		FVector EdgeWorldPos = ParentNeighbourhood->NeighbourhoodTileToWorld(
			NeighbourhoodOffsetRow + GridSizeX,
			NeighbourhoodOffsetColumn + Col
		);

		float TerrainHeight = ParentNeighbourhood->GetTerrainHeightAt(EdgeWorldPos);

		// TODO: Apply height to lot terrain bottom edge
	}

	// Rebuild lot terrain with stitched edges
	if (TerrainComponent)
	{
		TerrainComponent->RebuildLevel(0);
	}

	UE_LOG(LogTemp, Warning, TEXT("Terrain stitching complete (height application pending full implementation)"));
}

/*** END Neighbourhood Integration ***/
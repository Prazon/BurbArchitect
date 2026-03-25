// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BrushTools/BrushFlattenTool.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"

ABrushFlattenTool::ABrushFlattenTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	BrushDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("BrushDecal"));
    BrushDecal->SetupAttachment(RootComponent);

	Radius = 200.0f;
	TargetFlattenHeight = 30;
}

void ABrushFlattenTool::BeginPlay()
{
	Super::BeginPlay();

	BrushDecal->DecalSize = FVector(10000.0f, Radius, Radius);
    BrushDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // Rotate the decal to project downwards
}

void ABrushFlattenTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ABrushFlattenTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABrushFlattenTool::ShapeTerrain()
{
	// Get BuildServer subsystem
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushFlattenTool: BuildServer subsystem not found"));
		return;
	}

	BuildServer->SetCurrentLot(CurrentLot);

	int32 outRow=0;
	int32 outColumn=0;

	if (CurrentLot->LocationToTile(GetActorLocation(), outRow, outColumn))
	{
		int32 RadiusInTiles = static_cast<int32>(Radius / CurrentLot->GridTileSize);
		int32 StartX = FMath::Max(0, outRow - RadiusInTiles);
		int32 EndX = FMath::Min(CurrentLot->GridSizeX - 1, outRow + RadiusInTiles);
		int32 StartY = FMath::Max(0, outColumn - RadiusInTiles);
		int32 EndY = FMath::Min(CurrentLot->GridSizeY - 1, outColumn + RadiusInTiles);

		// Begin batch operation to prevent terrain component from rebuilding on every tile
		if (CurrentLot->TerrainComponent)
		{
			CurrentLot->TerrainComponent->BeginBatchOperation();
		}

		for (int32 y = StartY; y <= EndY; ++y)
		{
			for (int32 x = StartX; x <= EndX; ++x)
			{
				float Distance = FMath::Sqrt(FMath::Pow((outRow - x) * CurrentLot->GridTileSize, 2) + FMath::Pow((outColumn - y) * CurrentLot->GridTileSize, 2));
				if (Distance <= Radius)
				{
					// Use BuildServer with the captured target height
					BuildServer->FlattenTerrain(x, y, TargetFlattenHeight);
				}
			}
		}

		// End batch operation - triggers single mesh rebuild for all accumulated changes
		if (CurrentLot->TerrainComponent)
		{
			CurrentLot->TerrainComponent->EndBatchOperation();
		}
	}
}

void ABrushFlattenTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
    SetActorLocation(MoveLocation);

	//Tell blueprint children we moved successfully
	OnMoved();

	if (SelectPressed)
	{
		ServerDrag();
	}
	PreviousLocation = GetActorLocation();
}

void ABrushFlattenTool::Click_Implementation()
{
	// Brush tools only work on ground floor (Level == Basements)
	if (!CurrentLot || CurrentLot->CurrentLevel != CurrentLot->Basements)
	{
		UE_LOG(LogTemp, Warning, TEXT("Brush tools can only be used on the ground floor"));
		return;
	}

	PreviousLocation = GetActorLocation();

	// Sample terrain height at cursor position to use as flatten target
	int32 CursorRow = 0;
	int32 CursorColumn = 0;
	if (CurrentLot->LocationToTile(GetActorLocation(), CursorRow, CursorColumn))
	{
		// Sample terrain elevation at this grid position
		TargetFlattenHeight = CurrentLot->TerrainComponent->SampleTerrainElevation(CurrentLot->Basements, CursorRow, CursorColumn);

		UE_LOG(LogTemp, Log, TEXT("BrushFlattenTool: Sampled target flatten height %.2f at (%d, %d)"),
			TargetFlattenHeight, CursorRow, CursorColumn);
	}
	else
	{
		// Fallback to actor location Z if grid lookup fails
		TargetFlattenHeight = GetActorLocation().Z;
		UE_LOG(LogTemp, Warning, TEXT("BrushFlattenTool: Failed to sample terrain, using cursor Z %.2f"), TargetFlattenHeight);
	}

	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ABrushFlattenTool::ShapeTerrain, TimerDelay, true);
}

void ABrushFlattenTool::BroadcastRelease_Implementation()
{
	GetWorld()->GetTimerManager().ClearTimer(TimerHandle);

	// Rebuild terrain mesh with full normal recalculation for smooth lighting
	if (CurrentLot && CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->RebuildLevel(CurrentLot->Basements);
	}
}

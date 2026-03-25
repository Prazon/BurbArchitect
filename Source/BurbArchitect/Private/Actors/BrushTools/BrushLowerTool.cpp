// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BrushTools/BrushLowerTool.h"
#include "Actors/BrushTools/BrushLowerTool.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"

ABrushLowerTool::ABrushLowerTool()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	BrushDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("BrushDecal"));
    BrushDecal->SetupAttachment(RootComponent);

	SegmentCount = 36;
	Radius = 500.0f;
}

void ABrushLowerTool::BeginPlay()
{
    Super::BeginPlay();

    BrushDecal->DecalSize = FVector(10000.0f, Radius, Radius);
    BrushDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // Rotate the decal to project downwards
}

void ABrushLowerTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
}

void ABrushLowerTool::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void ABrushLowerTool::ShapeTerrain()
{
	// Get BuildServer subsystem
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushLowerTool: BuildServer subsystem not found"));
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
					TArray<float> Spans;

    				Spans.Add(CurrentLot->CalculateCenterSpan(outRow-1, outColumn-1, x, y, Radius));
					Spans.Add(CurrentLot->CalculateCenterSpan(outRow,   outColumn-1, x, y, Radius));
    				Spans.Add(CurrentLot->CalculateCenterSpan(outRow+1, outColumn-1, x, y, Radius));

    				Spans.Add(CurrentLot->CalculateCenterSpan(outRow-1, outColumn  , x, y, Radius));
					Spans.Add(CurrentLot->CalculateCenterSpan(outRow,   outColumn  , x, y, Radius));
    				Spans.Add(CurrentLot->CalculateCenterSpan(outRow+1, outColumn  , x, y, Radius));

    				Spans.Add(CurrentLot->CalculateCenterSpan(outRow-1, outColumn+1, x, y, Radius));
    				Spans.Add(CurrentLot->CalculateCenterSpan(outRow,   outColumn+1, x, y, Radius));
    				Spans.Add(CurrentLot->CalculateCenterSpan(outRow+1, outColumn+1, x, y, Radius));

					// Use BuildServer instead of direct LotManager call
					BuildServer->LowerTerrain(x, y, Spans, DefaultTerrainMaterial);
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

void ABrushLowerTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
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

void ABrushLowerTool::Click_Implementation()
{
	// Brush tools only work on ground floor (Level == Basements)
	if (!CurrentLot || CurrentLot->CurrentLevel != CurrentLot->Basements)
	{
		UE_LOG(LogTemp, Warning, TEXT("Brush tools can only be used on the ground floor"));
		return;
	}

    PreviousLocation = GetActorLocation();

	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ABrushLowerTool::ShapeTerrain, TimerDelay, true);
}

void ABrushLowerTool::BroadcastRelease_Implementation()
{
    GetWorld()->GetTimerManager().ClearTimer(TimerHandle);

	// Rebuild terrain mesh with full normal recalculation for smooth lighting
	if (CurrentLot && CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->RebuildLevel(CurrentLot->Basements);
	}
}

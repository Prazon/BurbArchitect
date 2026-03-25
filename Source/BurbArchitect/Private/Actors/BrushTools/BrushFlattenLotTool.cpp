// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BrushTools/BrushFlattenLotTool.h"
#include "Actors/LotManager.h"
#include "Subsystems/BuildServer.h"

ABrushFlattenLotTool::ABrushFlattenLotTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	// Default to flattening at lot base height (0.0f relative to BaseZ)
	DefaultHeight = 0.0f;

	// Hide the brush decal by default since we're flattening the entire lot
	bHideDecal = true;
}

void ABrushFlattenLotTool::BeginPlay()
{
	Super::BeginPlay();

	// Hide the decal if configured
	if (bHideDecal && BrushDecal)
	{
		BrushDecal->SetVisibility(false);
	}
}

void ABrushFlattenLotTool::Click_Implementation()
{
	// Brush tools only work on ground floor (Level == Basements)
	if (!CurrentLot || CurrentLot->CurrentLevel != CurrentLot->Basements)
	{
		UE_LOG(LogTemp, Warning, TEXT("Brush tools can only be used on the ground floor"));
		return;
	}

	// Set the target flatten height to the default height (relative to lot BaseZ)
	// BaseZ is the lot's actor location Z coordinate
	TargetFlattenHeight = CurrentLot->GetActorLocation().Z + DefaultHeight;

	UE_LOG(LogTemp, Log, TEXT("BrushFlattenLotTool: Flattening entire lot to height %.2f"), TargetFlattenHeight);

	// Flatten the entire lot immediately (no timer needed)
	FlattenEntireLot();
}

void ABrushFlattenLotTool::FlattenEntireLot()
{
	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushFlattenLotTool: No CurrentLot set"));
		return;
	}

	// Get BuildServer subsystem
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushFlattenLotTool: BuildServer subsystem not found"));
		return;
	}

	BuildServer->SetCurrentLot(CurrentLot);

	// Begin batch operation to prevent terrain component from rebuilding on every tile
	if (CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->BeginBatchOperation();
	}

	// Iterate through ALL tiles in the lot and flatten them
	int32 TilesFlattened = 0;
	for (int32 Row = 0; Row < CurrentLot->GridSizeX; ++Row)
	{
		for (int32 Column = 0; Column < CurrentLot->GridSizeY; ++Column)
		{
			// Use BuildServer to flatten each tile to the target height
			BuildServer->FlattenTerrain(Row, Column, TargetFlattenHeight);
			TilesFlattened++;
		}
	}

	// End batch operation - triggers single mesh rebuild for all accumulated changes
	if (CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->EndBatchOperation();
	}

	UE_LOG(LogTemp, Log, TEXT("BrushFlattenLotTool: Flattened %d tiles to height %.2f"), TilesFlattened, TargetFlattenHeight);
}

void ABrushFlattenLotTool::BroadcastRelease_Implementation()
{
	// Clear any timer that might be set by the parent class (shouldn't happen, but be safe)
	GetWorld()->GetTimerManager().ClearTimer(TimerHandle);

	// Rebuild terrain mesh with full normal recalculation for smooth lighting
	if (CurrentLot && CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->RebuildLevel(CurrentLot->Basements);
	}

	UE_LOG(LogTemp, Log, TEXT("BrushFlattenLotTool: Lot flattening complete"));
}

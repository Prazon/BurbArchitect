// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BrushTools/BrushSmoothTool.h"
#include "Actors/LotManager.h"
#include "Components/TerrainComponent.h"

ABrushSmoothTool::ABrushSmoothTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	BrushDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("BrushDecal"));
    BrushDecal->SetupAttachment(RootComponent);

	Radius = 500.0f;
	SmoothingStrength = 0.5f;
}

void ABrushSmoothTool::BeginPlay()
{
	Super::BeginPlay();

    BrushDecal->DecalSize = FVector(10000.0f, Radius, Radius);
    BrushDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // Rotate the decal to project downwards
}

void ABrushSmoothTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ABrushSmoothTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABrushSmoothTool::SmoothTerrain()
{
	if (!CurrentLot || !CurrentLot->TerrainComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("BrushSmoothTool: CurrentLot or TerrainComponent not found"));
		return;
	}

	// Convert world location to tile coordinates
	int32 CenterRow = 0;
	int32 CenterColumn = 0;
	if (!CurrentLot->LocationToTile(GetActorLocation(), CenterRow, CenterColumn))
	{
		UE_LOG(LogTemp, Warning, TEXT("BrushSmoothTool: Failed to convert location to tile"));
		return;
	}

	// Convert radius from world units to tile units
	float RadiusInTiles = Radius / CurrentLot->GridTileSize;

	// Use the ground floor level (Basements)
	int32 Level = CurrentLot->Basements;

	// Begin batch operation to prevent terrain component from rebuilding during smoothing
	if (CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->BeginBatchOperation();
	}

	// Call SmoothCornerRegion on the terrain component
	// Note: Corner coordinates are at tile intersections, so we use the tile center as reference
	CurrentLot->TerrainComponent->SmoothCornerRegion(Level, CenterRow, CenterColumn, RadiusInTiles, SmoothingStrength);

	// End batch operation - triggers single mesh rebuild for all smoothed corners
	if (CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->EndBatchOperation();
	}
}

void ABrushSmoothTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
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

void ABrushSmoothTool::Click_Implementation()
{
	// Brush tools only work on ground floor (Level == Basements)
	if (!CurrentLot || CurrentLot->CurrentLevel != CurrentLot->Basements)
	{
		UE_LOG(LogTemp, Warning, TEXT("Brush tools can only be used on the ground floor"));
		return;
	}

	PreviousLocation = GetActorLocation();

	// Start timer to smooth terrain repeatedly while mouse is held down
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ABrushSmoothTool::SmoothTerrain, TimerDelay, true);
}

void ABrushSmoothTool::Drag_Implementation()
{
	Super::Drag_Implementation();
}

void ABrushSmoothTool::BroadcastRelease_Implementation()
{
	// Stop the smoothing timer
	GetWorld()->GetTimerManager().ClearTimer(TimerHandle);

	// Rebuild terrain mesh with full normal recalculation for smooth lighting
	if (CurrentLot && CurrentLot->TerrainComponent)
	{
		CurrentLot->TerrainComponent->RebuildLevel(CurrentLot->Basements);
	}
}

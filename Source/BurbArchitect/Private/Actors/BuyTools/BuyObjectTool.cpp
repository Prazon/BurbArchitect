// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuyTools/BuyObjectTool.h"

#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Components/WallGraphComponent.h"
#include "Components/WallComponent.h"
#include "Data/FurnitureItem.h"
#include "Kismet/KismetMathLibrary.h"


// Sets default values
ABuyObjectTool::ABuyObjectTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

void ABuyObjectTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	if (!CurrentLot)
	{
		return;
	}

	// Update placement mode from catalog item
	if (CurrentCatalogItem)
	{
		CurrentPlacementMode = CurrentCatalogItem->PlacementRules.PlacementMode;
	}

	// Get tile coordinates
	int32 outRow = 0;
	int32 outColumn = 0;
	bool bValidTile = CurrentLot->LocationToTile(MoveLocation, outRow, outColumn);

	// Dispatch to appropriate placement handler based on mode
	switch (CurrentPlacementMode)
	{
	case EObjectPlacementMode::Floor:
		if (bValidTile)
		{
			HandleFloorPlacement(MoveLocation, TracedLevel, outRow, outColumn);
		}
		break;

	case EObjectPlacementMode::WallHanging:
		HandleWallHangingPlacement(MoveLocation, TracedLevel, CursorWorldHitResult);
		break;

	case EObjectPlacementMode::CeilingFixture:
		if (bValidTile)
		{
			HandleCeilingFixturePlacement(MoveLocation, TracedLevel, outRow, outColumn);
		}
		break;

	case EObjectPlacementMode::Surface:
		// Future: Handle surface placement (countertops, desks, etc.)
		// For now, fall back to floor placement
		if (bValidTile)
		{
			HandleFloorPlacement(MoveLocation, TracedLevel, outRow, outColumn);
		}
		break;

	default:
		// Legacy behavior: simple tile snapping
		if (bValidTile)
		{
			FVector TileCenter;
			CurrentLot->TileToGridLocation(TracedLevel, outRow, outColumn, true, TileCenter);
			if (GetActorLocation() != TileCenter)
			{
				SetActorLocation(TileCenter);
				UpdateLocation(GetActorLocation());
				OnMoved();
				PreviousLocation = GetActorLocation();
			}
		}
		break;
	}

	// Handle drag operation
	if (SelectPressed)
	{
		Drag_Implementation();
	}
}

void ABuyObjectTool::Click_Implementation()
{
	Super::Click_Implementation();
}

void ABuyObjectTool::Drag_Implementation()
{
	Super::Drag_Implementation();
}

void ABuyObjectTool::Release_Implementation()
{
	Super::Release_Implementation();
}

// Called when the game starts or when spawned
void ABuyObjectTool::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ABuyObjectTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// ========================================
// Floor Placement Helpers
// ========================================

bool ABuyObjectTool::CheckAdjacentWalls(int32 Row, int32 Col, int32 Level, FVector& OutWallNormal)
{
	if (!CurrentLot || !CurrentLot->WallGraph)
	{
		return false;
	}

	UWallGraphComponent* WallGraph = CurrentLot->WallGraph;

	// Check 4 adjacent tiles using grid-based determinism (O(1) lookups)
	// North (Row-1)
	if (WallGraph->IsWallBetweenTiles(Row, Col, Row - 1, Col, Level))
	{
		OutWallNormal = FVector(0, 1, 0); // Face south (away from north wall)
		return true;
	}

	// South (Row+1)
	if (WallGraph->IsWallBetweenTiles(Row, Col, Row + 1, Col, Level))
	{
		OutWallNormal = FVector(0, -1, 0); // Face north (away from south wall)
		return true;
	}

	// West (Col-1)
	if (WallGraph->IsWallBetweenTiles(Row, Col, Row, Col - 1, Level))
	{
		OutWallNormal = FVector(1, 0, 0); // Face east (away from west wall)
		return true;
	}

	// East (Col+1)
	if (WallGraph->IsWallBetweenTiles(Row, Col, Row, Col + 1, Level))
	{
		OutWallNormal = FVector(-1, 0, 0); // Face west (away from east wall)
		return true;
	}

	return false;
}

TArray<FVector> ABuyObjectTool::GetAdjacentWallNormals(int32 Row, int32 Col, int32 Level)
{
	TArray<FVector> WallNormals;

	if (!CurrentLot || !CurrentLot->WallGraph)
	{
		return WallNormals;
	}

	UWallGraphComponent* WallGraph = CurrentLot->WallGraph;

	// Check all 4 adjacent tiles
	if (WallGraph->IsWallBetweenTiles(Row, Col, Row - 1, Col, Level))
	{
		WallNormals.Add(FVector(0, 1, 0)); // North wall
	}

	if (WallGraph->IsWallBetweenTiles(Row, Col, Row + 1, Col, Level))
	{
		WallNormals.Add(FVector(0, -1, 0)); // South wall
	}

	if (WallGraph->IsWallBetweenTiles(Row, Col, Row, Col - 1, Level))
	{
		WallNormals.Add(FVector(1, 0, 0)); // West wall
	}

	if (WallGraph->IsWallBetweenTiles(Row, Col, Row, Col + 1, Level))
	{
		WallNormals.Add(FVector(-1, 0, 0)); // East wall
	}

	return WallNormals;
}

// ========================================
// Wall Hanging Helpers
// ========================================

bool ABuyObjectTool::TraceForWall(const FVector& FromLocation, FHitResult& OutHit)
{
	if (!GetWorld() || !CurrentLot)
	{
		return false;
	}

	// Raycast from camera/cursor toward FromLocation to find wall
	// Use the wall component's collision channel
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(CurrentPlayerPawn);

	// Trace from camera location toward the cursor hit location
	FVector CameraLocation = CurrentPlayerPawn ? CurrentPlayerPawn->GetActorLocation() : FromLocation;
	FVector TraceEnd = FromLocation + (FromLocation - CameraLocation).GetSafeNormal() * 1000.0f;

	// Trace for walls specifically (WallComponent should have collision enabled)
	bool bHit = GetWorld()->LineTraceSingleByChannel(
		OutHit,
		CameraLocation,
		TraceEnd,
		ECC_Visibility, // Could be custom trace channel for walls
		QueryParams
	);

	// Validate that we hit a wall component
	if (bHit && OutHit.GetComponent())
	{
		// Check if hit component belongs to WallComponent
		if (OutHit.GetComponent()->GetName().Contains(TEXT("Wall")) ||
			OutHit.GetComponent()->GetOwner() == CurrentLot)
		{
			return true;
		}
	}

	return false;
}

FVector ABuyObjectTool::CalculateWallMountPosition(const FHitResult& WallHit, float HeightAboveFloor, float WallOffset, int32 Level)
{
	if (!CurrentLot)
	{
		return FVector::ZeroVector;
	}

	// Get floor height for this level
	// Ground floor (Level == Basements) is at actor's Z, use DefaultWallHeight as offset between floors
	float FloorHeight = CurrentLot->GetActorLocation().Z + (CurrentLot->DefaultWallHeight * (Level - CurrentLot->Basements));

	// Calculate position: wall hit location + offset from wall
	FVector Position = WallHit.Location;
	Position += WallHit.Normal * WallOffset; // Offset from wall surface
	Position.Z = FloorHeight + HeightAboveFloor; // Set specific height above floor

	return Position;
}

FRotator ABuyObjectTool::CalculateWallMountRotation(const FVector& WallNormal)
{
	// Calculate rotation to face away from wall
	// WallNormal points away from wall, so we want to face that direction
	FRotator Rotation = WallNormal.Rotation();
	return Rotation;
}

// ========================================
// Ceiling Placement Helpers
// ========================================

float ABuyObjectTool::CalculateCeilingHeight(int32 Level)
{
	if (!CurrentLot)
	{
		return 0.0f;
	}

	// Ceiling is at the bottom of the next floor
	// Ground floor (Level == Basements) is at actor's Z, use DefaultWallHeight as offset between floors
	float NextLevelHeight = CurrentLot->GetActorLocation().Z + (CurrentLot->DefaultWallHeight * ((Level + 1) - CurrentLot->Basements));
	return NextLevelHeight;
}

bool ABuyObjectTool::CheckCeilingExists(int32 Row, int32 Col, int32 Level)
{
	if (!CurrentLot)
	{
		return false;
	}

	// Check if there's a roof component at this location
	// Or check if there's a floor tile above
	// For now, assume ceiling exists if we're not on the top floor
	// TODO: Implement proper roof/ceiling detection using RoofComponent and FloorComponent

	// Simple check: ceiling exists if not on top floor
	return Level < (CurrentLot->Floors - 1);
}

// ========================================
// Placement Mode Handlers
// ========================================

void ABuyObjectTool::HandleFloorPlacement(const FVector& MoveLocation, int32 TracedLevel, int32 Row, int32 Column)
{
	if (!CurrentLot || !CurrentCatalogItem)
	{
		return;
	}

	const FPlacementConstraints& Constraints = CurrentCatalogItem->PlacementRules;

	// Get tile center location
	FVector TileCenter;
	CurrentLot->TileToGridLocation(TracedLevel, Row, Column, true, TileCenter);

	// Default: valid placement
	bValidPlacement = true;
	FRotator ObjectRotation = GetActorRotation();

	// Check wall-adjacency requirement
	if (Constraints.bMustBeAgainstWall)
	{
		FVector WallNormal;
		bool bHasAdjacentWall = CheckAdjacentWalls(Row, Column, TracedLevel, WallNormal);

		if (bHasAdjacentWall)
		{
			// Auto-rotate to face away from wall if enabled
			if (Constraints.bAutoRotateFromWall)
			{
				ObjectRotation = WallNormal.Rotation();
			}
		}
		else
		{
			// Invalid: no wall adjacent
			bValidPlacement = false;
		}
	}

	// Check floor requirement
	if (Constraints.bRequiresFloor)
	{
		// TODO: Check if floor tile exists at this location using FloorComponent
		// For now, assume floor exists if we traced to this level
	}

	// Update actor position
	if (GetActorLocation() != TileCenter)
	{
		SetActorLocationAndRotation(TileCenter, ObjectRotation);
		UpdateLocation(GetActorLocation());
		OnMoved();
		PreviousLocation = GetActorLocation();
	}
}

void ABuyObjectTool::HandleWallHangingPlacement(const FVector& MoveLocation, int32 TracedLevel, const FHitResult& CursorHit)
{
	if (!CurrentLot || !CurrentCatalogItem)
	{
		return;
	}

	const FPlacementConstraints& Constraints = CurrentCatalogItem->PlacementRules;

	// Trace for wall
	FHitResult WallHit;
	bool bHitWall = TraceForWall(MoveLocation, WallHit);

	if (bHitWall)
	{
		// Calculate position on wall
		FVector WallPosition = CalculateWallMountPosition(
			WallHit,
			Constraints.WallMountHeight,
			Constraints.WallOffset,
			TracedLevel
		);

		// Calculate rotation facing away from wall
		FRotator WallRotation = CalculateWallMountRotation(WallHit.Normal);

		// Valid placement
		bValidPlacement = true;

		// Update actor position
		SetActorLocationAndRotation(WallPosition, WallRotation);
		UpdateLocation(GetActorLocation());
		OnMoved();
		PreviousLocation = GetActorLocation();
	}
	else
	{
		// Invalid: no wall found
		bValidPlacement = false;
	}
}

void ABuyObjectTool::HandleCeilingFixturePlacement(const FVector& MoveLocation, int32 TracedLevel, int32 Row, int32 Column)
{
	if (!CurrentLot || !CurrentCatalogItem)
	{
		return;
	}

	const FPlacementConstraints& Constraints = CurrentCatalogItem->PlacementRules;

	// Get tile center (X, Y)
	FVector TileCenter;
	CurrentLot->TileToGridLocation(TracedLevel, Row, Column, true, TileCenter);

	// Calculate ceiling height
	float CeilingHeight = CalculateCeilingHeight(TracedLevel);
	TileCenter.Z = CeilingHeight - Constraints.CeilingOffset;

	// Check if ceiling exists
	bool bHasCeiling = CheckCeilingExists(Row, Column, TracedLevel);
	bValidPlacement = bHasCeiling;

	// Face downward
	FRotator CeilingRotation = FRotator(-90.0f, 0.0f, 0.0f);

	// Update actor position
	if (GetActorLocation() != TileCenter)
	{
		SetActorLocationAndRotation(TileCenter, CeilingRotation);
		UpdateLocation(GetActorLocation());
		OnMoved();
		PreviousLocation = GetActorLocation();
	}
}


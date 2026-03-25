// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildRoofTool.h"
#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Actors/Roofs/RoofBase.h"
#include "Actors/Roofs/GableRoof.h"
#include "Actors/Roofs/HipRoof.h"
#include "Actors/Roofs/ShedRoof.h"
#include "Subsystems/BuildServer.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// Sets default values
ABuildRoofTool::ABuildRoofTool()
{
	// Set this actor to call Tick() every frame
	PrimaryActorTick.bCanEverTick = true;

	// Create the scene component
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Scene"));
	RootComponent = SceneComponent;

	// Initialize with null actors
	PreviewRoofActor = nullptr;
	PlacedRoofActor = nullptr;
}

void ABuildRoofTool::BeginPlay()
{
	Super::BeginPlay();

	GetWorld()->GetFirstPlayerController()->bEnableClickEvents = true;

	// Initialize height from LotManager if available
	if (CurrentLot)
	{
		Height = CurrentLot->DefaultWallHeight;
	}

	// Initialize RoofDirection to forward (north) by default
	RoofDirection = FVector::ForwardVector;
}

void ABuildRoofTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up any uncommitted preview roof when tool is destroyed
	if (PreviewRoofActor && IsValid(PreviewRoofActor))
	{
		// Only destroy if it hasn't been committed
		if (!PreviewRoofActor->bCommitted)
		{
			PreviewRoofActor->Destroy();
		}
		PreviewRoofActor = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

TSubclassOf<ARoofBase> ABuildRoofTool::GetRoofActorClass() const
{
	// If a custom Blueprint class is set, use it
	if (RoofActorClass)
	{
		return RoofActorClass;
	}

	// Otherwise, fall back to C++ default based on roof type
	ERoofType RoofType = GetRoofType();
	switch (RoofType)
	{
	case ERoofType::Gable:
		return AGableRoof::StaticClass();

	case ERoofType::Hip:
		return AHipRoof::StaticClass();

	case ERoofType::Shed:
		return AShedRoof::StaticClass();

	default:
		return AGableRoof::StaticClass();
	}
}

void ABuildRoofTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildRoofTool::CreateRoofPreview()
{
	// Clean up any existing preview
	if (PreviewRoofActor)
	{
		PreviewRoofActor->Destroy();
		PreviewRoofActor = nullptr;
	}

	// Get spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;

	// Get the roof actor class
	TSubclassOf<ARoofBase> RoofClass = GetRoofActorClass();
	if (!RoofClass)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildRoofTool: No roof actor class found"));
		return;
	}

	// Spawn the preview roof actor
	PreviewRoofActor = GetWorld()->SpawnActor<ARoofBase>(RoofClass, GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (PreviewRoofActor)
	{
		// Setup roof dimensions
		FRoofDimensions RoofDimensions = FRoofDimensions(
			CurrentLot->GridTileSize * FMath::FloorToInt(FrontUnits),
			CurrentLot->GridTileSize * FMath::FloorToInt(BackUnits),
			CurrentLot->GridTileSize * FMath::FloorToInt(RightUnits),
			CurrentLot->GridTileSize * FMath::FloorToInt(LeftUnits),
			FrontRake,
			BackRake,
			RightEve,
			LeftEve,
			Height);

		// Set the roof type
		RoofDimensions.RoofType = GetRoofType();

		// Initialize the preview roof
		PreviewRoofActor->InitializeRoof(
			CurrentLot,
			GetActorLocation(),
			RoofDirection,
			RoofDimensions,
			RoofThickness,
			GableThickness
		);

		// Don't set RoofMaterial during preview - let it use ValidPreviewMaterial
		// Material will be set when CreateRoof() is called

		// Make it semi-transparent for preview
		if (PreviewRoofActor->RoofMeshComponent)
		{
			PreviewRoofActor->RoofMeshComponent->SetRenderCustomDepth(true);
			PreviewRoofActor->RoofMeshComponent->SetCustomDepthStencilValue(1);  // Preview stencil value
		}
	}

	bLockToForwardAxis = true;
}

void ABuildRoofTool::CreateRoof()
{
	// Debounce protection
	if (bIsCreatingRoof || !PreviewRoofActor)
	{
		return;
	}

	// Prevent placement on basement (Level 0)
	if (Level == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ABuildRoofTool::CreateRoof - Cannot place roof on basement (Level 0). Placement blocked."));
		return;
	}

	// Set flag to prevent re-entry
	bIsCreatingRoof = true;

	// Apply final material to the preview actor
	if (DefaultRoofMaterial)
	{
		PreviewRoofActor->RoofMaterial = DefaultRoofMaterial;
	}

	// Commit the preview roof (generates walls, applies final material)
	PreviewRoofActor->CommitRoof();

	// Enter edit mode immediately so roof is placed in "selected" state
	// This shows scale tools and footprint lines for immediate adjustment
	PreviewRoofActor->EnterEditMode();

	// The preview actor is now the placed actor - store reference and clear preview
	PlacedRoofActor = PreviewRoofActor;
	PreviewRoofActor = nullptr;

	// Reset flag
	bIsCreatingRoof = false;
}

void ABuildRoofTool::AdjustRoofPosition(const FVector& MoveLocation, const FHitResult& CursorWorldHitResult, int32 TracedLevel)
{
	// Move to place the Roof
	if (CurrentLot->LocationToTileCorner(TracedLevel, MoveLocation, TargetLocation) && GetActorLocation() != TargetLocation)
	{
		// Default to traced level
		Level = TracedLevel;

		// Place the Roof above the wall if Hit
		if (Cast<UWallComponent>(CursorWorldHitResult.Component))
		{
			UWallComponent* HitWallComponent = Cast<UWallComponent>(CursorWorldHitResult.Component);
			HitMeshSection = HitWallComponent->GetSectionIDFromHitResult(CursorWorldHitResult);
			// Make sure hit section is valid
			if (HitMeshSection >= 0)
			{
				Level = HitWallComponent->WallDataArray[HitMeshSection].Level + 1;
			}
		}

		// Create preview if it doesn't exist
		if (!PreviewRoofActor)
		{
			CreateRoofPreview();
		}

		// Update preview location and direction
		if (PreviewRoofActor)
		{
			// Spawn roof at the level (ceiling tiles for enclosed rooms)
			// Roof sits ON the level, with Gable corners at or near level height
			// Small support walls generate upward from this level to hold the roof structure
			SetActorLocation(TargetLocation);
			UpdateLocation(GetActorLocation());

			// RoofDirection is now persistent and only changes via RotateLeft/RotateRight
			// No automatic camera-based rotation

			// Check if location changed
			bool bLocationChanged = !PreviewRoofActor->GetActorLocation().Equals(TargetLocation);

			// Move the preview actor
			PreviewRoofActor->SetActorLocation(TargetLocation);

			// Update level if it changed
			if (PreviewRoofActor->Level != Level)
			{
				PreviewRoofActor->Level = Level;
			}

			// Redraw footprint lines if location changed
			if (bLocationChanged)
			{
				PreviewRoofActor->DrawFootprintLines();
			}
		}

		PreviousLocation = MoveLocation;
	}
}

void ABuildRoofTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	// Placement mode - show preview
	if (ToolState.GetState() == EToolState::Ts_Placing)
	{
		AdjustRoofPosition(MoveLocation, CursorWorldHitResult, TracedLevel);
	}

	// Tell blueprint children we moved successfully
	OnMoved();

	if (SelectPressed)
	{
		ServerDrag();
	}
}

void ABuildRoofTool::Click_Implementation()
{
	// Place the roof when clicked
	if (ToolState.GetState() == EToolState::Ts_Placing && PreviewRoofActor)
	{
		CreateRoof();

		// Complete the tool operation
		ToolState.SetState(EToolState::Ts_Completed);

		// CreateRoof() already enters edit mode, no need to call it again

		// Return to default tool after placing roof (one-shot usage)
		UE_LOG(LogTemp, Log, TEXT("BuildRoofTool::Click - Returning to default tool after placement"));
		ServerCancel();
	}
}

void ABuildRoofTool::Drag_Implementation()
{
	// Dragging is now handled by the roof actors themselves
	// This tool only handles placement
}

void ABuildRoofTool::BroadcastRelease_Implementation()
{
	// Release is now handled by the roof actors themselves
}

void ABuildRoofTool::RotateLeft_Implementation()
{
	// First check if any committed roof is selected (in edit mode)
	if (GetWorld())
	{
		for (TActorIterator<ARoofBase> RoofItr(GetWorld()); RoofItr; ++RoofItr)
		{
			ARoofBase* Roof = *RoofItr;
			if (Roof && Roof->IsSelected_Implementation())
			{
				// Rotate the selected roof
				Roof->RotateLeft();
				UE_LOG(LogTemp, Log, TEXT("BuildRoofTool: Rotated selected roof left"));
				return; // Exit after rotating selected roof
			}
		}
	}

	// If no roof is selected, rotate the preview roof
	// Rotate RoofDirection 45 degrees counterclockwise (left)
	// Uses 2D rotation matrix: x' = x*cos(θ) - y*sin(θ), y' = x*sin(θ) + y*cos(θ)
	// For -45 degrees: cos(-45°) = 0.707107, sin(-45°) = -0.707107

	const float Cos45 = 0.707106781f;  // cos(45°)
	const float Sin45 = 0.707106781f;  // sin(45°)

	// Rotate counterclockwise by 45 degrees
	FVector NewDirection = FVector(
		RoofDirection.X * Cos45 + RoofDirection.Y * Sin45,   // x' = x*cos(-45) - y*sin(-45)
		-RoofDirection.X * Sin45 + RoofDirection.Y * Cos45,  // y' = x*sin(-45) + y*cos(-45)
		0.0f
	);

	// Normalize to ensure unit vector
	RoofDirection = NewDirection.GetSafeNormal();

	// Update preview roof if it exists - just rotate the actor, don't regenerate mesh
	if (PreviewRoofActor)
	{
		// Store the new direction for when the roof is committed
		PreviewRoofActor->RoofDirection = RoofDirection;

		// Rotate the entire actor visually by 45 degrees around Z axis
		FRotator CurrentRotation = PreviewRoofActor->GetActorRotation();
		CurrentRotation.Yaw -= 45.0f; // Counterclockwise
		PreviewRoofActor->SetActorRotation(CurrentRotation);

		// Update footprint lines to match new rotation
		PreviewRoofActor->DrawFootprintLines();
	}

	UE_LOG(LogTemp, Log, TEXT("BuildRoofTool::RotateLeft - New direction: %s"), *RoofDirection.ToString());
}

void ABuildRoofTool::RotateRight_Implementation()
{
	// First check if any committed roof is selected (in edit mode)
	if (GetWorld())
	{
		for (TActorIterator<ARoofBase> RoofItr(GetWorld()); RoofItr; ++RoofItr)
		{
			ARoofBase* Roof = *RoofItr;
			if (Roof && Roof->IsSelected_Implementation())
			{
				// Rotate the selected roof
				Roof->RotateRight();
				UE_LOG(LogTemp, Log, TEXT("BuildRoofTool: Rotated selected roof right"));
				return; // Exit after rotating selected roof
			}
		}
	}

	// If no roof is selected, rotate the preview roof
	// Rotate RoofDirection 45 degrees clockwise (right)
	// Uses 2D rotation matrix: x' = x*cos(θ) - y*sin(θ), y' = x*sin(θ) + y*cos(θ)
	// For +45 degrees: cos(45°) = 0.707107, sin(45°) = 0.707107

	const float Cos45 = 0.707106781f;  // cos(45°)
	const float Sin45 = 0.707106781f;  // sin(45°)

	// Rotate clockwise by 45 degrees
	FVector NewDirection = FVector(
		RoofDirection.X * Cos45 - RoofDirection.Y * Sin45,   // x' = x*cos(45) - y*sin(45)
		RoofDirection.X * Sin45 + RoofDirection.Y * Cos45,   // y' = x*sin(45) + y*cos(45)
		0.0f
	);

	// Normalize to ensure unit vector
	RoofDirection = NewDirection.GetSafeNormal();

	// Update preview roof if it exists - just rotate the actor, don't regenerate mesh
	if (PreviewRoofActor)
	{
		// Store the new direction for when the roof is committed
		PreviewRoofActor->RoofDirection = RoofDirection;

		// Rotate the entire actor visually by 45 degrees around Z axis
		FRotator CurrentRotation = PreviewRoofActor->GetActorRotation();
		CurrentRotation.Yaw += 45.0f; // Clockwise
		PreviewRoofActor->SetActorRotation(CurrentRotation);

		// Update footprint lines to match new rotation
		PreviewRoofActor->DrawFootprintLines();
	}

	UE_LOG(LogTemp, Log, TEXT("BuildRoofTool::RotateRight - New direction: %s"), *RoofDirection.ToString());
}
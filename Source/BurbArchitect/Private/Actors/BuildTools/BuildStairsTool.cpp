// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuildTools/BuildStairsTool.h"

#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Actors/StairsBase.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Subsystems/BuildServer.h"




// Sets default values
ABuildStairsTool::ABuildStairsTool(): SceneComponent(nullptr), DefaultStairsMaterial(nullptr), DragCreateStairs(nullptr)
{
	PrimaryActorTick.bCanEverTick = true;

	// Create the scene component
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Scene"));
	RootComponent = SceneComponent;
	SceneComponent->SetMobility(EComponentMobility::Movable);  // Ensure tool can move

	// Initialize stairs actor class to base class (can be overridden in Blueprint)
	StairsActorClass = AStairsBase::StaticClass();

	// Explicitly set trace channel to Grid/Tile (same as wall and floor tools)
	TraceChannel = ECC_GameTraceChannel3;
}

// Called when the game starts or when spawned
void ABuildStairsTool::BeginPlay()
{
	Super::BeginPlay();

	ToolState.SetState(EToolState::Ts_Placing);

	GetWorld()->GetFirstPlayerController()->bEnableClickEvents = true;

	// Initialize height from LotManager if available
	if (CurrentLot)
	{
		Height = CurrentLot->DefaultWallHeight;
		UE_LOG(LogTemp, Log, TEXT("BuildStairsTool::BeginPlay - CurrentLot valid: GridSize(%d,%d), TileSize=%.1f"),
			CurrentLot->GridSizeX, CurrentLot->GridSizeY, CurrentLot->GridTileSize);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BuildStairsTool::BeginPlay - CurrentLot is NULL!"));
	}

	UE_LOG(LogTemp, Log, TEXT("BuildStairsTool::BeginPlay - TreadsCount: %d, StairsActorClass: %s, TreadMesh: %s, LandingMesh: %s"),
		TreadsCount,
		StairsActorClass ? *StairsActorClass->GetName() : TEXT("NULL"),
		StairTreadMesh ? *StairTreadMesh->GetName() : TEXT("NULL"),
		StairLandingMesh ? *StairLandingMesh->GetName() : TEXT("NULL"));

	// Create initial stairs preview so it's visible immediately when tool is active
	if (CurrentLot)
	{
		// Initialize with straight stairs (all treads, no landings)
		// Landing creation only happens in edit mode via AdjustmentTool_LandingTool
		StairStructures.Init(FStairModuleStructure(EStairModuleType::Tread, ETurningSocket::Idle), TreadsCount);

		// Set initial position
		DragCreateVectors.StartOperation = GetActorLocation();

		CreateStairsPreview();
		UE_LOG(LogTemp, Log, TEXT("BuildStairsTool::BeginPlay - Created straight stairs preview with %d treads"), TreadsCount);
	}
}

void ABuildStairsTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up preview stairs if tool is destroyed/cancelled before placement
	if (DragCreateStairs)
	{
		UE_LOG(LogTemp, Log, TEXT("BuildStairsTool::EndPlay - Cleaning up preview stairs actor"));
		DragCreateStairs->Destroy();
		DragCreateStairs = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

// Called every frame
void ABuildStairsTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildStairsTool::CreateStairsPreview()
{
	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildStairsTool: Cannot create stairs preview - CurrentLot is null"));
		return;
	}

	// Destroy existing preview stairs if any
	if (DragCreateStairs)
	{
		DragCreateStairs->Destroy();
		DragCreateStairs = nullptr;
	}

	// Spawn at current tool location (grid-snapped)
	FVector SpawnLocation = GetActorLocation();

	UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Creating stairs preview at (%s) with %d structures, StairsActorClass: %s, TreadMesh: %s, LandingMesh: %s"),
		*SpawnLocation.ToString(), StairStructures.Num(),
		StairsActorClass ? *StairsActorClass->GetName() : TEXT("NULL"),
		StairTreadMesh ? *StairTreadMesh->GetName() : TEXT("NULL"),
		StairLandingMesh ? *StairLandingMesh->GetName() : TEXT("NULL"));

	// Spawn new stairs actor for preview using specified Blueprint class
	DragCreateStairs = CurrentLot->SpawnStairsActor(
		StairsActorClass,
		SpawnLocation,
		FVector::ForwardVector,
		StairStructures,
		StairTreadMesh,
		StairLandingMesh,
		30.0f
	);

	if (DragCreateStairs)
	{
		UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Stairs preview created successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BuildStairsTool: Failed to create stairs preview actor"));
	}
}

void ABuildStairsTool::CreateStairs()
{
	if (DragCreateStairs != nullptr && CurrentLot)
	{
		UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Committing stairs at (%s)"),
			*DragCreateStairs->GetActorLocation().ToString());

		// Check if stairs already exist at this location
		// Use actor's world location since StairsData.StartLoc is in local space
		AStairsBase* ExistingStairs = CurrentLot->FindStairsActorAtLocation(
			DragCreateStairs->GetActorLocation(),
			DragCreateStairs->StairsData.Structures
		);

		if (!ExistingStairs)
		{
			// Commit the preview stairs actor (makes it permanent)
			DragCreateStairs->TreadMaterial = DefaultStairsMaterial;
			DragCreateStairs->LandingMaterial = DefaultStairsMaterial;
			DragCreateStairs->CommitStairs();

			// Auto-enter edit mode for immediate adjustments (Sims-like workflow)
			DragCreateStairs->EnterEditMode();

			UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Committed stairs at (%s) and entered edit mode"), *DragCreateStairs->GetActorLocation().ToString());
		}
		else
		{
			// Stairs already exist, destroy the preview
			DragCreateStairs->Destroy();
			UE_LOG(LogTemp, Warning, TEXT("BuildStairsTool: Stairs already exist at this location"));
		}

		DragCreateStairs = nullptr;
	}
	else
	{
		if (!DragCreateStairs)
		{
			UE_LOG(LogTemp, Warning, TEXT("BuildStairsTool: Cannot create stairs - DragCreateStairs is null"));
		}
		if (!CurrentLot)
		{
			UE_LOG(LogTemp, Error, TEXT("BuildStairsTool: Cannot create stairs - CurrentLot is null"));
		}
	}
}

void ABuildStairsTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	if (!CurrentLot)
	{
		UE_LOG(LogTemp, Error, TEXT("BuildStairsTool::Move - CurrentLot is NULL!"));
		return;
	}

	int32 outRow=0;
	int32 outColumn=0;
	bool bValidTile = CurrentLot->LocationToTile(MoveLocation, outRow, outColumn);

	if (!bValidTile)
	{
		FVector LotOrigin = CurrentLot->GetActorLocation();
		float GridWidth = CurrentLot->GridSizeX * CurrentLot->GridTileSize;
		float GridHeight = CurrentLot->GridSizeY * CurrentLot->GridTileSize;

		UE_LOG(LogTemp, Warning, TEXT("BuildStairsTool::Move - LocationToTile FAILED! Cursor: %s, LotOrigin: %s, GridSize: %.1fx%.1f, TileSize: %.1f"),
			*MoveLocation.ToString(), *LotOrigin.ToString(), GridWidth, GridHeight, CurrentLot->GridTileSize);
	}

	if (bValidTile)
	{
		// GRID-BASED POSITIONING: Stairs always face forward (+X) during initial placement
		// Rotation/turning is only handled by the landing adjustment tool during edit mode
		// Position at tile center (stairs extend from this point)
		int32 TileRow = outRow;
		int32 TileCol = outColumn;

		// Convert tile grid coordinates to world position (bCenter=true for tile centers)
		CurrentLot->TileToGridLocation(TracedLevel, TileRow, TileCol, true, TargetLocation);

		// Always update preview position in Ts_Placing state
		if(ToolState.GetState() == EToolState::Ts_Placing && DragCreateStairs)
		{
			// Place preview at grid corner position
			DragCreateStairs->SetActorLocation(TargetLocation);

			// Update stairs level based on traced level and validate placement
			DragCreateStairs->Level = TracedLevel;
			DragCreateStairs->LotManager = CurrentLot;
			bool bValid = DragCreateStairs->ValidatePlacement();

			// Update preview material based on validity
			UMaterialInterface* PreviewMaterial = bValid ? CurrentLot->ValidPreviewMaterial : CurrentLot->InvalidPreviewMaterial;
			if (PreviewMaterial)
			{
				for (UStaticMeshComponent* Module : DragCreateStairs->StairsData.StairModules)
				{
					if (Module)
					{
						Module->SetMaterial(0, PreviewMaterial);
					}
				}
			}

			// Store level for later use
			Level = TracedLevel;
		}

		if (GetActorLocation() !=  TargetLocation)
		{
			if(ToolState.GetState() == EToolState::Ts_Placing)
			{
				SetActorLocation(TargetLocation);
				UpdateLocation(GetActorLocation());
			}

			if(ToolState.GetState() == EToolState::Ts_Dragging)
			{
				DragCreateVectors.EndOperation = TargetLocation;
			}

			//Tell blueprint children we moved successfully
			OnMoved();

			if (SelectPressed)
			{
				ServerDrag();
			}

			PreviousLocation = TargetLocation;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildStairsTool::Move - LocationToTile failed for %s"), *MoveLocation.ToString());
	}
}

void ABuildStairsTool::Click_Implementation()
{
	UE_LOG(LogTemp, Log, TEXT("BuildStairsTool::Click - Current state: %d, DragCreateStairs: %s"),
		static_cast<int32>(ToolState.GetState()),
		DragCreateStairs ? TEXT("EXISTS") : TEXT("NULL"));

	if(ToolState.GetState() == EToolState::Ts_Placing)
	{
		// Preview already exists and is following cursor
		// Click commits the stairs at current position
		if (DragCreateStairs)
		{
			// Check if placement is valid before committing
			if (!DragCreateStairs->bValidPlacement)
			{
				UE_LOG(LogTemp, Warning, TEXT("BuildStairsTool::Click - Cannot place stairs: %s"), *DragCreateStairs->ValidationError);
				// TODO: Play error sound or show UI feedback
				return;
			}

			UE_LOG(LogTemp, Log, TEXT("BuildStairsTool::Click - Committing stairs at current position (Level %d -> %d)"),
				DragCreateStairs->Level, DragCreateStairs->TopLevel);
			CreateStairs();
			ToolState.SetState(EToolState::Ts_Completed);

			// Return to default tool after placing stairs (one-shot usage)
			UE_LOG(LogTemp, Log, TEXT("BuildStairsTool::Click - Returning to default tool after placement"));
			ServerCancel();
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("BuildStairsTool::Click - No preview exists to commit!"));
		}
	}
}

void ABuildStairsTool::Drag_Implementation()
{
	Super::Drag_Implementation();

	// Drag is not used for stairs placement - stairs are always straight during preview
	// Rotation/landings only happen in edit mode via AdjustmentTool_LandingTool
}

void ABuildStairsTool::BroadcastRelease_Implementation()
{
	// Tool is destroyed after placement (one-shot usage)
	// No reset needed
}

void ABuildStairsTool::RotateLeft_Implementation()
{
	// First check if any committed stairs is selected (in edit mode) via BurbPawn
	if (CurrentPlayerPawn && CurrentPlayerPawn->CurrentEditModeActor)
	{
		AStairsBase* SelectedStairs = Cast<AStairsBase>(CurrentPlayerPawn->CurrentEditModeActor);
		if (SelectedStairs)
		{
			SelectedStairs->RotateLeft();
			UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Rotated selected stairs left"));
			return;
		}
	}

	// If no stairs is selected, rotate the preview stairs
	if (DragCreateStairs)
	{
		DragCreateStairs->RotateLeft();
		UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Rotated preview left"));
	}
}

void ABuildStairsTool::RotateRight_Implementation()
{
	// First check if any committed stairs is selected (in edit mode) via BurbPawn
	if (CurrentPlayerPawn && CurrentPlayerPawn->CurrentEditModeActor)
	{
		AStairsBase* SelectedStairs = Cast<AStairsBase>(CurrentPlayerPawn->CurrentEditModeActor);
		if (SelectedStairs)
		{
			SelectedStairs->RotateRight();
			UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Rotated selected stairs right"));
			return;
		}
	}

	// If no stairs is selected, rotate the preview stairs
	if (DragCreateStairs)
	{
		DragCreateStairs->RotateRight();
		UE_LOG(LogTemp, Log, TEXT("BuildStairsTool: Rotated preview right"));
	}
}

	
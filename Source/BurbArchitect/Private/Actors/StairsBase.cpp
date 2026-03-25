// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/StairsBase.h"
#include "Actors/LotManager.h"
#include "Actors/BurbPawn.h"
#include "Components/StairsComponent.h"
#include "Components/GizmoComponent.h"
#include "Components/FloorComponent.h"
#include "Components/TerrainComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"

AStairsBase::AStairsBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// Create root scene component
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;
	RootSceneComponent->SetMobility(EComponentMobility::Movable);  // Ensure actor can be moved at runtime

	// Create stairs mesh component
	StairsMeshComponent = CreateDefaultSubobject<UStairsComponent>(TEXT("StairsMeshComponent"));
	StairsMeshComponent->SetupAttachment(RootSceneComponent);
	StairsMeshComponent->SetCastHiddenShadow(true);

	// Create adjustment tool components
	// NOTE: Gizmos ARE attached to RootSceneComponent to enable input events (OnClicked, OnBeginCursorOver)
	// We use SetWorldLocation/SetWorldRotation to maintain world-space positioning
	AdjustmentTool_StartingStep = CreateDefaultSubobject<UGizmoComponent>(TEXT("AdjustmentTool_StartingStep"));
	SetupAdjustmentToolComponent(AdjustmentTool_StartingStep, EGizmoType::Height);
	AdjustmentTool_StartingStep->SetupAttachment(RootSceneComponent);

	AdjustmentTool_EndingStep = CreateDefaultSubobject<UGizmoComponent>(TEXT("AdjustmentTool_EndingStep"));
	SetupAdjustmentToolComponent(AdjustmentTool_EndingStep, EGizmoType::Height);
	AdjustmentTool_EndingStep->SetupAttachment(RootSceneComponent);

	AdjustmentTool_LandingTool = CreateDefaultSubobject<UGizmoComponent>(TEXT("AdjustmentTool_LandingTool"));
	SetupAdjustmentToolComponent(AdjustmentTool_LandingTool, EGizmoType::Height);
	AdjustmentTool_LandingTool->SetupAttachment(RootSceneComponent);

	// Initialize properties
	LotManager = nullptr;
	StairsThickness = 30.0f;
	StairsDirection = FVector::ForwardVector;
	Level = 0;
	TopLevel = 1;  // Stairs go up one level by default
	bValidPlacement = false;  // Invalid until validated
	ValidationError = TEXT("");
	bInEditMode = false;
	bCommitted = false;
	TreadsCount = 12;
	StepsPerSection = 4;
	DefaultLandingIndex = 4;  // First valid landing position (first multiple of StepsPerSection=4)
	LastLandingIndex = -1;
	SelectedModuleIndex = -1;
	CurrentAdjustmentToolType = EScaleStairsToolType::Empty;
	bWasDraggingLastFrame = false;
	bCutawayApplied = false;

	// Initialize default structures for editor preview (similar to RoofBase initializing RoofDimensions)
	// Create a simple straight staircase with default treads
	StairsData.Structures.Init(FStairModuleStructure(EStairModuleType::Tread, ETurningSocket::Idle), TreadsCount);

	// Hide adjustment tools by default
	HideAdjustmentTools();
}

void AStairsBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Generate stairs mesh in construction script for Blueprint editor preview
	// Note: We don't generate walls here since they require LotManager which isn't available in editor
	if (IsInEditorPreview() && StairsMeshComponent)
	{
		// CRITICAL: Clear existing data to prevent state accumulation
		// OnConstruction can be called multiple times when properties change in editor
		StairsMeshComponent->ClearAllMeshSections();
		StairsMeshComponent->StairsDataArray.Empty();
		StairsMeshComponent->StairsFreeIndices.Empty();

		// Ensure direction is valid
		if (StairsDirection.IsNearlyZero())
		{
			StairsDirection = FVector::ForwardVector;
		}

		// Regenerate default structures based on current TreadsCount value
		// This allows Blueprint child classes to specify different default tread counts
		if (TreadsCount > 0)
		{
			StairsData.Structures.Empty();
			StairsData.Structures.Init(FStairModuleStructure(EStairModuleType::Tread, ETurningSocket::Idle), TreadsCount);
		}

		// Call virtual method to generate mesh - unconditional like RoofBase
		// Internal validation will handle missing meshes gracefully
		GenerateStairsMesh();

		// Apply materials if set (similar to RoofBase pattern)
		if ((TreadMaterial || LandingMaterial) && StairsData.StairModules.Num() > 0)
		{
			for (int32 i = 0; i < StairsData.StairModules.Num(); ++i)
			{
				if (StairsData.StairModules.IsValidIndex(i) &&
					StairsData.StairModules[i] &&
					StairsData.Structures.IsValidIndex(i))
				{
					UMaterialInterface* MaterialToUse = (StairsData.Structures[i].StairType == EStairModuleType::Tread)
						? TreadMaterial
						: LandingMaterial;

					if (MaterialToUse)
					{
						StairsData.StairModules[i]->SetMaterial(0, MaterialToUse);
					}
				}
			}
		}
	}

	// Setup adjustment tools if in game world
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		SetupAdjustmentTools();
	}
}

void AStairsBase::BeginPlay()
{
	Super::BeginPlay();
}

void AStairsBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up stair modules
	DestroyStairs();

	Super::EndPlay(EndPlayReason);
}

void AStairsBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only handle clicks for committed (placed) stairs
	if (!bCommitted)
	{
		// Still handle landing tool drag for uncommitted stairs
		HandleLandingToolDrag();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
		return;

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
		return;

	// Check if deletion tool is active - if so, don't intercept clicks for edit mode
	ABurbPawn* BurbPawn = Cast<ABurbPawn>(PC->GetPawn());
	if (BurbPawn && BurbPawn->CurrentBuildTool)
	{
		// If the deletion tool is active, let it handle the click instead of entering edit mode
		if (BurbPawn->CurrentBuildTool->bDeletionMode)
		{
			// Handle landing tool drag if active (still need this for edit mode)
			HandleLandingToolDrag();
			return;
		}
	}

	// Check if left mouse button was just pressed this frame
	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		// Perform line trace from mouse cursor
		FHitResult HitResult;
		bool bHit = PC->GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

		if (bHit)
		{
			// Check if the hit actor is this stairs or any of its components
			AActor* HitActor = HitResult.GetActor();
			bool bClickedOnThisStairs = (HitActor == this);

			// Also check if clicked on one of our components
			if (!bClickedOnThisStairs && HitResult.Component.IsValid())
			{
				// Check if clicked on our mesh component
				if (HitResult.Component.Get() == StairsMeshComponent)
				{
					bClickedOnThisStairs = true;
				}

				// Check if clicked on any stair module
				for (UStaticMeshComponent* Module : StairsData.StairModules)
				{
					if (Module && HitResult.Component.Get() == Module)
					{
						bClickedOnThisStairs = true;
						break;
					}
				}

				// Check if clicked on one of our gizmo components
				UGizmoComponent* HitGizmo = Cast<UGizmoComponent>(HitResult.Component.Get());
				if (HitGizmo)
				{
					// Check if this gizmo belongs to this stairs
					bClickedOnThisStairs = (HitGizmo->GetOwner() == this);
				}
			}

			if (bInEditMode)
			{
				// We're in edit mode - check if clicked away to exit
				if (!bClickedOnThisStairs)
				{
					UE_LOG(LogTemp, Log, TEXT("StairsBase: Clicked away from stairs, exiting edit mode"));
					ExitEditMode();
				}
				else
				{
					// Clicked on this stairs while in edit mode - check if it was a gizmo
					// Manually fire the click handler since UE's OnClicked delegate doesn't fire
					// when using manual click detection in Tick()
					UGizmoComponent* HitGizmo = Cast<UGizmoComponent>(HitResult.Component.Get());
					if (HitGizmo && HitGizmo->GetOwner() == this)
					{
						// Manually trigger the gizmo's OnClicked delegate
						HitGizmo->OnClicked.Broadcast(HitGizmo, EKeys::LeftMouseButton);
					}
				}
			}
			else
			{
				// We're NOT in edit mode - check if clicked on stairs to enter
				if (bClickedOnThisStairs)
				{
					UE_LOG(LogTemp, Log, TEXT("StairsBase: Clicked on stairs, entering edit mode"));
					EnterEditMode();
				}
			}
		}
	}

	// Handle landing tool drag if active
	HandleLandingToolDrag();
}

void AStairsBase::HandleLandingToolDrag()
{
	if (bInEditMode && CurrentAdjustmentToolType == EScaleStairsToolType::SelectionAdjustment)
	{
		UGizmoComponent* ActiveGizmo = GetAdjustmentToolByType(CurrentAdjustmentToolType);
		if (ActiveGizmo)
		{
			bool bIsDraggingNow = ActiveGizmo->bIsDragging;

			if (bIsDraggingNow)
			{
				// Currently dragging - update direction
				LandingToolDragged();
			}
			else if (bWasDraggingLastFrame && !bIsDraggingNow)
			{
				// Just finished dragging - apply the turn
				OnLandingDragComplete();
			}

			bWasDraggingLastFrame = bIsDraggingNow;
		}
	}
}

// ==================== INITIALIZATION ====================

void AStairsBase::InitializeStairs(ALotManager* InLotManager, const FVector& StartLocation, const FVector& Direction,
	const TArray<FStairModuleStructure>& Structures, UStaticMesh* InTreadMesh, UStaticMesh* InLandingMesh,
	float InStairsThickness)
{
	LotManager = InLotManager;
	StairsDirection = Direction;
	StairsThickness = InStairsThickness;
	StairTreadMesh = InTreadMesh;
	StairLandingMesh = InLandingMesh;

	// Set actor location
	SetActorLocation(StartLocation);

	// Setup stairs structures
	StairsData.Structures = Structures;

	// Set the LotManager on the component (similar to RoofBase pattern)
	if (StairsMeshComponent && LotManager)
	{
		StairsMeshComponent->LotManager = LotManager;
	}

	// Generate the stairs mesh
	GenerateStairsMesh();

	// Setup adjustment tools
	SetupAdjustmentTools();
}

void AStairsBase::GenerateStairsMesh()
{
	if (!StairsMeshComponent)
	{
		return;
	}

	// Safety check: ensure we have valid structures
	if (StairsData.Structures.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsBase::GenerateStairsMesh - No structures defined, skipping mesh generation"));
		return;
	}

	// Safety check: ensure we have valid meshes (like RoofBase doesn't need this, but stairs require mesh assets)
	if (!StairTreadMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsBase::GenerateStairsMesh - StairTreadMesh is null, skipping mesh generation (set in Blueprint)"));
		return;
	}

	if (!StairLandingMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsBase::GenerateStairsMesh - StairLandingMesh is null, skipping mesh generation (set in Blueprint)"));
		return;
	}

	// Setup stairs data with all configuration
	// IMPORTANT: StartLoc must be in local space (relative to this actor) because
	// StairsComponent uses SetRelativeLocation. The actor itself is positioned at the world location.
	// Position first step at front edge of tile (actor is at tile center, so offset forward by half tile)
	float HalfTileSize = LotManager ? (LotManager->GridTileSize * 0.5f) : 50.0f;
	StairsData.StartLoc = FVector(HalfTileSize, 0, 11.0f);  // Half tile forward, 11 units up to sit on ground level
	StairsData.Direction = StairsDirection;
	StairsData.StairsThickness = StairsThickness;
	StairsData.Level = Level;
	StairsData.StairTreadMesh = StairTreadMesh;
	StairsData.StairLandingMesh = StairLandingMesh;
	StairsData.TreadMaterial = TreadMaterial;
	StairsData.LandingMaterial = LandingMaterial;
	StairsData.bCommitted = false;  // Never commit during mesh generation

	// Pass data to component
	StairsMeshComponent->StairsData = StairsData;

	// CRITICAL: Destroy old stair modules before generating new ones
	// GenerateStairs() creates NEW mesh components, so we must clean up old ones first
	// Otherwise they stack/accumulate causing flying stairs bugs
	StairsMeshComponent->DestroyStairs();

	// Generate mesh via component - this calls the component's mesh generation logic
	StairsMeshComponent->GenerateStairs();

	// Copy the updated data back (includes calculated vertices, roof tunnel location, etc.)
	StairsData = StairsMeshComponent->StairsData;

	// Apply materials to generated modules (with safety checks)
	if ((TreadMaterial || LandingMaterial) && StairsData.StairModules.Num() > 0)
	{
		for (int32 i = 0; i < StairsData.StairModules.Num(); ++i)
		{
			if (StairsData.StairModules.IsValidIndex(i) &&
			    StairsData.StairModules[i] &&
			    StairsData.Structures.IsValidIndex(i))
			{
				UMaterialInterface* MaterialToUse = (StairsData.Structures[i].StairType == EStairModuleType::Tread)
					? TreadMaterial
					: LandingMaterial;

				if (MaterialToUse)
				{
					StairsData.StairModules[i]->SetMaterial(0, MaterialToUse);
				}
			}
		}
	}

	// Setup adjustment tools if in edit mode
	if (bInEditMode)
	{
		SetupAdjustmentTools();
	}
}

void AStairsBase::UpdateStairsMesh()
{
	// Clear existing mesh and regenerate (similar to RoofBase pattern)
	if (StairsMeshComponent)
	{
		StairsMeshComponent->ClearAllMeshSections();
		GenerateStairsMesh();
		SetupAdjustmentTools();
	}
}

void AStairsBase::CommitStairs()
{
	if (bCommitted)
	{
		return;
	}

	// Mark as committed
	bCommitted = true;

	// Apply final materials to all stair modules
	for (int32 i = 0; i < StairsData.StairModules.Num(); ++i)
	{
		if (StairsData.StairModules[i] && StairsData.Structures.IsValidIndex(i))
		{
			UMaterialInterface* MaterialToUse = (StairsData.Structures[i].StairType == EStairModuleType::Tread)
				? TreadMaterial
				: LandingMaterial;

			if (MaterialToUse)
			{
				StairsData.StairModules[i]->SetMaterial(0, MaterialToUse);
			}
		}
	}

	// Exit edit mode and hide adjustment tools
	ExitEditMode();

	// Mark component as committed
	if (StairsMeshComponent)
	{
		StairsMeshComponent->StairsData.bCommitted = true;
		StairsMeshComponent->bCommitted = true;
	}

	// Cut away floor/terrain for stair opening
	CutAwayStairOpening();

	// Use actor's world location for logging since StairsData.StartLoc is in local space
	UE_LOG(LogTemp, Log, TEXT("StairsBase: Committed stairs at (%s), Level %d -> %d"), *GetActorLocation().ToString(), Level, TopLevel);
}

void AStairsBase::DestroyStairs()
{
	if (StairsMeshComponent)
	{
		StairsMeshComponent->DestroyStairs();
	}
}

// ==================== EDIT MODE ====================

void AStairsBase::EnterEditMode()
{
	if (bInEditMode)
	{
		return;
	}

	bInEditMode = true;

	// Register with BurbPawn as the current edit mode actor
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			PC->bEnableClickEvents = true;
			PC->bEnableMouseOverEvents = true;

			// Set this stairs as the current edit mode actor
			ABurbPawn* BurbPawn = Cast<ABurbPawn>(PC->GetPawn());
			if (BurbPawn)
			{
				BurbPawn->CurrentEditModeActor = this;
			}

			UE_LOG(LogTemp, Log, TEXT("StairsBase: Enabled click events on PlayerController"));
		}
	}

	// Enable custom depth rendering for selection outline
	if (StairsMeshComponent)
	{
		StairsMeshComponent->SetRenderCustomDepth(true);
		StairsMeshComponent->SetCustomDepthStencilValue(1);  // Edit mode stencil value
	}

	// Enable custom depth on all stair module meshes
	for (UStaticMeshComponent* Module : StairsData.StairModules)
	{
		if (Module)
		{
			Module->SetRenderCustomDepth(true);
			Module->SetCustomDepthStencilValue(1);
		}
	}

	SetupAdjustmentTools();
	ShowAdjustmentTools();

	UE_LOG(LogTemp, Log, TEXT("StairsBase: Entered edit mode, custom depth enabled"));
}

void AStairsBase::ExitEditMode()
{
	if (!bInEditMode)
	{
		return;
	}

	bInEditMode = false;

	// Unregister from BurbPawn
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			ABurbPawn* BurbPawn = Cast<ABurbPawn>(PC->GetPawn());
			if (BurbPawn && BurbPawn->CurrentEditModeActor == this)
			{
				BurbPawn->CurrentEditModeActor = nullptr;
			}
		}
	}

	// Disable custom depth rendering
	if (StairsMeshComponent)
	{
		StairsMeshComponent->SetRenderCustomDepth(false);
	}

	// Disable custom depth on all stair module meshes
	for (UStaticMeshComponent* Module : StairsData.StairModules)
	{
		if (Module)
		{
			Module->SetRenderCustomDepth(false);
		}
	}

	HideAdjustmentTools();
	CurrentAdjustmentToolType = EScaleStairsToolType::Empty;

	UE_LOG(LogTemp, Log, TEXT("StairsBase: Exited edit mode, custom depth disabled"));
}

void AStairsBase::ToggleEditMode()
{
	if (bInEditMode)
	{
		ExitEditMode();
	}
	else
	{
		EnterEditMode();
	}
}

void AStairsBase::RotateLeft()
{
	// Rotate actor counter-clockwise by 90 degrees using quaternions for numerical stability
	// This prevents floating-point drift and lighting artifacts over multiple rotations

	// Get current rotation as quaternion for stable math
	FQuat CurrentQuat = GetActorQuat();

	// Create 90-degree rotation quaternion around Z-axis (yaw, counter-clockwise)
	FQuat RotationDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));

	// Apply rotation using quaternion multiplication (order matters: Delta * Current)
	FQuat NewQuat = RotationDelta * CurrentQuat;

	// Convert back to rotator and snap yaw to exact 90-degree increments
	FRotator NewRotation = NewQuat.Rotator();
	NewRotation.Yaw = FMath::RoundToFloat(NewRotation.Yaw / 90.0f) * 90.0f;

	// Normalize yaw to [0, 360) range to prevent accumulation issues
	NewRotation.Yaw = FMath::Fmod(NewRotation.Yaw + 360.0f, 360.0f);

	// Apply the clean rotation
	SetActorRotation(NewRotation);

	// Update StairsDirection to match new rotation
	FVector ForwardVector = GetActorForwardVector();
	ForwardVector.Z = 0.0f;
	StairsDirection = ForwardVector.GetSafeNormal();

	UE_LOG(LogTemp, Log, TEXT("StairsBase: Rotated left to %s, new direction: %s"),
		*NewRotation.ToString(), *StairsDirection.ToString());
}

void AStairsBase::RotateRight()
{
	// Rotate actor clockwise by 90 degrees using quaternions for numerical stability
	// This prevents floating-point drift and lighting artifacts over multiple rotations

	// Get current rotation as quaternion for stable math
	FQuat CurrentQuat = GetActorQuat();

	// Create 90-degree rotation quaternion around Z-axis (yaw, clockwise)
	FQuat RotationDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0f));

	// Apply rotation using quaternion multiplication (order matters: Delta * Current)
	FQuat NewQuat = RotationDelta * CurrentQuat;

	// Convert back to rotator and snap yaw to exact 90-degree increments
	FRotator NewRotation = NewQuat.Rotator();
	NewRotation.Yaw = FMath::RoundToFloat(NewRotation.Yaw / 90.0f) * 90.0f;

	// Normalize yaw to [0, 360) range to prevent accumulation issues
	NewRotation.Yaw = FMath::Fmod(NewRotation.Yaw + 360.0f, 360.0f);

	// Apply the clean rotation
	SetActorRotation(NewRotation);

	// Update StairsDirection to match new rotation
	FVector ForwardVector = GetActorForwardVector();
	ForwardVector.Z = 0.0f;
	StairsDirection = ForwardVector.GetSafeNormal();

	UE_LOG(LogTemp, Log, TEXT("StairsBase: Rotated right to %s, new direction: %s"),
		*NewRotation.ToString(), *StairsDirection.ToString());
}

// ==================== ADJUSTMENT TOOLS ====================

void AStairsBase::SetupAdjustmentTools()
{
	// Adjustment tools are positioned by ShowAdjustmentTools() when entering edit mode.
	// StartingStep and EndingStep gizmos are reserved for future use (e.g., adjusting stair length).
	// LandingTool gizmo is positioned at DefaultLandingIndex to allow adding turns.
	//
	// This method ensures tools start hidden until explicitly shown via EnterEditMode().
	if (!StairsMeshComponent || StairsData.StairModules.Num() == 0)
	{
		return;
	}

	HideAdjustmentTools();
}

void AStairsBase::HideAdjustmentTools()
{
	if (AdjustmentTool_StartingStep)
	{
		AdjustmentTool_StartingStep->HideGizmo();  // Use proper HideGizmo method
	}
	if (AdjustmentTool_EndingStep)
	{
		AdjustmentTool_EndingStep->HideGizmo();  // Use proper HideGizmo method
	}
	if (AdjustmentTool_LandingTool)
	{
		AdjustmentTool_LandingTool->HideGizmo();  // Use proper HideGizmo method
	}
}

void AStairsBase::ShowAdjustmentTools()
{
	// Show landing tool at default landing position (step 4)
	if (AdjustmentTool_LandingTool && StairsData.StairModules.Num() > DefaultLandingIndex)
	{
		// Position gizmo at the default landing step
		UStaticMeshComponent* LandingStepMesh = StairsData.StairModules[DefaultLandingIndex];
		if (LandingStepMesh)
		{
			FVector GizmoPosition = LandingStepMesh->GetComponentLocation();
			GizmoPosition.Z += 50.0f;  // Offset above step

			AdjustmentTool_LandingTool->SetWorldLocation(GizmoPosition);
			AdjustmentTool_LandingTool->SetWorldRotation(FRotator::ZeroRotator);  // Ensure no rotation inheritance
			AdjustmentTool_LandingTool->ShowGizmo();  // Use proper ShowGizmo method

			// Set selected module index to default landing position
			SelectedModuleIndex = DefaultLandingIndex;

			UE_LOG(LogTemp, Log, TEXT("StairsBase: Landing gizmo shown at step %d, position: %s"),
				DefaultLandingIndex, *GizmoPosition.ToString());
		}
	}
}

void AStairsBase::OnAdjustmentToolClicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	UE_LOG(LogTemp, Log, TEXT("StairsBase::OnAdjustmentToolClicked - Component '%s' clicked with %s"),
		TouchedComponent ? *TouchedComponent->GetName() : TEXT("null"), *ButtonPressed.ToString());

	UGizmoComponent* GizmoComp = Cast<UGizmoComponent>(TouchedComponent);
	if (!GizmoComp)
	{
		return;
	}

	CurrentAdjustmentToolType = GetAdjustmentToolType(GizmoComp);

	// Only landing tool supports drag - others are for future use
	if (CurrentAdjustmentToolType != EScaleStairsToolType::SelectionAdjustment)
		return;

	// Start drag operation
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			FVector WorldLocation, WorldDirection;
			PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection);
			DragCreateVectors.StartOperation = WorldLocation;
			DragCreateVectors.EndOperation = WorldLocation + WorldDirection * 10000;

			GizmoComp->StartDrag(WorldLocation);

			// Store which step the gizmo is on (use SelectedModuleIndex)
			// This was set when the user clicked the tread to show the gizmo
			UE_LOG(LogTemp, Log, TEXT("StairsBase: Started drag on landing tool at step %d, world pos: %s"),
				SelectedModuleIndex, *WorldLocation.ToString());
		}
	}
}

void AStairsBase::AdjustmentToolDragged()
{
	// Legacy method - now handled by LandingToolDragged in Tick
}

void AStairsBase::LandingToolDragged()
{
	UGizmoComponent* ActiveGizmo = GetAdjustmentToolByType(CurrentAdjustmentToolType);
	if (!ActiveGizmo || !ActiveGizmo->bIsDragging)
		return;

	// Get current mouse position and intersect with a fixed horizontal plane at gizmo height
	// This prevents camera rotation from affecting the drag
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			FVector WorldLocation, WorldDirection;
			PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection);

			// Create a horizontal plane at the gizmo's Z height
			FVector GizmoLocation = ActiveGizmo->GetComponentLocation();
			FPlane DragPlane(GizmoLocation, FVector::UpVector);

			// Intersect mouse ray with the drag plane
			FVector IntersectionPoint = FMath::RayPlaneIntersection(WorldLocation, WorldDirection, DragPlane);

			// Update drag with the plane intersection point (not raw mouse world position)
			ActiveGizmo->UpdateDrag(IntersectionPoint);

			// Get drag delta
			FVector Delta = ActiveGizmo->GetConstrainedDragDelta();

			// Normalize to 2D direction (ignore Z)
			FVector2D DragDirection2D(Delta.X, Delta.Y);

			// Only process if drag is significant
			if (DragDirection2D.Size() < 50.0f)
				return;

			DragDirection2D.Normalize();

			// Transform drag direction from world space to stairs local space
			// This gives us the drag direction relative to the stairs' current forward direction
			FRotator StairsRotation = GetActorRotation();
			FVector WorldDrag = FVector(Delta.X, Delta.Y, 0.0f);
			FVector LocalDrag = StairsRotation.UnrotateVector(WorldDrag);

			// Determine turn direction based on local drag
			// Local Y-axis = left/right relative to stairs forward
			ETurningSocket TurnSocket = ETurningSocket::Idle;
			if (FMath::Abs(LocalDrag.Y) > FMath::Abs(LocalDrag.X))
			{
				// Drag is primarily lateral (left/right relative to stairs)
				TurnSocket = (LocalDrag.Y > 0) ? ETurningSocket::Right : ETurningSocket::Left;
			}

			// Apply the turn to provide live visual feedback
			// Use SelectedModuleIndex if valid, otherwise fall back to DefaultLandingIndex
			int32 LandingIndex = (SelectedModuleIndex >= 0) ? SelectedModuleIndex : DefaultLandingIndex;

			// Update stairs structure with the new turn (live preview)
			UpdateStairStructure(TurnSocket, LandingIndex);
		}
	}
}

void AStairsBase::OnLandingDragComplete()
{
	// Finalize the drag operation
	// Stairs were already updated during LandingToolDragged() for live preview
	// Just need to clean up gizmo and state

	UE_LOG(LogTemp, Log, TEXT("StairsBase: Landing drag complete. Turn finalized at landing index: %d"),
		(SelectedModuleIndex >= 0) ? SelectedModuleIndex : DefaultLandingIndex);

	// Hide gizmo after applying turn
	if (AdjustmentTool_LandingTool)
	{
		AdjustmentTool_LandingTool->HideGizmo();  // Use proper HideGizmo method
	}

	// Reset adjustment tool state
	CurrentAdjustmentToolType = EScaleStairsToolType::Empty;
	SelectedModuleIndex = -1;
	bWasDraggingLastFrame = false;
}

UGizmoComponent* AStairsBase::GetAdjustmentToolByType(EScaleStairsToolType Type)
{
	switch (Type)
	{
		case EScaleStairsToolType::StartingStep:
			return AdjustmentTool_StartingStep;
		case EScaleStairsToolType::EndingStep:
			return AdjustmentTool_EndingStep;
		case EScaleStairsToolType::SelectionAdjustment:
			return AdjustmentTool_LandingTool;
		default:
			return nullptr;
	}
}

int32 AStairsBase::FindNearestStairTread(UStaticMeshComponent* HitMesh)
{
	if (!StairsMeshComponent)
	{
		return -1;
	}

	return StairsMeshComponent->FindNearestStairTread(HitMesh);
}

void AStairsBase::UpdateStairStructure(ETurningSocket TurnSocket, int32 LandingIndex)
{
	// Validate landing index is at a valid section boundary
	if (LandingIndex > 0 && LandingIndex < TreadsCount)
	{
		if (LandingIndex % StepsPerSection != 0)
		{
			return;  // Reject invalid landing positions
		}
	}

	// Store the landing index
	LastLandingIndex = LandingIndex;

	// Rebuild the entire Structures array with proper turn
	// This prevents stacking/flying stairs bugs by ensuring clean structure
	StairsData.Structures.Empty();
	StairsData.Structures.Reserve(TreadsCount);

	// Add treads before the landing (all straight/Idle)
	for (int32 i = 0; i < LandingIndex; ++i)
	{
		StairsData.Structures.Add(FStairModuleStructure(EStairModuleType::Tread, ETurningSocket::Idle));
	}

	// Add the landing with Idle (required for offset calculation in GenerateStairs)
	// The landing itself is always Idle - the turn comes from the tread after it
	StairsData.Structures.Add(FStairModuleStructure(EStairModuleType::Landing, ETurningSocket::Idle));

	// Add the first tread after landing with the turn socket
	// THIS is what creates the actual turn in the new direction
	if (LandingIndex + 1 < TreadsCount)
	{
		StairsData.Structures.Add(FStairModuleStructure(EStairModuleType::Tread, TurnSocket));
	}

	// Add remaining treads (all straight/Idle in the new direction)
	for (int32 i = LandingIndex + 2; i < TreadsCount; ++i)
	{
		StairsData.Structures.Add(FStairModuleStructure(EStairModuleType::Tread, ETurningSocket::Idle));
	}

	// Regenerate stairs with new structure
	GenerateStairsMesh();
}

// ==================== UTILITY ====================

bool AStairsBase::FindExistingStairs(const FVector& StartLoc, const TArray<FStairModuleStructure>& Structures)
{
	if (!LotManager)
	{
		return false;
	}

	// Check if any existing committed stairs actors are at this location with matching structure
	return LotManager->FindStairsActorAtLocation(StartLoc, Structures) != nullptr;
}

int32 AStairsBase::GetSectionIDFromHitResult(const FHitResult& HitResult) const
{
	if (!StairsMeshComponent)
	{
		return -1;
	}

	return StairsMeshComponent->GetSectionIDFromHitResult(HitResult);
}

// ==================== PRIVATE METHODS ====================

void AStairsBase::SetupAdjustmentToolComponent(UGizmoComponent* Component, EGizmoType Type)
{
	if (!Component)
	{
		return;
	}

	Component->SetupAttachment(RootSceneComponent);
	Component->GizmoType = Type;

	// CRITICAL: Set a static mesh so there's something to click on
	// Without a mesh, the gizmo has no geometry to intercept ray traces
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMesh.Succeeded())
	{
		Component->SetStaticMesh(SphereMesh.Object);
	}

	// Set colors for visual feedback (matching RoofBase pattern)
	Component->SetColors(
		FLinearColor(0.5f, 0.5f, 1.0f, 1.0f),  // Blue-gray default
		FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),  // Yellow hover
		FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)   // Green active
	);

	// Note: Gizmo component manages its own visibility and collision state internally
	// It starts hidden and will be shown when ShowGizmo() is called
	// Collision is already configured in GizmoComponent constructor

	// Bind click event - using OnClicked directly since handler expects UPrimitiveComponent*
	Component->OnClicked.AddDynamic(this, &AStairsBase::OnAdjustmentToolClicked);

	// Exclude gizmos from all rendering systems except direct view
	Component->SetVisibleInSceneCaptureOnly(false);
	Component->bVisibleInReflectionCaptures = false;
	Component->bVisibleInRayTracing = false;
	Component->bVisibleInRealTimeSkyCaptures = false;

	// Set auto-hide
	Component->bAutoHide = true;
}

void AStairsBase::UpdateAdjustmentToolVisibility()
{
	if (bInEditMode)
	{
		ShowAdjustmentTools();
	}
	else
	{
		HideAdjustmentTools();
	}
}

EScaleStairsToolType AStairsBase::GetAdjustmentToolType(UGizmoComponent* Component)
{
	if (Component == AdjustmentTool_StartingStep)
	{
		return EScaleStairsToolType::StartingStep;
	}
	else if (Component == AdjustmentTool_EndingStep)
	{
		return EScaleStairsToolType::EndingStep;
	}
	else if (Component == AdjustmentTool_LandingTool)
	{
		return EScaleStairsToolType::SelectionAdjustment;
	}

	return EScaleStairsToolType::Empty;
}

bool AStairsBase::IsInEditorPreview() const
{
	// Check if we have a valid world and if it's not a game world
	// This returns true when in Blueprint editor viewport or construction script preview
	UWorld* World = GetWorld();
	return World && !World->IsGameWorld();
}

// ==================== VALIDATION METHODS ====================

bool AStairsBase::ValidatePlacement()
{
	// Reset validation state
	bValidPlacement = false;
	ValidationError = TEXT("");

	// Calculate top level (stairs go up one level)
	TopLevel = Level + 1;

	// Check if LotManager exists
	if (!LotManager)
	{
		ValidationError = TEXT("No lot manager");
		return false;
	}

	// Check if both levels exist
	if (!DoesLevelExist(Level))
	{
		ValidationError = TEXT("Bottom level does not exist");
		return false;
	}

	if (!DoesLevelExist(TopLevel))
	{
		ValidationError = TEXT("Top level does not exist - need at least 2 levels");
		return false;
	}

	// Get bottom landing tile (tile in front of first step)
	FIntVector BottomLanding = GetBottomLandingTile();
	if (!HasFloorOrTerrainAtTile(Level, BottomLanding.X, BottomLanding.Y))
	{
		ValidationError = TEXT("No floor/terrain at bottom of stairs");
		return false;
	}

	// Get top landing tile (tile in front of last step)
	FIntVector TopLanding = GetTopLandingTile();
	if (!HasFloorOrTerrainAtTile(TopLevel, TopLanding.X, TopLanding.Y))
	{
		ValidationError = TEXT("No floor at top of stairs");
		return false;
	}

	// All checks passed
	bValidPlacement = true;
	ValidationError = TEXT("");
	return true;
}

bool AStairsBase::DoesLevelExist(int32 CheckLevel) const
{
	if (!LotManager)
	{
		return false;
	}

	// Level numbering: Basements are negative (-1, -2, etc.), Ground = 0, Upper floors = positive (1, 2, etc.)
	// Valid range: [-Basements, Floors]
	// Example: With 1 basement and 2 floors, valid levels are: -1, 0, 1, 2
	int32 MinLevel = -LotManager->Basements;
	int32 MaxLevel = LotManager->Floors;

	return (CheckLevel >= MinLevel && CheckLevel <= MaxLevel);
}

bool AStairsBase::HasFloorOrTerrainAtTile(int32 CheckLevel, int32 Row, int32 Column) const
{
	if (!LotManager)
	{
		return false;
	}

	// Check grid bounds
	if (Row < 0 || Row >= LotManager->GridSizeX || Column < 0 || Column >= LotManager->GridSizeY)
	{
		return false;
	}

	// For basement level (Level < 0) or ground level (Level == 0), check terrain
	if (CheckLevel <= 0 && LotManager->TerrainComponent)
	{
		FTerrainSegmentData* TerrainTile = LotManager->TerrainComponent->FindTerrainTile(0, Row, Column);
		if (TerrainTile)
		{
			return true;
		}
	}

	// Check for floor tile at any level
	if (LotManager->FloorComponent)
	{
		// Check if any triangle exists at this tile
		if (LotManager->FloorComponent->HasAnyFloorTile(CheckLevel, Row, Column))
		{
			return true;
		}
	}

	return false;
}

FIntVector AStairsBase::GetBottomLandingTile() const
{
	// Get the tile in front of the first step (opposite to stair direction)
	// Stairs go UP in their direction, so the bottom landing is BEHIND the start point

	if (!LotManager)
	{
		return FIntVector(0, 0, Level);
	}

	// Get actor location (tile center where stairs are placed)
	FVector ActorLoc = GetActorLocation();

	// Convert to grid coordinates
	int32 Row, Column;
	if (!LotManager->LocationToTile(ActorLoc, Row, Column))
	{
		return FIntVector(0, 0, Level);
	}

	// Get direction opposite to stairs (where player stands to climb up)
	// Stairs face in StairsDirection, so landing is BEHIND the stair base
	FVector LandingDirection = -GetActorForwardVector();
	LandingDirection.Z = 0;
	LandingDirection.Normalize();

	// Calculate landing tile (one tile behind the stair start)
	// Grid coordinate mapping: World X → Column, World Y → Row
	int32 LandingRow = Row + FMath::RoundToInt(LandingDirection.Y);
	int32 LandingCol = Column + FMath::RoundToInt(LandingDirection.X);

	return FIntVector(LandingRow, LandingCol, Level);
}

FIntVector AStairsBase::GetTopLandingTile() const
{
	// Get the tile in front of the last step (in stair direction at top level)
	// This is where player exits the stairs at the top

	if (!LotManager)
	{
		return FIntVector(0, 0, TopLevel);
	}

	// Get actor location (tile center where stairs are placed)
	FVector ActorLoc = GetActorLocation();

	// Convert to grid coordinates
	int32 Row, Column;
	if (!LotManager->LocationToTile(ActorLoc, Row, Column))
	{
		return FIntVector(0, 0, TopLevel);
	}

	// Get stair direction (where stairs go up to)
	FVector StairDir = GetActorForwardVector();
	StairDir.Z = 0;
	StairDir.Normalize();

	// Calculate how many tiles the stairs span based on tread count
	// Each tread is approximately GridTileSize / 4 in depth (25 units for 100 unit tiles)
	// With 12 treads, stairs span roughly 3 tiles in length
	float TreadDepth = 25.0f;  // Standard tread depth
	float StairLength = TreadsCount * TreadDepth;
	int32 TilesSpanned = FMath::CeilToInt(StairLength / LotManager->GridTileSize);

	// Top landing tile is at the end of the stairs in the stair direction
	// Grid coordinate mapping: World X → Column, World Y → Row
	int32 TopRow = Row + FMath::RoundToInt(StairDir.Y * TilesSpanned);
	int32 TopCol = Column + FMath::RoundToInt(StairDir.X * TilesSpanned);

	return FIntVector(TopRow, TopCol, TopLevel);
}

TArray<FIntVector> AStairsBase::GetBottomLevelFootprint() const
{
	TArray<FIntVector> Footprint;

	if (!LotManager)
	{
		return Footprint;
	}

	// Get actor location
	FVector ActorLoc = GetActorLocation();

	// Convert to grid coordinates
	int32 Row, Column;
	if (!LotManager->LocationToTile(ActorLoc, Row, Column))
	{
		return Footprint;
	}

	// Get stair direction
	FVector StairDir = GetActorForwardVector();
	StairDir.Z = 0;
	StairDir.Normalize();

	// Calculate tiles occupied by stairs at bottom level
	// Stairs typically occupy 1-2 tiles width and extend in the stair direction
	float TreadDepth = 25.0f;
	float StairLength = TreadsCount * TreadDepth;
	int32 TilesSpanned = FMath::CeilToInt(StairLength / LotManager->GridTileSize);

	// Add tiles along the stair direction
	for (int32 i = 0; i < TilesSpanned; ++i)
	{
		// Grid coordinate mapping: World X → Column, World Y → Row
		int32 TileRow = Row + FMath::RoundToInt(StairDir.Y * i);
		int32 TileCol = Column + FMath::RoundToInt(StairDir.X * i);

		if (TileRow >= 0 && TileRow < LotManager->GridSizeX &&
			TileCol >= 0 && TileCol < LotManager->GridSizeY)
		{
			Footprint.Add(FIntVector(TileRow, TileCol, Level));
		}
	}

	return Footprint;
}

TArray<FIntVector> AStairsBase::GetTopLevelFootprint() const
{
	TArray<FIntVector> Footprint;

	if (!LotManager)
	{
		return Footprint;
	}

	// Get actor location
	FVector ActorLoc = GetActorLocation();

	// Convert to grid coordinates
	int32 Row, Column;
	if (!LotManager->LocationToTile(ActorLoc, Row, Column))
	{
		return Footprint;
	}

	// Get stair direction
	FVector StairDir = GetActorForwardVector();
	StairDir.Z = 0;
	StairDir.Normalize();

	// Calculate tiles at top level where stairs emerge
	// This is typically the last 1-2 tiles of the stair run
	float TreadDepth = 25.0f;
	float StairLength = TreadsCount * TreadDepth;
	int32 TilesSpanned = FMath::CeilToInt(StairLength / LotManager->GridTileSize);

	// The top opening is at the END of the stair run
	// Typically just 1 tile where the stair emerges through the floor
	// Grid coordinate mapping: World X → Column, World Y → Row
	int32 TopRow = Row + FMath::RoundToInt(StairDir.Y * (TilesSpanned - 1));
	int32 TopCol = Column + FMath::RoundToInt(StairDir.X * (TilesSpanned - 1));

	if (TopRow >= 0 && TopRow < LotManager->GridSizeX &&
		TopCol >= 0 && TopCol < LotManager->GridSizeY)
	{
		Footprint.Add(FIntVector(TopRow, TopCol, TopLevel));
	}

	return Footprint;
}

// ==================== CUTAWAY METHODS ====================

void AStairsBase::CutAwayStairOpening()
{
	if (!LotManager || bCutawayApplied)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("StairsBase::CutAwayStairOpening - Cutting floor/terrain for stairs at Level %d -> %d"), Level, TopLevel);

	// Clear previous removed tiles
	RemovedFloorTiles.Empty();
	RemovedTerrainTiles.Empty();

	// Get the tiles at the top level where we need to cut the floor
	TArray<FIntVector> TopFootprint = GetTopLevelFootprint();

	// Also need to cut the tile at the start of the stairs (where stairs begin)
	FVector ActorLoc = GetActorLocation();
	int32 StartRow, StartCol;
	if (LotManager->LocationToTile(ActorLoc, StartRow, StartCol))
	{
		// Get stair direction to determine which tiles to cut
		FVector StairDir = GetActorForwardVector();
		StairDir.Z = 0;
		StairDir.Normalize();

		// Calculate all tiles that stairs pass through
		float TreadDepth = 25.0f;
		float StairLength = TreadsCount * TreadDepth;
		int32 TilesSpanned = FMath::CeilToInt(StairLength / LotManager->GridTileSize);

		// Cut floor at TopLevel for all tiles where stairs emerge
		// Offset by 2 tiles to align with where stairs actually emerge at the top
		const int32 StairStartOffset = 2;

		if (LotManager->FloorComponent)
		{
			LotManager->FloorComponent->BeginBatchOperation();

			for (int32 i = 0; i < TilesSpanned; ++i)
			{
				// Grid coordinate mapping: World X → Column, World Y → Row
				int32 TileRow = StartRow + FMath::RoundToInt(StairDir.Y * (i - StairStartOffset));
				int32 TileCol = StartCol + FMath::RoundToInt(StairDir.X * (i - StairStartOffset));

				// Check grid bounds
				if (TileRow < 0 || TileRow >= LotManager->GridSizeX ||
					TileCol < 0 || TileCol >= LotManager->GridSizeY)
				{
					continue;
				}

				// Remove floor tiles at TopLevel (the floor above the stairs)
				if (LotManager->FloorComponent->HasAnyFloorTile(TopLevel, TileRow, TileCol))
				{
					// Store for undo
					RemovedFloorTiles.Add(FIntVector(TileRow, TileCol, TopLevel));

					// Remove all triangles at this tile
					LotManager->FloorComponent->RemoveFloorTile(TopLevel, TileRow, TileCol, ETriangleType::Top);
					LotManager->FloorComponent->RemoveFloorTile(TopLevel, TileRow, TileCol, ETriangleType::Right);
					LotManager->FloorComponent->RemoveFloorTile(TopLevel, TileRow, TileCol, ETriangleType::Bottom);
					LotManager->FloorComponent->RemoveFloorTile(TopLevel, TileRow, TileCol, ETriangleType::Left);

					UE_LOG(LogTemp, Log, TEXT("  - Removed floor at TopLevel %d, Row %d, Col %d"), TopLevel, TileRow, TileCol);
				}
			}

			LotManager->FloorComponent->EndBatchOperation();
		}

		// Remove terrain at ground level where stairs pass through
		// Terrain exists at GroundLevel (Basements), not at Level 0
		// Use RemoveTerrainRegion like the pool tool does for proper heightmap handling
		const int32 GroundLevel = LotManager->Basements;

		if (LotManager->TerrainComponent)
		{
			// Calculate the min/max tile coordinates for the terrain region to remove
			int32 MinRow = INT_MAX, MinCol = INT_MAX;
			int32 MaxRow = INT_MIN, MaxCol = INT_MIN;

			for (int32 i = 0; i < TilesSpanned; ++i)
			{
				// Grid coordinate mapping: World X → Column, World Y → Row
				int32 TileRow = StartRow + FMath::RoundToInt(StairDir.Y * (i - StairStartOffset));
				int32 TileCol = StartCol + FMath::RoundToInt(StairDir.X * (i - StairStartOffset));

				// Check grid bounds
				if (TileRow < 0 || TileRow >= LotManager->GridSizeX ||
					TileCol < 0 || TileCol >= LotManager->GridSizeY)
				{
					continue;
				}

				// Track min/max for region removal
				MinRow = FMath::Min(MinRow, TileRow);
				MinCol = FMath::Min(MinCol, TileCol);
				MaxRow = FMath::Max(MaxRow, TileRow);
				MaxCol = FMath::Max(MaxCol, TileCol);

				// Store for undo
				RemovedTerrainTiles.Add(FIntVector(TileRow, TileCol, GroundLevel));
			}

			// Remove terrain region (like pool tool does) - handles heightmap properly
			if (MinRow <= MaxRow && MinCol <= MaxCol)
			{
				UE_LOG(LogTemp, Log, TEXT("  - Removing terrain region from (%d,%d) to (%d,%d) at GroundLevel %d"),
					MinRow, MinCol, MaxRow, MaxCol, GroundLevel);

				LotManager->TerrainComponent->RemoveTerrainRegion(GroundLevel, MinRow, MinCol, MaxRow, MaxCol);
			}
		}
	}

	bCutawayApplied = true;
	UE_LOG(LogTemp, Log, TEXT("StairsBase::CutAwayStairOpening - Cutaway complete. Removed %d floor tiles, %d terrain tiles"),
		RemovedFloorTiles.Num(), RemovedTerrainTiles.Num());
}

void AStairsBase::RestoreStairOpening()
{
	if (!LotManager || !bCutawayApplied)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("StairsBase::RestoreStairOpening - Restoring %d floor tiles, %d terrain tiles"),
		RemovedFloorTiles.Num(), RemovedTerrainTiles.Num());

	// Restore floor tiles
	if (LotManager->FloorComponent && RemovedFloorTiles.Num() > 0)
	{
		LotManager->FloorComponent->BeginBatchOperation();

		for (const FIntVector& TileCoord : RemovedFloorTiles)
		{
			// Create floor tile data
			FFloorTileData TileData;
			TileData.Level = TileCoord.Z;
			TileData.Row = TileCoord.X;
			TileData.Column = TileCoord.Y;
			TileData.bCommitted = true;

			// Restore all four triangles
			for (int32 TriIdx = 0; TriIdx < 4; ++TriIdx)
			{
				TileData.Triangle = static_cast<ETriangleType>(TriIdx);
				LotManager->FloorComponent->AddFloorTile(TileData, LotManager->DefaultFloorMaterial);
			}

			UE_LOG(LogTemp, Log, TEXT("  - Restored floor at Level %d, Row %d, Col %d"), TileCoord.Z, TileCoord.X, TileCoord.Y);
		}

		LotManager->FloorComponent->EndBatchOperation();
	}

	// Restore terrain tiles
	if (LotManager->TerrainComponent && RemovedTerrainTiles.Num() > 0)
	{
		LotManager->TerrainComponent->BeginBatchOperation();

		for (const FIntVector& TileCoord : RemovedTerrainTiles)
		{
			// Get world position for terrain tile
			FVector TilePos;
			if (LotManager->TileToGridLocation(TileCoord.Z, TileCoord.X, TileCoord.Y, true, TilePos))
			{
				// Create terrain segment
				LotManager->TerrainComponent->GenerateTerrainSection(TileCoord.X, TileCoord.Y, TilePos, LotManager->DefaultTerrainMaterial);

				UE_LOG(LogTemp, Log, TEXT("  - Restored terrain at Row %d, Col %d"), TileCoord.X, TileCoord.Y);
			}
		}

		LotManager->TerrainComponent->EndBatchOperation();
	}

	// Clear stored tiles
	RemovedFloorTiles.Empty();
	RemovedTerrainTiles.Empty();
	bCutawayApplied = false;

	UE_LOG(LogTemp, Log, TEXT("StairsBase::RestoreStairOpening - Restore complete"));
}

// ==================== IDELETABLE INTERFACE ====================

bool AStairsBase::CanBeDeleted_Implementation() const
{
	// Only allow deletion if the stairs has been committed (finalized)
	// Uncommitted preview stairs should be managed by their tools
	return bCommitted;
}

void AStairsBase::OnDeleted_Implementation()
{
	// Exit edit mode before deletion
	ExitEditMode();

	// Restore floor/terrain that was cut away when stairs were committed
	RestoreStairOpening();

	// Remove from LotManager's StairsActors array
	if (LotManager)
	{
		LotManager->StairsActors.Remove(this);
		UE_LOG(LogTemp, Log, TEXT("StairsBase: OnDeleted - Removed from LotManager StairsActors array"));
	}

	UE_LOG(LogTemp, Log, TEXT("StairsBase: OnDeleted - Cleanup complete"));
}

bool AStairsBase::IsSelected_Implementation() const
{
	// Stairs is considered selected when it's in edit mode
	return bInEditMode;
}

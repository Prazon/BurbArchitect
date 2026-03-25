// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/Roofs/RoofBase.h"
#include "Actors/LotManager.h"
#include "Actors/BurbPawn.h"
#include "Components/RoofComponent.h"
#include "Components/GizmoComponent.h"
#include "Components/WallComponent.h"
#include "Components/FootprintComponent.h"
#include "Components/RoomManagerComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"

ARoofBase::ARoofBase()
{
	// Set this actor to call Tick() every frame
	PrimaryActorTick.bCanEverTick = true;

	// Create root component
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;

	// Create roof mesh component
	// NOTE: Not attached to root to avoid rotation inheritance issues
	// Mesh vertices are calculated in world space, component stays at world origin with no rotation
	RoofMeshComponent = CreateDefaultSubobject<URoofComponent>(TEXT("RoofMeshComponent"));

	// Create footprint component
	FootprintComponent = CreateDefaultSubobject<UFootprintComponent>(TEXT("FootprintComponent"));
	FootprintComponent->SetupAttachment(RootSceneComponent);

	// Create scale tool gizmos
	// NOTE: Gizmos ARE attached to RootSceneComponent to enable input events (OnClicked, OnBeginCursorOver)
	// We attach with EAttachmentRule::KeepWorld in BeginPlay() to maintain world-space positioning
	// This ensures MovementAxis vectors work correctly AND input events fire
	ScaleTool_Height = CreateDefaultSubobject<UGizmoComponent>(TEXT("ScaleTool_Height"));
	SetupScaleToolComponent(ScaleTool_Height, EGizmoType::Height);
	ScaleTool_Height->SetupAttachment(RootSceneComponent);

	ScaleTool_FrontExtent = CreateDefaultSubobject<UGizmoComponent>(TEXT("ScaleTool_FrontExtent"));
	SetupScaleToolComponent(ScaleTool_FrontExtent, EGizmoType::FrontExtent);
	ScaleTool_FrontExtent->SetupAttachment(RootSceneComponent);

	ScaleTool_BackExtent = CreateDefaultSubobject<UGizmoComponent>(TEXT("ScaleTool_BackExtent"));
	SetupScaleToolComponent(ScaleTool_BackExtent, EGizmoType::BackExtent);
	ScaleTool_BackExtent->SetupAttachment(RootSceneComponent);

	ScaleTool_LeftExtent = CreateDefaultSubobject<UGizmoComponent>(TEXT("ScaleTool_LeftExtent"));
	SetupScaleToolComponent(ScaleTool_LeftExtent, EGizmoType::LeftExtent);
	ScaleTool_LeftExtent->SetupAttachment(RootSceneComponent);

	ScaleTool_RightExtent = CreateDefaultSubobject<UGizmoComponent>(TEXT("ScaleTool_RightExtent"));
	SetupScaleToolComponent(ScaleTool_RightExtent, EGizmoType::RightExtent);
	ScaleTool_RightExtent->SetupAttachment(RootSceneComponent);

	// Initialize properties
	RoofThickness = 15.0f;
	GableThickness = 20.0f;
	RoofDirection = FVector::ForwardVector;
	Level = 0;
	bInEditMode = false;
	bCommitted = false;
	CurrentScaleToolType = EScaleToolType::Empty;

	// Initialize default dimensions
	RoofDimensions = FRoofDimensions(200, 200, 200, 200, 50, 50, 50, 50, 200);
}

void ARoofBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// ONLY generate mesh in editor preview (Blueprint editor viewport), NOT during game world spawning
	// In game, InitializeRoof() will handle mesh generation
	//
	// Check multiple conditions to ensure we're truly in editor preview:
	// 1. World exists
	// 2. World is NOT a game world (PIE, standalone game, etc.)
	// 3. We're in the editor (WITH_EDITOR is defined)
	// 4. LotManager is null (it gets set during InitializeRoof in runtime)
	UWorld* World = GetWorld();
	const bool bIsEditorPreview = World &&
	                              !World->IsGameWorld() &&
	                              !LotManager; // LotManager is only set during runtime initialization

	if (bIsEditorPreview)
	{
		// Generate roof mesh in construction script for Blueprint editor preview
		// Note: We don't generate walls here since they require LotManager which isn't available in editor
		if (RoofMeshComponent)
		{
			// CRITICAL: Clear existing data to prevent state accumulation and array index crashes
			// OnConstruction can be called multiple times when properties change in editor
			RoofMeshComponent->ClearAllMeshSections();
			RoofMeshComponent->RoofDataArray.Empty();
			RoofMeshComponent->RoofFreeIndices.Empty();

			// Ensure direction is valid
			if (RoofDirection.IsNearlyZero())
			{
				RoofDirection = FVector::ForwardVector;
			}

			// Call virtual method to generate mesh - child classes override this
			// This allows AGableRoof, AHipRoof, AShedRoof to use their specific implementations
			GenerateRoofMesh();

			// Apply material if set
			if (RoofMaterial)
			{
				RoofMeshComponent->SetMaterial(0, RoofMaterial);
			}

			// Draw footprint visualization in editor preview
			DrawFootprintLines();
		}
	}
}

void ARoofBase::BeginPlay()
{
	Super::BeginPlay();

	// Start with scale tools hidden
	HideScaleTools();
}

void ARoofBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up any walls created by this roof
	CleanupWalls();

	// Clean up footprint visualization
	ClearFootprintLines();

	Super::EndPlay(EndPlayReason);
}

void ARoofBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Sync mesh component location when not attached (preview mode or anytime)
	if (RoofMeshComponent)
	{
		FVector ActorLoc = GetActorLocation();
		if (!RoofMeshComponent->GetComponentLocation().Equals(ActorLoc, 1.0f))
		{
			RoofMeshComponent->SetWorldLocation(ActorLoc, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// Only handle clicks for committed (placed) roofs
	if (!bCommitted)
		return;

	UWorld* World = GetWorld();
	if (!World)
		return;

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
		return;

	// Check if left mouse button was just pressed this frame
	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		// Perform line trace from mouse cursor
		FHitResult HitResult;
		bool bHit = PC->GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

		if (bHit)
		{
			// Check if the hit actor is this roof or any of its components
			AActor* HitActor = HitResult.GetActor();
			bool bClickedOnThisRoof = (HitActor == this);

			// Also check if clicked on one of our gizmo components
			if (!bClickedOnThisRoof && HitResult.Component.IsValid())
			{
				UGizmoComponent* HitGizmo = Cast<UGizmoComponent>(HitResult.Component.Get());
				if (HitGizmo)
				{
					// Check if this gizmo belongs to this roof
					bClickedOnThisRoof = (HitGizmo->GetOwner() == this);
				}
			}

			if (bInEditMode)
			{
				// We're in edit mode - check if clicked away to exit
				if (!bClickedOnThisRoof)
				{
					UE_LOG(LogTemp, Log, TEXT("RoofBase: Clicked away from roof, exiting edit mode"));
					ExitEditMode();
				}
				else
				{
					// Clicked on this roof while in edit mode - check if it was a gizmo
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
				// We're NOT in edit mode - check if clicked on roof to enter
				if (bClickedOnThisRoof)
				{
					UE_LOG(LogTemp, Log, TEXT("RoofBase: Clicked on roof, entering edit mode"));
					EnterEditMode();
				}
			}
		}
	}

	// Handle scale tool drag operations
	if (bInEditMode && CurrentScaleToolType != EScaleToolType::Empty)
	{
		UGizmoComponent* ActiveGizmo = GetScaleToolByType(CurrentScaleToolType);
		if (ActiveGizmo && ActiveGizmo->bIsDragging)
		{
			// Process drag movement
			ScaleToolDragged();
		}

		// Detect drag end (mouse button released)
		if (PC->WasInputKeyJustReleased(EKeys::LeftMouseButton))
		{
			if (ActiveGizmo)
			{
				ActiveGizmo->EndDrag();
			}

			// Apply snapped dimensions on release
			RoofDimensions = SnappedDimensions;

			CurrentScaleToolType = EScaleToolType::Empty;

			// IMPORTANT: Clean up walls BEFORE UpdateRoofMesh()
			// UpdateRoofMesh() regenerates RoofData which clears CreatedWallIndices
			if (bCommitted)
			{
				CleanupWalls();
			}

			// Update mesh to snapped dimensions
			UpdateRoofMesh();

			// Reposition scale tools to match new snapped dimensions
			SetupScaleTools();

			// Regenerate supporting walls with new dimensions
			if (bCommitted)
			{
				GenerateSupportingWalls();
			}

			UE_LOG(LogTemp, Log, TEXT("RoofBase: Scale tool drag completed, applied snapped dimensions"));
		}
	}
}

void ARoofBase::InitializeRoof(ALotManager* InLotManager, const FVector& Location, const FVector& Direction,
	const FRoofDimensions& InDimensions, float InRoofThickness, float InGableThickness)
{
	LotManager = InLotManager;
	RoofDimensions = InDimensions;
	RoofThickness = InRoofThickness;
	GableThickness = InGableThickness;
	RoofDirection = Direction;

	// Set actor location
	SetActorLocation(Location);

	// Since RoofMeshComponent is not attached, manually position it at actor location
	// This ensures proper bounds calculation and local-space vertex conversion
	if (RoofMeshComponent)
	{
		RoofMeshComponent->SetWorldLocation(Location);
		RoofMeshComponent->SetWorldRotation(FRotator::ZeroRotator); // Always identity rotation
	}

	// Set the LotManager on the component
	if (RoofMeshComponent && LotManager)
	{
		RoofMeshComponent->LotManager = LotManager;
	}

	// SAFETY: Clear any existing mesh data before generating
	// This prevents duplicate meshes if OnConstruction somehow ran during spawning
	if (RoofMeshComponent)
	{
		RoofMeshComponent->ClearAllMeshSections();
		RoofMeshComponent->RoofDataArray.Empty();
		RoofMeshComponent->RoofFreeIndices.Empty();
	}

	// Generate the roof mesh
	GenerateRoofMesh();

	// Setup scale tools
	SetupScaleTools();

	// Draw footprint visualization
	DrawFootprintLines();
}

void ARoofBase::GenerateRoofMesh()
{
	if (!RoofMeshComponent)
		return;

	// Setup roof data
	FRoofSegmentData RoofData;
	RoofData.Location = GetActorLocation();
	RoofData.Direction = RoofDirection;
	RoofData.Dimensions = RoofDimensions;
	RoofData.RoofThickness = RoofThickness;
	RoofData.GableThickness = GableThickness;
	RoofData.SectionIndex = 0;
	RoofData.Level = Level;
	RoofData.bCommitted = false;  // Never commit during mesh generation

	// IMPORTANT: LotManager handling for editor vs runtime
	// - Editor Preview: Set to nullptr to prevent wall generation (walls require LotManager)
	// - Runtime: Also nullptr here - walls are generated later in CommitRoof()
	// This keeps mesh generation separate from wall generation
	RoofData.LotManager = nullptr;

	// Generate mesh via component (without walls)
	// Note: Child classes (AGableRoof, AHipRoof, AShedRoof) override this method
	// to call their specific mesh generation methods (GenerateGableRoofMesh, etc.)
	RoofMeshComponent->RoofData = RoofMeshComponent->GenerateRoofMeshSection(RoofData);

	// Don't store wall indices during mesh generation
	// CreatedWallIndices will be populated when CommitRoof() calls GenerateSupportingWalls()

	// Apply material based on committed state
	if (!bCommitted && LotManager && LotManager->ValidPreviewMaterial)
	{
		// Preview mode: use valid preview material
		RoofMeshComponent->SetMaterial(0, LotManager->ValidPreviewMaterial);
	}
	else if (RoofMaterial)
	{
		// Committed or no preview material: use actual roof material
		RoofMeshComponent->SetMaterial(0, RoofMaterial);
	}
}

void ARoofBase::UpdateRoofMesh()
{
	// Clear existing mesh and regenerate
	if (RoofMeshComponent)
	{
		RoofMeshComponent->ClearAllMeshSections();
		GenerateRoofMesh();

		// Reposition scale tools to match new roof geometry
		// SetupScaleTools() now intelligently skips the actively dragging gizmo
		SetupScaleTools();

		// Update footprint visualization
		if (CurrentScaleToolType == EScaleToolType::Empty)
		{
			DrawFootprintLines();
		}
		// During drag, footprint is updated manually in ScaleToolDragged() with snapped dimensions
	}
}

void ARoofBase::CommitRoof()
{
	if (bCommitted)
		return;

	bCommitted = true;

	// Switch from valid preview material to actual roof material
	if (RoofMeshComponent && RoofMaterial)
	{
		RoofMeshComponent->SetMaterial(0, RoofMaterial);
	}

	// Generate supporting walls
	GenerateSupportingWalls();

	// Exit edit mode and hide scale tools
	ExitEditMode();

	// Clear footprint lines - no longer needed after commit
	ClearFootprintLines();

	// Mark roof component as committed
	if (RoofMeshComponent)
	{
		RoofMeshComponent->RoofData.bCommitted = true;
		RoofMeshComponent->bCommitted = true;
	}
}

void ARoofBase::EnterEditMode()
{
	if (!bCommitted)  // Only allow edit mode for committed roofs
		return;

	bInEditMode = true;

	// Register with BurbPawn as the current edit mode actor
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			PC->bEnableClickEvents = true;
			PC->bEnableMouseOverEvents = true;

			// Set this roof as the current edit mode actor
			ABurbPawn* BurbPawn = Cast<ABurbPawn>(PC->GetPawn());
			if (BurbPawn)
			{
				BurbPawn->CurrentEditModeActor = this;
			}

			UE_LOG(LogTemp, Log, TEXT("RoofBase: Enabled click events on PlayerController"));
		}
	}

	// Setup scale tools at current roof geometry before showing them
	SetupScaleTools();

	ShowScaleTools();

	// Show footprint lines when entering edit mode
	DrawFootprintLines();

	// Enable custom depth for selection highlight
	if (RoofMeshComponent)
	{
		RoofMeshComponent->SetRenderCustomDepth(true);
		RoofMeshComponent->SetCustomDepthStencilValue(1);  // Selected stencil value
	}
}

void ARoofBase::ExitEditMode()
{
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

	HideScaleTools();

	// Hide footprint lines when exiting edit mode
	ClearFootprintLines();

	// Disable custom depth when deselecting
	if (RoofMeshComponent)
	{
		RoofMeshComponent->SetRenderCustomDepth(false);
		RoofMeshComponent->SetCustomDepthStencilValue(0);
	}
}

void ARoofBase::ToggleEditMode()
{
	if (bInEditMode)
		ExitEditMode();
	else
		EnterEditMode();
}

void ARoofBase::RotateLeft()
{
	// Rotate actor counter-clockwise by 45 degrees using quaternions for numerical stability
	// This prevents floating-point drift and lighting artifacts over multiple rotations

	// Get current rotation as quaternion for stable math
	FQuat CurrentQuat = GetActorQuat();

	// Create 45-degree rotation quaternion around Z-axis (yaw, counter-clockwise)
	FQuat RotationDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(-45.0f));

	// Apply rotation using quaternion multiplication (order matters: Delta * Current)
	FQuat NewQuat = RotationDelta * CurrentQuat;

	// Convert back to rotator and snap yaw to exact 45-degree increments
	FRotator NewRotation = NewQuat.Rotator();
	NewRotation.Yaw = FMath::RoundToFloat(NewRotation.Yaw / 45.0f) * 45.0f;

	// Normalize yaw to [0, 360) range to prevent accumulation issues
	NewRotation.Yaw = FMath::Fmod(NewRotation.Yaw + 360.0f, 360.0f);

	// Apply the clean rotation
	SetActorRotation(NewRotation);

	// Update RoofDirection to match new rotation
	FVector ForwardVector = GetActorForwardVector();
	ForwardVector.Z = 0.0f;
	RoofDirection = ForwardVector.GetSafeNormal();

	// Ensure mesh component stays at actor location with identity rotation
	if (RoofMeshComponent)
	{
		RoofMeshComponent->SetWorldLocation(GetActorLocation());
		RoofMeshComponent->SetWorldRotation(FRotator::ZeroRotator);
	}

	// NOTE: Dimensions are kept constant - they are relative to RoofDirection
	// When RoofDirection rotates, the same dimensions produce a rotated roof in world space
	// We do NOT modify RoofDimensions here - that would change the roof shape!

	// IMPORTANT: Clean up walls BEFORE UpdateRoofMesh()
	// UpdateRoofMesh() regenerates RoofData which clears CreatedWallIndices
	if (bCommitted)
	{
		CleanupWalls();
	}

	// Regenerate mesh with new rotation
	UpdateRoofMesh();

	// Regenerate supporting walls with new orientation
	if (bCommitted)
	{
		GenerateSupportingWalls();
	}

	// Update footprint lines if in edit mode
	if (bInEditMode)
	{
		DrawFootprintLines();
	}

	// Update scale tool positions to match new rotation
	if (bInEditMode)
	{
		SetupScaleTools();
	}

	UE_LOG(LogTemp, Log, TEXT("RoofBase: Rotated left to %s, new direction: %s"),
		*NewRotation.ToString(), *RoofDirection.ToString());
}

void ARoofBase::RotateRight()
{
	// Rotate actor clockwise by 45 degrees using quaternions for numerical stability
	// This prevents floating-point drift and lighting artifacts over multiple rotations

	// Get current rotation as quaternion for stable math
	FQuat CurrentQuat = GetActorQuat();

	// Create 45-degree rotation quaternion around Z-axis (yaw, clockwise)
	FQuat RotationDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0f));

	// Apply rotation using quaternion multiplication (order matters: Delta * Current)
	FQuat NewQuat = RotationDelta * CurrentQuat;

	// Convert back to rotator and snap yaw to exact 45-degree increments
	FRotator NewRotation = NewQuat.Rotator();
	NewRotation.Yaw = FMath::RoundToFloat(NewRotation.Yaw / 45.0f) * 45.0f;

	// Normalize yaw to [0, 360) range to prevent accumulation issues
	NewRotation.Yaw = FMath::Fmod(NewRotation.Yaw + 360.0f, 360.0f);

	// Apply the clean rotation
	SetActorRotation(NewRotation);

	// Update RoofDirection to match new rotation
	FVector ForwardVector = GetActorForwardVector();
	ForwardVector.Z = 0.0f;
	RoofDirection = ForwardVector.GetSafeNormal();

	// Ensure mesh component stays at actor location with identity rotation
	if (RoofMeshComponent)
	{
		RoofMeshComponent->SetWorldLocation(GetActorLocation());
		RoofMeshComponent->SetWorldRotation(FRotator::ZeroRotator);
	}

	// NOTE: Dimensions are kept constant - they are relative to RoofDirection
	// When RoofDirection rotates, the same dimensions produce a rotated roof in world space
	// We do NOT modify RoofDimensions here - that would change the roof shape!

	// IMPORTANT: Clean up walls BEFORE UpdateRoofMesh()
	// UpdateRoofMesh() regenerates RoofData which clears CreatedWallIndices
	if (bCommitted)
	{
		CleanupWalls();
	}

	// Regenerate mesh with new rotation
	UpdateRoofMesh();

	// Regenerate supporting walls with new orientation
	if (bCommitted)
	{
		GenerateSupportingWalls();
	}

	// Update footprint lines if in edit mode
	if (bInEditMode)
	{
		DrawFootprintLines();
	}

	// Update scale tool positions to match new rotation
	if (bInEditMode)
	{
		SetupScaleTools();
	}

	UE_LOG(LogTemp, Log, TEXT("RoofBase: Rotated right to %s, new direction: %s"),
		*NewRotation.ToString(), *RoofDirection.ToString());
}

void ARoofBase::SetupScaleTools()
{
	if (!RoofMeshComponent)
		return;

	// Calculate roof vertices for positioning
	FRoofVertices RoofVerts = URoofComponent::CalculateRoofVertices(
		GetActorLocation(), RoofDirection, RoofDimensions);

	// Get actor location for converting world positions to relative positions
	FVector ActorLoc = GetActorLocation();

	// Position height tool at peak (use relative positioning to respect Blueprint transform hierarchy)
	if (ScaleTool_Height)
	{
		FVector PeakCenter = (RoofVerts.PeakFront + RoofVerts.PeakBack) * 0.5f;
		FVector RelativePos = PeakCenter - ActorLoc;
		ScaleTool_Height->SetRelativeLocation(RelativePos);
		ScaleTool_Height->SetRelativeRotation(FRotator::ZeroRotator);
		ScaleTool_Height->MovementAxis = FVector::UpVector;
		ScaleTool_Height->bConstrainToAxis = true;
	}

	// Position extent tools at wall bases (use relative positioning)
	if (ScaleTool_FrontExtent)
	{
		FVector FrontBase = (RoofVerts.GableFrontRight + RoofVerts.GableFrontLeft) * 0.5f;
		FVector RelativePos = FrontBase - ActorLoc;
		ScaleTool_FrontExtent->SetRelativeLocation(RelativePos);
		ScaleTool_FrontExtent->SetRelativeRotation(FRotator::ZeroRotator);
		ScaleTool_FrontExtent->MovementAxis = RoofDirection;
		ScaleTool_FrontExtent->bConstrainToAxis = true;
	}

	if (ScaleTool_BackExtent)
	{
		FVector BackBase = (RoofVerts.GableBackRight + RoofVerts.GableBackLeft) * 0.5f;
		FVector RelativePos = BackBase - ActorLoc;
		ScaleTool_BackExtent->SetRelativeLocation(RelativePos);
		ScaleTool_BackExtent->SetRelativeRotation(FRotator::ZeroRotator);
		ScaleTool_BackExtent->MovementAxis = -RoofDirection;
		ScaleTool_BackExtent->bConstrainToAxis = true;
	}

	if (ScaleTool_RightExtent)
	{
		FVector RightBase = (RoofVerts.GableFrontRight + RoofVerts.GableBackRight) * 0.5f;
		FVector RelativePos = RightBase - ActorLoc;
		ScaleTool_RightExtent->SetRelativeLocation(RelativePos);
		ScaleTool_RightExtent->SetRelativeRotation(FRotator::ZeroRotator);
		ScaleTool_RightExtent->MovementAxis = RoofDirection.RotateAngleAxis(-90, FVector::UpVector);
		ScaleTool_RightExtent->bConstrainToAxis = true;
	}

	if (ScaleTool_LeftExtent)
	{
		FVector LeftBase = (RoofVerts.GableFrontLeft + RoofVerts.GableBackLeft) * 0.5f;
		FVector RelativePos = LeftBase - ActorLoc;
		ScaleTool_LeftExtent->SetRelativeLocation(RelativePos);
		ScaleTool_LeftExtent->SetRelativeRotation(FRotator::ZeroRotator);
		ScaleTool_LeftExtent->MovementAxis = RoofDirection.RotateAngleAxis(90, FVector::UpVector);
		ScaleTool_LeftExtent->bConstrainToAxis = true;
	}
}

void ARoofBase::HideScaleTools()
{
	if (ScaleTool_Height) ScaleTool_Height->HideGizmo();
	if (ScaleTool_FrontExtent) ScaleTool_FrontExtent->HideGizmo();
	if (ScaleTool_BackExtent) ScaleTool_BackExtent->HideGizmo();
	if (ScaleTool_LeftExtent) ScaleTool_LeftExtent->HideGizmo();
	if (ScaleTool_RightExtent) ScaleTool_RightExtent->HideGizmo();
}

void ARoofBase::ShowScaleTools()
{
	// Show tools that are valid for this roof type
	if (ScaleTool_Height && IsScaleToolValid(EScaleToolType::Peak))
		ScaleTool_Height->ShowGizmo();

	if (ScaleTool_FrontExtent && IsScaleToolValid(EScaleToolType::FrontWall))
		ScaleTool_FrontExtent->ShowGizmo();

	if (ScaleTool_BackExtent && IsScaleToolValid(EScaleToolType::BackWall))
		ScaleTool_BackExtent->ShowGizmo();

	if (ScaleTool_LeftExtent && IsScaleToolValid(EScaleToolType::LeftWall))
		ScaleTool_LeftExtent->ShowGizmo();

	if (ScaleTool_RightExtent && IsScaleToolValid(EScaleToolType::RightWall))
		ScaleTool_RightExtent->ShowGizmo();
}

void ARoofBase::OnScaleToolClicked(UGizmoComponent* TouchedComponent, FKey ButtonPressed)
{
	UE_LOG(LogTemp, Log, TEXT("RoofBase::OnScaleToolClicked - Gizmo '%s' clicked with %s"),
		TouchedComponent ? *TouchedComponent->GetName() : TEXT("null"), *ButtonPressed.ToString());

	// Find which gizmo was clicked
	UGizmoComponent* ClickedGizmo = TouchedComponent;
	if (!ClickedGizmo)
		return;

	CurrentScaleToolType = GetScaleToolType(ClickedGizmo);

	// Store original dimensions for snap-on-release
	OriginalDragDimensions = RoofDimensions;
	SnappedDimensions = RoofDimensions;

	// Start drag operation
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			FVector WorldLocation, WorldDirection;
			PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection);

			// Calculate reference point on roof geometry (same as drag update logic)
			FRoofVertices RoofVerts = URoofComponent::CalculateRoofVertices(
				GetActorLocation(), RoofDirection, RoofDimensions);

			FVector ReferencePoint;
			switch (CurrentScaleToolType)
			{
			case EScaleToolType::Peak:
				ReferencePoint = (RoofVerts.PeakFront + RoofVerts.PeakBack) * 0.5f;
				break;
			case EScaleToolType::FrontWall:
			case EScaleToolType::FrontRake:
				ReferencePoint = (RoofVerts.GableFrontRight + RoofVerts.GableFrontLeft) * 0.5f;
				break;
			case EScaleToolType::BackWall:
			case EScaleToolType::BackRake:
				ReferencePoint = (RoofVerts.GableBackRight + RoofVerts.GableBackLeft) * 0.5f;
				break;
			case EScaleToolType::RightWall:
			case EScaleToolType::RightEve:
				ReferencePoint = (RoofVerts.GableFrontRight + RoofVerts.GableBackRight) * 0.5f;
				break;
			case EScaleToolType::LeftWall:
			case EScaleToolType::LeftEve:
				ReferencePoint = (RoofVerts.GableFrontLeft + RoofVerts.GableBackLeft) * 0.5f;
				break;
			default:
				ReferencePoint = ClickedGizmo->GetComponentLocation();
				break;
			}

			FVector AxisDir = ClickedGizmo->MovementAxis.GetSafeNormal();

			// Project mouse ray onto axis
			FVector RayStart = WorldLocation;
			FVector RayDir = WorldDirection.GetSafeNormal();
			FVector w0 = RayStart - ReferencePoint;
			float b = FVector::DotProduct(AxisDir, RayDir);
			float d = FVector::DotProduct(AxisDir, w0);
			float e = FVector::DotProduct(RayDir, w0);
			float denom = 1.0f - b * b;  // Simplified since both vectors are normalized
			float t = (denom != 0.0f) ? ((b * e - d) / denom) : 0.0f;
			FVector PointOnAxis = ReferencePoint + AxisDir * t;

			DragCreateVectors.StartOperation = PointOnAxis;
			DragCreateVectors.EndOperation = PointOnAxis + AxisDir * 10000;

			ClickedGizmo->StartDrag(PointOnAxis);
		}
	}
}

void ARoofBase::OnSubmitClicked(UGizmoComponent* TouchedComponent, FKey ButtonPressed)
{
	// Exit edit mode when confirm is clicked
	ExitEditMode();

	// Could also trigger a save or other confirmation action here
	UE_LOG(LogTemp, Log, TEXT("Roof configuration confirmed"));
}

void ARoofBase::ScaleToolDragged()
{
	if (CurrentScaleToolType == EScaleToolType::Empty)
		return;

	UGizmoComponent* ActiveGizmo = GetScaleToolByType(CurrentScaleToolType);
	if (!ActiveGizmo || !ActiveGizmo->bIsDragging)
		return;

	// Get current mouse position
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC)
		{
			FVector WorldLocation, WorldDirection;
			PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection);

			// Calculate the reference point from ORIGINAL dimensions (before drag started)
			// This provides a stable reference that doesn't change as we scale
			// The gizmo visual position uses current RoofDimensions (updated by SetupScaleTools)
			// but drag calculations use the original starting position to prevent feedback loop
			FRoofVertices OriginalRoofVerts = URoofComponent::CalculateRoofVertices(
				GetActorLocation(), RoofDirection, OriginalDragDimensions);

			FVector ReferencePoint;
			switch (CurrentScaleToolType)
			{
			case EScaleToolType::Peak:
				ReferencePoint = (OriginalRoofVerts.PeakFront + OriginalRoofVerts.PeakBack) * 0.5f;
				break;
			case EScaleToolType::FrontWall:
			case EScaleToolType::FrontRake:
				ReferencePoint = (OriginalRoofVerts.GableFrontRight + OriginalRoofVerts.GableFrontLeft) * 0.5f;
				break;
			case EScaleToolType::BackWall:
			case EScaleToolType::BackRake:
				ReferencePoint = (OriginalRoofVerts.GableBackRight + OriginalRoofVerts.GableBackLeft) * 0.5f;
				break;
			case EScaleToolType::RightWall:
			case EScaleToolType::RightEve:
				ReferencePoint = (OriginalRoofVerts.GableFrontRight + OriginalRoofVerts.GableBackRight) * 0.5f;
				break;
			case EScaleToolType::LeftWall:
			case EScaleToolType::LeftEve:
				ReferencePoint = (OriginalRoofVerts.GableFrontLeft + OriginalRoofVerts.GableBackLeft) * 0.5f;
				break;
			default:
				ReferencePoint = ActiveGizmo->GetComponentLocation();
				break;
			}

			FVector AxisDir = ActiveGizmo->MovementAxis.GetSafeNormal();

			// Find closest point on the axis to the mouse ray
			// This is a line-to-line closest point calculation
			FVector RayStart = WorldLocation;
			FVector RayDir = WorldDirection.GetSafeNormal();

			// Calculate the closest points using the formula for line-to-line distance
			FVector w0 = RayStart - ReferencePoint;
			float a = FVector::DotProduct(AxisDir, AxisDir);  // Always 1 for normalized axis
			float b = FVector::DotProduct(AxisDir, RayDir);
			float c = FVector::DotProduct(RayDir, RayDir);    // Always 1 for normalized ray
			float d = FVector::DotProduct(AxisDir, w0);
			float e = FVector::DotProduct(RayDir, w0);

			float denom = a * c - b * b;
			float t = (denom != 0.0f) ? ((b * e - c * d) / denom) : 0.0f;

			// Point on axis (using reference point, not gizmo location)
			FVector PointOnAxis = ReferencePoint + AxisDir * t;

			// Update drag with the actual point on the axis
			ActiveGizmo->UpdateDrag(PointOnAxis);

			// Get constrained delta (should now be accurate along the axis)
			FVector Delta = ActiveGizmo->GetConstrainedDragDelta();

			// Get tile size for snapping extent gizmos
			float TileSize = LotManager ? LotManager->GridTileSize : 100.0f;

			// Track previous snapped dimensions to detect when footprint should update
			FRoofDimensions PreviousSnapped = SnappedDimensions;

			// Apply smooth delta to roof dimensions (no snapping during drag)
			// IMPORTANT: Delta is cumulative from drag start, so always use OriginalDragDimensions + Delta
			// All deltas are negated because the axis projection gives inverted movement
			switch (CurrentScaleToolType)
			{
			case EScaleToolType::Peak:
				RoofDimensions.Height = FMath::Max(50.0f, OriginalDragDimensions.Height - Delta.Z);
				// Height doesn't snap
				SnappedDimensions.Height = RoofDimensions.Height;
				break;

			case EScaleToolType::FrontRake:
				RoofDimensions.FrontRake = FMath::Max(0.0f, OriginalDragDimensions.FrontRake - FVector::DotProduct(Delta, RoofDirection));
				SnappedDimensions.FrontRake = RoofDimensions.FrontRake;
				break;

			case EScaleToolType::BackRake:
				RoofDimensions.BackRake = FMath::Max(0.0f, OriginalDragDimensions.BackRake + FVector::DotProduct(Delta, RoofDirection));
				SnappedDimensions.BackRake = RoofDimensions.BackRake;
				break;

			case EScaleToolType::RightEve:
				{
					FVector RightDir = RoofDirection.RotateAngleAxis(-90, FVector::UpVector);
					RoofDimensions.RightEve = FMath::Max(0.0f, OriginalDragDimensions.RightEve - FVector::DotProduct(Delta, RightDir));
					SnappedDimensions.RightEve = RoofDimensions.RightEve;
				}
				break;

			case EScaleToolType::LeftEve:
				{
					FVector LeftDir = RoofDirection.RotateAngleAxis(90, FVector::UpVector);
					RoofDimensions.LeftEve = FMath::Max(0.0f, OriginalDragDimensions.LeftEve - FVector::DotProduct(Delta, LeftDir));
					SnappedDimensions.LeftEve = RoofDimensions.LeftEve;
				}
				break;

			case EScaleToolType::FrontWall:
				{
					// Smooth scaling for visual feedback (use original + cumulative delta)
					float NewDistance = OriginalDragDimensions.FrontDistance - FVector::DotProduct(Delta, RoofDirection);
					RoofDimensions.FrontDistance = FMath::Max(50.0f, NewDistance);

					// Calculate snapped dimension for footprint
					SnappedDimensions.FrontDistance = FMath::RoundToFloat(NewDistance / TileSize) * TileSize;
					SnappedDimensions.FrontDistance = FMath::Max(TileSize, SnappedDimensions.FrontDistance);
				}
				break;

			case EScaleToolType::BackWall:
				{
					// Smooth scaling for visual feedback (use original + cumulative delta)
					float NewDistance = OriginalDragDimensions.BackDistance + FVector::DotProduct(Delta, RoofDirection);
					RoofDimensions.BackDistance = FMath::Max(50.0f, NewDistance);

					// Calculate snapped dimension for footprint
					SnappedDimensions.BackDistance = FMath::RoundToFloat(NewDistance / TileSize) * TileSize;
					SnappedDimensions.BackDistance = FMath::Max(TileSize, SnappedDimensions.BackDistance);
				}
				break;

			case EScaleToolType::RightWall:
				{
					// Smooth scaling for visual feedback (use original + cumulative delta)
					FVector RightDir = RoofDirection.RotateAngleAxis(-90, FVector::UpVector);
					float NewDistance = OriginalDragDimensions.RightDistance - FVector::DotProduct(Delta, RightDir);
					RoofDimensions.RightDistance = FMath::Max(50.0f, NewDistance);

					// Calculate snapped dimension for footprint
					SnappedDimensions.RightDistance = FMath::RoundToFloat(NewDistance / TileSize) * TileSize;
					SnappedDimensions.RightDistance = FMath::Max(TileSize, SnappedDimensions.RightDistance);
				}
				break;

			case EScaleToolType::LeftWall:
				{
					// Smooth scaling for visual feedback (use original + cumulative delta)
					FVector LeftDir = RoofDirection.RotateAngleAxis(90, FVector::UpVector);
					float NewDistance = OriginalDragDimensions.LeftDistance - FVector::DotProduct(Delta, LeftDir);
					RoofDimensions.LeftDistance = FMath::Max(50.0f, NewDistance);

					// Calculate snapped dimension for footprint
					SnappedDimensions.LeftDistance = FMath::RoundToFloat(NewDistance / TileSize) * TileSize;
					SnappedDimensions.LeftDistance = FMath::Max(TileSize, SnappedDimensions.LeftDistance);
				}
				break;

			case EScaleToolType::Edge:
				{
					// Scale all edges proportionally
					float ScaleFactor = 1.0f + (Delta.Size() / 1000.0f);
					RoofDimensions.FrontRake = OriginalDragDimensions.FrontRake * ScaleFactor;
					RoofDimensions.BackRake = OriginalDragDimensions.BackRake * ScaleFactor;
					RoofDimensions.RightEve = OriginalDragDimensions.RightEve * ScaleFactor;
					RoofDimensions.LeftEve = OriginalDragDimensions.LeftEve * ScaleFactor;
					SnappedDimensions = RoofDimensions;
				}
				break;
			}

			// Update the roof mesh with smooth dimensions
			UpdateRoofMesh();

			// Only update footprint if snapped dimensions changed (crossed tile boundary)
			bool bSnappedChanged = false;
			switch (CurrentScaleToolType)
			{
			case EScaleToolType::FrontWall:
				bSnappedChanged = !FMath::IsNearlyEqual(SnappedDimensions.FrontDistance, PreviousSnapped.FrontDistance, 1.0f);
				break;
			case EScaleToolType::BackWall:
				bSnappedChanged = !FMath::IsNearlyEqual(SnappedDimensions.BackDistance, PreviousSnapped.BackDistance, 1.0f);
				break;
			case EScaleToolType::RightWall:
				bSnappedChanged = !FMath::IsNearlyEqual(SnappedDimensions.RightDistance, PreviousSnapped.RightDistance, 1.0f);
				break;
			case EScaleToolType::LeftWall:
				bSnappedChanged = !FMath::IsNearlyEqual(SnappedDimensions.LeftDistance, PreviousSnapped.LeftDistance, 1.0f);
				break;
			}

			// Update footprint with snapped dimensions if changed
			if (bSnappedChanged)
			{
				DrawFootprintLines(&SnappedDimensions);
			}
		}
	}
}

UGizmoComponent* ARoofBase::GetScaleToolByType(EScaleToolType Type)
{
	switch (Type)
	{
	case EScaleToolType::Peak: return ScaleTool_Height;
	case EScaleToolType::Edge: return nullptr;  // AllEdges removed
	case EScaleToolType::FrontRake: return nullptr;  // FrontOverhang removed
	case EScaleToolType::BackRake: return nullptr;  // BackOverhang removed
	case EScaleToolType::RightEve: return nullptr;  // RightOverhang removed
	case EScaleToolType::LeftEve: return nullptr;  // LeftOverhang removed
	case EScaleToolType::FrontWall: return ScaleTool_FrontExtent;
	case EScaleToolType::BackWall: return ScaleTool_BackExtent;
	case EScaleToolType::LeftWall: return ScaleTool_LeftExtent;
	case EScaleToolType::RightWall: return ScaleTool_RightExtent;
	case EScaleToolType::Submit: return nullptr;  // Confirm button removed
	default: return nullptr;
	}
}

void ARoofBase::GenerateSupportingWalls()
{
	// This will be overridden in child classes to generate appropriate walls
	// For example, gable roofs need gable end walls, hip roofs don't
}

void ARoofBase::CleanupWalls()
{
	// Clean up walls created by this roof
	if (LotManager && LotManager->WallComponent)
	{
		for (int32 WallIndex : CreatedWallIndices)
		{
			if (WallIndex >= 0)
			{
				LotManager->WallComponent->RemoveWallSection(WallIndex);
			}
		}
		CreatedWallIndices.Empty();
	}

	// Also clean up walls stored in the component
	if (RoofMeshComponent)
	{
		for (int32 WallIndex : RoofMeshComponent->RoofData.CreatedWallIndices)
		{
			if (WallIndex >= 0 && LotManager && LotManager->WallComponent)
			{
				LotManager->WallComponent->RemoveWallSection(WallIndex);
			}
		}
		RoofMeshComponent->RoofData.CreatedWallIndices.Empty();
	}
}

void ARoofBase::DrawFootprintLines(const FRoofDimensions* CustomDimensions)
{
	if (!FootprintComponent)
		return;

	// Use custom dimensions if provided, otherwise use current dimensions
	const FRoofDimensions& DimensionsToUse = CustomDimensions ? *CustomDimensions : RoofDimensions;

	// Draw footprint using the component's API
	FootprintComponent->DrawFromRoofDimensions(GetActorLocation(), RoofDirection, DimensionsToUse);
}

void ARoofBase::ClearFootprintLines()
{
	if (FootprintComponent)
	{
		FootprintComponent->ClearFootprint();
	}
}

bool ARoofBase::IsScaleToolValid(EScaleToolType ToolType) const
{
	// Base implementation - all tools valid by default
	// Child classes can override to restrict based on roof type
	return true;
}

void ARoofBase::SetupScaleToolComponent(UGizmoComponent* Component, EGizmoType Type)
{
	if (!Component)
		return;

	// NOTE: This function does NOT attach the gizmo to any parent component
	// Gizmos remain in world space to avoid inheriting actor rotation
	// This ensures MovementAxis vectors work correctly at any rotation angle

	// Set gizmo type
	Component->GizmoType = Type;

	// Set default mesh (can be overridden in Blueprint)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMesh.Succeeded())
	{
		Component->SetStaticMesh(SphereMesh.Object);
	}

	// Set colors based on type
	if (Type == EGizmoType::Confirm)
	{
		Component->SetColors(
			FLinearColor(0.0f, 1.0f, 0.0f, 1.0f),  // Green default
			FLinearColor(0.0f, 1.0f, 0.5f, 1.0f),  // Light green hover
			FLinearColor(0.0f, 0.5f, 0.0f, 1.0f)   // Dark green active
		);
	}
	else
	{
		Component->SetColors(
			FLinearColor(0.5f, 0.5f, 1.0f, 1.0f),  // Blue-gray default
			FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),  // Yellow hover
			FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)   // Green active
		);
	}

	// Bind events
	Component->OnGizmoClicked.AddDynamic(this, &ARoofBase::OnScaleToolClicked);

	if (Type == EGizmoType::Confirm)
	{
		Component->OnGizmoClicked.AddDynamic(this, &ARoofBase::OnSubmitClicked);
	}

	// Note: Gizmo component manages its own visibility state internally
	// It starts hidden and will be shown when ShowGizmo() is called

	// Exclude gizmos from all rendering systems except direct view
	Component->SetVisibleInSceneCaptureOnly(false);  // Exclude from scene captures
	Component->bVisibleInReflectionCaptures = false;  // Exclude from reflection captures
	Component->bVisibleInRayTracing = false;          // Exclude from ray tracing
	Component->bVisibleInRealTimeSkyCaptures = false; // Exclude from real-time sky captures

	// Set auto-hide
	Component->bAutoHide = true;
}

void ARoofBase::UpdateScaleToolVisibility()
{
	if (bInEditMode)
		ShowScaleTools();
	else
		HideScaleTools();
}

EScaleToolType ARoofBase::GetScaleToolType(UGizmoComponent* Component)
{
	if (!Component)
		return EScaleToolType::Empty;

	// Map gizmo type to scale tool type
	switch (Component->GizmoType)
	{
	case EGizmoType::Height: return EScaleToolType::Peak;
	case EGizmoType::Edge: return EScaleToolType::Edge;
	case EGizmoType::FrontOverhang: return EScaleToolType::FrontRake;
	case EGizmoType::BackOverhang: return EScaleToolType::BackRake;
	case EGizmoType::RightOverhang: return EScaleToolType::RightEve;
	case EGizmoType::LeftOverhang: return EScaleToolType::LeftEve;
	case EGizmoType::FrontExtent: return EScaleToolType::FrontWall;
	case EGizmoType::BackExtent: return EScaleToolType::BackWall;
	case EGizmoType::LeftExtent: return EScaleToolType::LeftWall;
	case EGizmoType::RightExtent: return EScaleToolType::RightWall;
	case EGizmoType::Confirm: return EScaleToolType::Submit;
	default: return EScaleToolType::Empty;
	}
}

bool ARoofBase::IsInEditorPreview() const
{
	// Check if we have a valid world and if it's not a game world
	// This returns true when in Blueprint editor viewport or construction script preview
	UWorld* World = GetWorld();
	return World && !World->IsGameWorld();
}

// ==================== IDELETABLE INTERFACE ====================

bool ARoofBase::CanBeDeleted_Implementation() const
{
	// Only allow deletion if the roof has been committed (finalized)
	// Uncommitted preview roofs should be managed by their tools
	return bCommitted;
}

void ARoofBase::OnDeleted_Implementation()
{
	if (!LotManager || !LotManager->WallComponent || !LotManager->RoomManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoofBase: OnDeleted - Missing LotManager or components"));
		return;
	}

	// ========== STEP 1: Find wall edge IDs created by this roof ==========
	TSet<int32> RoofWallEdgeIDs;
	for (int32 WallArrayIndex : CreatedWallIndices)
	{
		if (LotManager->WallComponent->WallDataArray.IsValidIndex(WallArrayIndex))
		{
			int32 EdgeID = LotManager->WallComponent->WallDataArray[WallArrayIndex].WallEdgeID;
			if (EdgeID != -1)
			{
				RoofWallEdgeIDs.Add(EdgeID);
			}
		}
	}

	// ========== STEP 2: Find roof room(s) that have these walls as boundaries ==========
	TArray<int32> RoomsToDelete;
	for (const auto& RoomPair : LotManager->RoomManager->Rooms)
	{
		const FRoomData& Room = RoomPair.Value;

		// Skip if not at our level or not a roof room
		if (Room.Level != Level || !Room.bIsRoofRoom)
			continue;

		// Check if any of this room's boundary edges match our roof's walls
		for (int32 BoundaryEdge : Room.BoundaryEdges)
		{
			if (RoofWallEdgeIDs.Contains(BoundaryEdge))
			{
				// This room belongs to this roof
				RoomsToDelete.Add(Room.RoomID);
				break;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RoofBase: Found %d roof rooms to delete"), RoomsToDelete.Num());

	// ========== STEP 3: Delete floor tiles (but only if NOT serving as ceilings) ==========
	if (LotManager->FloorComponent)
	{
		for (int32 RoomID : RoomsToDelete)
		{
			FRoomData RoomData;
			if (LotManager->RoomManager->GetRoom(RoomID, RoomData))
			{
				UE_LOG(LogTemp, Log, TEXT("RoofBase: Processing roof room %d (%d tiles)"),
					RoomID, RoomData.InteriorTiles.Num());

				int32 DeletedTiles = 0;
				int32 SkippedCeilingTiles = 0;

				for (const FIntVector& Tile : RoomData.InteriorTiles)
				{
					// Tile format: X=Row, Y=Column, Z=Level
					int32 Row = Tile.X;
					int32 Column = Tile.Y;
					int32 TileLevel = Tile.Z;

					// Check if this floor tile is serving as a ceiling to a room below
					bool bIsCeiling = false;
					if (TileLevel > 0) // Can't be a ceiling if we're at ground level
					{
						// Check if there's a room at the level below that contains this tile
						FIntVector TileBelow(Row, Column, TileLevel - 1);
						int32 RoomBelow = LotManager->RoomManager->GetRoomAtTile(TileBelow);

						if (RoomBelow > 0)
						{
							bIsCeiling = true;
							SkippedCeilingTiles++;
							UE_LOG(LogTemp, Verbose, TEXT("  Skipping floor at (%d,%d,%d) - serves as ceiling to room %d"),
								Row, Column, TileLevel, RoomBelow);
						}
					}

					// Only delete if NOT a ceiling
					if (!bIsCeiling)
					{
						LotManager->FloorComponent->RemoveFloorTile(TileLevel, Row, Column);
						DeletedTiles++;
					}
				}

				UE_LOG(LogTemp, Log, TEXT("  Room %d: Deleted %d floors, preserved %d ceiling tiles"),
					RoomID, DeletedTiles, SkippedCeilingTiles);
			}
		}
	}

	// ========== STEP 4: Clean up walls created by this roof ==========
	const_cast<ARoofBase*>(this)->CleanupWalls();

	// ========== STEP 5: Invalidate roof rooms ==========
	for (int32 RoomID : RoomsToDelete)
	{
		LotManager->RoomManager->InvalidateRoom(RoomID);
		UE_LOG(LogTemp, Log, TEXT("RoofBase: Invalidated roof room %d"), RoomID);
	}

	// ========== STEP 6: Exit edit mode ==========
	const_cast<ARoofBase*>(this)->ExitEditMode();

	UE_LOG(LogTemp, Log, TEXT("RoofBase: OnDeleted complete - cleaned up %d roof rooms"), RoomsToDelete.Num());
}

bool ARoofBase::IsSelected_Implementation() const
{
	// Roof is considered selected when it's in edit mode
	return bInEditMode;
}
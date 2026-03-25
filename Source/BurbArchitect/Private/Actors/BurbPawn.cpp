// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BurbPawn.h"

#include "Actors/LotManager.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Components/SceneComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Data/CatalogItem.h"
#include "Data/FurnitureItem.h"
#include "Data/ArchitectureItem.h"
#include "Data/DoorItem.h"
#include "Data/WindowItem.h"
#include "Actors/BuyTools/BuyObjectTool.h"
#include "Actors/BuildTools/BuildPortalTool.h"
#include "Actors/BuildTools/SelectionTool.h"
#include "Actors/Roofs/RoofBase.h"
#include "Actors/PortalBase.h"
#include "Components/WallComponent.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
ABurbPawn::ABurbPawn(): CurrentLot(nullptr)
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// Create component hierarchy: DefaultSceneRoot -> SpringArmRoot -> SpringArm -> Camera
	DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	RootComponent = DefaultSceneRoot;

	SpringArmRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpringArmRoot"));
	SpringArmRoot->SetupAttachment(DefaultSceneRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(SpringArmRoot);
	SpringArm->TargetArmLength = 1600.0f; // Will be set to CameraZoomDefault in BeginPlay
	SpringArm->bDoCollisionTest = false; // Disable camera collision for build mode

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	// Create floating pawn movement component
	PawnMovement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("PawnMovement"));
	PawnMovement->UpdatedComponent = RootComponent;

	// Camera properties
	CameraRotationSpeed = 3.5f;
	CameraMaxZoom = 6000.0f;
	CameraMinZoom = 100.0f;
	CameraZoomDefault = 1600.0f;
	CameraDefaultRotation = FVector(0.0f, -30.0f, 0.0f);
	CameraZoomIncrementValue = 100.0f;
	ZInterpSpeed = 8.0f;
	CameraMinPitch = -85.0f;
	CameraMaxPitch = 45.0f;

	// State properties
	bRotateButtonPressed = false;
	CurrentLevel = 0;
	CurrentMode = EBurbMode::Build;
	CurrentBuildTool = nullptr;
	TargetCameraZoom = CameraZoomDefault;
	TargetCameraRotation = FRotator::ZeroRotator;

	// Camera mode properties
	DefaultCameraMode = ECameraMode::Perspective;
	CameraMode = ECameraMode::Perspective;
	IsometricCameraPitch = -45.0f;
	IsometricOrthoWidth = 5000.0f;
	IsometricMinOrthoWidth = 1000.0f;
	IsometricMaxOrthoWidth = 10000.0f;
	IsometricOrthoWidthIncrement = 500.0f;
	TargetOrthoWidth = 5000.0f;
	IsometricAngleIndex = 0; // 0° (North)
	IsometricTargetYaw = 0.0f;
	bIsSnappingToIsometricAngle = false;
}

void ABurbPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABurbPawn, CurrentBuildTool);
	DOREPLIFETIME(ABurbPawn, CurrentLevel);
	DOREPLIFETIME(ABurbPawn, CurrentLot);
	DOREPLIFETIME(ABurbPawn, CurrentMode);
	DOREPLIFETIME(ABurbPawn, DisplayName);
	DOREPLIFETIME(ABurbPawn, CameraMode);

}

void ABurbPawn::SetCurrentBuildTool_Implementation(ABuildTool* BuildTool)
{
	// Set guard flag to prevent re-entry during tool destruction
	// Blueprint events or callbacks from the old tool's destruction could trigger EnsureToolEquipped
	bIsChangingTool = true;

	if (CurrentBuildTool)
	{
		CurrentBuildTool->Destroy();
	}
	CurrentBuildTool = BuildTool;

	// Clear guard flag now that tool switch is complete
	bIsChangingTool = false;

	// If we just cleared the tool (BuildTool is nullptr), auto-equip the default tool
	if (BuildTool == nullptr)
	{
		EnsureToolEquipped();
	}
}

void ABurbPawn::SetMode(EBurbMode NewMode)
{
	// If we don't have authority, forward to server
	if (!HasAuthority())
	{
		ServerSetMode(NewMode);
		return;
	}

	// Only broadcast if mode actually changes
	if (CurrentMode != NewMode)
	{
		EBurbMode OldMode = CurrentMode;
		CurrentMode = NewMode;

		// Broadcast the mode change
		OnModeChanged.Broadcast(OldMode, NewMode);

		UE_LOG(LogTemp, Log, TEXT("BurbPawn: Mode changed from %d to %d"), static_cast<int32>(OldMode), static_cast<int32>(NewMode));
	}
}

void ABurbPawn::ServerSetMode_Implementation(EBurbMode NewMode)
{
	SetMode(NewMode);
}

void ABurbPawn::ToggleMode()
{
	EBurbMode NewMode = (CurrentMode == EBurbMode::Build) 
		? EBurbMode::Live 
		: EBurbMode::Build;
	SetMode(NewMode);
}

void ABurbPawn::EnsureToolEquipped()
{
	// Don't auto-equip during a tool switch operation
	// This prevents Blueprint callbacks during tool destruction from interfering
	if (bIsChangingTool)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPawn::EnsureToolEquipped - Skipped (tool switch in progress)"));
		return;
	}

	// Only auto-equip if we have no current tool and a default tool class is set
	if (CurrentBuildTool != nullptr || !DefaultToolClass)
	{
		return;
	}

	// Don't spawn tools if we don't have a valid world or lot yet
	UWorld* World = GetWorld();
	if (!World || !CurrentLot)
	{
		return;
	}

	// Spawn the default tool with proper initialization
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;

	ABuildTool* NewTool = World->SpawnActor<ABuildTool>(DefaultToolClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (NewTool)
	{
		// Initialize the tool with lot and pawn references
		NewTool->CurrentLot = CurrentLot;
		NewTool->CurrentPlayerPawn = this;

		// Equip the tool
		SetCurrentBuildTool(NewTool);

		UE_LOG(LogTemp, Log, TEXT("BurbPawn: Auto-equipped default tool %s"), *NewTool->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Failed to spawn default tool class"));
	}
}

void ABurbPawn::EnsureDeletionToolEquipped()
{
	// Check if we already have the deletion tool equipped
	if (CurrentBuildTool && DeletionToolClass && CurrentBuildTool->IsA(DeletionToolClass))
	{
		return; // Already have deletion tool equipped
	}

	// Don't spawn tools if we don't have a valid world, lot, or deletion tool class
	UWorld* World = GetWorld();
	if (!World || !CurrentLot || !DeletionToolClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Cannot spawn deletion tool - missing World, Lot, or DeletionToolClass"));
		return;
	}

	// Spawn the deletion tool with proper initialization
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;

	ABuildTool* NewTool = World->SpawnActor<ABuildTool>(DeletionToolClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (NewTool)
	{
		// Initialize the tool with lot and pawn references
		NewTool->CurrentLot = CurrentLot;
		NewTool->CurrentPlayerPawn = this;

		// Equip the tool (this will destroy the old tool)
		SetCurrentBuildTool(NewTool);

		UE_LOG(LogTemp, Log, TEXT("BurbPawn: Equipped deletion tool %s"), *NewTool->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Failed to spawn deletion tool class"));
	}
}

// Called when the game starts or when spawned
void ABurbPawn::BeginPlay()
{
	Super::BeginPlay();

	// Find unowned lot (Blueprint implementation)
	FindUnownedLot();

	// Set initial camera zoom (both target and actual)
	TargetCameraZoom = CameraZoomDefault;
	if (SpringArm)
	{
		SpringArm->TargetArmLength = CameraZoomDefault;
	}

	// Set initial camera rotation (pitch down 30 degrees)
	AddActorLocalRotation(FRotator(-30.0f, 0.0f, 0.0f));

	// Clamp camera pitch to valid range
	AdjustCamera();

	// Initialize target rotation to current rotation after adjustment
	TargetCameraRotation = GetActorRotation();

	// Set initial Z position for level 1 (300 units high)
	TargetZ = 300.0;

	// Apply default camera mode (allows setting startup mode in Blueprint)
	SetCameraMode(DefaultCameraMode);

	// Auto-equip default tool if no tool is equipped
	EnsureToolEquipped();
}

// Called every frame
void ABurbPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Wall cutaway system
	if (CurrentLot && CurrentLot->WallComponent)
	{
		// Different tracking behavior based on cutaway mode
		if (CutawayMode == ECutawayMode::PartialInteriors || CutawayMode == ECutawayMode::Partial)
		{
			// PartialInteriors and Partial modes: Track both camera rotation (sectors) and level changes
			const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
			if (!PlayerController)
				return;

			FVector CameraLocation;
			FRotator CameraRotation;
			PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

			// Calculate which sector the camera is in (0-7 for 8 sectors of 45° each)
			// Get LotManager yaw offset to calculate lot-relative sectors
			float LotManagerYaw = 0.0f;
			if (CurrentLot)
			{
				LotManagerYaw = CurrentLot->GetActorRotation().Yaw;
			}

			// When snapping to isometric angle, use the target yaw for sector calculation
			// to prevent flickering from the interpolating yaw (BURB-42)
			float YawForSector = CameraRotation.Yaw;
			if (CameraMode == ECameraMode::Isometric && bIsSnappingToIsometricAngle)
			{
				YawForSector = IsometricTargetYaw;
			}

			// Use lot-relative yaw to match isometric angle coordinate frame
			float LotRelativeYaw = YawForSector - LotManagerYaw;
			float NormalizedYaw = FMath::Fmod(LotRelativeYaw + 360.0f, 360.0f); // Ensure 0-360 range
			int32 CurrentSector = (int32)(NormalizedYaw / CutawaySectorSize);

			// Update when camera moves to a different sector or level changes
			if (CurrentSector != LastCutawaySector || CurrentLevel != LastCutawayLevel)
			{
				LastCutawaySector = CurrentSector;
				LastCutawayLevel = CurrentLevel;

				// When snapping in isometric mode, use the target rotation
				// so cutaway is applied for the destination angle (BURB-42)
				FRotator EffectiveRotation = CameraRotation;
				if (CameraMode == ECameraMode::Isometric && bIsSnappingToIsometricAngle)
				{
					EffectiveRotation.Yaw = IsometricTargetYaw;
				}

				ApplyCutawayMode(EffectiveRotation);
			}
		}
		else
		{
			// WallsUp and Full modes: Only track level changes (static visibility states)
			if (CurrentLevel != LastCutawayLevel)
			{
				LastCutawayLevel = CurrentLevel;

				// No need for camera rotation in static modes
				ApplyCutawayMode(FRotator::ZeroRotator);
			}
		}
	}

	// Z-position interpolation for smooth level transitions
	FVector CurrentLocation = GetActorLocation();
	if (!FMath::IsNearlyEqual(CurrentLocation.Z, TargetZ, 0.1))
	{
		FVector TargetLocation(CurrentLocation.X, CurrentLocation.Y, TargetZ);
		FVector NewLocation = FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaTime, ZInterpSpeed);
		SetActorLocation(NewLocation);
	}

	// Camera zoom interpolation for smooth zooming
	if (SpringArm && !FMath::IsNearlyEqual(SpringArm->TargetArmLength, TargetCameraZoom, 0.1f))
	{
		float NewArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetCameraZoom, DeltaTime, ZInterpSpeed);
		SpringArm->TargetArmLength = NewArmLength;
	}

	// Isometric ortho width interpolation for smooth zooming in isometric mode
	if (Camera && CameraMode == ECameraMode::Isometric && !FMath::IsNearlyEqual(Camera->OrthoWidth, TargetOrthoWidth, 0.1f))
	{
		float NewOrthoWidth = FMath::FInterpTo(Camera->OrthoWidth, TargetOrthoWidth, DeltaTime, ZInterpSpeed);
		Camera->SetOrthoWidth(NewOrthoWidth);
	}

	// Camera rotation interpolation for smooth rotation
	FRotator CurrentRotation = GetActorRotation();

	// Isometric mode: Handle smooth snapping to target angle
	if (CameraMode == ECameraMode::Isometric && bIsSnappingToIsometricAngle)
	{
		// Only interpolate yaw, keep pitch fixed
		FRotator IsometricTarget = CurrentRotation;
		IsometricTarget.Yaw = IsometricTargetYaw;
		IsometricTarget.Pitch = IsometricCameraPitch;

		// Use FindDeltaAngleDegrees to properly handle angle wrapping (e.g., 350° to 10° is 20°, not 340°)
		float AngleDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, IsometricTargetYaw));

		if (AngleDelta > 0.1f)
		{
			FRotator NewRotation = FMath::RInterpTo(CurrentRotation, IsometricTarget, DeltaTime, ZInterpSpeed);
			SetActorRotation(NewRotation);
		}
		else
		{
			// Snap complete, disable flag
			bIsSnappingToIsometricAngle = false;
			SetActorRotation(IsometricTarget); // Ensure exact angle
			TargetCameraRotation = IsometricTarget; // Update target to prevent perspective system from reverting rotation

			// Force cutaway update at the final angle (BURB-42)
			ForceUpdateCutaway();
		}
	}
	// Perspective mode or not snapping: Normal rotation interpolation
	else if (!CurrentRotation.Equals(TargetCameraRotation, 0.1f))
	{
		FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetCameraRotation, DeltaTime, ZInterpSpeed);
		SetActorRotation(NewRotation);
	}
}

void ABurbPawn::NextCutawayMode()
{
	// Move to next cutaway mode (clamps at Full, doesn't wrap)
	// Order: WallsUp(0) -> PartialInteriors(1) -> Partial(2) -> Full(3)
	int32 CutawayModeValue = static_cast<int32>(CutawayMode);
	CutawayModeValue = FMath::Min(CutawayModeValue + 1, 3); // Max is Full (index 3)
	CutawayMode = static_cast<ECutawayMode>(CutawayModeValue);

	UE_LOG(LogTemp, Log, TEXT("NextCutawayMode: Changed to mode %d"), CutawayModeValue);

	// Force immediate update by resetting tracking state
	LastCutawaySector = -1;
	LastCutawayLevel = -1;
}

void ABurbPawn::PreviousCutawayMode()
{
	// Move to previous cutaway mode (clamps at WallsUp, doesn't wrap)
	// Order: Full(3) -> Partial(2) -> PartialInteriors(1) -> WallsUp(0)
	int32 CutawayModeValue = static_cast<int32>(CutawayMode);
	CutawayModeValue = FMath::Max(CutawayModeValue - 1, 0); // Min is WallsUp (index 0)
	CutawayMode = static_cast<ECutawayMode>(CutawayModeValue);

	// Force immediate update by resetting tracking state
	LastCutawaySector = -1;
	LastCutawayLevel = -1;
}

void ABurbPawn::SetCutawayMode(ECutawayMode NewMode)
{
	CutawayMode = NewMode;

	// Force immediate update by resetting tracking state
	LastCutawaySector = -1;
	LastCutawayLevel = -1;
}

void ABurbPawn::ForceUpdateCutaway()
{
	// Invalidate sector/level cache to force immediate update on next tick
	LastCutawaySector = -1;
	LastCutawayLevel = -1;

	// Also trigger immediate update now
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController)
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

		// When snapping to isometric angle, use the target yaw instead of the
		// mid-interpolation yaw to prevent incorrect cutaway during transition (BURB-43)
		if (CameraMode == ECameraMode::Isometric && bIsSnappingToIsometricAngle)
		{
			CameraRotation.Yaw = IsometricTargetYaw;
		}

		ApplyCutawayMode(CameraRotation);
	}
}

void ABurbPawn::ApplyCutawayMode(const FRotator& CameraRotation)
{
	if (!CurrentLot || !CurrentLot->WallComponent)
		return;

	// Update roof visibility based on cutaway mode AND current level
	// Roofs are visible when BOTH conditions are met:
	// 1. CutawayMode == WallsUp (roofs only visible when all walls are up)
	// 2. CurrentLevel >= RoofLevel (viewing at or above the roof's level)
	if (CurrentLot)
	{
		bool bCutawayModeShowsRoofs = (CutawayMode == ECutawayMode::WallsUp);

		// Roofs are standalone actors, not attached, so we need to find them in the world
		TArray<AActor*> FoundRoofs;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARoofBase::StaticClass(), FoundRoofs);

		int32 RoofsProcessed = 0;

		for (AActor* Actor : FoundRoofs)
		{
			if (ARoofBase* RoofActor = Cast<ARoofBase>(Actor))
			{
				// Only process roofs that belong to this lot
				if (RoofActor->LotManager == CurrentLot && RoofActor->RoofMeshComponent)
				{
					RoofsProcessed++;

					// Check both cutaway mode AND level
					bool bLevelAllowsVisibility = (CurrentLevel >= RoofActor->Level);
					bool bShouldShowRoof = bCutawayModeShowsRoofs && bLevelAllowsVisibility;

					// Hide/show all mesh sections in the roof component
					int32 NumSections = RoofActor->RoofMeshComponent->GetNumSections();

					for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
					{
						RoofActor->RoofMeshComponent->SetMeshSectionVisible(SectionIndex, bShouldShowRoof);
					}
				}
			}
		}
	}

	// Fast path for static modes (no camera calculations needed)
	// WallsUp: all walls visible, Full: all walls hidden
	// PartialInteriors and Partial both need camera calculations for exterior walls
	if (CutawayMode == ECutawayMode::WallsUp || CutawayMode == ECutawayMode::Full)
	{
		bool bHideAll = (CutawayMode == ECutawayMode::Full);

		// First pass: Update all cutaway states
		for (int32 WallIndex = 0; WallIndex < CurrentLot->WallComponent->WallDataArray.Num(); WallIndex++)
		{
			FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallIndex];

			if (!Wall.bCommitted || !Wall.WallMaterial)
				continue;

			// Per-level cutaway: Only apply cutaway mode to walls on the current level
			// Walls on other levels are controlled by VisibilityLevel parameter (set by SetCurrentLevel)
			bool bIsCurrentLevel = (Wall.Level == CurrentLevel);
			bool bShouldHide = bIsCurrentLevel && bHideAll;

			// Update wall cutaway state (just sets flag, no mesh regeneration)
			Wall.bIsInCutawayMode = bShouldHide;

			// Wall visibility is controlled ONLY via material parameters:
			// - VisibilityLevel (set by SetCurrentLevel) hides walls on wrong levels
			// - EnableCutaway (set here) controls cutaway mode for walls on current level
			// NEVER use SetMeshSectionVisible on walls - it overrides VisibilityLevel!

			// Apply cutaway material parameter
			if (CurrentLot->WallComponent->IsSharedMaterial(Wall.WallMaterial))
			{
				Wall.WallMaterial = UMaterialInstanceDynamic::Create(
					Wall.WallMaterial->Parent, CurrentLot->WallComponent);
				CurrentLot->WallComponent->SetMaterial(Wall.SectionIndex, Wall.WallMaterial);
			}

			float CutawayValue = bShouldHide ? 1.0f : 0.0f;
			Wall.WallMaterial->SetScalarParameterValue("EnableCutaway", CutawayValue);

			// Explicitly disable transitions (they only exist in Partial mode)
			Wall.bShowStartTransition = false;
			Wall.bShowEndTransition = false;
			Wall.WallMaterial->SetScalarParameterValue("ShowStartTransition", 0.0f);
			Wall.WallMaterial->SetScalarParameterValue("ShowEndTransition", 0.0f);
		}

		// Update portal visibility for all walls based on cutaway and level state
		for (FWallSegmentData& Wall : CurrentLot->WallComponent->WallDataArray)
		{
			UpdatePortalVisibilityForWall(Wall);
		}

		// NO transitions in Full/WallsUp modes - transitions only exist in PartialInteriors/Partial modes

		return;
	}

	// PartialInteriors and Partial modes: Camera-dependent cutaway logic
	// PartialInteriors: Interior walls visible, exterior walls facing camera hidden
	// Partial: Interior walls hidden, exterior walls facing camera hidden
	// Get camera location
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!PlayerController)
		return;

	FVector CameraLocation;
	FRotator CameraRotationOut;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotationOut);

	// First pass: Update all cutaway states
	for (int32 WallIndex = 0; WallIndex < CurrentLot->WallComponent->WallDataArray.Num(); WallIndex++)
	{
		FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallIndex];

		if (!Wall.bCommitted || !Wall.WallMaterial)
			continue;

		// Per-level cutaway: Only apply cutaway logic to walls on the current level
		// Walls on other levels always show as WallsUp (fully visible)
		bool bIsCurrentLevel = (Wall.Level == CurrentLevel);
		bool bShouldHide = false;

		if (bIsCurrentLevel)
		{
			// Only calculate cutaway for walls on the current level
			// Query RoomIDs from WallGraph using WallEdgeID
			int32 RoomID1 = 0;
			int32 RoomID2 = 0;

			if (Wall.WallEdgeID != -1 && CurrentLot->WallGraph)
			{
				const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(Wall.WallEdgeID);
				if (Edge)
				{
					RoomID1 = Edge->Room1;
					RoomID2 = Edge->Room2;

					// Debug logging (only log once per second to avoid spam)
					static float LastLogTime = 0.0f;
					float CurrentTime = GetWorld()->GetTimeSeconds();
					if (CurrentTime - LastLogTime > 1.0f)
					{
						UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Wall at index %d (EdgeID=%d) has Room1=%d, Room2=%d"),
							WallIndex, Wall.WallEdgeID, RoomID1, RoomID2);
						LastLogTime = CurrentTime;
					}
				}
				else
				{
					// Debug logging
					static float LastLogTime = 0.0f;
					float CurrentTime = GetWorld()->GetTimeSeconds();
					if (CurrentTime - LastLogTime > 1.0f)
					{
						UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Wall at index %d has EdgeID=%d but edge not found in graph"),
							WallIndex, Wall.WallEdgeID);
						LastLogTime = CurrentTime;
					}
				}
			}
			else
			{
				// Debug logging
				static float LastLogTime = 0.0f;
				float CurrentTime = GetWorld()->GetTimeSeconds();
				if (CurrentTime - LastLogTime > 1.0f)
				{
					if (Wall.WallEdgeID == -1)
					{
						UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Wall at index %d has WallEdgeID=-1 (not linked to graph)"), WallIndex);
					}
					else if (!CurrentLot->WallGraph)
					{
						UE_LOG(LogTemp, Error, TEXT("BurbPawn: CurrentLot->WallGraph is null!"));
					}
					LastLogTime = CurrentTime;
				}
			}

			// Check wall type based on RoomIDs
			bool bBothSidesExterior = (RoomID1 == 0 && RoomID2 == 0);
			bool bBothSidesInterior = (RoomID1 > 0 && RoomID2 > 0);
			bool bIsExteriorWall = !bBothSidesExterior && !bBothSidesInterior; // One side exterior, one interior

			if (bBothSidesExterior)
			{
				// Both sides exterior - wall is in open space, never hide
				bShouldHide = false;
			}
			else if (bBothSidesInterior)
			{
				// Interior walls behavior depends on mode:
				// PartialInteriors: Keep interior walls visible
				// Partial: Hide interior walls
				bShouldHide = (CutawayMode == ECutawayMode::Partial);
			}
			else if (bIsExteriorWall)
			{
				// Exterior wall - one side is interior, one is exterior
				// Determine which side is exterior using RoomIDs
				bool bRoomID1IsExterior = (RoomID1 == 0);

				// Calculate wall normal (right-hand perpendicular)
				FVector WallDirection = (Wall.EndLoc - Wall.StartLoc).GetSafeNormal();
				FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();
				FVector WallCenter = (Wall.StartLoc + Wall.EndLoc) * 0.5f;

				// Half-plane test: which side of the wall is the camera on?
				// Positive = +normal side, Negative = -normal side
				// In isometric (orthographic) mode, camera position is arbitrary —
				// use camera forward direction instead (BURB-41)
				FVector ToCamera;
				if (CameraMode == ECameraMode::Isometric)
				{
					// Negate forward vector: we want "toward camera", not "camera looks at"
					ToCamera = -CameraRotation.Vector();
				}
				else
				{
					ToCamera = CameraLocation - WallCenter;
				}
				ToCamera.Z = 0.0f; // 2D test
				float CameraSide = FVector::DotProduct(ToCamera, WallNormal);

				// From our grid sampling, we know:
				// RoomID1 is on the +normal side
				// RoomID2 is on the -normal side
				bool bCameraOnRoomID1Side = (CameraSide > 0.0f);

				// Hide if camera is on the same side as the exterior
				bShouldHide = (bCameraOnRoomID1Side == bRoomID1IsExterior);
			}
		}
		// If not current level, bShouldHide stays false (walls are fully visible)

		// Update wall cutaway state (just sets flag, no mesh regeneration)
		Wall.bIsInCutawayMode = bShouldHide;

		// Wall visibility is controlled ONLY via material parameters (VisibilityLevel, EnableCutaway)
		// NEVER use SetMeshSectionVisible on walls - it overrides level visibility system

		// Apply cutaway material parameter
		if (CurrentLot->WallComponent->IsSharedMaterial(Wall.WallMaterial))
		{
			Wall.WallMaterial = UMaterialInstanceDynamic::Create(
				Wall.WallMaterial->Parent, CurrentLot->WallComponent);
			CurrentLot->WallComponent->SetMaterial(Wall.SectionIndex, Wall.WallMaterial);
		}

		float CutawayValue = bShouldHide ? 1.0f : 0.0f;
		Wall.WallMaterial->SetScalarParameterValue("EnableCutaway", CutawayValue);
	}

	// Update portal visibility for all walls based on cutaway and level state
	for (FWallSegmentData& Wall : CurrentLot->WallComponent->WallDataArray)
	{
		UpdatePortalVisibilityForWall(Wall);
	}

	// Refresh selection tool room visuals (portal outlines depend on visibility state)
	if (ASelectionTool* SelectionTool = Cast<ASelectionTool>(CurrentBuildTool))
	{
		SelectionTool->RefreshRoomSelection();
	}

	// Second pass: Update transitions AFTER all cutaway states are set (PartialInteriors/Partial modes only!)
	for (int32 WallIndex = 0; WallIndex < CurrentLot->WallComponent->WallDataArray.Num(); WallIndex++)
	{
		FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallIndex];

		if (!Wall.bCommitted || !Wall.WallMaterial)
			continue;

		// Only update transitions for walls on the current level that are in cutaway
		bool bIsCurrentLevel = (Wall.Level == CurrentLevel);
		if (bIsCurrentLevel && Wall.bIsInCutawayMode)
		{
			CurrentLot->WallComponent->UpdateWallTransitionState(WallIndex);
		}
		else if (!bIsCurrentLevel)
		{
			// Walls on other levels should have transitions disabled
			Wall.bShowStartTransition = false;
			Wall.bShowEndTransition = false;
			Wall.WallMaterial->SetScalarParameterValue("ShowStartTransition", 0.0f);
			Wall.WallMaterial->SetScalarParameterValue("ShowEndTransition", 0.0f);
		}
	}
}

void ABurbPawn::UpdatePortalVisibilityForWall(FWallSegmentData& Wall)
{
	// Portals should be hidden if:
	// 1. Wall is in cutaway mode (bIsInCutawayMode = true), OR
	// 2. Wall is on a level above the current viewing level
	bool bShouldHidePortals = Wall.bIsInCutawayMode || (Wall.Level > CurrentLevel);

	// Hide/show all portals on this wall
	for (APortalBase* Portal : Wall.PortalArray)
	{
		if (Portal)
		{
			Portal->SetActorHiddenInGame(bShouldHidePortals);
		}
	}
}

// Called to bind functionality to input
void ABurbPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ABurbPawn::ZoomIn()
{
	if (CameraMode == ECameraMode::Isometric)
	{
		// Isometric mode: Increase ortho width to zoom in
		TargetOrthoWidth += IsometricOrthoWidthIncrement;
		TargetOrthoWidth = FMath::Clamp(TargetOrthoWidth, IsometricMinOrthoWidth, IsometricMaxOrthoWidth);
	}
	else
	{
		// Perspective mode: Increase zoom (arm length)
		TargetCameraZoom += CameraZoomIncrementValue;
		TargetCameraZoom = FMath::Clamp(TargetCameraZoom, CameraMinZoom, CameraMaxZoom);
	}
}

void ABurbPawn::ZoomOut()
{
	if (CameraMode == ECameraMode::Isometric)
	{
		// Isometric mode: Decrease ortho width to zoom out
		TargetOrthoWidth -= IsometricOrthoWidthIncrement;
		TargetOrthoWidth = FMath::Clamp(TargetOrthoWidth, IsometricMinOrthoWidth, IsometricMaxOrthoWidth);
	}
	else
	{
		// Perspective mode: Decrease zoom (arm length)
		TargetCameraZoom -= CameraZoomIncrementValue;
		TargetCameraZoom = FMath::Clamp(TargetCameraZoom, CameraMinZoom, CameraMaxZoom);
	}
}

void ABurbPawn::RotateHorizontal(float AxisValue)
{
	// Disable mouse rotation in isometric mode
	if (CameraMode == ECameraMode::Isometric)
		return;

	// Only rotate if button is pressed
	if (!bRotateButtonPressed)
		return;

	// Calculate rotation delta (yaw)
	float DeltaYaw = AxisValue * CameraRotationSpeed;

	// Update target yaw
	TargetCameraRotation.Yaw += DeltaYaw;

	// Normalize yaw to -180 to 180 range
	TargetCameraRotation.Yaw = FRotator::NormalizeAxis(TargetCameraRotation.Yaw);
}

void ABurbPawn::RotateVertical(float AxisValue)
{
	// Disable mouse rotation in isometric mode
	if (CameraMode == ECameraMode::Isometric)
		return;

	// Only rotate if button is pressed
	if (!bRotateButtonPressed)
		return;

	// Calculate rotation delta (pitch)
	float DeltaPitch = AxisValue * CameraRotationSpeed;

	// Update target pitch and clamp to valid range
	TargetCameraRotation.Pitch += DeltaPitch;
	TargetCameraRotation.Pitch = FMath::Clamp(TargetCameraRotation.Pitch, CameraMinPitch, CameraMaxPitch);
}

void ABurbPawn::AdjustCamera()
{
	// Get current actor rotation
	FRotator CurrentRotation = GetActorRotation();

	// Clamp pitch to prevent over-rotation
	float ClampedPitch = FMath::Clamp(CurrentRotation.Pitch, CameraMinPitch, CameraMaxPitch);

	// Set new rotation with clamped pitch
	FRotator NewRotation(ClampedPitch, CurrentRotation.Yaw, CurrentRotation.Roll);
	SetActorRotation(NewRotation);

	// Also update target rotation to match
	TargetCameraRotation = NewRotation;
}

void ABurbPawn::HandleOnLevelChanged(int32 NewLevel)
{
	// Convert level to Z position (each level is 300 units tall)
	TargetZ = static_cast<double>(NewLevel * 300);
}

void ABurbPawn::MoveUpFloor()
{
	if (!CurrentLot)
		return;

	// Calculate target level
	int32 TargetLevel = FMath::Clamp(CurrentLevel + 1, 0, CurrentLot->Floors);

	// Don't allow moving up if we're already at the target level
	if (TargetLevel == CurrentLevel)
		return;

	// Check if there are any tiles on the target level
	bool bHasTilesOnTargetLevel = false;
	for (const FTileData& Tile : CurrentLot->GridData)
	{
		if (Tile.Level == TargetLevel && !Tile.bOutOfBounds)
		{
			bHasTilesOnTargetLevel = true;
			break;
		}
	}

	// Only allow moving up if there are tiles on that level
	if (!bHasTilesOnTargetLevel)
		return;

	CurrentLevel = TargetLevel;

	// Update TargetZ
	HandleOnLevelChanged(CurrentLevel);

	// Update lot's current level
	CurrentLot->SetCurrentLevel(CurrentLevel);
}

void ABurbPawn::MoveDownFloor()
{
	if (!CurrentLot)
		return;

	// Clamp CurrentLevel - 1 to range [0, Floors]
	int32 NewLevel = FMath::Clamp(CurrentLevel - 1, 0, CurrentLot->Floors);
	CurrentLevel = NewLevel;

	// Update TargetZ
	HandleOnLevelChanged(CurrentLevel);

	// Update lot's current level if it's a BP_LotManager
	if (CurrentLot)
	{
		CurrentLot->SetCurrentLevel(CurrentLevel);
	}
}

void ABurbPawn::HandleCatalogItemActivation(UCatalogItem* Item)
{
	if (!Item)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPawn::HandleCatalogItemActivation - Item is null"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !CurrentLot)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPawn::HandleCatalogItemActivation - No valid world or lot"));
		return;
	}

	// Spawn parameters for new tools
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;

	ABuildTool* NewTool = nullptr;

	// Check item type and spawn appropriate tool
	if (UFurnitureItem* FurnitureItem = Cast<UFurnitureItem>(Item))
	{
		// Furniture items use BuyObjectTool
		ABuyObjectTool* BuyTool = World->SpawnActor<ABuyObjectTool>(ABuyObjectTool::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (BuyTool)
		{
			// Note: BuyObjectTool would need a method to set the furniture item
			// For now, just spawn the tool
			NewTool = BuyTool;
			UE_LOG(LogTemp, Log, TEXT("BurbPawn: Spawned BuyObjectTool for furniture item '%s'"), *Item->DisplayName.ToString());
		}
	}
	else if (UDoorItem* DoorItem = Cast<UDoorItem>(Item))
	{
		// Door items use their specified BuildToolClass (typically BP_DoorTool_C)
		if (DoorItem->BuildToolClass.IsNull())
		{
			UE_LOG(LogTemp, Error, TEXT("BurbPawn: DoorItem '%s' has no BuildToolClass set!"), *DoorItem->GetName());
			return;
		}

		// Load and spawn the tool class from the data asset
		TSubclassOf<ABuildTool> LoadedToolClass = DoorItem->BuildToolClass.LoadSynchronous();
		if (!LoadedToolClass)
		{
			UE_LOG(LogTemp, Error, TEXT("BurbPawn: Failed to load BuildToolClass for DoorItem '%s'"), *DoorItem->GetName());
			return;
		}

		ABuildPortalTool* PortalTool = World->SpawnActor<ABuildPortalTool>(LoadedToolClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (PortalTool)
		{
			// Configure portal tool with door settings
			PortalTool->ClassToSpawn = DoorItem->ClassToSpawn;
			PortalTool->HorizontalSnap = DoorItem->HorizontalSnap;
			PortalTool->VerticalSnap = DoorItem->VerticalSnap;
			PortalTool->bSnapsToFloor = DoorItem->bSnapsToFloor;

			// Apply portal size, offset, and meshes from data asset
			PortalTool->PortalSize = DoorItem->PortalSize;
			PortalTool->PortalOffset = DoorItem->PortalOffset;
			PortalTool->DoorStaticMesh = DoorItem->DoorStaticMesh;
			PortalTool->DoorFrameMesh = DoorItem->DoorFrameMesh;

			// Log values read from data asset for diagnostics
			UE_LOG(LogTemp, Log, TEXT("BurbPawn: Door item '%s' properties:"), *Item->DisplayName.ToString());
			UE_LOG(LogTemp, Log, TEXT("  BuildToolClass: %s"), *LoadedToolClass->GetName());
			UE_LOG(LogTemp, Log, TEXT("  PortalSize: (%.1f, %.1f)"), DoorItem->PortalSize.X, DoorItem->PortalSize.Y);
			UE_LOG(LogTemp, Log, TEXT("  PortalOffset: (%.1f, %.1f)"), DoorItem->PortalOffset.X, DoorItem->PortalOffset.Y);
			UE_LOG(LogTemp, Log, TEXT("  DoorStaticMesh: %s"), DoorItem->DoorStaticMesh.IsNull() ? TEXT("NULL") : *DoorItem->DoorStaticMesh.ToString());
			UE_LOG(LogTemp, Log, TEXT("  DoorFrameMesh: %s"), DoorItem->DoorFrameMesh.IsNull() ? TEXT("NULL") : *DoorItem->DoorFrameMesh.ToString());

			NewTool = PortalTool;
			UE_LOG(LogTemp, Log, TEXT("BurbPawn: Spawned door tool '%s' for door item '%s'"), *LoadedToolClass->GetName(), *Item->DisplayName.ToString());
		}
	}
	else if (UWindowItem* WindowItem = Cast<UWindowItem>(Item))
	{
		// Window items use their specified BuildToolClass (typically BP_WindowTool_C)
		if (WindowItem->BuildToolClass.IsNull())
		{
			UE_LOG(LogTemp, Error, TEXT("BurbPawn: WindowItem '%s' has no BuildToolClass set!"), *WindowItem->GetName());
			return;
		}

		// Load and spawn the tool class from the data asset
		TSubclassOf<ABuildTool> LoadedToolClass = WindowItem->BuildToolClass.LoadSynchronous();
		if (!LoadedToolClass)
		{
			UE_LOG(LogTemp, Error, TEXT("BurbPawn: Failed to load BuildToolClass for WindowItem '%s'"), *WindowItem->GetName());
			return;
		}

		ABuildPortalTool* PortalTool = World->SpawnActor<ABuildPortalTool>(LoadedToolClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (PortalTool)
		{
			// Configure portal tool with window settings
			PortalTool->ClassToSpawn = WindowItem->ClassToSpawn;
			PortalTool->HorizontalSnap = WindowItem->HorizontalSnap;
			PortalTool->VerticalSnap = WindowItem->VerticalSnap;
			PortalTool->bSnapsToFloor = WindowItem->bSnapsToFloor;

			// Apply portal size, offset, and mesh from data asset
			PortalTool->PortalSize = WindowItem->PortalSize;
			PortalTool->PortalOffset = WindowItem->PortalOffset;
			PortalTool->WindowMesh = WindowItem->WindowMesh;

			// Log values read from data asset for diagnostics
			UE_LOG(LogTemp, Log, TEXT("BurbPawn: Window item '%s' properties:"), *Item->DisplayName.ToString());
			UE_LOG(LogTemp, Log, TEXT("  BuildToolClass: %s"), *LoadedToolClass->GetName());
			UE_LOG(LogTemp, Log, TEXT("  PortalSize: (%.1f, %.1f)"), WindowItem->PortalSize.X, WindowItem->PortalSize.Y);
			UE_LOG(LogTemp, Log, TEXT("  PortalOffset: (%.1f, %.1f)"), WindowItem->PortalOffset.X, WindowItem->PortalOffset.Y);
			UE_LOG(LogTemp, Log, TEXT("  WindowMesh: %s"), WindowItem->WindowMesh.IsNull() ? TEXT("NULL") : *WindowItem->WindowMesh.ToString());

			NewTool = PortalTool;
			UE_LOG(LogTemp, Log, TEXT("BurbPawn: Spawned window tool '%s' for window item '%s'"), *LoadedToolClass->GetName(), *Item->DisplayName.ToString());
		}
	}
	else if (UArchitectureItem* ArchitectureItem = Cast<UArchitectureItem>(Item))
	{
		// Architecture items (including WallPattern and FloorPattern) specify their own build tool class
		if (ArchitectureItem->BuildToolClass.IsValid() || ArchitectureItem->BuildToolClass.IsPending())
		{
			TSubclassOf<ABuildTool> ToolClass = ArchitectureItem->BuildToolClass.LoadSynchronous();
			if (ToolClass)
			{
				NewTool = World->SpawnActor<ABuildTool>(ToolClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
				if (NewTool)
				{
					UE_LOG(LogTemp, Log, TEXT("BurbPawn: Spawned %s for architecture item '%s'"), *ToolClass->GetName(), *Item->DisplayName.ToString());
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Failed to load BuildToolClass for architecture item '%s'"), *Item->DisplayName.ToString());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Architecture item '%s' has no BuildToolClass specified"), *Item->DisplayName.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPawn: Unknown catalog item type for '%s'"), *Item->DisplayName.ToString());
		return;
	}

	// If we spawned a new tool, initialize and equip it
	if (NewTool)
	{
		// Initialize the tool with lot and pawn references
		NewTool->CurrentLot = CurrentLot;
		NewTool->CurrentPlayerPawn = this;

		// Equip the tool (this will destroy the old tool)
		SetCurrentBuildTool(NewTool);

		UE_LOG(LogTemp, Log, TEXT("BurbPawn: Successfully activated catalog item '%s'"), *Item->DisplayName.ToString());
	}
}

void ABurbPawn::ToggleCameraMode()
{
	// Toggle between Perspective and Isometric
	ECameraMode NewMode = (CameraMode == ECameraMode::Perspective) ? ECameraMode::Isometric : ECameraMode::Perspective;
	SetCameraMode(NewMode);
}

void ABurbPawn::SetCameraMode(ECameraMode NewMode)
{
	if (CameraMode == NewMode)
		return;

	CameraMode = NewMode;

	if (CameraMode == ECameraMode::Isometric)
	{
		ApplyIsometricCameraSettings();
		UE_LOG(LogTemp, Log, TEXT("BurbPawn: Switched to Isometric camera mode"));
	}
	else
	{
		ApplyPerspectiveCameraSettings();
		UE_LOG(LogTemp, Log, TEXT("BurbPawn: Switched to Perspective camera mode"));
	}

	// Refresh cutaway after mode switch — the half-plane test method changes
	// between perspective (position-based) and isometric (direction-based) (BURB-44)
	ForceUpdateCutaway();
}

void ABurbPawn::ApplyIsometricCameraSettings()
{
	if (!Camera)
		return;

	// Set to orthographic projection
	Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
	Camera->SetOrthoWidth(IsometricOrthoWidth);
	TargetOrthoWidth = IsometricOrthoWidth; // Initialize target for smooth zooming

	// Set fixed pitch angle
	FRotator CurrentRotation = GetActorRotation();
	CurrentRotation.Pitch = IsometricCameraPitch;

	// Get LotManager's yaw offset (if available)
	float LotManagerYaw = 0.0f;
	if (CurrentLot)
	{
		LotManagerYaw = CurrentLot->GetActorRotation().Yaw;
	}

	// Snap yaw to nearest isometric angle relative to LotManager
	// Isometric views are from diagonal corners (45°, 135°, 225°, 315°), not cardinal directions
	float CurrentYaw = CurrentRotation.Yaw - LotManagerYaw - 45.0f; // Convert to lot-relative angle, offset by 45°
	IsometricAngleIndex = FMath::RoundToInt(CurrentYaw / 90.0f) % 4;
	if (IsometricAngleIndex < 0)
		IsometricAngleIndex += 4;

	// Calculate target yaw for current angle index (relative to LotManager, viewing from diagonal)
	IsometricTargetYaw = (IsometricAngleIndex * 90.0f + 45.0f) + LotManagerYaw;
	// Normalize to -180 to 180 range to prevent rotation wrapping issues
	IsometricTargetYaw = FRotator::NormalizeAxis(IsometricTargetYaw);
	CurrentRotation.Yaw = IsometricTargetYaw;

	// Apply rotation immediately
	SetActorRotation(CurrentRotation);
	TargetCameraRotation = CurrentRotation;

	bIsSnappingToIsometricAngle = false;

	UE_LOG(LogTemp, Log, TEXT("BurbPawn: Applied isometric settings - Pitch: %.1f, Yaw: %.1f, AngleIndex: %d, LotYaw: %.1f"),
		IsometricCameraPitch, IsometricTargetYaw, IsometricAngleIndex, LotManagerYaw);
}

void ABurbPawn::ApplyPerspectiveCameraSettings()
{
	if (!Camera)
		return;

	// Set back to perspective projection
	Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
	Camera->SetFieldOfView(90.0f); // Default FOV

	// Keep current rotation as-is (allow free-look)
	TargetCameraRotation = GetActorRotation();

	UE_LOG(LogTemp, Log, TEXT("BurbPawn: Applied perspective settings"));
}

void ABurbPawn::RotateIsometricLeft()
{
	// Only works in isometric mode
	if (CameraMode != ECameraMode::Isometric)
		return;

	// Get LotManager's yaw offset (if available)
	float LotManagerYaw = 0.0f;
	if (CurrentLot)
	{
		LotManagerYaw = CurrentLot->GetActorRotation().Yaw;
	}

	// Rotate counter-clockwise (decrease angle index)
	IsometricAngleIndex = (IsometricAngleIndex - 1 + 4) % 4; // +4 to handle negative wrap-around
	// Isometric views are from diagonal corners (45°, 135°, 225°, 315°)
	IsometricTargetYaw = (IsometricAngleIndex * 90.0f + 45.0f) + LotManagerYaw;
	// Normalize to -180 to 180 range to prevent rotation wrapping issues
	IsometricTargetYaw = FRotator::NormalizeAxis(IsometricTargetYaw);

	// Enable smooth snapping
	bIsSnappingToIsometricAngle = true;

	UE_LOG(LogTemp, Log, TEXT("BurbPawn: Rotating isometric left to angle %d (%.1f degrees, LotYaw: %.1f)"),
		IsometricAngleIndex, IsometricTargetYaw, LotManagerYaw);
}

void ABurbPawn::RotateIsometricRight()
{
	// Only works in isometric mode
	if (CameraMode != ECameraMode::Isometric)
		return;

	// Get LotManager's yaw offset (if available)
	float LotManagerYaw = 0.0f;
	if (CurrentLot)
	{
		LotManagerYaw = CurrentLot->GetActorRotation().Yaw;
	}

	// Rotate clockwise (increase angle index)
	IsometricAngleIndex = (IsometricAngleIndex + 1) % 4;
	// Isometric views are from diagonal corners (45°, 135°, 225°, 315°)
	IsometricTargetYaw = (IsometricAngleIndex * 90.0f + 45.0f) + LotManagerYaw;
	// Normalize to -180 to 180 range to prevent rotation wrapping issues
	IsometricTargetYaw = FRotator::NormalizeAxis(IsometricTargetYaw);

	// Enable smooth snapping
	bIsSnappingToIsometricAngle = true;

	UE_LOG(LogTemp, Log, TEXT("BurbPawn: Rotating isometric right to angle %d (%.1f degrees, LotYaw: %.1f)"),
		IsometricAngleIndex, IsometricTargetYaw, LotManagerYaw);
}


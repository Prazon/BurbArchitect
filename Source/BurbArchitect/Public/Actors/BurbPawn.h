// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "GameFramework/Pawn.h"
#include "BurbPawn.generated.h"

// Burb gameplay modes
UENUM(BlueprintType)
enum class EBurbMode : uint8
{
	Build       UMETA(DisplayName = "Build Mode"),        // Build walls, floors, roofs, terrain AND place furniture/objects
	Live        UMETA(DisplayName = "Live Mode")          // Simulation/gameplay mode
};

// Wall cutaway display modes (Sims-style)
UENUM(BlueprintType)
enum class ECutawayMode : uint8
{
	WallsUp          UMETA(DisplayName = "Walls Up"),           // All walls visible, roofs visible
	PartialInteriors UMETA(DisplayName = "Partial Interiors"),  // Interior walls visible, exterior walls facing camera hidden, roofs hidden
	Partial          UMETA(DisplayName = "Walls Cutaway"),      // All walls facing camera hidden (interior + exterior), roofs hidden
	Full             UMETA(DisplayName = "Full Cutaway")        // All walls hidden (floor plan view), roofs hidden
};

// Camera mode (perspective vs isometric)
UENUM(BlueprintType)
enum class ECameraMode : uint8
{
	Perspective UMETA(DisplayName = "Perspective"),       // Free-look camera with mouse rotation
	Isometric   UMETA(DisplayName = "Isometric")          // Fixed pitch with 4 cardinal angles
};

// Delegate for broadcasting mode changes
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnModeChanged, EBurbMode, OldMode, EBurbMode, NewMode);

/*
 * ABurbPawn: Class that represents the user/player and holds our camera and spring arm, lets us move around and view things
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API ABurbPawn : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ABurbPawn();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	void SetCurrentBuildTool(ABuildTool* BuildTool);

	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* DefaultSceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* SpringArmRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Components")
	class USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UCameraComponent* Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UFloatingPawnMovement* PawnMovement;
	
	UPROPERTY(BlueprintReadWrite)
	bool bRotateButtonPressed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Rotation")
	float CameraRotationSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Defaults")
	FVector CameraDefaultRotation;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Zoom")
	float CameraMaxZoom;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Defaults")
	float CameraZoomDefault;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Zoom")
	float CameraMinZoom;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Zoom")
	float CameraZoomIncrementValue;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Movement")
	float ZInterpSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Rotation")
	float CameraMinPitch;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Rotation")
	float CameraMaxPitch;

	// Camera mode settings
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Mode")
	ECameraMode DefaultCameraMode;

	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Camera | Mode")
	ECameraMode CameraMode;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Isometric")
	float IsometricCameraPitch;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Isometric")
	float IsometricOrthoWidth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Isometric")
	float IsometricMinOrthoWidth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Isometric")
	float IsometricMaxOrthoWidth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Camera | Isometric")
	float IsometricOrthoWidthIncrement;

	UPROPERTY(BlueprintReadWrite, Category = "Camera | Isometric")
	float TargetOrthoWidth;

	UPROPERTY(BlueprintReadWrite, Category = "Camera | Isometric")
	int32 IsometricAngleIndex;

	UPROPERTY(BlueprintReadWrite, Category = "Camera | Isometric")
	float IsometricTargetYaw;

	UPROPERTY(BlueprintReadWrite, Category = "Camera | Isometric")
	bool bIsSnappingToIsometricAngle;

	UPROPERTY(BlueprintReadWrite, Replicated)
	ALotManager* CurrentLot;

	UPROPERTY(BlueprintReadWrite, Replicated)
	int32 CurrentLevel;

	// Current gameplay mode (Build, Buy, Live)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Mode")
	EBurbMode CurrentMode;

	// Delegate that broadcasts when mode changes
	UPROPERTY(BlueprintAssignable, Category = "Mode")
	FOnModeChanged OnModeChanged;

	UPROPERTY(BlueprintReadWrite, Replicated)
	ABuildTool* CurrentBuildTool;

	UPROPERTY(BlueprintReadWrite, Replicated)
	FText DisplayName;

	// Default tool class to spawn when no tool is equipped (e.g., SelectionTool)
	// Set this in Blueprint children to use BP_SelectionTool instead of C++ version
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Build Tools")
	TSubclassOf<ABuildTool> DefaultToolClass;

	// Deletion tool class to spawn when delete key is pressed (e.g., DeletionTool)
	// Set this in Blueprint children to use BP_DeletionTool instead of C++ version
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Build Tools")
	TSubclassOf<ABuildTool> DeletionToolClass;

	// Currently selected object in edit mode (stairs, roof, etc.)
	// Set by objects when they call EnterEditMode(), cleared on ExitEditMode()
	UPROPERTY(BlueprintReadWrite, Category = "Build Tools")
	AActor* CurrentEditModeActor = nullptr;

	// Mode system
	/**
	 * Set the current gameplay mode (Build, Buy, Live)
	 * Broadcasts OnModeChanged delegate when mode changes
	 * If called on client, forwards to server via ServerSetMode RPC
	 * @param NewMode - The mode to switch to
	 */
	UFUNCTION(BlueprintCallable, Category = "Mode")
	void SetMode(EBurbMode NewMode);

	/** Server RPC for mode changes - ensures mode is set with authority */
	UFUNCTION(Server, Reliable)
	void ServerSetMode(EBurbMode NewMode);

	/** Toggle between Build and Live modes */
	UFUNCTION(BlueprintCallable, Category = "Mode")
	void ToggleMode();

	// Wall cutaway system
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cutaway")
	ECutawayMode CutawayMode = ECutawayMode::WallsUp;

	// Cutaway facing threshold for directional wall hiding
	// Controls how aggressively walls are hidden based on camera direction
	// Higher values = more walls hidden, Lower values = fewer walls hidden
	// Range: -1.0 (hide walls facing away) to 1.0 (hide all walls)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cutaway", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float CutawayFacingThreshold = -0.3f;

	UFUNCTION(BlueprintCallable)
	void NextCutawayMode();

	UFUNCTION(BlueprintCallable)
	void PreviousCutawayMode();

	UFUNCTION(BlueprintCallable)
	void SetCutawayMode(ECutawayMode NewMode);

	UFUNCTION(BlueprintCallable)
	void ApplyCutawayMode(const FRotator& CameraRotation);

	UFUNCTION(BlueprintCallable)
	void ForceUpdateCutaway();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

private:
	// Wall cutaway tracking - sector-based to prevent rapid updates
	static constexpr float CutawaySectorSize = 45.0f; // 8 sectors (360° / 45° = 8)
	int32 LastCutawaySector = -1;
	int32 LastCutawayLevel = -1;

	// Guard flag to prevent recursive tool equipping during tool switch operations
	// Set to true during SetCurrentBuildTool_Implementation to prevent EnsureToolEquipped
	// from being called by Blueprint events or callbacks triggered during tool destruction
	bool bIsChangingTool = false;

	// Helper function to update portal visibility based on wall cutaway state
	void UpdatePortalVisibilityForWall(struct FWallSegmentData& Wall);

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	//This is always going to be set to the current level we're viewing + default wall height (300)
	UPROPERTY(BlueprintReadWrite)
	float CameraZLevel;

	// Target Z position for camera interpolation based on current level
	UPROPERTY(BlueprintReadWrite)
	double TargetZ;

	// Target camera zoom for smooth interpolation
	UPROPERTY(BlueprintReadWrite)
	float TargetCameraZoom;

	// Target camera rotation for smooth interpolation
	UPROPERTY(BlueprintReadWrite)
	FRotator TargetCameraRotation;

	// Move to next floor level (up)
	UFUNCTION(BlueprintCallable)
	void MoveUpFloor();

	// Move to previous floor level (down)
	UFUNCTION(BlueprintCallable)
	void MoveDownFloor();

	// Handle level change and update TargetZ
	UFUNCTION(BlueprintCallable)
	void HandleOnLevelChanged(int32 NewLevel);

	// Camera zoom control functions
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ZoomIn();

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ZoomOut();

	// Camera rotation control functions (called from input axis events)
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void RotateHorizontal(float AxisValue);

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void RotateVertical(float AxisValue);

	// Clamp camera pitch to prevent over-rotation
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void AdjustCamera();

	// Camera mode control functions
	UFUNCTION(BlueprintCallable, Category = "Camera | Mode")
	void ToggleCameraMode();

	UFUNCTION(BlueprintCallable, Category = "Camera | Mode")
	void SetCameraMode(ECameraMode NewMode);

	// Isometric camera rotation functions (90 degree increments)
	UFUNCTION(BlueprintCallable, Category = "Camera | Isometric")
	void RotateIsometricLeft();

	UFUNCTION(BlueprintCallable, Category = "Camera | Isometric")
	void RotateIsometricRight();

	// Apply camera projection settings based on mode
	UFUNCTION(BlueprintCallable, Category = "Camera | Mode")
	void ApplyIsometricCameraSettings();

	UFUNCTION(BlueprintCallable, Category = "Camera | Mode")
	void ApplyPerspectiveCameraSettings();

	// Find unowned lot (implemented in Blueprint)
	UFUNCTION(BlueprintImplementableEvent, Category = "Lot")
	void FindUnownedLot();

	// Ensures a tool is equipped, spawning the default tool if none is active
	UFUNCTION(BlueprintCallable, Category = "Build Tools")
	void EnsureToolEquipped();

	// Ensures deletion tool is equipped, spawning it if not already active
	UFUNCTION(BlueprintCallable, Category = "Build Tools")
	void EnsureDeletionToolEquipped();

	/**
	 * Handle catalog item activation from the catalog browser UI
	 * Spawns and configures the appropriate build tool based on item type
	 * @param Item - The catalog item to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void HandleCatalogItemActivation(class UCatalogItem* Item);
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/PlaceableObject.h"
#include "GameFramework/Actor.h"
#include "Components/StairsComponent.h"
#include "Components/GizmoComponent.h"
#include "Components/LineBatchComponent.h"
#include "Actors/BuildTool.h"
#include "Interfaces/IDeletable.h"
#include "StairsBase.generated.h"

class ALotManager;
class UGizmoComponent;

/**
 * Base Stairs Actor for managing staircase structures
 *
 * This actor-based approach provides:
 * - Better encapsulation of stairs functionality
 * - Blueprint extensibility
 * - Self-contained adjustment tool management
 * - Independent stair generation/cleanup
 * - Cleaner separation from LotManager
 * - DEL key deletion support via IDeletable interface
 */
UCLASS(Abstract)
class BURBARCHITECT_API AStairsBase : public AActor, public IDeletable
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AStairsBase();

	// Called when an instance of this class is placed (in editor) or spawned
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when actor is being destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// ==================== COMPONENTS ====================

	/** Root scene component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RootSceneComponent;

	/** Stairs mesh component - handles procedural mesh generation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStairsComponent* StairsMeshComponent;

	/** Adjustment tool components for modifying stairs configuration */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adjustment Tools")
	UGizmoComponent* AdjustmentTool_StartingStep;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adjustment Tools")
	UGizmoComponent* AdjustmentTool_EndingStep;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adjustment Tools")
	UGizmoComponent* AdjustmentTool_LandingTool;

	// ==================== PROPERTIES ====================

	/** Reference to the lot manager */
	UPROPERTY(Transient)
	ALotManager* LotManager;

	/** Current stairs configuration data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Properties")
	FStairsSegmentData StairsData;

	/** Stair tread mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Properties")
	UStaticMesh* StairTreadMesh;

	/** Stair landing mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Properties")
	UStaticMesh* StairLandingMesh;

	/** Stairs thickness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Properties")
	float StairsThickness;

	/** Direction the stairs faces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Properties")
	FVector StairsDirection;

	/** Current level the stairs is on (bottom level where stairs start) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Properties")
	int32 Level;

	/** Level that stairs END at (top of stairs) - automatically calculated as Level + 1 */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Stairs Properties")
	int32 TopLevel;

	/** Whether current placement is valid (both levels exist + landing tiles present) */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Stairs Properties")
	bool bValidPlacement;

	/** Validation error message for UI feedback */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Stairs Properties")
	FString ValidationError;

	/** Material for stairs treads */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	UMaterialInstance* TreadMaterial;

	/** Material for stairs landings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	UMaterialInstance* LandingMaterial;

	/** Whether the stairs is in edit mode with adjustment tools visible */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bInEditMode;

	/** Whether the stairs has been committed (finalized) */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bCommitted;

	/** Currently selected adjustment tool type */
	UPROPERTY(BlueprintReadOnly, Category = "Adjustment Tools")
	EScaleStairsToolType CurrentAdjustmentToolType;

	/** Drag operation vectors for adjustment tools */
	UPROPERTY(BlueprintReadOnly, Category = "Adjustment Tools")
	FToolDragOperation DragCreateVectors;

	/** Track if we were dragging last frame (to detect drag end) */
	bool bWasDraggingLastFrame;

	/** Current tread count */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Stairs Properties")
	int32 TreadsCount;

	/** Minimum number of steps per section before a landing/turn can occur */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Stairs Properties", meta = (ClampMin = "1", ClampMax = "20", UIMin = "1", UIMax = "20"))
	int32 StepsPerSection;

	/** Default landing index in the stair sequence */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Stairs Properties")
	int32 DefaultLandingIndex;

	/** Last landing index (-1 if no landing) */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Stairs Properties")
	int32 LastLandingIndex;

	/** Currently selected stair module index for adjustment */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Adjustment Tools")
	int32 SelectedModuleIndex;

	// ==================== METHODS ====================

	/** Initialize the stairs with given parameters */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	virtual void InitializeStairs(ALotManager* InLotManager, const FVector& StartLocation, const FVector& Direction,
		const TArray<FStairModuleStructure>& Structures, UStaticMesh* InTreadMesh, UStaticMesh* InLandingMesh,
		float InStairsThickness = 30.0f);

	/** Generate the stairs mesh */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	virtual void GenerateStairsMesh();

	/** Update stairs mesh when configuration changes */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	virtual void UpdateStairsMesh();

	/** Commit the stairs (finalize placement) */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	virtual void CommitStairs();

	/** Destroy all stair modules */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	void DestroyStairs();

	/** Enter edit mode and show adjustment tools */
	UFUNCTION(BlueprintCallable, Category = "Edit Mode")
	void EnterEditMode();

	/** Exit edit mode and hide adjustment tools */
	UFUNCTION(BlueprintCallable, Category = "Edit Mode")
	void ExitEditMode();

	/** Toggle edit mode */
	UFUNCTION(BlueprintCallable, Category = "Edit Mode")
	void ToggleEditMode();

	/** Rotate stairs left (counter-clockwise) by 90 degrees */
	UFUNCTION(BlueprintCallable, Category = "Edit Mode")
	void RotateLeft();

	/** Rotate stairs right (clockwise) by 90 degrees */
	UFUNCTION(BlueprintCallable, Category = "Edit Mode")
	void RotateRight();

	// ==================== ADJUSTMENT TOOL METHODS ====================

	/** Setup adjustment tool positions based on current stairs geometry */
	UFUNCTION(BlueprintCallable, Category = "Adjustment Tools")
	virtual void SetupAdjustmentTools();

	/** Hide all adjustment tools */
	UFUNCTION(BlueprintCallable, Category = "Adjustment Tools")
	void HideAdjustmentTools();

	/** Show all adjustment tools */
	UFUNCTION(BlueprintCallable, Category = "Adjustment Tools")
	void ShowAdjustmentTools();

	/** Handle adjustment tool click */
	UFUNCTION()
	void OnAdjustmentToolClicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

	/** Process adjustment tool dragging */
	UFUNCTION(BlueprintCallable, Category = "Adjustment Tools")
	void AdjustmentToolDragged();

	/** Process landing tool drag to determine turn direction */
	void LandingToolDragged();

	/** Called when landing drag is complete - applies the turn */
	void OnLandingDragComplete();

	/** Get adjustment tool component by type */
	UFUNCTION(BlueprintCallable, Category = "Adjustment Tools")
	UGizmoComponent* GetAdjustmentToolByType(EScaleStairsToolType Type);

	/** Find nearest stair tread to hit mesh */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	int32 FindNearestStairTread(UStaticMeshComponent* HitMesh);

	/** Update stair structure based on turn direction and landing index */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	void UpdateStairStructure(ETurningSocket TurnSocket, int32 LandingIndex);

	// ==================== UTILITY ====================

	/** Check if stairs already exist at a location */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	bool FindExistingStairs(const FVector& StartLoc, const TArray<FStairModuleStructure>& Structures);

	/** Get the section ID from a hit result */
	UFUNCTION(BlueprintCallable, Category = "Stairs")
	int32 GetSectionIDFromHitResult(const FHitResult& HitResult) const;

	// ==================== VALIDATION METHODS ====================

	/** Validate stairs placement - checks level existence and landing tiles */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Validation")
	bool ValidatePlacement();

	/** Check if a level exists in the lot (within Basements to Floors range) */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Validation")
	bool DoesLevelExist(int32 CheckLevel) const;

	/** Check if floor or terrain exists at a specific tile */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Validation")
	bool HasFloorOrTerrainAtTile(int32 CheckLevel, int32 Row, int32 Column) const;

	/** Get the grid tile position for the bottom landing (in front of first step) */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Validation")
	FIntVector GetBottomLandingTile() const;

	/** Get the grid tile position for the top landing (in front of last step) */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Validation")
	FIntVector GetTopLandingTile() const;

	/** Get all tiles occupied by stair footprint at bottom level */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Validation")
	TArray<FIntVector> GetBottomLevelFootprint() const;

	/** Get all tiles occupied by stair footprint at top level (stair opening) */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Validation")
	TArray<FIntVector> GetTopLevelFootprint() const;

	// ==================== CUTAWAY METHODS ====================

	/** Cut away floor/terrain for stair opening when committed */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Cutaway")
	void CutAwayStairOpening();

	/** Restore floor/terrain if stairs are deleted (for undo support) */
	UFUNCTION(BlueprintCallable, Category = "Stairs|Cutaway")
	void RestoreStairOpening();

	// ==================== IDELETABLE INTERFACE ====================

	/** Check if this stairs can currently be deleted */
	virtual bool CanBeDeleted_Implementation() const override;

	/** Called before deletion to clean up references */
	virtual void OnDeleted_Implementation() override;

	/** Check if this stairs is currently selected/in edit mode */
	virtual bool IsSelected_Implementation() const override;

private:
	/** Setup an adjustment tool component with common properties */
	void SetupAdjustmentToolComponent(UGizmoComponent* Component, EGizmoType Type);

	/** Update adjustment tool visibility based on edit mode */
	void UpdateAdjustmentToolVisibility();

	/** Helper to get adjustment tool type from component */
	EScaleStairsToolType GetAdjustmentToolType(UGizmoComponent* Component);

	/** Check if we're currently in editor preview mode (not game world) */
	bool IsInEditorPreview() const;

	/** Handle landing tool drag operations (extracted from Tick for clarity) */
	void HandleLandingToolDrag();

	// ==================== CUTAWAY STORAGE ====================

	/** Stored floor tiles that were removed for undo support */
	TArray<FIntVector> RemovedFloorTiles;

	/** Stored terrain tiles that were removed for undo support */
	TArray<FIntVector> RemovedTerrainTiles;

	/** Whether cutaway has been applied (prevents double cutaway) */
	bool bCutawayApplied = false;
};

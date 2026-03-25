// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "GizmoComponent.generated.h"

UENUM(BlueprintType)
enum class EGizmoType : uint8
{
	// Generic types
	Move            UMETA(DisplayName = "Move"),
	Rotate          UMETA(DisplayName = "Rotate"),
	Scale           UMETA(DisplayName = "Scale"),

	// Roof specific
	Height          UMETA(DisplayName = "Height"),
	Edge            UMETA(DisplayName = "Edge"),
	FrontOverhang   UMETA(DisplayName = "Front Overhang"),
	BackOverhang    UMETA(DisplayName = "Back Overhang"),
	RightOverhang   UMETA(DisplayName = "Right Overhang"),
	LeftOverhang    UMETA(DisplayName = "Left Overhang"),
	FrontExtent     UMETA(DisplayName = "Front Extent"),
	BackExtent      UMETA(DisplayName = "Back Extent"),
	LeftExtent      UMETA(DisplayName = "Left Extent"),
	RightExtent     UMETA(DisplayName = "Right Extent"),

	// Stairs specific
	TopExtent       UMETA(DisplayName = "Top Extent"),
	BottomExtent    UMETA(DisplayName = "Bottom Extent"),
	Width           UMETA(DisplayName = "Width"),
	Landing           UMETA(DisplayName = "Landing"),

	// Control
	Confirm         UMETA(DisplayName = "Confirm"),
	Cancel          UMETA(DisplayName = "Cancel"),

	None            UMETA(DisplayName = "None")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGizmoClicked, UGizmoComponent*, Gizmo, FKey, ButtonPressed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGizmoReleased, UGizmoComponent*, Gizmo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGizmoHovered, UGizmoComponent*, Gizmo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGizmoUnhovered, UGizmoComponent*, Gizmo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGizmoDragged, UGizmoComponent*, Gizmo, const FVector&, DragDelta);

/**
 * Base Gizmo Component for interactive manipulation handles
 *
 * Provides a reusable component for creating interactive gizmos
 * that can be used for scaling, rotating, or moving objects.
 * Extends StaticMeshComponent to leverage existing mesh rendering.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UGizmoComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UGizmoComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// Called when the game ends
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ==================== PROPERTIES ====================

	/** Type of gizmo for determining behavior */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	EGizmoType GizmoType;

	/** Whether this gizmo is currently being hovered */
	UPROPERTY(BlueprintReadOnly, Category = "Gizmo")
	bool bIsHovered;

	/** Whether this gizmo is currently being dragged */
	UPROPERTY(BlueprintReadOnly, Category = "Gizmo")
	bool bIsDragging;

	/** Drag start location for calculating deltas */
	UPROPERTY(BlueprintReadOnly, Category = "Gizmo")
	FVector DragStartLocation;

	/** Current drag location */
	UPROPERTY(BlueprintReadOnly, Category = "Gizmo")
	FVector CurrentDragLocation;

	/** Axis constraints for movement (normalized vector) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	FVector MovementAxis;

	/** Whether to constrain movement to the specified axis */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	bool bConstrainToAxis;

	/** Color when not hovered or selected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo|Appearance")
	FLinearColor DefaultColor;

	/** Color when hovered */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo|Appearance")
	FLinearColor HoveredColor;

	/** Color when active/selected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo|Appearance")
	FLinearColor ActiveColor;

	/** Whether to auto-hide when not in edit mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	bool bAutoHide;

	// ==================== EVENTS ====================

	/** Called when gizmo is clicked */
	UPROPERTY(BlueprintAssignable, Category = "Gizmo|Events")
	FOnGizmoClicked OnGizmoClicked;

	/** Called when gizmo click is released */
	UPROPERTY(BlueprintAssignable, Category = "Gizmo|Events")
	FOnGizmoReleased OnGizmoReleased;

	/** Called when gizmo is hovered */
	UPROPERTY(BlueprintAssignable, Category = "Gizmo|Events")
	FOnGizmoHovered OnGizmoHovered;

	/** Called when gizmo hover ends */
	UPROPERTY(BlueprintAssignable, Category = "Gizmo|Events")
	FOnGizmoUnhovered OnGizmoUnhovered;

	/** Called when gizmo is dragged */
	UPROPERTY(BlueprintAssignable, Category = "Gizmo|Events")
	FOnGizmoDragged OnGizmoDragged;

	// ==================== METHODS ====================

	/** Activate this gizmo */
	virtual void ActivateGizmo();

	/** Deactivate this gizmo */
	virtual void DeactivateGizmo();

	/** Show the gizmo */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void ShowGizmo();

	/** Hide the gizmo */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void HideGizmo();

	/** Set whether gizmo is being dragged */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void SetDragging(bool bInDragging);

	/** Start drag operation */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void StartDrag(const FVector& StartLocation);

	/** Update drag operation */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void UpdateDrag(const FVector& CurrentLocation);

	/** End drag operation */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void EndDrag();

	/** Get drag delta from start */
	UFUNCTION(BlueprintPure, Category = "Gizmo")
	FVector GetDragDelta() const;

	/** Get constrained drag delta based on movement axis */
	UFUNCTION(BlueprintPure, Category = "Gizmo")
	FVector GetConstrainedDragDelta() const;

	/** Update gizmo appearance based on state */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void UpdateAppearance();

	/** Set gizmo colors */
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void SetColors(const FLinearColor& InDefaultColor, const FLinearColor& InHoveredColor, const FLinearColor& InActiveColor);

	/** Check if this gizmo type matches */
	UFUNCTION(BlueprintPure, Category = "Gizmo")
	bool IsGizmoType(EGizmoType Type) const { return GizmoType == Type; }

private:
	/** Handle component clicked */
	UFUNCTION()
	void HandleClicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

	/** Handle component released */
	UFUNCTION()
	void HandleReleased(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed);

	/** Handle begin cursor over */
	UFUNCTION()
	void HandleBeginCursorOver(UPrimitiveComponent* TouchedComponent);

	/** Handle end cursor over */
	UFUNCTION()
	void HandleEndCursorOver(UPrimitiveComponent* TouchedComponent);

	/** Apply color to material */
	void ApplyColor(const FLinearColor& Color);

	/** Dynamic material instance for color changes */
	UPROPERTY()
	UMaterialInstanceDynamic* DynamicMaterial;

	/** Original scale of the component (stored at initialization) */
	FVector OriginalScale;
};
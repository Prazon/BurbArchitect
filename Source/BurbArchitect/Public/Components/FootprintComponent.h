// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "Components/RoofComponent.h"
#include "FootprintComponent.generated.h"

/**
 * Component for rendering footprint boundary lines on architecture actors
 *
 * This component provides a reusable way to visualize the grid-aligned footprint
 * of architectural structures (roofs, porches, decks, etc.) during placement and editing.
 *
 * Features:
 * - Draws rectangular or polygonal footprints
 * - Configurable line appearance (color, thickness, depth priority)
 * - Supports custom dimensions for drag preview
 * - Automatic cleanup and lifecycle management
 * - Z-offset to prevent z-fighting with geometry
 */
UCLASS(ClassGroup = (BurbArchitect), meta = (BlueprintSpawnableComponent))
class BURBARCHITECT_API UFootprintComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UFootprintComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// Called when component is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ==================== PROPERTIES ====================

	/** Line batch component for rendering footprint lines */
	UPROPERTY()
	ULineBatchComponent* LineBatchComponent;

	/** Color of the footprint lines */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footprint Appearance")
	FColor LineColor;

	/** Thickness of the footprint lines in pixels */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footprint Appearance")
	float LineThickness;

	/** Depth priority for rendering (SDPG_World, SDPG_Foreground, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footprint Appearance")
	TEnumAsByte<ESceneDepthPriorityGroup> DepthPriority;

	/** Vertical offset above geometry to prevent z-fighting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footprint Appearance")
	float ZOffset;

	/** Translucency sort priority (higher values render on top) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footprint Appearance")
	int32 TranslucencySortPriority;

	// ==================== METHODS ====================

	/**
	 * Draw a rectangular footprint from four corner vertices
	 * @param FrontLeft - Front-left corner position
	 * @param FrontRight - Front-right corner position
	 * @param BackRight - Back-right corner position
	 * @param BackLeft - Back-left corner position
	 */
	UFUNCTION(BlueprintCallable, Category = "Footprint")
	void DrawRectangle(const FVector& FrontLeft, const FVector& FrontRight,
	                   const FVector& BackRight, const FVector& BackLeft);

	/**
	 * Draw a polygonal footprint from an array of vertices
	 * @param Points - Array of vertices forming the polygon (in order)
	 */
	UFUNCTION(BlueprintCallable, Category = "Footprint")
	void DrawPolygon(const TArray<FVector>& Points);

	/**
	 * Draw footprint from roof vertices structure
	 * Convenience method for roof actors that uses the Gable corners
	 * Note: Not exposed to blueprints as FRoofVertices is a C++-only struct
	 * @param RoofVerts - Roof vertex structure containing footprint corners
	 */
	void DrawFromRoofVertices(const FRoofVertices& RoofVerts);

	/**
	 * Draw footprint for a roof using dimensions
	 * Calculates vertices and draws the footprint
	 * Note: Not exposed to blueprints as FRoofDimensions is a C++-only struct
	 * @param Location - Center location of the roof
	 * @param Direction - Forward direction of the roof
	 * @param Dimensions - Roof dimensions structure
	 */
	void DrawFromRoofDimensions(const FVector& Location, const FVector& Direction,
	                           const FRoofDimensions& Dimensions);

	/**
	 * Clear all footprint lines
	 */
	UFUNCTION(BlueprintCallable, Category = "Footprint")
	void ClearFootprint();

	/**
	 * Show the footprint (if lines exist)
	 */
	UFUNCTION(BlueprintCallable, Category = "Footprint")
	void ShowFootprint();

	/**
	 * Hide the footprint
	 */
	UFUNCTION(BlueprintCallable, Category = "Footprint")
	void HideFootprint();

	/**
	 * Check if footprint is currently visible
	 */
	UFUNCTION(BlueprintPure, Category = "Footprint")
	bool IsFootprintVisible() const;

private:
	/**
	 * Initialize the line batch component if it doesn't exist
	 */
	void EnsureLineBatchComponent();

	/**
	 * Apply Z-offset to a vertex
	 */
	FVector ApplyZOffset(const FVector& Vertex) const;
};

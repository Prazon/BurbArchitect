// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/Roofs/RoofBase.h"
#include "ShedRoof.generated.h"

/**
 * Shed Roof Actor
 *
 * Implements a single-sloped (mono-pitch) roof with:
 * - Single sloped surface
 * - Higher front edge, lower back edge
 * - Triangular side walls
 * - Simplified scale controls
 * - Adjustable slope height and extents
 */
UCLASS()
class BURBARCHITECT_API AShedRoof : public ARoofBase
{
	GENERATED_BODY()

public:
	AShedRoof();

	// ==================== OVERRIDES ====================

	/** Generate the shed roof mesh */
	virtual void GenerateRoofMesh() override;

	/** Generate supporting walls including side triangular walls */
	virtual void GenerateSupportingWalls() override;

	/** Get the roof type */
	virtual ERoofType GetRoofType() const override { return ERoofType::Shed; }

	/** Check if a specific scale tool is valid for shed roofs */
	virtual bool IsScaleToolValid(EScaleToolType ToolType) const override;

	/** Setup scale tools with shed-specific positioning */
	virtual void SetupScaleTools() override;

protected:
	// ==================== SHED-SPECIFIC METHODS ====================

	/** Generate shed roof geometry (implementation details) */
	void GenerateShedRoofGeometry();

	/** Calculate shed-specific vertices */
	void CalculateShedVertices(FRoofVertices& OutVertices);

	/** Get the direction of the slope (from high to low) */
	FVector GetSlopeDirection() const;
};
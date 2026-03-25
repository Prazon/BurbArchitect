// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/Roofs/RoofBase.h"
#include "HipRoof.generated.h"

/**
 * Hip Roof Actor
 *
 * Implements a hip roof with:
 * - All four sides sloped
 * - No vertical gable ends
 * - Ridge line (or pyramid point if square)
 * - Uniform overhangs on all sides
 * - Limited scale tool controls (no separate rake/eave adjustments)
 */
UCLASS()
class BURBARCHITECT_API AHipRoof : public ARoofBase
{
	GENERATED_BODY()

public:
	AHipRoof();

	// ==================== OVERRIDES ====================

	/** Generate the hip roof mesh */
	virtual void GenerateRoofMesh() override;

	/** Generate supporting walls (perimeter only, no gable ends) */
	virtual void GenerateSupportingWalls() override;

	/** Get the roof type */
	virtual ERoofType GetRoofType() const override { return ERoofType::Hip; }

	/** Check if a specific scale tool is valid for hip roofs */
	virtual bool IsScaleToolValid(EScaleToolType ToolType) const override;

protected:
	// ==================== HIP-SPECIFIC METHODS ====================

	/** Generate hip roof geometry (implementation details) */
	void GenerateHipRoofGeometry();

	/** Calculate hip-specific vertices */
	void CalculateHipVertices(FRoofVertices& OutVertices);

	/** Check if this hip roof forms a pyramid (square base) */
	bool IsPyramidHip() const;
};
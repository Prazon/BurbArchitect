// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/Roofs/RoofBase.h"
#include "GableRoof.generated.h"

/**
 * Gable Roof Actor
 *
 * Implements a traditional gable (peaked) roof with:
 * - Two sloped sides meeting at a ridge
 * - Triangular gable end walls
 * - Adjustable overhangs on all sides
 * - Full scale tool support
 */
UCLASS()
class BURBARCHITECT_API AGableRoof : public ARoofBase
{
	GENERATED_BODY()

public:
	AGableRoof();

	// ==================== OVERRIDES ====================

	/** Generate the gable roof mesh */
	virtual void GenerateRoofMesh() override;

	/** Generate supporting walls including gable ends */
	virtual void GenerateSupportingWalls() override;

	/** Get the roof type */
	virtual ERoofType GetRoofType() const override { return ERoofType::Gable; }

	/** Check if a specific scale tool is valid for gable roofs */
	virtual bool IsScaleToolValid(EScaleToolType ToolType) const override;

protected:
	// ==================== GABLE-SPECIFIC METHODS ====================

	/** Generate gable roof geometry (implementation details) */
	void GenerateGableRoofGeometry();

	/** Calculate gable-specific vertices */
	void CalculateGableVertices(FRoofVertices& OutVertices);
};
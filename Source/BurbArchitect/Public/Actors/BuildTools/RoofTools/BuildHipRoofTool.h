// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildRoofTool.h"
#include "BuildHipRoofTool.generated.h"

/**
 * Build tool specifically for Hip roofs
 * Hip roofs have four sloped faces converging to a peak or ridge
 * All sides are sloped with no vertical gable ends
 */
UCLASS()
class BURBARCHITECT_API ABuildHipRoofTool : public ABuildRoofTool
{
	GENERATED_BODY()

protected:
	virtual ERoofType GetRoofType() const override { return ERoofType::Hip; }
};

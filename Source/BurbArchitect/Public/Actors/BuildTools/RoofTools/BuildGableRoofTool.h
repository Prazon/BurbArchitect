// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildRoofTool.h"
#include "BuildGableRoofTool.generated.h"

/**
 * Build tool specifically for Gable roofs
 * Gable roofs have two opposing slopes meeting at a ridge with triangular gable ends
 */
UCLASS()
class BURBARCHITECT_API ABuildGableRoofTool : public ABuildRoofTool
{
	GENERATED_BODY()

protected:
	virtual ERoofType GetRoofType() const override { return ERoofType::Gable; }
};

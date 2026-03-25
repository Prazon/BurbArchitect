// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildRoofTool.h"
#include "BuildShedRoofTool.generated.h"

/**
 * Build tool specifically for Shed roofs
 * Shed roofs have a single sloped surface from high to low
 * Popular for modern and contemporary architecture
 */
UCLASS()
class BURBARCHITECT_API ABuildShedRoofTool : public ABuildRoofTool
{
	GENERATED_BODY()

protected:
	virtual ERoofType GetRoofType() const override { return ERoofType::Shed; }
};

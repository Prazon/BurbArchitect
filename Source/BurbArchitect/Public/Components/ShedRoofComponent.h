// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/RoofComponent.h"
#include "ShedRoofComponent.generated.h"

/**
 * Component specifically for Shed roofs
 * Handles a single shed roof instance with a single sloped surface
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UShedRoofComponent : public URoofComponent
{
	GENERATED_BODY()

public:
	UShedRoofComponent(const FObjectInitializer& ObjectInitializer);

	// Override to generate shed roof geometry
	virtual FRoofSegmentData GenerateRoofMeshSection(FRoofSegmentData InRoofData) override;
};

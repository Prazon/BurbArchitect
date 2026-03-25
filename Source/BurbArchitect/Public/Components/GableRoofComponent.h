// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/RoofComponent.h"
#include "GableRoofComponent.generated.h"

/**
 * Component specifically for Gable roofs
 * Handles a single gable roof instance with two opposing slopes and triangular gable ends
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UGableRoofComponent : public URoofComponent
{
	GENERATED_BODY()

public:
	UGableRoofComponent(const FObjectInitializer& ObjectInitializer);

	// Override to generate gable roof geometry
	virtual FRoofSegmentData GenerateRoofMeshSection(FRoofSegmentData InRoofData) override;
};

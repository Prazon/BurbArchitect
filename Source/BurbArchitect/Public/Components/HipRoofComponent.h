// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/RoofComponent.h"
#include "HipRoofComponent.generated.h"

/**
 * Component specifically for Hip roofs
 * Handles a single hip roof instance with four sloped faces converging to peak or ridge
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UHipRoofComponent : public URoofComponent
{
	GENERATED_BODY()

public:
	UHipRoofComponent(const FObjectInitializer& ObjectInitializer);

	// Override to generate hip roof geometry
	virtual FRoofSegmentData GenerateRoofMeshSection(FRoofSegmentData InRoofData) override;
};

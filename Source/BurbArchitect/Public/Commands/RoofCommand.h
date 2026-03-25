// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "Actors/LotManager.h"
#include "Components/RoofComponent.h"
#include "RoofCommand.generated.h"

/**
 * Roof Command - Handles creating and destroying roof segments
 */
UCLASS()
class URoofCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	void Initialize(ALotManager* Lot, const FVector& Loc, const FVector& Dir, const FRoofDimensions& Dims, float RoofThick, float GableThick, UMaterialInstance* Mat);

	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

protected:
	UPROPERTY()
	ALotManager* LotManager;

	UPROPERTY()
	FVector Location;

	UPROPERTY()
	FVector Direction;

	UPROPERTY()
	FRoofDimensions Dimensions;

	UPROPERTY()
	float RoofThickness;

	UPROPERTY()
	float GableThickness;

	UPROPERTY()
	UMaterialInstance* RoofMaterial;

	// Roof data created by this command
	UPROPERTY()
	FRoofSegmentData RoofData;

	// Reference to the roof component created by this command (legacy - for backward compat)
	UPROPERTY()
	URoofComponent* CreatedRoofComponent;

	// Reference to the new roof actor created by this command
	UPROPERTY()
	class ARoofBase* CreatedRoofActor;

	// Whether roof was successfully created
	bool bRoofCreated;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "Actors/LotManager.h"
#include "Components/StairsComponent.h"
#include "StairsCommand.generated.h"

/**
 * Stairs Command - Handles creating and destroying staircase actors
 */
UCLASS()
class UStairsCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	void Initialize(ALotManager* Lot, TSubclassOf<class AStairsBase> StairsClass, const FVector& Start, const FVector& Dir, const TArray<FStairModuleStructure>& Structs, float StairsThick, UStaticMesh* TreadMesh, UStaticMesh* LandingMesh);

	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

protected:
	UPROPERTY()
	ALotManager* LotManager;

	UPROPERTY()
	TSubclassOf<class AStairsBase> StairsActorClass;

	UPROPERTY()
	FVector StartLoc;

	UPROPERTY()
	FVector Direction;

	UPROPERTY()
	TArray<FStairModuleStructure> Structures;

	UPROPERTY()
	float StairsThickness;

	UPROPERTY()
	UStaticMesh* StairTreadMesh;

	UPROPERTY()
	UStaticMesh* StairLandingMesh;

	// Stairs actor created by this command
	UPROPERTY()
	class AStairsBase* StairsActor;
};

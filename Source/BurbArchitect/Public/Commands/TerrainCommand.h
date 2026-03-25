// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "Actors/LotManager.h"
#include "Components/TerrainComponent.h"
#include "TerrainCommand.generated.h"

/**
 * Terrain operation type
 */
UENUM()
enum class ETerrainOperation : uint8
{
	Raise,
	Lower,
	Flatten
};

/**
 * Terrain Command - Handles terrain modifications with undo/redo
 * Stores before/after state of terrain heights
 */
UCLASS()
class UTerrainCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	void InitializeRaise(ALotManager* Lot, int32 InRow, int32 InColumn, const TArray<float>& InSpans, UMaterialInstance* Mat);
	void InitializeLower(ALotManager* Lot, int32 InRow, int32 InColumn, const TArray<float>& InSpans, UMaterialInstance* Mat);
	void InitializeFlatten(ALotManager* Lot, int32 InRow, int32 InColumn, float InTargetHeight);

	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

protected:
	UPROPERTY()
	ALotManager* LotManager;

	UPROPERTY()
	int32 Row;

	UPROPERTY()
	int32 Column;

	UPROPERTY()
	TArray<float> Spans;

	UPROPERTY()
	float TargetHeight;

	UPROPERTY()
	UMaterialInstance* Material;

	UPROPERTY()
	ETerrainOperation Operation;

	// Level of the terrain being modified
	UPROPERTY()
	int32 Level;

	// Before state - stores original corner heights for undo
	// Order: [BottomLeft, BottomRight, TopLeft, TopRight]
	UPROPERTY()
	TArray<float> BeforeCornerHeights;

	// After state - stores modified corner heights for redo
	// Order: [BottomLeft, BottomRight, TopLeft, TopRight]
	UPROPERTY()
	TArray<float> AfterCornerHeights;

	// Whether we've captured the before state
	bool bHasBeforeState;
};

// Fill out your copyright notice in the Description page of Project Settings.

#include "Commands/TerrainCommand.h"

void UTerrainCommand::InitializeRaise(ALotManager* Lot, int32 InRow, int32 InColumn, const TArray<float>& InSpans, UMaterialInstance* Mat)
{
	LotManager = Lot;
	Row = InRow;
	Column = InColumn;
	Spans = InSpans;
	Material = Mat;
	Operation = ETerrainOperation::Raise;
	TargetHeight = 0.0f;
	bHasBeforeState = false;
}

void UTerrainCommand::InitializeLower(ALotManager* Lot, int32 InRow, int32 InColumn, const TArray<float>& InSpans, UMaterialInstance* Mat)
{
	LotManager = Lot;
	Row = InRow;
	Column = InColumn;
	Spans = InSpans;
	Material = Mat;
	Operation = ETerrainOperation::Lower;
	TargetHeight = 0.0f;
	bHasBeforeState = false;
}

void UTerrainCommand::InitializeFlatten(ALotManager* Lot, int32 InRow, int32 InColumn, float InTargetHeight)
{
	LotManager = Lot;
	Row = InRow;
	Column = InColumn;
	TargetHeight = InTargetHeight;
	Operation = ETerrainOperation::Flatten;
	Material = nullptr;
	Spans.Empty();
	bHasBeforeState = false;
}

void UTerrainCommand::Commit()
{
	if (!LotManager || !LotManager->TerrainComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("TerrainCommand: LotManager or TerrainComponent is null"));
		return;
	}

	// Get terrain tile to determine level
	FTerrainSegmentData* TerrainData = nullptr;
	if (!LotManager->FindTerrain(Row, Column, TerrainData))
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainCommand: Could not find terrain at Row=%d, Column=%d"), Row, Column);
		return;
	}

	Level = TerrainData->Level;

	// Get height map for this level
	FTerrainHeightMap* HeightMap = LotManager->TerrainComponent->GetOrCreateHeightMap(Level);
	if (!HeightMap)
	{
		UE_LOG(LogTemp, Error, TEXT("TerrainCommand: Failed to get height map for level %d"), Level);
		return;
	}

	// Capture before state - store corner heights from HeightMap
	// Corner order: [BottomLeft, BottomRight, TopLeft, TopRight]
	BeforeCornerHeights.SetNum(4);
	BeforeCornerHeights[0] = HeightMap->GetCornerHeight(Row, Column);         // BottomLeft
	BeforeCornerHeights[1] = HeightMap->GetCornerHeight(Row, Column + 1);     // BottomRight
	BeforeCornerHeights[2] = HeightMap->GetCornerHeight(Row + 1, Column);     // TopLeft
	BeforeCornerHeights[3] = HeightMap->GetCornerHeight(Row + 1, Column + 1); // TopRight
	bHasBeforeState = true;

	// Apply the terrain modification
	switch (Operation)
	{
	case ETerrainOperation::Raise:
		LotManager->BrushTerrainRaise(Row, Column, Spans, Material);
		break;

	case ETerrainOperation::Lower:
		LotManager->BrushTerrainLower(Row, Column, Spans, Material);
		break;

	case ETerrainOperation::Flatten:
		LotManager->BrushTerrainFlatten(Row, Column, TargetHeight);
		break;
	}

	// Capture after state - store corner heights from HeightMap
	AfterCornerHeights.SetNum(4);
	AfterCornerHeights[0] = HeightMap->GetCornerHeight(Row, Column);         // BottomLeft
	AfterCornerHeights[1] = HeightMap->GetCornerHeight(Row, Column + 1);     // BottomRight
	AfterCornerHeights[2] = HeightMap->GetCornerHeight(Row + 1, Column);     // TopLeft
	AfterCornerHeights[3] = HeightMap->GetCornerHeight(Row + 1, Column + 1); // TopRight
	bCommitted = true;

	UE_LOG(LogTemp, Log, TEXT("TerrainCommand: Committed %s terrain at Row=%d, Column=%d"),
		Operation == ETerrainOperation::Raise ? TEXT("Raise") :
		Operation == ETerrainOperation::Lower ? TEXT("Lower") : TEXT("Flatten"),
		Row, Column);
}

void UTerrainCommand::Undo()
{
	if (!LotManager || !LotManager->TerrainComponent || !bHasBeforeState)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainCommand: Cannot undo - invalid state"));
		return;
	}

	// Restore corner heights via UpdateCornerHeight (uses incremental updates)
	// Use batch operation to update all 4 corners at once
	LotManager->TerrainComponent->BeginBatchOperation();

	// Corner order: [BottomLeft, BottomRight, TopLeft, TopRight]
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row, Column, BeforeCornerHeights[0]);         // BottomLeft
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row, Column + 1, BeforeCornerHeights[1]);     // BottomRight
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column, BeforeCornerHeights[2]);     // TopLeft
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column + 1, BeforeCornerHeights[3]); // TopRight

	LotManager->TerrainComponent->EndBatchOperation();

	UE_LOG(LogTemp, Log, TEXT("TerrainCommand: Undid terrain modification at Row=%d, Column=%d"), Row, Column);
}

void UTerrainCommand::Redo()
{
	if (!LotManager || !LotManager->TerrainComponent || !bCommitted)
	{
		UE_LOG(LogTemp, Warning, TEXT("TerrainCommand: Cannot redo - not committed"));
		return;
	}

	// Restore corner heights via UpdateCornerHeight (uses incremental updates)
	// Use batch operation to update all 4 corners at once
	LotManager->TerrainComponent->BeginBatchOperation();

	// Corner order: [BottomLeft, BottomRight, TopLeft, TopRight]
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row, Column, AfterCornerHeights[0]);         // BottomLeft
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row, Column + 1, AfterCornerHeights[1]);     // BottomRight
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column, AfterCornerHeights[2]);     // TopLeft
	LotManager->TerrainComponent->UpdateCornerHeight(Level, Row + 1, Column + 1, AfterCornerHeights[3]); // TopRight

	LotManager->TerrainComponent->EndBatchOperation();

	UE_LOG(LogTemp, Log, TEXT("TerrainCommand: Redid terrain modification at Row=%d, Column=%d"), Row, Column);
}

FString UTerrainCommand::GetDescription() const
{
	FString OpName;
	switch (Operation)
	{
	case ETerrainOperation::Raise:
		OpName = TEXT("Raise");
		break;
	case ETerrainOperation::Lower:
		OpName = TEXT("Lower");
		break;
	case ETerrainOperation::Flatten:
		OpName = TEXT("Flatten");
		break;
	}

	return FString::Printf(TEXT("%s Terrain at (%d, %d)"), *OpName, Row, Column);
}

bool UTerrainCommand::IsValid() const
{
	return LotManager && bHasBeforeState;
}

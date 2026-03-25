// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BrushTools/BrushFlattenTool.h"
#include "BrushFlattenLotTool.generated.h"

/**
 * Flatten entire lot tool
 * Flattens the entire lot to a default height in a single operation
 * Child of BrushFlattenTool but operates on the entire lot instead of a brush radius
 */
UCLASS()
class BURBARCHITECT_API ABrushFlattenLotTool : public ABrushFlattenTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABrushFlattenLotTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Override click to flatten entire lot
	virtual void Click_Implementation() override;

	// Override release to finalize operation
	virtual void BroadcastRelease_Implementation() override;

	// Default height to flatten the lot to (relative to lot BaseZ)
	// 0.0f means perfectly flat at lot base height
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Flatten Lot Tool")
	float DefaultHeight = 0.0f;

	// Whether to hide the brush decal (typically true since we're flattening entire lot)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Flatten Lot Tool")
	bool bHideDecal = true;

private:
	// Flatten the entire lot to the default height
	void FlattenEntireLot();
};

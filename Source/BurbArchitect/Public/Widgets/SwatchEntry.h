// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SwatchEntry.generated.h"

class UArchitectureItem;

/**
 * Simple data object for representing a color swatch in a TileView
 * Reuses CatalogItemCard to display the pattern with a color tint
 */
UCLASS(BlueprintType)
class BURBARCHITECT_API USwatchEntry : public UObject
{
	GENERATED_BODY()

public:
	/** The pattern this swatch belongs to */
	UPROPERTY(BlueprintReadOnly, Category = "Swatch")
	TObjectPtr<UArchitectureItem> Pattern;

	/** The color this swatch represents */
	UPROPERTY(BlueprintReadOnly, Category = "Swatch")
	FLinearColor Color;

	/** Index of this swatch in the pattern's ColourSwatches array */
	UPROPERTY(BlueprintReadOnly, Category = "Swatch")
	int32 SwatchIndex = 0;
};

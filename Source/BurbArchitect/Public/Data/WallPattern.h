// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/ArchitectureItem.h"
#include "Engine/DataAsset.h"
#include "WallPattern.generated.h"

/**
 * Wall Patterns represent various wall surface designs, materials, and textures.
 * They can be applied to wall sections to change their appearance.
 */
UCLASS()
class BURBARCHITECT_API UWallPattern : public UArchitectureItem
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	UTexture* BaseTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	UTexture* NormalMap;

	// Enable detail normal map for additional surface detail
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	bool bUseDetailNormal = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (EditCondition = "bUseDetailNormal"))
	UTexture* DetailNormal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern", meta = (EditCondition = "bUseDetailNormal", ClampMin = "0.0", ClampMax = "32.0"))
	float DetailNormalIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	UTexture* RoughnessMap;

	// Optional: Base material template for this pattern
	// If not set, the tool's default base material will be used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern")
	UMaterialInstance* BaseMaterial;

	// Enable color swatch system for recoloring the pattern
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Swatches")
	bool bUseColourSwatches = false;

	// Enable color mask from RMA texture (alpha channel contains mask)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Swatches", meta = (EditCondition = "bUseColourSwatches"))
	bool bUseColourMask = false;

	// Array of colors that can be applied to the pattern
	// User selects from these via swatch picker UI
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Swatches", meta = (EditCondition = "bUseColourSwatches"))
	TArray<FLinearColor> ColourSwatches;
};

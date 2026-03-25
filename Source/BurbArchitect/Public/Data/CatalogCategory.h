// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatalogCategory.generated.h"

/**
 * Catalog category data asset for organizing catalog items
 * Represents a root category in the catalog system (e.g., Comfort, Surfaces, Lighting)
 */
UCLASS(BlueprintType)
class BURBARCHITECT_API UCatalogCategory : public UDataAsset
{
	GENERATED_BODY()

public:
	// Display name shown in UI
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FText DisplayName;

	// Icon for category tab (e.g., chair icon for seating)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	UTexture2D* Icon;

	// Sort order in catalog UI
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	int32 SortOrder = 0;

	// Optional: Description tooltip
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (MultiLine = true))
	FText Description;
};

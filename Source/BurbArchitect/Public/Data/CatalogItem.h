// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/PlaceableObject.h"
#include "Engine/DataAsset.h"
#include "Data/CatalogCategory.h"
#include "Data/CatalogSubcategory.h"
#include "CatalogItem.generated.h"

// Forward declarations
class ABuildTool;

/**
 * Base class for all catalog items (furniture, architecture, etc.)
 */
UCLASS()
class BURBARCHITECT_API UCatalogItem : public UDataAsset
{
	GENERATED_BODY()

public:
	//boolean for disabling this item from catalog view
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bDisabledInCatalog = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FSlateBrush Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	float Cost;

	// Root category (type-safe: only UCatalogCategory, NOT subcategories)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog",
		meta = (AllowedClasses = "/Script/BurbArchitect.CatalogCategory"))
	UCatalogCategory* Category;

	// Optional: Subcategory (type-safe: only UCatalogSubcategory)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog",
		meta = (AllowedClasses = "/Script/BurbArchitect.CatalogSubcategory", EditCondition = "Category != nullptr"))
	UCatalogSubcategory* Subcategory;
};

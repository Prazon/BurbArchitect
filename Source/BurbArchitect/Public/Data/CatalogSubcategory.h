// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/CatalogCategory.h"
#include "CatalogSubcategory.generated.h"

/**
 * Catalog subcategory data asset for nested organization within categories
 * Represents a subcategory that belongs to a parent UCatalogCategory (e.g., Seating within Comfort)
 */
UCLASS(BlueprintType)
class BURBARCHITECT_API UCatalogSubcategory : public UCatalogCategory
{
	GENERATED_BODY()

public:
	// Parent category (type-safe: designers can ONLY select UCatalogCategory, not other subcategories)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (AllowedClasses = "/Script/BurbArchitect.CatalogCategory"))
	UCatalogCategory* ParentCategory;

	// Note: SortOrder inherited from base class determines order within parent category
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "CatalogItemEntry.generated.h"

/**
 * Data wrapper for catalog items in UTileView
 * Stores the soft object path for lazy-loaded catalog items
 */
UCLASS(BlueprintType)
class BURBARCHITECT_API UCatalogItemEntry : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Initialize with an item path
	 * @param InItemPath - Soft object path to the catalog item
	 */
	void Initialize(const FSoftObjectPath& InItemPath);

	/**
	 * Get the item path
	 * @return The soft object path to the catalog item
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	const FSoftObjectPath& GetItemPath() const { return ItemPath; }

protected:
	/**
	 * Soft object path to the catalog item (lazy loading)
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Catalog")
	FSoftObjectPath ItemPath;
};

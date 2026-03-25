// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/CatalogCategory.h"
#include "Data/CatalogSubcategory.h"
#include "Data/CatalogItem.h"
#include "CatalogSubsystem.generated.h"

/**
 * Catalog subsystem for managing catalog items and categories
 * Scans all catalog item data assets at initialization and organizes them by category/subcategory
 * Supports modding - directory agnostic scanning via Asset Registry
 */
UCLASS()
class BURBARCHITECT_API UCatalogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Get all root categories (sorted by SortOrder)
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	TArray<UCatalogCategory*> GetRootCategories() const { return RootCategories; }

	// Get subcategories for a category (sorted by SortOrder)
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	TArray<UCatalogSubcategory*> GetSubcategories(UCatalogCategory* Category) const;

	// Get all items in category (includes items with or without subcategories) - loads items on-demand
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	TArray<UCatalogItem*> GetItemsInCategory(UCatalogCategory* Category);

	// Get items in specific subcategory only - loads items on-demand
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	TArray<UCatalogItem*> GetItemsInSubcategory(UCatalogSubcategory* Subcategory);

	// Get item paths in category without loading (fast, for UI lists)
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	TArray<FSoftObjectPath> GetItemPathsInCategory(UCatalogCategory* Category) const;

	// Get item paths in subcategory without loading (fast, for UI lists)
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	TArray<FSoftObjectPath> GetItemPathsInSubcategory(UCatalogSubcategory* Subcategory) const;

	// Load a catalog item on-demand from its path
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	UCatalogItem* LoadItem(const FSoftObjectPath& ItemPath);

	// Load multiple catalog items on-demand from their paths
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	TArray<UCatalogItem*> LoadItems(const TArray<FSoftObjectPath>& ItemPaths);

	// Search items by display name (case-insensitive, partial match) - loads items on-demand
	UFUNCTION(BlueprintCallable, Category = "Catalog|Search")
	TArray<UCatalogItem*> SearchItemsByName(const FString& SearchText);

	// Search item paths by asset name without loading (fast)
	UFUNCTION(BlueprintCallable, Category = "Catalog|Search")
	TArray<FSoftObjectPath> SearchItemPathsByName(const FString& SearchText) const;

	// Refresh catalog (rescan Asset Registry - useful for editor workflow)
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void RefreshCatalog();

private:
	// Main storage: Category -> Item Paths (lazy loading - stores paths instead of loaded objects)
	TMultiMap<UCatalogCategory*, FSoftObjectPath> ItemPathsByCategory;

	// Subcategory -> Item Paths (only items with specific subcategory)
	TMultiMap<UCatalogSubcategory*, FSoftObjectPath> ItemPathsBySubcategory;

	// Category -> Subcategories (parent-child relationships)
	TMultiMap<UCatalogCategory*, UCatalogSubcategory*> SubcategoriesByCategory;

	// All root categories (sorted by SortOrder)
	UPROPERTY()
	TArray<UCatalogCategory*> RootCategories;

	// Cache for loaded items (weak pointers allow GC if needed)
	TMap<FSoftObjectPath, TWeakObjectPtr<UCatalogItem>> LoadedItemsCache;

	// Internal: Build catalog from Asset Registry
	void BuildCatalog();
};

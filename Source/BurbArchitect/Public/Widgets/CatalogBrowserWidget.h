// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/CatalogWidgetBase.h"
#include "CatalogBrowserWidget.generated.h"

class UCategoryNavigationWidget;
class USubcategoryNavigationWidget;
class UCatalogItemGrid;
class UCatalogSwatchPicker;
class UCatalogItem;
class UCatalogCategory;
class UCatalogSubcategory;

/**
 * Delegate for item activation events (when user clicks to place/buy an item)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnItemActivated, UCatalogItem*, Item);

/**
 * Main catalog browser widget that orchestrates all catalog UI components
 * Combines category navigation, subcategory filtering, and item grid display
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API UCatalogBrowserWidget : public UCatalogWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 * Initialize the catalog browser
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void InitializeBrowser();

	/**
	 * Refresh all catalog data (rescans from subsystem)
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void RefreshCatalog();

	/**
	 * Set the active category (updates subcategories and item grid)
	 * @param Category - The category to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetActiveCategory(UCatalogCategory* Category);

	/**
	 * Set the active subcategory filter (updates item grid)
	 * @param Subcategory - The subcategory to filter by (nullptr = show all in category)
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetActiveSubcategory(UCatalogSubcategory* Subcategory);

	/**
	 * Get the currently selected item
	 * @return The selected item, or nullptr if none selected
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogItem* GetSelectedItem() const;

	/**
	 * Get the active category
	 * @return The active category
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogCategory* GetActiveCategory() const { return ActiveCategory; }

	/**
	 * Get the active subcategory
	 * @return The active subcategory, or nullptr if showing all
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogSubcategory* GetActiveSubcategory() const { return ActiveSubcategory; }

	/**
	 * Event fired when user activates an item (double-click or explicit activation)
	 */
	UPROPERTY(BlueprintAssignable, Category = "Catalog Events")
	FOnItemActivated OnItemActivated;

protected:
	virtual void NativeConstruct() override;

	//~ Sub-Widget References (bind in Blueprint)

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCategoryNavigationWidget> CategoryNavigation;

	UPROPERTY(meta = (BindWidget, BindWidgetOptional))
	TObjectPtr<USubcategoryNavigationWidget> SubcategoryNavigation;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCatalogItemGrid> ItemGrid;

public:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCatalogSwatchPicker> SwatchPicker;

protected:

	//~ State

	/**
	 * Currently active category
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	TObjectPtr<UCatalogCategory> ActiveCategory;

	/**
	 * Currently active subcategory (nullptr = show all in category)
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	TObjectPtr<UCatalogSubcategory> ActiveSubcategory;

	//~ Configuration

	/**
	 * Automatically initialize on construct
	 */
	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAutoInitializeOnConstruct = true;

	//~ Event Handlers

	UFUNCTION()
	void OnCategorySelected(UCatalogCategory* Category);

	UFUNCTION()
	void OnSubcategorySelected(UCatalogSubcategory* Subcategory);

	UFUNCTION()
	void OnItemSelected(UCatalogItem* Item);

	//~ Blueprint Events

	/**
	 * Called when the browser is initialized
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnBrowserInitialized();

	/**
	 * Called when the active category changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnActiveCategoryChanged(UCatalogCategory* Category);

	/**
	 * Called when the active subcategory changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnActiveSubcategoryChanged(UCatalogSubcategory* Subcategory);

	/**
	 * Called when the active item changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnActiveItemChanged(UCatalogItem* Item);

private:
	/**
	 * Update the item grid based on current category/subcategory selection
	 */
	void UpdateItemGrid();
};

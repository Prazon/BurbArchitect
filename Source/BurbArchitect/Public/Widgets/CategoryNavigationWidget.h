// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/CatalogWidgetBase.h"
#include "Components/PanelWidget.h"
#include "CategoryNavigationWidget.generated.h"

class UCategoryTabWidget;
class UCatalogCategory;

/**
 * Widget that displays root categories as a tab bar
 * Manages category selection and switching
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API UCategoryNavigationWidget : public UCatalogWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 * Initialize and populate with categories from the catalog subsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void RefreshCategories();

	/**
	 * Select a specific category
	 * @param Category - The category to select
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SelectCategory(UCatalogCategory* Category);

	/**
	 * Get the currently selected category
	 * @return The selected category, or nullptr if none selected
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogCategory* GetSelectedCategory() const { return SelectedCategory; }

protected:
	virtual void NativeConstruct() override;

	/**
	 * Currently selected category
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	TObjectPtr<UCatalogCategory> SelectedCategory;

	//~ Widget Bindings

	/**
	 * Container for category tabs (can be HorizontalBox or VerticalBox)
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> CategoryTabContainer;

	//~ Configuration

	/**
	 * Widget class to use for category tabs
	 */
	UPROPERTY(EditAnywhere, Category = "Widget Classes")
	TSubclassOf<UCategoryTabWidget> CategoryTabClass;

	/**
	 * Automatically refresh categories on construction
	 */
	UPROPERTY(EditAnywhere, Category = "Options")
	bool bAutoRefreshOnConstruct = true;

	//~ Spawned Widgets

	/**
	 * Array of spawned category tab widgets
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCategoryTabWidget>> CategoryTabs;

	//~ Event Handlers

	UFUNCTION()
	void OnCategoryTabSelected(UCatalogCategory* Category);

	//~ Blueprint Events

	/**
	 * Called when categories are refreshed
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnCategoriesRefreshed();

	/**
	 * Called when a category is selected
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnCategorySelected(UCatalogCategory* Category);

private:
	/**
	 * Update visual selection state on all tabs
	 */
	void UpdateTabSelection();
};

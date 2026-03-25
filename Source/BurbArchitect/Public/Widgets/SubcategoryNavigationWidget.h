// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/CatalogWidgetBase.h"
#include "Components/PanelWidget.h"
#include "SubcategoryNavigationWidget.generated.h"

class UCategoryTabWidget;
class UCatalogCategory;
class UCatalogSubcategory;

/**
 * Delegate for subcategory selection events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubcategorySelected, UCatalogSubcategory*, Subcategory);

/**
 * Widget that displays subcategories as a tab bar
 * Shows subcategories for the currently selected parent category
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API USubcategoryNavigationWidget : public UCatalogWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 * Set the parent category to show subcategories for
	 * @param Category - The parent category
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetParentCategory(UCatalogCategory* Category);

	/**
	 * Select a specific subcategory
	 * @param Subcategory - The subcategory to select (nullptr for "All" / no filter)
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SelectSubcategory(UCatalogSubcategory* Subcategory);

	/**
	 * Get the currently selected subcategory
	 * @return The selected subcategory, or nullptr if showing all items
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogSubcategory* GetSelectedSubcategory() const { return SelectedSubcategory; }

	/**
	 * Get the parent category
	 * @return The parent category
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogCategory* GetParentCategory() const { return ParentCategory; }

	/**
	 * Event fired when a subcategory is selected
	 */
	UPROPERTY(BlueprintAssignable, Category = "Subcategory Events")
	FOnSubcategorySelected OnSubcategorySelected;

protected:
	virtual void NativeConstruct() override;

	/**
	 * Parent category whose subcategories are displayed
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	TObjectPtr<UCatalogCategory> ParentCategory;

	/**
	 * Currently selected subcategory (nullptr = show all items in parent category)
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	TObjectPtr<UCatalogSubcategory> SelectedSubcategory;

	//~ Widget Bindings

	/**
	 * Container for subcategory tabs (can be HorizontalBox or VerticalBox)
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> SubcategoryTabContainer;

	/**
	 * Optional "All" button to show all items in parent category
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCategoryTabWidget> AllButton;

	//~ Configuration

	/**
	 * Widget class to use for subcategory tabs
	 */
	UPROPERTY(EditAnywhere, Category = "Widget Classes")
	TSubclassOf<UCategoryTabWidget> SubcategoryTabClass;

	/**
	 * Show "All" button to display all items in parent category
	 */
	UPROPERTY(EditAnywhere, Category = "Options")
	bool bShowAllButton = true;

	/**
	 * Text for the "All" button
	 */
	UPROPERTY(EditAnywhere, Category = "Options")
	FText AllButtonText = FText::FromString(TEXT("All"));

	//~ Spawned Widgets

	/**
	 * Array of spawned subcategory tab widgets
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCategoryTabWidget>> SubcategoryTabs;

	//~ Event Handlers

	UFUNCTION()
	void OnSubcategoryTabSelected(UCatalogCategory* Category);

	UFUNCTION()
	void OnAllButtonClicked();

	//~ Blueprint Events

	/**
	 * Called when parent category changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnParentCategoryChanged(UCatalogCategory* Category);

	/**
	 * Called when subcategory filter changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnSubcategoryFilterChanged(UCatalogSubcategory* Subcategory);

private:
	/**
	 * Rebuild subcategory tabs for current parent category
	 */
	void RebuildSubcategoryTabs();

	/**
	 * Update visual selection state on all tabs
	 */
	void UpdateTabSelection();
};

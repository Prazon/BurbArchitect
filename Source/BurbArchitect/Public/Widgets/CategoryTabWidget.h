// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/CatalogWidgetBase.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "CategoryTabWidget.generated.h"

class UCatalogCategory;

/**
 * Delegate for tab selection events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTabSelected, UCatalogCategory*, Category);

/**
 * Widget representing a single category or subcategory tab button
 * Reusable for both root categories and subcategories
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API UCategoryTabWidget : public UCatalogWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 * Set the category this tab represents
	 * @param InCategory - The catalog category or subcategory
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetCategory(UCatalogCategory* InCategory);

	/**
	 * Set the selection state of this tab
	 * @param bInSelected - Whether this tab is selected
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetSelected(bool bInSelected);

	/**
	 * Get the category this tab represents
	 * @return The catalog category
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogCategory* GetCategory() const { return Category; }

	/**
	 * Is this tab currently selected?
	 * @return True if selected
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	bool IsSelected() const { return bIsSelected; }

	/**
	 * Event fired when this tab is selected
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tab Events")
	FOnTabSelected OnTabSelected;

protected:
	virtual void NativeConstruct() override;

	/**
	 * The category this tab represents
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catalog")
	TObjectPtr<UCatalogCategory> Category;

	/**
	 * Is this tab currently selected?
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	bool bIsSelected = false;

	//~ Widget Bindings

	UPROPERTY(meta = (BindWidget, BindWidgetOptional))
	TObjectPtr<UImage> CategoryIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CategoryNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> TabButton;

	//~ Styling

	/**
	 * Tint color when selected
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor SelectedTint = FLinearColor(1.0f, 0.8f, 0.3f, 1.0f);

	/**
	 * Tint color when unselected
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor UnselectedTint = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

	/**
	 * Text color when selected
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FSlateColor SelectedTextColor = FSlateColor(FLinearColor::White);

	/**
	 * Text color when unselected
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FSlateColor UnselectedTextColor = FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f));

	//~ Event Handlers

	UFUNCTION()
	void OnTabButtonClicked();

	//~ Blueprint Events

	/**
	 * Called when the category is set
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnCategorySet(UCatalogCategory* InCategory);

	/**
	 * Called when selection state changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnSelectionChanged(bool bSelected);

private:
	/**
	 * Update visual appearance based on selection state
	 */
	void UpdateVisuals();
};

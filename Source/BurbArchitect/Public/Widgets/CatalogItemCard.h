// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Widgets/CatalogWidgetBase.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "CatalogItemCard.generated.h"

class UCatalogItem;
class UCatalogSwatchPicker;

/**
 * Widget that displays a single catalog item as a card
 * Shows item icon, name, and cost with hover/selection states
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API UCatalogItemCard : public UCatalogWidgetBase, public IUserObjectListEntry
{
	GENERATED_BODY()

public:
	/**
	 * Set the item by path (lazy loading)
	 * The item will be loaded asynchronously when needed
	 * @param InItemPath - Soft object path to the catalog item
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetItemPath(const FSoftObjectPath& InItemPath);

	/**
	 * Set the item directly (already loaded)
	 * @param InItem - The catalog item to display
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetItem(UCatalogItem* InItem);

	/**
	 * Get the catalog item (loads if needed)
	 * @return The catalog item, or nullptr if not loaded
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	UCatalogItem* GetItem();

	/**
	 * Set the selection state of this card
	 * @param bIsSelected - Whether this card is selected
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetSelected(bool bInSelected);

	/**
	 * Set the hover state of this card
	 * @param bIsHovered - Whether this card is being hovered
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetHovered(bool bInHovered);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	//~ IUserObjectListEntry Interface
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
	//~ End IUserObjectListEntry Interface

	/**
	 * Blueprint event called when this card is displaying a swatch entry
	 * Use this to apply the color tint material to the ItemIcon
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnSwatchEntrySet(const FLinearColor& SwatchColor, int32 SwatchIndex, bool bUseColourSwatches, bool bUseColourMask, UTexture* BaseTexture);
	
	/**
	 * Lazy-loaded item path
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catalog")
	FSoftObjectPath ItemPath;

	/**
	 * The loaded catalog item
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catalog")
	TObjectPtr<UCatalogItem> Item;

	/**
	 * Is this card currently selected?
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	bool bIsSelected = false;

	/**
	 * Is this card currently being hovered?
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	bool bIsHovered = false;

	/**
	 * Is this card displaying a swatch entry (color swatch) rather than a catalog item?
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	bool bIsSwatchEntry = false;

	/**
	 * Cached reference to the list item object this card represents
	 */
	UPROPERTY(Transient)
	TObjectPtr<UObject> CachedListItemObject;

public:
	//~ Widget Bindings (must be created in Blueprint with these names)

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemName;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UButton> ItemButton;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, BindWidgetOptional))
	TObjectPtr<UBorder> SelectionBorder;

	/**
	 * Find the SwatchPicker for this card by traversing up to the owning CatalogBrowserWidget
	 * This ensures each player's UI uses their own SwatchPicker (important for multiplayer)
	 */
	UCatalogSwatchPicker* FindOwningSwatchPicker() const;

protected:

	//~ Styling

	/**
	 * Border color when selected
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor SelectedBorderColor = FLinearColor(0.0f, 1.0f, 0.0f, 1.0f);
	/**
	 * Border color when unselected
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor UnselectedBorderColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	
	/**
	 * Tint when hovered
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor HoverTint = FLinearColor(1.2f, 1.2f, 1.2f, 1.0f);

	/**
	 * Normal tint
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor NormalTint = FLinearColor::White;

	/**
	 * Color for affordable items
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor AffordableColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/**
	 * Color for unaffordable items
	 */
	UPROPERTY(EditAnywhere, Category = "Styling")
	FLinearColor UnaffordableColor = FLinearColor(1.0f, 0.0f, 0.0f, 0.75f);

	//~ Event Handlers

	UFUNCTION()
	void OnItemButtonClicked();

	//~ Blueprint Events

	/**
	 * Called when the item is loaded
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnItemLoaded(UCatalogItem* LoadedItem);

	/**
	 * Called when selection state changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnSelectionStateChanged(bool bSelected);

	/**
	 * Called when hover state changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnHoverStateChanged(bool bHovered);

private:
	/**
	 * Update visual appearance based on current state
	 */
	void UpdateVisuals();

	/**
	 * Load the item asynchronously if needed
	 */
	void LoadItemAsync();
};

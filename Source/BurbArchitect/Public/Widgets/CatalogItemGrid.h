// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Components/TileView.h"
#include "Widgets/CatalogWidgetBase.h"
#include "CatalogItemGrid.generated.h"

class UCatalogItemCard;
class UCatalogItem;

/**
 * Widget that displays multiple catalog items in a scrollable grid layout
 * Supports lazy loading and dynamic item population
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API UCatalogItemGrid : public UCatalogWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 * Set items to display by paths (lazy loading)
	 * @param InItemPaths - Array of soft object paths to catalog items
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetItemPaths(const TArray<FSoftObjectPath>& InItemPaths);

	/**
	 * Refresh the grid with current item paths
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void RefreshGrid();

	/**
	 * Clear all items from the grid
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void ClearGrid();

	/**
	 * Select a specific item in the grid
	 * @param Item - The item to select
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SelectItem(UCatalogItem* Item);

	/**
	 * Get the currently selected item
	 * @return The selected item, or nullptr if none selected
	 */
	UFUNCTION(BlueprintPure, Category = "Catalog")
	UCatalogItem* GetSelectedItem() const { return SelectedItem; }

protected:
	virtual void NativeConstruct() override;

	/**
	 * Array of item paths to display
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	TArray<FSoftObjectPath> ItemPaths;

	/**
	 * Currently selected item
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	TObjectPtr<UCatalogItem> SelectedItem;

	/**
	 * Entry objects for TileView (prevents garbage collection)
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> ListEntries;

	//~ Widget Bindings

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTileView> TileView;

	//~ Configuration

	/**
	 * Widget class to use for item cards
	 */
	UPROPERTY(EditAnywhere, Category = "Widget Classes")
	TSubclassOf<UCatalogItemCard> ItemCardClass;

	//~ Event Handlers

	UFUNCTION()
	void OnItemCardSelected(UCatalogItem* Item);

	/**
	 * Handle TileView selection change (receives UObject* from TileView)
	 */
	void OnTileViewSelectionChanged(UObject* SelectedEntry);

	//~ Blueprint Events

	/**
	 * Called when the grid is refreshed
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnGridRefreshed(int32 ItemCount);

	/**
	 * Called when item selection changes
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catalog")
	void OnItemSelectionChanged(UCatalogItem* Item);

private:
	/**
	 * Rebuild the entire grid from scratch
	 */
	void RebuildGrid();
};

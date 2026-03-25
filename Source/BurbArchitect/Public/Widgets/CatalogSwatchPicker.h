// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/CatalogWidgetBase.h"
#include "Components/TileView.h"
#include "Components/Button.h"
#include "CatalogSwatchPicker.generated.h"

class UArchitectureItem;
class UWallPattern;
class UFloorPattern;
class ABuildTool;
class UCatalogItemCard;

/**
 * Widget that displays color swatch options for patterns
 * Spawns adjacent to catalog item cards when pattern supports swatches
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API UCatalogSwatchPicker : public UCatalogWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 * Show the swatch picker at the specified screen position
	 * @param ScreenPosition - Position to spawn the picker (adjacent to catalog card)
	 * @param Pattern - The pattern whose swatches to display
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog Swatch Picker")
	void ShowAtPosition(const FVector2D& ScreenPosition, UArchitectureItem* Pattern);

	/**
	 * Close and remove the swatch picker from screen
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog Swatch Picker")
	void ClosePicker();

	/**
	 * Get the currently active build tool
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog Swatch Picker")
	ABuildTool* GetActiveBuildTool() const;

protected:
	virtual void NativeConstruct() override;

	/**
	 * The pattern whose swatches are being displayed
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Swatch Picker")
	TObjectPtr<UArchitectureItem> CurrentPattern;

	/**
	 * Tile view to hold swatch cards (created in Blueprint)
	 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTileView> SwatchGrid;

	/**
	 * Blueprint event called when a swatch is selected
	 * @param SwatchIndex - Index of the selected swatch in the ColourSwatches array
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Swatch Picker")
	void OnSwatchSelected(int32 SwatchIndex);

	/**
	 * Populate the grid with swatch entries
	 */
	UFUNCTION(BlueprintCallable, Category = "Swatch Picker")
	void PopulateSwatches();

	/**
	 * Called when a swatch selection changes in the tile view
	 * Note: We use OnItemSelectionChanged instead of OnItemClicked because
	 * the CatalogItemCard's Button consumes the click event.
	 */
	void OnSwatchSelectionChanged(UObject* Item);

private:
	/**
	 * Map to track which button corresponds to which swatch index (deprecated but kept for now)
	 */
	TMap<UButton*, int32> SwatchButtonIndexMap;

	/**
	 * Map to track which catalog card corresponds to which swatch index (deprecated but kept for now)
	 */
	TMap<UCatalogItemCard*, int32> SwatchCardIndexMap;
};

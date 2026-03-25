// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CatalogItemCard.h"
#include "Widgets/CatalogItemEntry.h"
#include "Widgets/CatalogSwatchPicker.h"
#include "Widgets/CatalogBrowserWidget.h"
#include "Widgets/CatalogItemGrid.h"
#include "Widgets/SwatchEntry.h"
#include "Data/CatalogItem.h"
#include "Data/ArchitectureItem.h"
#include "Data/WallPattern.h"
#include "Data/FloorPattern.h"
#include "Subsystems/CatalogSubsystem.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/TileView.h"
#include "Components/ListViewBase.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "UObject/UObjectIterator.h"


void UCatalogItemCard::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind button click event
	if (ItemButton)
	{
		ItemButton->OnClicked.AddDynamic(this, &UCatalogItemCard::OnItemButtonClicked);
	}

	// Don't call UpdateVisuals here - let NativeOnListItemObjectSet handle it
	// UpdateVisuals will be called after the item data is set
}

void UCatalogItemCard::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	SetHovered(true);
}

void UCatalogItemCard::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	SetHovered(false);
}

void UCatalogItemCard::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	IUserObjectListEntry::NativeOnListItemObjectSet(ListItemObject);

	// Cache the list item object for later use
	CachedListItemObject = ListItemObject;

	// Check if this is a swatch entry
	if (USwatchEntry* SwatchEntry = Cast<USwatchEntry>(ListItemObject))
	{
		// Mark this as a swatch entry so Blueprint can check it
		bIsSwatchEntry = true;

		// Set the pattern as the item
		SetItem(SwatchEntry->Pattern);

		// Get the bool values and texture from the pattern
		bool bUseColourSwatches = false;
		bool bUseColourMask = false;
		UTexture* BaseTexture = nullptr;

		if (UWallPattern* WallPattern = Cast<UWallPattern>(SwatchEntry->Pattern))
		{
			bUseColourSwatches = WallPattern->bUseColourSwatches;
			bUseColourMask = WallPattern->bUseColourMask;
			BaseTexture = WallPattern->BaseTexture;
		}
		else if (UFloorPattern* FloorPattern = Cast<UFloorPattern>(SwatchEntry->Pattern))
		{
			bUseColourSwatches = FloorPattern->bUseColourSwatches;
			bUseColourMask = FloorPattern->bUseColourMask;
			BaseTexture = FloorPattern->BaseTexture;
		}

		// Fire Blueprint event so the user can apply color material
		OnSwatchEntrySet(SwatchEntry->Color, SwatchEntry->SwatchIndex, bUseColourSwatches, bUseColourMask, BaseTexture);
		return;
	}

	// Mark this as a normal catalog entry (not a swatch)
	bIsSwatchEntry = false;

	// Cast to our entry type
	UCatalogItemEntry* Entry = Cast<UCatalogItemEntry>(ListItemObject);
	if (Entry)
	{
		// Set the item path from the entry
		SetItemPath(Entry->GetItemPath());
	}
	else if (ListItemObject == nullptr)
	{
		// TileView is clearing/recycling this widget
		ItemPath = FSoftObjectPath();
		Item = nullptr;
		CachedListItemObject = nullptr;
		bIsSwatchEntry = false;
		UpdateVisuals();
	}
}

void UCatalogItemCard::SetItemPath(const FSoftObjectPath& InItemPath)
{
	ItemPath = InItemPath;
	Item = nullptr;

	// Load the item asynchronously
	LoadItemAsync();
}

void UCatalogItemCard::SetItem(UCatalogItem* InItem)
{
	Item = InItem;

	if (Item)
	{
		ItemPath = FSoftObjectPath(Item);
		UpdateVisuals();
		OnItemLoaded(Item);
	}
}

UCatalogItem* UCatalogItemCard::GetItem()
{
	// If item is not loaded yet, try to load it synchronously
	if (!Item && ItemPath.IsValid() && CatalogSubsystem)
	{
		Item = CatalogSubsystem->LoadItem(ItemPath);
		if (Item)
		{
			UpdateVisuals();
			OnItemLoaded(Item);
		}
	}

	return Item;
}

void UCatalogItemCard::SetSelected(bool bInSelected)
{
	if (bIsSelected != bInSelected)
	{
		bIsSelected = bInSelected;
		UpdateVisuals();
		OnSelectionStateChanged(bIsSelected);
	}
}

void UCatalogItemCard::SetHovered(bool bInHovered)
{
	if (bIsHovered != bInHovered)
	{
		bIsHovered = bInHovered;
		UpdateVisuals();
		OnHoverStateChanged(bIsHovered);
	}
}

void UCatalogItemCard::OnItemButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("CatalogItemCard::OnItemButtonClicked - Button clicked!"));

	// Find the owning TileView and tell it to select this item (works for both swatches and regular items)
	if (CachedListItemObject)
	{
		UTileView* OwningTileView = Cast<UTileView>(GetOwningListView());
		if (OwningTileView)
		{
			UE_LOG(LogTemp, Warning, TEXT("CatalogItemCard::OnItemButtonClicked - Found owning TileView, setting selection"));
			OwningTileView->SetSelectedItem(CachedListItemObject);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("CatalogItemCard::OnItemButtonClicked - Could not find owning TileView!"));
		}
	}

	// If this is a swatch entry, we're done (selection handled above)
	if (Cast<USwatchEntry>(CachedListItemObject))
	{
		return;
	}

	// Ensure item is loaded
	GetItem();

	if (Item && CachedListItemObject)
	{
		UE_LOG(LogTemp, Warning, TEXT("CatalogItemCard::OnItemButtonClicked - Item '%s', ListItemObject valid"), *Item->DisplayName.ToString());

		// Fire the selection event (might have listeners in Blueprint)
		OnItemSelected.Broadcast(Item);

		// Check if this item supports color swatches
		UArchitectureItem* ArchItem = Cast<UArchitectureItem>(Item);
		if (ArchItem)
		{
			bool bSupportsSwatches = false;

			if (UWallPattern* WallPattern = Cast<UWallPattern>(ArchItem))
			{
				bSupportsSwatches = WallPattern->bUseColourSwatches && WallPattern->ColourSwatches.Num() > 0;
			}
			else if (UFloorPattern* FloorPattern = Cast<UFloorPattern>(ArchItem))
			{
				bSupportsSwatches = FloorPattern->bUseColourSwatches && FloorPattern->ColourSwatches.Num() > 0;
			}

			// Find the swatch picker for this player's UI (not a static reference)
			UCatalogSwatchPicker* SwatchPicker = FindOwningSwatchPicker();

			if (bSupportsSwatches)
			{
				UE_LOG(LogTemp, Warning, TEXT("Item supports swatches"));

				if (!SwatchPicker)
				{
					UE_LOG(LogTemp, Error, TEXT("SwatchPicker is NULL! Could not find owning CatalogBrowserWidget."));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Found SwatchPicker, showing it"));

					// Show the swatch picker (it's already positioned in Blueprint)
					SwatchPicker->ShowAtPosition(FVector2D::ZeroVector, ArchItem);
				}
			}
			else
			{
				// Hide the swatch picker when clicking items without swatches
				if (SwatchPicker)
				{
					SwatchPicker->ClosePicker();
				}
			}
		}
		else
		{
			// Not an ArchitectureItem - hide picker
			UCatalogSwatchPicker* SwatchPicker = FindOwningSwatchPicker();
			if (SwatchPicker)
			{
				SwatchPicker->ClosePicker();
			}
		}
	}
	else
	{
		if (!Item)
		{
			UE_LOG(LogTemp, Error, TEXT("CatalogItemCard::OnItemButtonClicked - No item loaded!"));
		}
		if (!CachedListItemObject)
		{
			UE_LOG(LogTemp, Error, TEXT("CatalogItemCard::OnItemButtonClicked - No cached list item object!"));
		}
	}
}

void UCatalogItemCard::UpdateVisuals()
{
	if (!Item)
	{
		// If no item but we have a path, try to reload it
		if (ItemPath.IsValid() && CatalogSubsystem)
		{
			Item = CatalogSubsystem->LoadItem(ItemPath);
		}

		if (!Item)
		{
			// Clear the UI
			if (ItemName)
			{
				ItemName->SetText(FText::FromString(TEXT("")));
			}
			if (ItemIcon)
			{
				ItemIcon->SetVisibility(ESlateVisibility::Hidden);
			}
			return;
		}
	}

	// Update item name
	if (ItemName)
	{
		ItemName->SetText(Item->DisplayName);
	}

	// Update item icon (skip for swatch entries - icon is set by OnSwatchEntrySet Blueprint event)
	if (ItemIcon && !bIsSwatchEntry)
	{
		ItemIcon->SetVisibility(ESlateVisibility::Visible);
		if (Item->Icon.GetResourceObject())
		{
			ItemIcon->SetBrush(Item->Icon);
		}
	}

	// Update selection border
	if (SelectionBorder)
	{
		SelectionBorder->SetBrushColor(bIsSelected ? SelectedBorderColor : UnselectedBorderColor);
		// For now, just show green if cost is low, red if high
		// In a real implementation, you'd check against player's current funds
		bool bCanAfford = Item->Cost <= 1000.0f; // Placeholder threshold
		SelectionBorder->SetContentColorAndOpacity(bCanAfford ? AffordableColor : UnaffordableColor);
	}
}

void UCatalogItemCard::LoadItemAsync()
{
	UE_LOG(LogTemp, Warning, TEXT("      LoadItemAsync: ItemPath valid=%s, CatalogSubsystem=%s"),
		ItemPath.IsValid() ? TEXT("YES") : TEXT("NO"),
		CatalogSubsystem ? TEXT("YES") : TEXT("NO"));

	if (!ItemPath.IsValid() || !CatalogSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("      LoadItemAsync FAILED: ItemPath or CatalogSubsystem invalid"));
		return;
	}

	// Try to load from cache first
	Item = CatalogSubsystem->LoadItem(ItemPath);

	UE_LOG(LogTemp, Warning, TEXT("      LoadItemAsync: Item loaded=%s"), Item ? TEXT("YES") : TEXT("NO"));

	if (Item)
	{
		UE_LOG(LogTemp, Warning, TEXT("      LoadItemAsync: Calling UpdateVisuals..."));
		UpdateVisuals();
		UE_LOG(LogTemp, Warning, TEXT("      LoadItemAsync: Calling OnItemLoaded BP event..."));
		OnItemLoaded(Item);
		UE_LOG(LogTemp, Warning, TEXT("      LoadItemAsync: OnItemLoaded complete. Item still valid: %s"), Item ? TEXT("YES") : TEXT("NO"));
	}
}

UCatalogSwatchPicker* UCatalogItemCard::FindOwningSwatchPicker() const
{
	// For TileView entry widgets, the normal GetParent() hierarchy doesn't work
	// because entry widgets are managed by Slate internally and nested UserWidgets
	// don't share a parent hierarchy.

	// Approach 1: Use GetTypedOuter to traverse the UObject Outer chain
	// This works because TileView entry widgets are created with an Outer that
	// eventually leads to the owning UserWidget hierarchy.
	if (UCatalogBrowserWidget* Browser = GetTypedOuter<UCatalogBrowserWidget>())
	{
		UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found via GetTypedOuter"));
		return Browser->SwatchPicker;
	}

	// Approach 2: Get the owning list view and traverse its Outer chain
	if (UListViewBase* OwningListView = const_cast<UCatalogItemCard*>(this)->GetOwningListView())
	{
		UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found owning ListView: %s"), *OwningListView->GetName());

		// Try GetTypedOuter from the ListView
		if (UCatalogBrowserWidget* Browser = OwningListView->GetTypedOuter<UCatalogBrowserWidget>())
		{
			UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found via ListView's GetTypedOuter"));
			return Browser->SwatchPicker;
		}

		// Traverse the widget parent hierarchy from the ListView
		UWidget* CurrentWidget = OwningListView->GetParent();
		while (CurrentWidget)
		{
			// Check if this widget IS the browser
			if (UCatalogBrowserWidget* Browser = Cast<UCatalogBrowserWidget>(CurrentWidget))
			{
				UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found CatalogBrowserWidget in parent chain"));
				return Browser->SwatchPicker;
			}

			// Also check the Outer chain from each widget (handles nested UserWidgets)
			if (UCatalogBrowserWidget* Browser = CurrentWidget->GetTypedOuter<UCatalogBrowserWidget>())
			{
				UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found via parent widget's Outer chain"));
				return Browser->SwatchPicker;
			}

			CurrentWidget = CurrentWidget->GetParent();
		}

		// Approach 2b: The ListView might be inside a nested UserWidget (CatalogItemGrid)
		// Try to get the UserWidget that owns this ListView via its WidgetTree
		if (UCatalogItemGrid* ItemGrid = OwningListView->GetTypedOuter<UCatalogItemGrid>())
		{
			UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found ItemGrid, checking its outer"));
			if (UCatalogBrowserWidget* Browser = ItemGrid->GetTypedOuter<UCatalogBrowserWidget>())
			{
				UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found via ItemGrid's Outer chain"));
				return Browser->SwatchPicker;
			}
		}
	}

	// Approach 3: Iterate all UObjects looking for CatalogBrowserWidget
	// This is a last resort and is slower but reliable
	if (APlayerController* PC = GetOwningPlayer())
	{
		for (TObjectIterator<UCatalogBrowserWidget> It; It; ++It)
		{
			UCatalogBrowserWidget* Browser = *It;
			if (Browser && Browser->GetOwningPlayer() == PC)
			{
				UE_LOG(LogTemp, Log, TEXT("FindOwningSwatchPicker: Found via TObjectIterator"));
				return Browser->SwatchPicker;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("FindOwningSwatchPicker: Could not find CatalogBrowserWidget"));
	return nullptr;
}

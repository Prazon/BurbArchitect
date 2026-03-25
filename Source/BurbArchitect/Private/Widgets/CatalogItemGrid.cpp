// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CatalogItemGrid.h"
#include "Widgets/CatalogItemCard.h"
#include "Widgets/CatalogItemEntry.h"
#include "Data/CatalogItem.h"
#include "Subsystems/CatalogSubsystem.h"


void UCatalogItemGrid::NativeConstruct()
{
	Super::NativeConstruct();

	// Note: Entry widget class should be set in Blueprint designer or via SetListItems
	// TileView doesn't have a runtime SetEntryClass method

	// Bind TileView selection event
	if (TileView)
	{
		TileView->OnItemSelectionChanged().AddUObject(this, &UCatalogItemGrid::OnTileViewSelectionChanged);
	}
}

void UCatalogItemGrid::SetItemPaths(const TArray<FSoftObjectPath>& InItemPaths)
{
	// BURB-48: Filter out items with bDisabledInCatalog before displaying
	ItemPaths.Empty();
	ItemPaths.Reserve(InItemPaths.Num());

	if (CatalogSubsystem)
	{
		for (const FSoftObjectPath& Path : InItemPaths)
		{
			if (UCatalogItem* Item = CatalogSubsystem->LoadItem(Path))
			{
				if (!Item->bDisabledInCatalog)
				{
					ItemPaths.Add(Path);
				}
			}
		}
	}
	else
	{
		// Fallback if no subsystem (shouldn't happen at runtime)
		ItemPaths = InItemPaths;
	}

	RebuildGrid();
}

void UCatalogItemGrid::RefreshGrid()
{
	RebuildGrid();
}

void UCatalogItemGrid::ClearGrid()
{
	ItemPaths.Empty();
	SelectedItem = nullptr;

	if (TileView)
	{
		TileView->ClearListItems();
	}
}

void UCatalogItemGrid::SelectItem(UCatalogItem* Item)
{
	if (SelectedItem == Item)
	{
		return;
	}

	SelectedItem = Item;

	// TileView manages widget selection automatically via IUserObjectListEntry
	// Just broadcast the selection change
	OnItemSelected.Broadcast(Item);
	OnItemSelectionChanged(Item);
}

void UCatalogItemGrid::OnItemCardSelected(UCatalogItem* Item)
{
	UE_LOG(LogTemp, Warning, TEXT("CatalogItemGrid::OnItemCardSelected - Item: %s"), Item ? *Item->DisplayName.ToString() : TEXT("NULL"));
	SelectItem(Item);
}

void UCatalogItemGrid::OnTileViewSelectionChanged(UObject* SelectedEntry)
{
	// Cast the entry to our catalog item entry type
	UCatalogItemEntry* Entry = Cast<UCatalogItemEntry>(SelectedEntry);
	if (!Entry)
	{
		return;
	}

	// Get the catalog item from the entry
	// For now, we'll need to load it to get the UCatalogItem*
	// In the future, you might want to cache the UCatalogItem* in the entry
	if (CatalogSubsystem)
	{
		UCatalogItem* Item = CatalogSubsystem->LoadItem(Entry->GetItemPath());
		if (Item)
		{
			SelectItem(Item);
		}
	}
}

void UCatalogItemGrid::RebuildGrid()
{
	if (!TileView)
	{
		UE_LOG(LogTemp, Error, TEXT("RebuildGrid: TileView is NULL!"));
		return;
	}

	// Clear existing list
	TileView->ClearListItems();
	ListEntries.Empty();

	// Create entry objects for each item path
	for (const FSoftObjectPath& ItemPath : ItemPaths)
	{
		UCatalogItemEntry* Entry = NewObject<UCatalogItemEntry>(this);
		Entry->Initialize(ItemPath);
		ListEntries.Add(Entry);
		UE_LOG(LogTemp, Warning, TEXT("  Created entry %d for: %s"), ListEntries.Num(), *ItemPath.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("Created %d entries total"), ListEntries.Num());

	// Set the list items (TileView will create/recycle widgets as needed)
	TileView->SetListItems(ListEntries);

	UE_LOG(LogTemp, Warning, TEXT("SetListItems complete. Calling RequestRefresh..."));
	TileView->RegenerateAllEntries();

	// Notify Blueprint
	OnGridRefreshed(ItemPaths.Num());
	UE_LOG(LogTemp, Warning, TEXT("=== RebuildGrid complete ==="));
}


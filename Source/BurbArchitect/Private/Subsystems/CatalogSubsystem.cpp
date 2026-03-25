// Fill out your copyright notice in the Description page of Project Settings.

#include "Subsystems/CatalogSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"

void UCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Build catalog from Asset Registry
	BuildCatalog();
}

void UCatalogSubsystem::Deinitialize()
{
	// Clear all data structures
	ItemPathsByCategory.Reset();
	ItemPathsBySubcategory.Reset();
	SubcategoriesByCategory.Reset();
	RootCategories.Reset();
	LoadedItemsCache.Reset();

	Super::Deinitialize();
}

void UCatalogSubsystem::BuildCatalog()
{
	// Clear existing data
	ItemPathsByCategory.Reset();
	ItemPathsBySubcategory.Reset();
	SubcategoriesByCategory.Reset();
	RootCategories.Reset();
	LoadedItemsCache.Reset();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Load all catalog categories (both UCatalogCategory and UCatalogSubcategory)
	TArray<FAssetData> CategoryAssets;
	AssetRegistry.GetAssetsByClass(UCatalogCategory::StaticClass()->GetClassPathName(), CategoryAssets, true);

	for (const FAssetData& Asset : CategoryAssets)
	{
		// Type-safe casting to distinguish categories from subcategories
		if (UCatalogSubcategory* Subcategory = Cast<UCatalogSubcategory>(Asset.GetAsset()))
		{
			// It's a subcategory - add to parent-child mapping
			if (Subcategory->ParentCategory)
			{
				SubcategoriesByCategory.Add(Subcategory->ParentCategory, Subcategory);
			}
		}
		else if (UCatalogCategory* Category = Cast<UCatalogCategory>(Asset.GetAsset()))
		{
			// It's a root category
			RootCategories.Add(Category);
		}
	}

	// Sort root categories by SortOrder
	RootCategories.Sort([](const UCatalogCategory& A, const UCatalogCategory& B) {
		return A.SortOrder < B.SortOrder;
	});

	// Sort subcategories for each category
	for (UCatalogCategory* Category : RootCategories)
	{
		TArray<UCatalogSubcategory*> Subcategories;
		SubcategoriesByCategory.MultiFind(Category, Subcategories);

		Subcategories.Sort([](const UCatalogSubcategory& A, const UCatalogSubcategory& B) {
			return A.SortOrder < B.SortOrder;
		});

		// Update the multimap with sorted subcategories
		SubcategoriesByCategory.Remove(Category);
		for (UCatalogSubcategory* Subcategory : Subcategories)
		{
			SubcategoriesByCategory.Add(Category, Subcategory);
		}
	}

	// Scan all catalog items (directory agnostic for modding support)
	// LAZY LOADING: Store paths only, don't load the actual assets yet
	TArray<FAssetData> ItemAssets;
	AssetRegistry.GetAssetsByClass(UCatalogItem::StaticClass()->GetClassPathName(), ItemAssets, true);

	for (const FAssetData& Asset : ItemAssets)
	{
		// Load only the item to read its Category and Subcategory properties
		// This is a minimal load to build the index
		UCatalogItem* Item = Cast<UCatalogItem>(Asset.GetAsset());
		if (Item && Item->Category)
		{
			FSoftObjectPath ItemPath(Asset.ToSoftObjectPath());

			// Add path to category mapping
			ItemPathsByCategory.Add(Item->Category, ItemPath);

			// Add path to subcategory mapping if applicable
			if (Item->Subcategory)
			{
				ItemPathsBySubcategory.Add(Item->Subcategory, ItemPath);
			}
		}
	}
}

TArray<UCatalogSubcategory*> UCatalogSubsystem::GetSubcategories(UCatalogCategory* Category) const
{
	TArray<UCatalogSubcategory*> Subcategories;

	if (Category)
	{
		SubcategoriesByCategory.MultiFind(Category, Subcategories);
	}

	return Subcategories;
}

UCatalogItem* UCatalogSubsystem::LoadItem(const FSoftObjectPath& ItemPath)
{
	if (!ItemPath.IsValid())
	{
		return nullptr;
	}

	// Check cache first
	if (TWeakObjectPtr<UCatalogItem>* CachedItem = LoadedItemsCache.Find(ItemPath))
	{
		if (CachedItem->IsValid())
		{
			return CachedItem->Get();
		}
	}

	// Load the item
	UCatalogItem* LoadedItem = Cast<UCatalogItem>(ItemPath.TryLoad());
	if (LoadedItem)
	{
		// Cache for future use
		LoadedItemsCache.Add(ItemPath, LoadedItem);
	}

	return LoadedItem;
}

TArray<UCatalogItem*> UCatalogSubsystem::LoadItems(const TArray<FSoftObjectPath>& ItemPaths)
{
	TArray<UCatalogItem*> LoadedItems;
	LoadedItems.Reserve(ItemPaths.Num());

	for (const FSoftObjectPath& Path : ItemPaths)
	{
		if (UCatalogItem* Item = LoadItem(Path))
		{
			// BURB-48: Filter out items disabled in catalog (debug/WIP items)
			if (Item->bDisabledInCatalog)
			{
				continue;
			}

			LoadedItems.Add(Item);
		}
	}

	return LoadedItems;
}

TArray<FSoftObjectPath> UCatalogSubsystem::GetItemPathsInCategory(UCatalogCategory* Category) const
{
	TArray<FSoftObjectPath> ItemPaths;

	if (Category)
	{
		ItemPathsByCategory.MultiFind(Category, ItemPaths);
	}

	return ItemPaths;
}

TArray<FSoftObjectPath> UCatalogSubsystem::GetItemPathsInSubcategory(UCatalogSubcategory* Subcategory) const
{
	TArray<FSoftObjectPath> ItemPaths;

	if (Subcategory)
	{
		ItemPathsBySubcategory.MultiFind(Subcategory, ItemPaths);
	}

	return ItemPaths;
}

TArray<UCatalogItem*> UCatalogSubsystem::GetItemsInCategory(UCatalogCategory* Category)
{
	TArray<FSoftObjectPath> ItemPaths = GetItemPathsInCategory(Category);
	return LoadItems(ItemPaths);
}

TArray<UCatalogItem*> UCatalogSubsystem::GetItemsInSubcategory(UCatalogSubcategory* Subcategory)
{
	TArray<FSoftObjectPath> ItemPaths = GetItemPathsInSubcategory(Subcategory);
	return LoadItems(ItemPaths);
}

TArray<FSoftObjectPath> UCatalogSubsystem::SearchItemPathsByName(const FString& SearchText) const
{
	TArray<FSoftObjectPath> MatchingPaths;

	if (SearchText.IsEmpty())
	{
		return MatchingPaths;
	}

	// Convert search text to lowercase for case-insensitive search
	FString SearchLower = SearchText.ToLower();

	// Iterate all item paths across all categories
	for (const auto& Pair : ItemPathsByCategory)
	{
		const FSoftObjectPath& ItemPath = Pair.Value;

		// Extract asset name from path (e.g., "/Game/Data/Catalog/CAT_WallPattern_Pine" -> "CAT_WallPattern_Pine")
		FString AssetName = ItemPath.GetAssetName();
		FString AssetNameLower = AssetName.ToLower();

		// Partial match on asset name
		if (AssetNameLower.Contains(SearchLower))
		{
			MatchingPaths.AddUnique(ItemPath);
		}
	}

	return MatchingPaths;
}

TArray<UCatalogItem*> UCatalogSubsystem::SearchItemsByName(const FString& SearchText)
{
	if (SearchText.IsEmpty())
	{
		return TArray<UCatalogItem*>();
	}

	// Convert search text to lowercase for case-insensitive search
	FString SearchLower = SearchText.ToLower();

	TArray<UCatalogItem*> MatchingItems;

	// Get all item paths first (fast path-only search)
	TArray<FSoftObjectPath> CandidatePaths = SearchItemPathsByName(SearchText);

	// Load candidate items and check their DisplayName property for more accurate matching
	for (const FSoftObjectPath& ItemPath : CandidatePaths)
	{
		UCatalogItem* Item = LoadItem(ItemPath);
		if (Item)
		{
			// BURB-48: Skip items disabled in catalog
			if (Item->bDisabledInCatalog)
			{
				continue;
			}

			// Check DisplayName for more accurate matching (supports spaces, special chars, etc.)
			FString DisplayNameLower = Item->DisplayName.ToString().ToLower();

			if (DisplayNameLower.Contains(SearchLower))
			{
				MatchingItems.Add(Item);
			}
		}
	}

	return MatchingItems;
}

void UCatalogSubsystem::RefreshCatalog()
{
	BuildCatalog();
}

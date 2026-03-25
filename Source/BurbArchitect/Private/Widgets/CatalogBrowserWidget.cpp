// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CatalogBrowserWidget.h"
#include "Widgets/CategoryNavigationWidget.h"
#include "Widgets/SubcategoryNavigationWidget.h"
#include "Widgets/CatalogItemGrid.h"
#include "Widgets/CatalogSwatchPicker.h"
#include "Data/CatalogItem.h"
#include "Data/CatalogCategory.h"
#include "Data/CatalogSubcategory.h"
#include "Subsystems/CatalogSubsystem.h"

void UCatalogBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (bAutoInitializeOnConstruct)
	{
		InitializeBrowser();
	}
}

void UCatalogBrowserWidget::InitializeBrowser()
{
	if (!CatalogSubsystem)
	{
		return;
	}

	// Bind event handlers
	if (CategoryNavigation)
	{
		CategoryNavigation->OnCategoryChanged.AddDynamic(this, &UCatalogBrowserWidget::OnCategorySelected);
	}

	if (SubcategoryNavigation)
	{
		SubcategoryNavigation->OnSubcategorySelected.AddDynamic(this, &UCatalogBrowserWidget::OnSubcategorySelected);
	}

	if (ItemGrid)
	{
		ItemGrid->OnItemSelected.AddDynamic(this, &UCatalogBrowserWidget::OnItemSelected);
	}

	// Notify Blueprint
	OnBrowserInitialized();
}

void UCatalogBrowserWidget::RefreshCatalog()
{
	if (!CatalogSubsystem)
	{
		return;
	}

	// Refresh the catalog subsystem
	CatalogSubsystem->RefreshCatalog();

	// Refresh category navigation
	if (CategoryNavigation)
	{
		CategoryNavigation->RefreshCategories();
	}

	// Update the active category
	if (CategoryNavigation)
	{
		SetActiveCategory(CategoryNavigation->GetSelectedCategory());
	}
}

void UCatalogBrowserWidget::SetActiveCategory(UCatalogCategory* Category)
{
	if (ActiveCategory == Category)
	{
		return;
	}

	ActiveCategory = Category;
	ActiveSubcategory = nullptr;

	// Update subcategory navigation
	if (SubcategoryNavigation)
	{
		SubcategoryNavigation->SetParentCategory(Category);
	}

	// Update item grid
	UpdateItemGrid();

	// Notify Blueprint
	OnActiveCategoryChanged(Category);
}

void UCatalogBrowserWidget::SetActiveSubcategory(UCatalogSubcategory* Subcategory)
{
	if (ActiveSubcategory == Subcategory)
	{
		return;
	}

	ActiveSubcategory = Subcategory;

	// Update item grid
	UpdateItemGrid();

	// Notify Blueprint
	OnActiveSubcategoryChanged(Subcategory);
}

UCatalogItem* UCatalogBrowserWidget::GetSelectedItem() const
{
	return ItemGrid ? ItemGrid->GetSelectedItem() : nullptr;
}

void UCatalogBrowserWidget::OnCategorySelected(UCatalogCategory* Category)
{
	SetActiveCategory(Category);
}

void UCatalogBrowserWidget::OnSubcategorySelected(UCatalogSubcategory* Subcategory)
{
	SetActiveSubcategory(Subcategory);
}

void UCatalogBrowserWidget::OnItemSelected(UCatalogItem* Item)
{
	UE_LOG(LogTemp, Warning, TEXT("CatalogBrowserWidget::OnItemSelected - Item: %s"), Item ? *Item->DisplayName.ToString() : TEXT("NULL"));

	// Fire activation event
	UE_LOG(LogTemp, Warning, TEXT("CatalogBrowserWidget::OnItemSelected - Broadcasting OnItemActivated"));
	OnItemActivated.Broadcast(Item);

	// Notify Blueprint
	OnActiveItemChanged(Item);
}

void UCatalogBrowserWidget::UpdateItemGrid()
{
	if (!CatalogSubsystem || !ItemGrid || !ActiveCategory)
	{
		return;
	}

	TArray<FSoftObjectPath> ItemPaths;

	if (ActiveSubcategory)
	{
		// Show items in the specific subcategory
		ItemPaths = CatalogSubsystem->GetItemPathsInSubcategory(ActiveSubcategory);
	}
	else
	{
		// Show all items in the category
		ItemPaths = CatalogSubsystem->GetItemPathsInCategory(ActiveCategory);
	}

	// Update the item grid
	ItemGrid->SetItemPaths(ItemPaths);
}

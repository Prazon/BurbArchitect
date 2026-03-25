// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CategoryNavigationWidget.h"
#include "Widgets/CategoryTabWidget.h"
#include "Data/CatalogCategory.h"
#include "Subsystems/CatalogSubsystem.h"
#include "Components/PanelWidget.h"

void UCategoryNavigationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (bAutoRefreshOnConstruct)
	{
		RefreshCategories();
	}
}

void UCategoryNavigationWidget::RefreshCategories()
{
	if (!CatalogSubsystem || !CategoryTabContainer || !CategoryTabClass)
	{
		return;
	}

	// Clear existing tabs
	CategoryTabContainer->ClearChildren();
	CategoryTabs.Empty();

	// Get root categories from the catalog subsystem
	TArray<UCatalogCategory*> RootCategories = CatalogSubsystem->GetRootCategories();

	// Create a tab for each category (skip empty categories — BURB-48)
	for (UCatalogCategory* Category : RootCategories)
	{
		if (!Category)
		{
			continue;
		}

		// Hide categories with no visible items (all items disabled or none exist)
		if (CatalogSubsystem->GetItemsInCategory(Category).Num() == 0)
		{
			continue;
		}

		// Create the tab widget
		UCategoryTabWidget* TabWidget = CreateWidget<UCategoryTabWidget>(this, CategoryTabClass);
		if (!TabWidget)
		{
			continue;
		}

		// Configure the tab
		TabWidget->SetCategory(Category);

		// Bind selection event
		TabWidget->OnTabSelected.AddDynamic(this, &UCategoryNavigationWidget::OnCategoryTabSelected);

		// Add to container
		CategoryTabContainer->AddChild(TabWidget);

		// Store reference
		CategoryTabs.Add(TabWidget);
	}

	// Select the first category by default if none selected
	if (!SelectedCategory && CategoryTabs.Num() > 0)
	{
		SelectCategory(CategoryTabs[0]->GetCategory());
	}

	// Notify Blueprint
	OnCategoriesRefreshed();
}

void UCategoryNavigationWidget::SelectCategory(UCatalogCategory* Category)
{
	if (SelectedCategory == Category)
	{
		return;
	}

	SelectedCategory = Category;

	// Update visual selection on all tabs
	UpdateTabSelection();

	// Fire events
	OnCategoryChanged.Broadcast(Category);
	OnCategorySelected(Category);
}

void UCategoryNavigationWidget::OnCategoryTabSelected(UCatalogCategory* Category)
{
	SelectCategory(Category);
}

void UCategoryNavigationWidget::UpdateTabSelection()
{
	for (UCategoryTabWidget* Tab : CategoryTabs)
	{
		if (Tab)
		{
			Tab->SetSelected(Tab->GetCategory() == SelectedCategory);
		}
	}
}

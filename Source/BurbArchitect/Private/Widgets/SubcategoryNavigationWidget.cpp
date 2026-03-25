// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SubcategoryNavigationWidget.h"
#include "Widgets/CategoryTabWidget.h"
#include "Data/CatalogCategory.h"
#include "Data/CatalogSubcategory.h"
#include "Subsystems/CatalogSubsystem.h"
#include "Components/PanelWidget.h"

void USubcategoryNavigationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Configure "All" button if it exists
	if (AllButton)
	{
		// Create a temporary category to represent "All"
		UCatalogCategory* AllCategory = NewObject<UCatalogCategory>(this);
		AllCategory->DisplayName = AllButtonText;
		AllButton->SetCategory(AllCategory);
		AllButton->OnTabSelected.AddDynamic(this, &USubcategoryNavigationWidget::OnSubcategoryTabSelected);
		AllButton->SetVisibility(bShowAllButton ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void USubcategoryNavigationWidget::SetParentCategory(UCatalogCategory* Category)
{
	if (ParentCategory == Category)
	{
		return;
	}

	ParentCategory = Category;
	SelectedSubcategory = nullptr;

	// Rebuild subcategory tabs
	RebuildSubcategoryTabs();

	// Notify Blueprint
	OnParentCategoryChanged(Category);
}

void USubcategoryNavigationWidget::SelectSubcategory(UCatalogSubcategory* Subcategory)
{
	if (SelectedSubcategory == Subcategory)
	{
		return;
	}

	SelectedSubcategory = Subcategory;

	// Update visual selection
	UpdateTabSelection();

	// Fire events
	OnSubcategorySelected.Broadcast(Subcategory);
	OnSubcategoryFilterChanged(Subcategory);
}

void USubcategoryNavigationWidget::OnSubcategoryTabSelected(UCatalogCategory* Category)
{
	// Cast to subcategory (will be nullptr if it's the "All" button)
	UCatalogSubcategory* Subcategory = Cast<UCatalogSubcategory>(Category);
	SelectSubcategory(Subcategory);
}

void USubcategoryNavigationWidget::OnAllButtonClicked()
{
	SelectSubcategory(nullptr);
}

void USubcategoryNavigationWidget::RebuildSubcategoryTabs()
{
	if (!CatalogSubsystem || !SubcategoryTabContainer || !SubcategoryTabClass)
	{
		return;
	}

	// Clear existing tabs
	SubcategoryTabContainer->ClearChildren();
	SubcategoryTabs.Empty();

	if (!ParentCategory)
	{
		// Hide the widget if no parent category
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Show the widget
	SetVisibility(ESlateVisibility::Visible);

	// Get subcategories for this parent category
	TArray<UCatalogSubcategory*> Subcategories = CatalogSubsystem->GetSubcategories(ParentCategory);

	// If no subcategories, hide the widget
	if (Subcategories.Num() == 0)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Create a tab for each subcategory (skip empty subcategories — BURB-48)
	for (UCatalogSubcategory* Subcategory : Subcategories)
	{
		if (!Subcategory)
		{
			continue;
		}

		// Hide subcategories with no visible items
		if (CatalogSubsystem->GetItemsInSubcategory(Subcategory).Num() == 0)
		{
			continue;
		}

		// Create the tab widget
		UCategoryTabWidget* TabWidget = CreateWidget<UCategoryTabWidget>(this, SubcategoryTabClass);
		if (!TabWidget)
		{
			continue;
		}

		// Configure the tab
		TabWidget->SetCategory(Subcategory);

		// Bind selection event
		TabWidget->OnTabSelected.AddDynamic(this, &USubcategoryNavigationWidget::OnSubcategoryTabSelected);

		// Add to container
		SubcategoryTabContainer->AddChild(TabWidget);

		// Store reference
		SubcategoryTabs.Add(TabWidget);
	}

	// Hide widget if all subcategories were filtered out (BURB-48)
	if (SubcategoryTabs.Num() == 0)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Select "All" by default (no subcategory filter)
	SelectSubcategory(nullptr);
}

void USubcategoryNavigationWidget::UpdateTabSelection()
{
	// Update subcategory tabs
	for (UCategoryTabWidget* Tab : SubcategoryTabs)
	{
		if (Tab)
		{
			Tab->SetSelected(Tab->GetCategory() == SelectedSubcategory);
		}
	}

	// Update "All" button
	if (AllButton)
	{
		AllButton->SetSelected(SelectedSubcategory == nullptr);
	}
}

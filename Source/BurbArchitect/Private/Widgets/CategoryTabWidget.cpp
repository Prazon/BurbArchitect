// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CategoryTabWidget.h"
#include "Data/CatalogCategory.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void UCategoryTabWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind button click event
	if (TabButton)
	{
		TabButton->OnClicked.AddDynamic(this, &UCategoryTabWidget::OnTabButtonClicked);
	}

	// Initialize visuals
	UpdateVisuals();
}

void UCategoryTabWidget::SetCategory(UCatalogCategory* InCategory)
{
	Category = InCategory;

	if (Category)
	{
		// Update category name
		if (CategoryNameText)
		{
			CategoryNameText->SetText(Category->DisplayName);
		}

		// Update category icon
		if (CategoryIcon && Category->Icon)
		{
			FSlateBrush IconBrush;
			IconBrush.SetResourceObject(Category->Icon);
			CategoryIcon->SetBrush(IconBrush);
		}

		// Notify Blueprint
		OnCategorySet(Category);
	}

	UpdateVisuals();
}

void UCategoryTabWidget::SetSelected(bool bInSelected)
{
	if (bIsSelected != bInSelected)
	{
		bIsSelected = bInSelected;
		UpdateVisuals();
		OnSelectionChanged(bIsSelected);
	}
}

void UCategoryTabWidget::OnTabButtonClicked()
{
	if (Category)
	{
		// Fire selection events
		OnTabSelected.Broadcast(Category);
		OnCategoryChanged.Broadcast(Category);
	}
}

void UCategoryTabWidget::UpdateVisuals()
{
	// Update icon tint
	if (CategoryIcon)
	{
		FLinearColor CurrentTint = bIsSelected ? SelectedTint : UnselectedTint;
		CategoryIcon->SetColorAndOpacity(CurrentTint);
	}

	// Update text color
	if (CategoryNameText)
	{
		FSlateColor CurrentTextColor = bIsSelected ? SelectedTextColor : UnselectedTextColor;
		CategoryNameText->SetColorAndOpacity(CurrentTextColor);
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeCategories.h"
#include "Modules/ModuleManager.h"

class FSlateStyleSet;

class FBurbArchitectEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static EAssetTypeCategories::Type GetBurbDataAssetCategory() { return BurbDataAssetCategory; }

private:
	void RegisterAssetStyles();
	void UnregisterAssetStyles();

	static EAssetTypeCategories::Type BurbDataAssetCategory;

	// Asset Styles
	TSharedPtr<FSlateStyleSet> WallPatternStyleSet;
	TSharedPtr<FSlateStyleSet> FloorPatternStyleSet;
	TSharedPtr<FSlateStyleSet> FurnitureItemStyleSet;
	TSharedPtr<FSlateStyleSet> ArchitectureItemStyleSet;
	TSharedPtr<FSlateStyleSet> CatalogCategoryStyleSet;
	TSharedPtr<FSlateStyleSet> CatalogSubcategoryStyleSet;
	TSharedPtr<FSlateStyleSet> DoorItemStyleSet;
	TSharedPtr<FSlateStyleSet> WindowItemStyleSet;
	TSharedPtr<FSlateStyleSet> FenceItemStyleSet;
	TSharedPtr<FSlateStyleSet> GateItemStyleSet;
};
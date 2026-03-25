// Copyright Epic Games, Inc. All Rights Reserved.

#include "BurbArchitectEditor.h"

// Editor Includes
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "BurbAssetFactory.h"
#include "AssetEditors/AssetEditor_PortalItem.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FBurbArchitectEditorModule"

EAssetTypeCategories::Type FBurbArchitectEditorModule::BurbDataAssetCategory;

void FBurbArchitectEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	BurbDataAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("BurbData")), LOCTEXT("BurbDataCategory", "Burb Data"));

	{
		// WallPattern Style
		TSharedRef<IAssetTypeActions> ACT_WallPattern = MakeShareable(new FATA_WallPattern(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_WallPattern);
	}

	{
		// FloorPattern Style
		TSharedRef<IAssetTypeActions> ACT_FloorPattern = MakeShareable(new FATA_FloorPattern(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_FloorPattern);
	}

	{
		// FurnitureItem Style
		TSharedRef<IAssetTypeActions> ACT_FurnitureItem = MakeShareable(new FATA_FurnitureItem(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_FurnitureItem);
	}

	{
		// ArchitectureItem
		TSharedRef<IAssetTypeActions> ACT_ArchitectureItem = MakeShareable(new FATA_ArchitectureItem(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_ArchitectureItem);
	}

	{
		// CatalogCategory
		TSharedRef<IAssetTypeActions> ACT_CatalogCategory = MakeShareable(new FATA_CatalogCategory(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_CatalogCategory);
	}

	{
		// CatalogSubcategory
		TSharedRef<IAssetTypeActions> ACT_CatalogSubcategory = MakeShareable(new FATA_CatalogSubcategory(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_CatalogSubcategory);
	}

	{
		// DoorItem - Custom editor with 3D preview
		TSharedRef<IAssetTypeActions> ACT_DoorItem = MakeShareable(new FAssetEditor_DoorItem(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_DoorItem);
	}

	{
		// WindowItem - Custom editor with 3D preview
		TSharedRef<IAssetTypeActions> ACT_WindowItem = MakeShareable(new FAssetEditor_WindowItem(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_WindowItem);
	}

	{
		// FenceItem
		TSharedRef<IAssetTypeActions> ACT_FenceItem = MakeShareable(new FATA_FenceItem(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_FenceItem);
	}

	{
		// GateItem
		TSharedRef<IAssetTypeActions> ACT_GateItem = MakeShareable(new FATA_GateItem(BurbDataAssetCategory));
		AssetTools.RegisterAssetTypeActions(ACT_GateItem);
	}

	RegisterAssetStyles();
}

void FBurbArchitectEditorModule::ShutdownModule()
{
	UnregisterAssetStyles();
}

void FBurbArchitectEditorModule::RegisterAssetStyles()
{
	const FString PluginBaseDir = IPluginManager::Get().FindPlugin("BurbArchitect")->GetBaseDir();

	// Lambda to create + Register a style
	auto CreateStyle = [PluginBaseDir](const FName& StyleName, const FString& ClassName, const FString& ClassThumbnail)
	{
		TSharedPtr<FSlateStyleSet> StyleSet = MakeShareable(new FSlateStyleSet(StyleName));
		StyleSet->SetContentRoot(PluginBaseDir / TEXT("/Resources"));

		FSlateImageBrush* ThumbnailBrush = new FSlateImageBrush(StyleSet->RootToContentDir(*ClassThumbnail, TEXT(".png")), FVector2D(128.f, 128.f));
		if (ThumbnailBrush)
		{
			StyleSet->Set(*FString::Printf(TEXT("ClassThumbnail.%s"), *ClassName), ThumbnailBrush);
		}
		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);

		return StyleSet;
	};

	// WallPattern Style
	WallPatternStyleSet = CreateStyle(FName("WallPatternStyleSet"), TEXT("WallPattern"), TEXT("WallPatternIcon128"));

	// FloorPattern Style
	FloorPatternStyleSet = CreateStyle(FName("FloorPatternStyleSet"), TEXT("FloorPattern"), TEXT("FloorPatternIcon128"));

	// FurnitureItem Style
	FurnitureItemStyleSet = CreateStyle(FName("FurnitureItemStyleSet"), TEXT("FurnitureItem"), TEXT("FurnitureItemIcon128"));

	// ArchitectureItem Style
	ArchitectureItemStyleSet = CreateStyle(FName("ArchitectureItemStyleSet"), TEXT("ArchitectureItem"), TEXT("ArchitectureIcon128"));
	
	// CatalogCategory Style
	CatalogCategoryStyleSet = CreateStyle(FName("CatalogCategoryStyleSet"), TEXT("CatalogCategory"), TEXT("CatalogCategoryIcon128"));

	// CatalogSubcategory Style
	CatalogSubcategoryStyleSet = CreateStyle(FName("CatalogSubcategoryStyleSet"), TEXT("CatalogSubcategory"), TEXT("CatalogCategoryIcon128"));

	// DoorItem Style
	DoorItemStyleSet = CreateStyle(FName("DoorItemStyleSet"), TEXT("DoorItem"), TEXT("DoorItemIcon128"));

	// WindowItem Style
	WindowItemStyleSet = CreateStyle(FName("WindowItemStyleSet"), TEXT("WindowItem"), TEXT("WindowItemIcon128"));

	// FenceItem Style
	FenceItemStyleSet = CreateStyle(FName("FenceItemStyleSet"), TEXT("FenceItem"), TEXT("FenceItemIcon128"));

	// GateItem Style
	GateItemStyleSet = CreateStyle(FName("GateItemStyleSet"), TEXT("GateItem"), TEXT("GateItemIcon128"));
}

void FBurbArchitectEditorModule::UnregisterAssetStyles()
{
	// WallPattern Style
	if (WallPatternStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*WallPatternStyleSet.Get());
		ensure(WallPatternStyleSet.IsUnique());
		WallPatternStyleSet.Reset();
	}

	// FloorPattern Style
	if (FloorPatternStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*FloorPatternStyleSet.Get());
		ensure(FloorPatternStyleSet.IsUnique());
		FloorPatternStyleSet.Reset();
	}

	// FurnitureItem Style
	if (FurnitureItemStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*FurnitureItemStyleSet.Get());
		ensure(FurnitureItemStyleSet.IsUnique());
		FurnitureItemStyleSet.Reset();
	}

	// ArchitectureItem Style
	if (ArchitectureItemStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*ArchitectureItemStyleSet.Get());
		ensure(ArchitectureItemStyleSet.IsUnique());
		ArchitectureItemStyleSet.Reset();
	}
	
	// CatalogCategory Style
	if (CatalogCategoryStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*CatalogCategoryStyleSet.Get());
		ensure(CatalogCategoryStyleSet.IsUnique());
		CatalogCategoryStyleSet.Reset();
	}

	// CatalogSubcategory Style
	if (CatalogSubcategoryStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*CatalogSubcategoryStyleSet.Get());
		ensure(CatalogSubcategoryStyleSet.IsUnique());
		CatalogSubcategoryStyleSet.Reset();
	}

	// DoorItem Style
	if (DoorItemStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*DoorItemStyleSet.Get());
		ensure(DoorItemStyleSet.IsUnique());
		DoorItemStyleSet.Reset();
	}

	// WindowItem Style
	if (WindowItemStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*WindowItemStyleSet.Get());
		ensure(WindowItemStyleSet.IsUnique());
		WindowItemStyleSet.Reset();
	}

	// FenceItem Style
	if (FenceItemStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*FenceItemStyleSet.Get());
		ensure(FenceItemStyleSet.IsUnique());
		FenceItemStyleSet.Reset();
	}

	// GateItem Style
	if (GateItemStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*GateItemStyleSet.Get());
		ensure(GateItemStyleSet.IsUnique());
		GateItemStyleSet.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBurbArchitectEditorModule, BurbArchitectEditor)

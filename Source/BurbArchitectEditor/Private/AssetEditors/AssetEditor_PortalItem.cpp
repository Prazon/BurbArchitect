// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetEditors/AssetEditor_PortalItem.h"
#include "AssetEditors/PortalItemEditorToolkit.h"
#include "Data/WindowItem.h"
#include "Data/DoorItem.h"

#define LOCTEXT_NAMESPACE "AssetEditor_PortalItem"

// ==================== WindowItem Asset Editor ====================

FAssetEditor_WindowItem::FAssetEditor_WindowItem(EAssetTypeCategories::Type InAssetCategory)
	: MyAssetCategory(InAssetCategory)
{
}

FText FAssetEditor_WindowItem::GetName() const
{
	return LOCTEXT("WindowItemName", "Window Item");
}

FColor FAssetEditor_WindowItem::GetTypeColor() const
{
	return FColor(100, 150, 255); // Light blue for windows
}

UClass* FAssetEditor_WindowItem::GetSupportedClass() const
{
	return UWindowItem::StaticClass();
}

void FAssetEditor_WindowItem::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		UWindowItem* WindowItem = Cast<UWindowItem>(*ObjIt);
		if (WindowItem)
		{
			TSharedRef<FPortalItemEditorToolkit> EditorToolkit = MakeShareable(new FPortalItemEditorToolkit());
			EditorToolkit->InitPortalItemEditor(Mode, EditWithinLevelEditor, WindowItem);
		}
	}
}

uint32 FAssetEditor_WindowItem::GetCategories()
{
	return MyAssetCategory;
}

// ==================== DoorItem Asset Editor ====================

FAssetEditor_DoorItem::FAssetEditor_DoorItem(EAssetTypeCategories::Type InAssetCategory)
	: MyAssetCategory(InAssetCategory)
{
}

FText FAssetEditor_DoorItem::GetName() const
{
	return LOCTEXT("DoorItemName", "Door Item");
}

FColor FAssetEditor_DoorItem::GetTypeColor() const
{
	return FColor(150, 100, 50); // Brown for doors
}

UClass* FAssetEditor_DoorItem::GetSupportedClass() const
{
	return UDoorItem::StaticClass();
}

void FAssetEditor_DoorItem::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		UDoorItem* DoorItem = Cast<UDoorItem>(*ObjIt);
		if (DoorItem)
		{
			TSharedRef<FPortalItemEditorToolkit> EditorToolkit = MakeShareable(new FPortalItemEditorToolkit());
			EditorToolkit->InitPortalItemEditor(Mode, EditWithinLevelEditor, DoorItem);
		}
	}
}

uint32 FAssetEditor_DoorItem::GetCategories()
{
	return MyAssetCategory;
}

#undef LOCTEXT_NAMESPACE

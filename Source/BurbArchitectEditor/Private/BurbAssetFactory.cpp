// Fill out your copyright notice in the Description page of Project Settings.

#if WITH_EDITOR

#include "BurbAssetFactory.h"

#define LOCTEXT_NAMESPACE "BurbData"

#pragma region WallPattern
FATA_WallPattern::FATA_WallPattern(EAssetTypeCategories::Type AssetCategory)
{
	WallPattern_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UWallPatternFactory::UWallPatternFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UWallPattern::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UWallPatternFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UWallPattern::StaticClass()));
	UWallPattern* WallPattern = NewObject<UWallPattern>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return WallPattern;
}
#pragma endregion

#pragma region FloorPattern
FATA_FloorPattern::FATA_FloorPattern(EAssetTypeCategories::Type AssetCategory)
{
	FloorPattern_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UFloorPatternFactory::UFloorPatternFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UFloorPattern::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UFloorPatternFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UFloorPattern::StaticClass()));
	UFloorPattern* FloorPattern = NewObject<UFloorPattern>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return FloorPattern;
}
#pragma endregion

#pragma region FurnitureItem
FATA_FurnitureItem::FATA_FurnitureItem(EAssetTypeCategories::Type AssetCategory)
{
	FurnitureItem_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UFurnitureItemFactory::UFurnitureItemFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UFurnitureItem::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UFurnitureItemFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UFurnitureItem::StaticClass()));
	UFurnitureItem* FurnitureItem = NewObject<UFurnitureItem>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return FurnitureItem;
}
#pragma endregion

#pragma region ArchitectureItem
FATA_ArchitectureItem::FATA_ArchitectureItem(EAssetTypeCategories::Type AssetCategory)
{
	ArchitectureItem_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UArchitectureItemFactory::UArchitectureItemFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UArchitectureItem::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UArchitectureItemFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UArchitectureItem::StaticClass()));
	UArchitectureItem* ArchitectureItem = NewObject<UArchitectureItem>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return ArchitectureItem;
}
#pragma endregion

#pragma region CatalogCategory
FATA_CatalogCategory::FATA_CatalogCategory(EAssetTypeCategories::Type AssetCategory)
{
	CatalogCategory_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("CatalogSubcategory", "Catalog"));
}

UCatalogCategoryFactory::UCatalogCategoryFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UCatalogCategory::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UCatalogCategoryFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UCatalogCategory::StaticClass()));
	UCatalogCategory* CatalogCategory = NewObject<UCatalogCategory>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return CatalogCategory;
}
#pragma endregion

#pragma region CatalogSubcategory
FATA_CatalogSubcategory::FATA_CatalogSubcategory(EAssetTypeCategories::Type AssetCategory)
{
	CatalogSubcategory_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("CatalogSubcategory", "Catalog"));
}

UCatalogSubcategoryFactory::UCatalogSubcategoryFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UCatalogSubcategory::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UCatalogSubcategoryFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UCatalogSubcategory::StaticClass()));
	UCatalogSubcategory* CatalogSubcategory = NewObject<UCatalogSubcategory>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return CatalogSubcategory;
}
#pragma endregion

#pragma region DoorItem
FATA_DoorItem::FATA_DoorItem(EAssetTypeCategories::Type AssetCategory)
{
	DoorItem_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UDoorItemFactory::UDoorItemFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UDoorItem::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UDoorItemFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UDoorItem::StaticClass()));
	UDoorItem* DoorItem = NewObject<UDoorItem>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return DoorItem;
}
#pragma endregion

#pragma region WindowItem
FATA_WindowItem::FATA_WindowItem(EAssetTypeCategories::Type AssetCategory)
{
	WindowItem_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UWindowItemFactory::UWindowItemFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UWindowItem::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UWindowItemFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UWindowItem::StaticClass()));
	UWindowItem* WindowItem = NewObject<UWindowItem>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return WindowItem;
}
#pragma endregion

#pragma region FenceItem
FATA_FenceItem::FATA_FenceItem(EAssetTypeCategories::Type AssetCategory)
{
	FenceItem_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UFenceItemFactory::UFenceItemFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UFenceItem::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UFenceItemFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UFenceItem::StaticClass()));
	UFenceItem* FenceItem = NewObject<UFenceItem>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return FenceItem;
}
#pragma endregion

#pragma region GateItem
FATA_GateItem::FATA_GateItem(EAssetTypeCategories::Type AssetCategory)
{
	GateItem_AssetCategory = AssetCategory;
	SubMenus.Add(LOCTEXT("BuildingSubcategory", "Building"));
}

UGateItemFactory::UGateItemFactory(const class FObjectInitializer& OBJ) : Super(OBJ)
{
	SupportedClass = UGateItem::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UGateItemFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UGateItem::StaticClass()));
	UGateItem* GateItem = NewObject<UGateItem>(InParent, Class, Name, Flags | RF_Transactional, Context);
	return GateItem;
}
#pragma endregion

#undef LOCTEXT_NAMESPACE

#endif

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

// Engine Includes
#include "CoreMinimal.h"
#include "UObject/Object.h"
// Editor Includes
#include "AssetTypeCategories.h"
#include "Factories/Factory.h"
#include "AssetTypeActions_Base.h"
// BurbArchitect Includes
#include "Data/WallPattern.h"
#include "Data/FloorPattern.h"
#include "Data/FurnitureItem.h"
#include "Data/ArchitectureItem.h"
#include "Data/CatalogCategory.h"
#include "Data/CatalogSubcategory.h"
#include "Data/DoorItem.h"
#include "Data/WindowItem.h"
#include "Data/FenceItem.h"
#include "Data/GateItem.h"

#include "BurbAssetFactory.generated.h"

#define LOCTEXT_NAMESPACE "BurbData"

#pragma region WallPattern
/**
 * Editor Utility Factory for creating WallPattern assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UWallPatternFactory : public UFactory
{
	GENERATED_BODY()

	UWallPatternFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * WallPattern Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_WallPattern : public FAssetTypeActions_Base
{
public:
	FATA_WallPattern(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("WallPatternAsset", "Wall Pattern"); }
	virtual uint32 GetCategories() override { return WallPattern_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(128, 0, 255); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("WallPatternAssetDesc", "A wall pattern that defines textures and materials for wall surfaces"); }
	virtual UClass* GetSupportedClass() const override { return UWallPattern::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type WallPattern_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region FloorPattern
/**
 * Editor Utility Factory for creating FloorPattern assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UFloorPatternFactory : public UFactory
{
	GENERATED_BODY()

	UFloorPatternFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * FloorPattern Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_FloorPattern : public FAssetTypeActions_Base
{
public:
	FATA_FloorPattern(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("FloorPatternAsset", "Floor Pattern"); }
	virtual uint32 GetCategories() override { return FloorPattern_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(128, 0, 255); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("FloorPatternAssetDesc", "A floor pattern that defines textures and materials for floor surfaces"); }
	virtual UClass* GetSupportedClass() const override { return UFloorPattern::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type FloorPattern_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region FurnitureItem
/**
 * Editor Utility Factory for creating FurnitureItem assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UFurnitureItemFactory : public UFactory
{
	GENERATED_BODY()

	UFurnitureItemFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * FurnitureItem Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_FurnitureItem : public FAssetTypeActions_Base
{
public:
	FATA_FurnitureItem(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("FurnitureItemAsset", "Furniture Item"); }
	virtual uint32 GetCategories() override { return FurnitureItem_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(50, 100, 128); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("FurnitureItemAssetDesc", "A furniture item that can be placed in lots for interior decoration"); }
	virtual UClass* GetSupportedClass() const override { return UFurnitureItem::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type FurnitureItem_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region ArchitectureItem
/**
 * Editor Utility Factory for creating ArchitectureItem assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UArchitectureItemFactory : public UFactory
{
	GENERATED_BODY()

	UArchitectureItemFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * ArchitectureItem Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_ArchitectureItem : public FAssetTypeActions_Base
{
public:
	FATA_ArchitectureItem(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("ArchitectureItemAsset", "Architecture Item"); }
	virtual uint32 GetCategories() override { return ArchitectureItem_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(128, 0, 128); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("ArchitectureItemAssetDesc", "An Architecture Item that can be used to build houses on a lot"); }
	virtual UClass* GetSupportedClass() const override { return UArchitectureItem::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type ArchitectureItem_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region CatalogCategory
/**
 * Editor Utility Factory for creating CatalogCategory assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UCatalogCategoryFactory : public UFactory
{
	GENERATED_BODY()

	UCatalogCategoryFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * CatalogCategory Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_CatalogCategory : public FAssetTypeActions_Base
{
public:
	FATA_CatalogCategory(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("CatalogCategoryAsset", "Catalog Category"); }
	virtual uint32 GetCategories() override { return CatalogCategory_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(0, 177, 155); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("CatalogCategoryAssetDesc", "A catalog category for organizing catalog items into groups"); }
	virtual UClass* GetSupportedClass() const override { return UCatalogCategory::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type CatalogCategory_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region CatalogSubcategory
/**
 * Editor Utility Factory for creating CatalogSubcategory assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UCatalogSubcategoryFactory : public UFactory
{
	GENERATED_BODY()

	UCatalogSubcategoryFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * CatalogSubcategory Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_CatalogSubcategory : public FAssetTypeActions_Base
{
public:
	FATA_CatalogSubcategory(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("CatalogSubcategoryAsset", "Catalog Subcategory"); }
	virtual uint32 GetCategories() override { return CatalogSubcategory_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(120, 255, 150); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("CatalogSubcategoryAssetDesc", "A catalog subcategory for nested organization within categories"); }
	virtual UClass* GetSupportedClass() const override { return UCatalogSubcategory::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type CatalogSubcategory_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region DoorItem
/**
 * Editor Utility Factory for creating DoorItem assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UDoorItemFactory : public UFactory
{
	GENERATED_BODY()

	UDoorItemFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * DoorItem Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_DoorItem : public FAssetTypeActions_Base
{
public:
	FATA_DoorItem(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("DoorItemAsset", "Door Item"); }
	virtual uint32 GetCategories() override { return DoorItem_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(200, 100, 50); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("DoorItemAssetDesc", "A door item that can be placed on walls to create entryways"); }
	virtual UClass* GetSupportedClass() const override { return UDoorItem::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type DoorItem_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region WindowItem
/**
 * Editor Utility Factory for creating WindowItem assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UWindowItemFactory : public UFactory
{
	GENERATED_BODY()

	UWindowItemFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * WindowItem Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_WindowItem : public FAssetTypeActions_Base
{
public:
	FATA_WindowItem(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("WindowItemAsset", "Window Item"); }
	virtual uint32 GetCategories() override { return WindowItem_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(100, 150, 220); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("WindowItemAssetDesc", "A window item that can be placed on walls to provide views and natural light"); }
	virtual UClass* GetSupportedClass() const override { return UWindowItem::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type WindowItem_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region FenceItem
/**
 * Editor Utility Factory for creating FenceItem assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UFenceItemFactory : public UFactory
{
	GENERATED_BODY()

	UFenceItemFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * FenceItem Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_FenceItem : public FAssetTypeActions_Base
{
public:
	FATA_FenceItem(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("FenceItemAsset", "Fence Item"); }
	virtual uint32 GetCategories() override { return FenceItem_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(150, 100, 50); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("FenceItemAssetDesc", "A fence with configurable panels and posts"); }
	virtual UClass* GetSupportedClass() const override { return UFenceItem::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type FenceItem_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

#pragma region GateItem
/**
 * Editor Utility Factory for creating GateItem assets
 */
UCLASS()
class BURBARCHITECTEDITOR_API UGateItemFactory : public UFactory
{
	GENERATED_BODY()

	UGateItemFactory(const class FObjectInitializer& OBJ);
protected:
	virtual bool IsMacroFactory() const { return false; }
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

/**
 * GateItem Asset Editor definition
 */
class BURBARCHITECTEDITOR_API FATA_GateItem : public FAssetTypeActions_Base
{
public:
	FATA_GateItem(EAssetTypeCategories::Type AssetCategory);

	virtual FText GetName() const override { return LOCTEXT("GateItemAsset", "Gate Item"); }
	virtual uint32 GetCategories() override { return GateItem_AssetCategory; }
	virtual FColor GetTypeColor() const override { return FColor(180, 120, 60); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return LOCTEXT("GateItemAssetDesc", "A gate that can be placed on fences"); }
	virtual UClass* GetSupportedClass() const override { return UGateItem::StaticClass(); }
	virtual const TArray<FText>& GetSubMenus() const override { return SubMenus; }
private:
	EAssetTypeCategories::Type GateItem_AssetCategory;
	mutable TArray<FText> SubMenus;
};
#pragma endregion

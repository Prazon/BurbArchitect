// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatalogWidgetBase.generated.h"

class UCatalogSubsystem;
class UCatalogItem;
class UCatalogCategory;

/**
 * Delegate for item selection events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnItemSelected, UCatalogItem*, Item);

/**
 * Delegate for category change events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCategoryChanged, UCatalogCategory*, Category);

/**
 * Abstract base class for all catalog-related widgets
 * Provides common functionality and access to the catalog subsystem
 */
UCLASS(Abstract, Blueprintable)
class BURBARCHITECT_API UCatalogWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Get the catalog subsystem (cached for performance)
	 * @return The catalog subsystem instance
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	UCatalogSubsystem* GetCatalogSubsystem() const;

	/**
	 * Event fired when an item is selected
	 */
	UPROPERTY(BlueprintAssignable, Category = "Catalog Events")
	FOnItemSelected OnItemSelected;

	/**
	 * Event fired when a category is changed
	 */
	UPROPERTY(BlueprintAssignable, Category = "Catalog Events")
	FOnCategoryChanged OnCategoryChanged;

protected:
	virtual void NativeConstruct() override;

	/**
	 * Cached reference to the catalog subsystem
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catalog")
	TObjectPtr<UCatalogSubsystem> CatalogSubsystem;

	/**
	 * Common styling: Background brush
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Styling")
	FSlateBrush BackgroundBrush;

	/**
	 * Common styling: Tint color
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Styling")
	FLinearColor TintColor = FLinearColor::White;
};

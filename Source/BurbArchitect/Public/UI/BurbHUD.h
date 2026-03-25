// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BurbHUD.generated.h"

class UCatalogBrowserWidget;
class UCatalogItem;
class ABurbPawn;
class ABurbPlayerController;

/**
 * HUD class for the BurbArchitect plugin
 * Manages the catalog browser UI and connects it to the player pawn
 */
UCLASS(Blueprintable)
class BURBARCHITECT_API ABurbHUD : public AHUD
{
	GENERATED_BODY()

public:
	ABurbHUD();

protected:
	virtual void BeginPlay() override;

	/**
	 * Widget class for the catalog browser
	 * Set this in Blueprint to use a custom styled catalog browser
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
	TSubclassOf<UCatalogBrowserWidget> CatalogBrowserClass;

	/**
	 * The spawned catalog browser widget instance
	 */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	TObjectPtr<UCatalogBrowserWidget> CatalogBrowser;

	/**
	 * Should the catalog browser be shown on BeginPlay?
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	bool bShowCatalogOnBeginPlay = true;

	/**
	 * Z-order for the catalog browser widget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	int32 CatalogBrowserZOrder = 0;

public:
	/**
	 * Show the catalog browser
	 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ShowCatalogBrowser();

	/**
	 * Hide the catalog browser
	 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void HideCatalogBrowser();

	/**
	 * Toggle catalog browser visibility
	 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ToggleCatalogBrowser();

	/**
	 * Get the catalog browser widget
	 * @return The catalog browser widget instance, or nullptr if not created
	 */
	UFUNCTION(BlueprintPure, Category = "UI")
	UCatalogBrowserWidget* GetCatalogBrowser() const { return CatalogBrowser; }

protected:
	/**
	 * Called when a catalog item is activated (clicked) in the browser
	 */
	UFUNCTION()
	void OnCatalogItemActivated(UCatalogItem* Item);

	/**
	 * Get the owning player's pawn as a BurbPawn
	 * @return The BurbPawn, or nullptr if not found
	 */
	ABurbPawn* GetBurbPawn() const;
};

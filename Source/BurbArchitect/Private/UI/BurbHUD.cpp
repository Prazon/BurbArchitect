// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/BurbHUD.h"
#include "Widgets/CatalogBrowserWidget.h"
#include "Data/CatalogItem.h"
#include "Actors/BurbPawn.h"
#include "Controller/BurbPlayerController.h"
#include "Blueprint/UserWidget.h"

ABurbHUD::ABurbHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABurbHUD::BeginPlay()
{
	Super::BeginPlay();

	// Create the catalog browser widget if a class is specified
	if (CatalogBrowserClass)
	{
		APlayerController* PC = GetOwningPlayerController();
		if (PC)
		{
			// Create the widget
			CatalogBrowser = CreateWidget<UCatalogBrowserWidget>(PC, CatalogBrowserClass);
			if (CatalogBrowser)
			{
				// Initialize the browser
				CatalogBrowser->InitializeBrowser();

				// Bind to item activation event
				CatalogBrowser->OnItemActivated.AddDynamic(this, &ABurbHUD::OnCatalogItemActivated);

				// Add to viewport
				CatalogBrowser->AddToViewport(CatalogBrowserZOrder);

				// Show or hide based on settings
				if (bShowCatalogOnBeginPlay)
				{
					CatalogBrowser->SetVisibility(ESlateVisibility::Visible);
				}
				else
				{
					CatalogBrowser->SetVisibility(ESlateVisibility::Hidden);
				}

				UE_LOG(LogTemp, Log, TEXT("BurbHUD: Catalog browser created and initialized"));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("BurbHUD: Failed to create catalog browser widget"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbHUD: No CatalogBrowserClass specified - catalog browser will not be created"));
	}
}

void ABurbHUD::ShowCatalogBrowser()
{
	if (CatalogBrowser)
	{
		CatalogBrowser->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Log, TEXT("BurbHUD: Catalog browser shown"));
	}
}

void ABurbHUD::HideCatalogBrowser()
{
	if (CatalogBrowser)
	{
		CatalogBrowser->SetVisibility(ESlateVisibility::Hidden);
		UE_LOG(LogTemp, Log, TEXT("BurbHUD: Catalog browser hidden"));
	}
}

void ABurbHUD::ToggleCatalogBrowser()
{
	if (CatalogBrowser)
	{
		ESlateVisibility CurrentVisibility = CatalogBrowser->GetVisibility();
		if (CurrentVisibility == ESlateVisibility::Visible)
		{
			HideCatalogBrowser();
		}
		else
		{
			ShowCatalogBrowser();
		}
	}
}

void ABurbHUD::OnCatalogItemActivated(UCatalogItem* Item)
{
	if (!Item)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbHUD::OnCatalogItemActivated - Item is null"));
		return;
	}

	// Get the PlayerController and forward the activation to it
	ABurbPlayerController* PC = Cast<ABurbPlayerController>(GetOwningPlayerController());
	if (PC)
	{
		PC->HandleCatalogItemActivation(Item);
		UE_LOG(LogTemp, Log, TEXT("BurbHUD: Forwarded catalog item activation for '%s' to BurbPlayerController"), *Item->DisplayName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbHUD: Could not find BurbPlayerController to handle catalog item activation"));
	}
}

ABurbPawn* ABurbHUD::GetBurbPawn() const
{
	APlayerController* PC = GetOwningPlayerController();
	if (PC)
	{
		return Cast<ABurbPawn>(PC->GetPawn());
	}
	return nullptr;
}

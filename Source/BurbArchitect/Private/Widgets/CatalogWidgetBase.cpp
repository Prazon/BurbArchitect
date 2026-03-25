// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CatalogWidgetBase.h"
#include "Subsystems/CatalogSubsystem.h"

void UCatalogWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Cache the catalog subsystem reference
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		CatalogSubsystem = GameInstance->GetSubsystem<UCatalogSubsystem>();
	}
}

UCatalogSubsystem* UCatalogWidgetBase::GetCatalogSubsystem() const
{
	return CatalogSubsystem;
}

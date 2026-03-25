// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/PortalBoxComponent.h"

UPortalBoxComponent::UPortalBoxComponent()
{
	// Set default visualization settings
	ShapeColor = FColor::Cyan;
	SetLineThickness(2.0f);
	bVisualizeComponent = true;
}

#if WITH_EDITOR
bool UPortalBoxComponent::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		// Prevent editing BoxExtent - it's controlled by PortalSize
		FName PropertyName = InProperty->GetFName();
		if (PropertyName == TEXT("BoxExtent"))
		{
			return false;
		}
	}

	return Super::CanEditChange(InProperty);
}
#endif

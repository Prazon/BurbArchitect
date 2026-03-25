// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "PortalBoxComponent.generated.h"

/**
 * Custom box component for portal visualization
 * Box extent is controlled by the parent PortalBase's PortalSize property
 */
UCLASS()
class BURBARCHITECT_API UPortalBoxComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UPortalBoxComponent();

#if WITH_EDITOR
	// Prevent editing BoxExtent in the editor
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
};

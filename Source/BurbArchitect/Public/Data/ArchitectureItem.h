// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/CatalogItem.h"
#include "ArchitectureItem.generated.h"

/**
 * 
 */
UCLASS()
class BURBARCHITECT_API UArchitectureItem : public UCatalogItem
{
	GENERATED_BODY()
public:
	
	//Default BuildTool to use when placing this item
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	TSoftClassPtr<ABuildTool> BuildToolClass;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LotSerialization.h"
#include "LotDataAsset.generated.h"

/**
 * Data asset containing a pre-built lot
 * Used for default lots that can be shipped with the game or loaded at runtime
 *
 * Usage:
 * - Create via Content Browser: Right-click -> Miscellaneous -> Data Asset -> LotDataAsset
 * - Save current lot as asset using LotManager::SaveAsDataAsset() (Editor only)
 * - Load via LotManager::LoadDefaultLot()
 */
UCLASS(BlueprintType)
class BURBARCHITECT_API ULotDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The serialized lot data containing all walls, floors, roofs, etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lot Data")
	FSerializedLotData LotData;

	/**
	 * Thumbnail texture for displaying in menus
	 * Can be captured automatically when saving the lot
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lot Data")
	UTexture2D* Thumbnail;

	/**
	 * Category for organizing lots in menus (e.g., "Starter", "Medium", "Large", "Empty")
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lot Data")
	FString Category;

	/**
	 * Tags for filtering and searching (e.g., "Modern", "Colonial", "Ranch")
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lot Data")
	TArray<FString> Tags;

	/**
	 * Price/cost to place this lot (0 = free)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lot Data")
	int32 Price = 0;

public:
	/**
	 * Validates that the lot data is complete and correct
	 * @return True if the lot data is valid
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Data")
	bool ValidateLotData() const;

	/**
	 * Gets a user-friendly name for this lot (uses LotData.LotName if available, otherwise asset name)
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Data")
	FString GetLotName() const;

	/**
	 * Gets a user-friendly description for this lot
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Data")
	FString GetDescription() const;
};

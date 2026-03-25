// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Data/LotSerialization.h"
#include "LotSaveGame.generated.h"

/**
 * Save game object for persisting lot data to disk
 * Uses Unreal's SaveGame system to write binary .sav files to Saved/SaveGames/
 *
 * Usage:
 * - Save: LotManager::SaveLotToSlot("Slot1")
 * - Load: LotManager::LoadLotFromSlot("Slot1")
 * - Export: LotManager::ExportLotToFile("MyLot.json") for human-readable format
 * - Import: LotManager::ImportLotFromFile("MyLot.json")
 */
UCLASS()
class BURBARCHITECT_API ULotSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * The complete lot data including all building elements
	 */
	UPROPERTY(VisibleAnywhere, Category = "Save Game")
	FSerializedLotData LotData;

	/**
	 * User-friendly slot name (for UI display)
	 */
	UPROPERTY(VisibleAnywhere, Category = "Save Game")
	FString SlotName;

	/**
	 * Index for save slots (if using numbered slots)
	 */
	UPROPERTY(VisibleAnywhere, Category = "Save Game")
	int32 SlotIndex = 0;

public:
	ULotSaveGame();
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "Data/LotSerialization.h"
#include "LoadLotCommand.generated.h"

class ALotManager;
class ULotSerializationSubsystem;

/**
 * Load Lot Command - Handles loading lot data with undo/redo support
 * Stores both the previous lot state and new lot state to allow undoing lot loads
 *
 * Usage:
 * - BuildServer->LoadLotFromSlot(SlotName) - Creates this command automatically
 * - Undo will restore the previous lot state
 * - Redo will re-apply the loaded lot state
 */
UCLASS()
class BURBARCHITECT_API ULoadLotCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	/**
	 * Initialize the load lot command
	 * @param Lot The lot manager to load data into
	 * @param NewLotData The new lot data to load
	 */
	void Initialize(ALotManager* Lot, const FSerializedLotData& NewLotData);

	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

protected:
	// Reference to the lot manager
	UPROPERTY()
	ALotManager* LotManager;

	// The lot state before this command was executed (for undo)
	UPROPERTY()
	FSerializedLotData OldLotData;

	// The lot state to apply (for redo)
	UPROPERTY()
	FSerializedLotData NewLotData;

	// Cached serialization subsystem
	ULotSerializationSubsystem* GetSerializationSubsystem() const;
};

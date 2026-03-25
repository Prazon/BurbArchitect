// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/LoadLotCommand.h"
#include "Actors/LotManager.h"
#include "Subsystems/LotSerializationSubsystem.h"

void ULoadLotCommand::Initialize(ALotManager* Lot, const FSerializedLotData& InNewLotData)
{
	LotManager = Lot;
	NewLotData = InNewLotData;

	// Capture current lot state for undo
	ULotSerializationSubsystem* SerializationSubsystem = GetSerializationSubsystem();
	if (SerializationSubsystem && LotManager)
	{
		OldLotData = SerializationSubsystem->SerializeLot(LotManager);
		UE_LOG(LogTemp, Log, TEXT("LoadLotCommand: Captured current lot state for undo"));
	}
}

void ULoadLotCommand::Commit()
{
	if (!IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand::Commit: Command is invalid"));
		return;
	}

	ULotSerializationSubsystem* SerializationSubsystem = GetSerializationSubsystem();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand::Commit: SerializationSubsystem not found"));
		return;
	}

	// Load the new lot data
	if (SerializationSubsystem->DeserializeLot(LotManager, NewLotData))
	{
		bCommitted = true;
		UE_LOG(LogTemp, Log, TEXT("LoadLotCommand: Successfully loaded lot '%s'"), *NewLotData.LotName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand: Failed to load lot '%s'"), *NewLotData.LotName);
	}
}

void ULoadLotCommand::Undo()
{
	if (!IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand::Undo: Command is invalid"));
		return;
	}

	ULotSerializationSubsystem* SerializationSubsystem = GetSerializationSubsystem();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand::Undo: SerializationSubsystem not found"));
		return;
	}

	// Restore the old lot state
	if (SerializationSubsystem->DeserializeLot(LotManager, OldLotData))
	{
		UE_LOG(LogTemp, Log, TEXT("LoadLotCommand: Successfully restored previous lot state"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand: Failed to restore previous lot state"));
	}
}

void ULoadLotCommand::Redo()
{
	if (!IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand::Redo: Command is invalid"));
		return;
	}

	ULotSerializationSubsystem* SerializationSubsystem = GetSerializationSubsystem();
	if (!SerializationSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand::Redo: SerializationSubsystem not found"));
		return;
	}

	// Re-apply the new lot data
	if (SerializationSubsystem->DeserializeLot(LotManager, NewLotData))
	{
		UE_LOG(LogTemp, Log, TEXT("LoadLotCommand: Successfully re-loaded lot '%s'"), *NewLotData.LotName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("LoadLotCommand: Failed to re-load lot '%s'"), *NewLotData.LotName);
	}
}

FString ULoadLotCommand::GetDescription() const
{
	return FString::Printf(TEXT("Load Lot: %s"), *NewLotData.LotName);
}

bool ULoadLotCommand::IsValid() const
{
	return LotManager != nullptr && LotManager->IsValidLowLevel();
}

ULotSerializationSubsystem* ULoadLotCommand::GetSerializationSubsystem() const
{
	if (!LotManager || !LotManager->GetWorld())
	{
		return nullptr;
	}

	UGameInstance* GameInstance = LotManager->GetWorld()->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<ULotSerializationSubsystem>();
}

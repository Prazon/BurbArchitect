// Fill out your copyright notice in the Description page of Project Settings.

#include "Commands/BatchCommand.h"

void UBatchCommand::SetCommands(const TArray<UBuildCommand*>& InCommands, const FString& InDescription)
{
	Commands = InCommands;
	Description = InDescription;
}

void UBatchCommand::AddCommand(UBuildCommand* Command)
{
	if (Command)
	{
		Commands.Add(Command);
	}
}

void UBatchCommand::Commit()
{
	// Commit all commands in order
	for (UBuildCommand* Command : Commands)
	{
		if (Command)
		{
			Command->Commit();
		}
	}

	bCommitted = true;
	UE_LOG(LogTemp, Log, TEXT("BatchCommand: Committed '%s' with %d commands"), *Description, Commands.Num());
}

void UBatchCommand::Undo()
{
	// Undo all commands in reverse order
	for (int32 i = Commands.Num() - 1; i >= 0; --i)
	{
		if (Commands[i])
		{
			Commands[i]->Undo();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("BatchCommand: Undid '%s' with %d commands"), *Description, Commands.Num());
}

void UBatchCommand::Redo()
{
	// Redo all commands in order
	for (UBuildCommand* Command : Commands)
	{
		if (Command)
		{
			Command->Redo();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("BatchCommand: Redid '%s' with %d commands"), *Description, Commands.Num());
}

FString UBatchCommand::GetDescription() const
{
	return FString::Printf(TEXT("%s (%d operations)"), *Description, Commands.Num());
}

bool UBatchCommand::IsValid() const
{
	// Batch is valid if all commands are valid
	for (UBuildCommand* Command : Commands)
	{
		if (!Command || !Command->IsValid())
		{
			return false;
		}
	}
	return true;
}

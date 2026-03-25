// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "BatchCommand.generated.h"

/**
 * Batch Command - Groups multiple commands into a single undoable operation
 * Used for complex operations like building a room (4 walls as one undo)
 */
UCLASS()
class UBatchCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

	/**
	 * Set the commands in this batch
	 */
	void SetCommands(const TArray<UBuildCommand*>& InCommands, const FString& InDescription);

	/**
	 * Add a command to this batch (for appending commands after creation)
	 */
	void AddCommand(UBuildCommand* Command);

	/**
	 * Get the number of commands in this batch
	 */
	int32 GetCommandCount() const { return Commands.Num(); }

	/**
	 * Get the array of commands in this batch (for room detection)
	 */
	const TArray<UBuildCommand*>& GetCommands() const { return Commands; }

protected:
	// Array of commands in this batch
	UPROPERTY()
	TArray<UBuildCommand*> Commands;

	// Description of this batch operation
	UPROPERTY()
	FString Description;
};

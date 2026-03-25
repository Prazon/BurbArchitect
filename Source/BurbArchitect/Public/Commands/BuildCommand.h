// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BuildCommand.generated.h"

/**
 * Base class for all undoable build commands
 * Implements the Command Pattern for undo/redo functionality
 */
UCLASS(Abstract)
class BURBARCHITECT_API UBuildCommand : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Commit the command - performs the building operation
	 */
	virtual void Commit() PURE_VIRTUAL(UBuildCommand::Commit, );

	/**
	 * Undo the command - reverses the building operation
	 */
	virtual void Undo() PURE_VIRTUAL(UBuildCommand::Undo, );

	/**
	 * Redo the command - re-applies the building operation
	 */
	virtual void Redo() PURE_VIRTUAL(UBuildCommand::Redo, );

	/**
	 * Get a human-readable description of this command
	 */
	virtual FString GetDescription() const PURE_VIRTUAL(UBuildCommand::GetDescription, return TEXT("Build Command"););

	/**
	 * Check if this command is still valid (e.g., objects still exist)
	 */
	virtual bool IsValid() const PURE_VIRTUAL(UBuildCommand::IsValid, return false;);

	/**
	 * Whether this command has been committed
	 */
	FORCEINLINE bool HasBeenCommitted() const { return bCommitted; }

protected:
	/**
	 * Tracks whether this command has been committed
	 */
	UPROPERTY()
	bool bCommitted = false;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IDeletable.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, Blueprintable)
class UDeletable : public UInterface
{
	GENERATED_BODY()
};

/**
 * IDeletable - Interface for actors that can be deleted via DEL key or other deletion mechanisms
 *
 * Any actor implementing this interface can respond to deletion requests.
 * Usage:
 * - Implement this interface on your actor class
 * - Override CanBeDeleted() to control when deletion is allowed
 * - Override OnDeleted() to perform custom cleanup
 * - Call RequestDeletion() to trigger the deletion process
 */
class BURBARCHITECT_API IDeletable
{
	GENERATED_BODY()

public:
	/**
	 * Check if this actor can currently be deleted
	 * Override this to add custom deletion restrictions (e.g., only when selected, not during certain states)
	 * @return true if the actor can be deleted, false otherwise
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Deletion")
	bool CanBeDeleted() const;
	virtual bool CanBeDeleted_Implementation() const { return true; }

	/**
	 * Request deletion of this actor
	 * This will check CanBeDeleted(), call OnDeleted() for cleanup, then destroy the actor
	 * @return true if deletion was successful, false if deletion was blocked
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Deletion")
	bool RequestDeletion();
	virtual bool RequestDeletion_Implementation();

	/**
	 * Called before the actor is deleted to perform cleanup
	 * Override this to clean up references, remove from managers, undo system, etc.
	 * DO NOT destroy the actor here - that's handled by RequestDeletion()
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Deletion")
	void OnDeleted();
	virtual void OnDeleted_Implementation() {}

	/**
	 * Get whether this actor is currently selected/in edit mode
	 * Used to determine if DEL key should affect this actor
	 * @return true if the actor is selected/active
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Deletion")
	bool IsSelected() const;
	virtual bool IsSelected_Implementation() const { return false; }
};

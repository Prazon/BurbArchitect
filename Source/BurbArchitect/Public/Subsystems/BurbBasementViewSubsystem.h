// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BurbBasementViewSubsystem.generated.h"

/**
 * World Subsystem that manages the basement view rendering system.
 * Toggles visibility of non-lot primitives using SetHiddenInGame() and SetCastHiddenShadow().
 * Hidden components continue casting shadows, preserving correct lighting in basement view.
 *
 * Filtering Rules:
 * 1. Lot-related actors (LotManager, BuildTools) - always visible
 * 2. Light actors (including skylights) - always visible
 * 3. Actors with light components (lamps, fixtures) - always visible
 * 4. Actors with "BasementView.AlwaysVisible" tag - always visible
 * 5. All other actors - hidden in basement view
 *
 * Usage:
 *   UBurbBasementViewSubsystem* Subsystem = GetWorld()->GetSubsystem<UBurbBasementViewSubsystem>();
 *   Subsystem->SetBasementViewEnabled(true);  // Enter basement view (hide non-lot objects, keep shadows)
 *   Subsystem->SetBasementViewEnabled(false); // Exit basement view (show all objects)
 */
UCLASS(BlueprintType)
class UBurbBasementViewSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	/**
	 * Enable or disable basement view mode.
	 * When enabled, hides all non-lot actors (SetActorHiddenInGame=true, SetCastHiddenShadow=true)
	 * so they're invisible but still cast shadows for correct lighting.
	 * Iterates through all world actors each time to avoid timing/caching issues.
	 *
	 * @param bEnabled - True to enable basement view, false to disable
	 */
	UFUNCTION(BlueprintCallable, Category = "Basement View")
	void SetBasementViewEnabled(bool bEnabled);

	/**
	 * Debug function to print all actors in the world and their primitive components.
	 * Use this to see what the subsystem can detect.
	 */
	UFUNCTION(BlueprintCallable, Category = "Basement View")
	void DebugPrintAllActors();

	/**
	 * Check if basement view is currently enabled.
	 *
	 * @return True if basement view is active
	 */
	UFUNCTION(BlueprintPure, Category = "Basement View")
	bool IsBasementViewEnabled() const;

private:
	/**
	 * Helper function to determine if an actor should remain visible in basement view.
	 * Returns true if actor should be SKIPPED (remain visible).
	 * Checks for:
	 * - Lot-related actors (LotManager, BuildTool, owned/attached to lot)
	 * - Light actors (including skylights)
	 * - Actors with light components (lamps, fixtures with attached lights)
	 * - Actors with "BasementView.AlwaysVisible" gameplay tag
	 */
	bool ShouldSkipActor(AActor* Actor) const;

	/** Whether basement view is currently active */
	bool bBasementViewEnabled = false;
};

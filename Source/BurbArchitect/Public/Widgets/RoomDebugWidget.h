// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomDebugWidget.generated.h"

class SRoomDebugWidget;
class ALotManager;

/**
 * DEPRECATED: Use URoomDebugCanvas instead for a modular canvas-only widget.
 *
 * This widget includes built-in controls that may not fit your layout.
 * URoomDebugCanvas provides just the visualization, letting you add
 * your own buttons and labels in UMG.
 */
UCLASS()
class BURBARCHITECT_API URoomDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set the lot manager to visualize */
	UFUNCTION(BlueprintCallable, Category = "Room Debug")
	void SetLotManager(ALotManager* InLotManager);

	/** Set the current level/floor to display */
	UFUNCTION(BlueprintCallable, Category = "Room Debug")
	void SetCurrentLevel(int32 InLevel);

	/** Refresh the visualization */
	UFUNCTION(BlueprintCallable, Category = "Room Debug")
	void Refresh();

protected:
	//~ Begin UUserWidget Interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	//~ End UUserWidget Interface

private:
	/** The underlying Slate widget */
	TSharedPtr<SRoomDebugWidget> SlateWidget;

	/** Cached lot manager reference */
	UPROPERTY(Transient)
	ALotManager* CachedLotManager;

	/** Cached current level */
	UPROPERTY(Transient)
	int32 CachedCurrentLevel;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomDebugCanvas.generated.h"

class SRoomDebugCanvas;
class ALotManager;

/**
 * Minimal UMG widget that displays only the room grid visualization.
 * No built-in controls - compose your own UI around this canvas.
 *
 * Usage:
 * 1. Add RoomDebugCanvas to your widget layout
 * 2. Call SetLotManager() to connect to a lot
 * 3. Call SetCurrentLevel() to change floors
 * 4. Call Refresh() after room detection updates
 *
 * Add your own buttons/labels around this widget in UMG.
 */
UCLASS()
class BURBARCHITECT_API URoomDebugCanvas : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set the lot manager to visualize */
	UFUNCTION(BlueprintCallable, Category = "Room Debug")
	void SetLotManager(ALotManager* InLotManager);

	/** Set the current level/floor to display */
	UFUNCTION(BlueprintCallable, Category = "Room Debug")
	void SetCurrentLevel(int32 InLevel);

	/** Get the current level being displayed */
	UFUNCTION(BlueprintPure, Category = "Room Debug")
	int32 GetCurrentLevel() const;

	/** Set whether to show room ID numbers on tiles */
	UFUNCTION(BlueprintCallable, Category = "Room Debug")
	void SetShowRoomNumbers(bool bShow);

	/** Get whether room numbers are currently shown */
	UFUNCTION(BlueprintPure, Category = "Room Debug")
	bool GetShowRoomNumbers() const;

	/** Refresh the visualization */
	UFUNCTION(BlueprintCallable, Category = "Room Debug")
	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SRoomDebugCanvas> SlateCanvas;

	UPROPERTY(Transient)
	ALotManager* CachedLotManager;

	UPROPERTY(Transient)
	int32 CachedCurrentLevel;

	UPROPERTY(Transient)
	bool bCachedShowRoomNumbers;
};

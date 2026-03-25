// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildPortalTool.h"
#include "BuildWindowTool.generated.h"

/**
 * Build Window Tool - Places window portals on walls with preview
 */
UCLASS()
class ABuildWindowTool : public ABuildPortalTool
{
	GENERATED_BODY()

public:
	ABuildWindowTool();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;
	virtual void Delete_Implementation() override;
	virtual void Destroyed() override;

protected:
	// Preview window actor shown during placement - Replicated so all clients see it
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Replicated)
	APortalBase* PreviewWindow;

	// Cached loaded class from ClassToSpawn
	UPROPERTY()
	TSubclassOf<APortalBase> ClassToPlace;

private:
	// Helper to spawn or update preview window
	void UpdatePreviewWindow();

	// Helper to destroy preview window
	void DestroyPreviewWindow();

	// Helper to register preview portal with walls for cutout rendering
	void RegisterPreviewWithWalls();

	// Helper to unregister preview portal from walls
	void UnregisterPreviewFromWalls();

	// Track previous validity state to detect transitions
	bool bPreviousValidPlacement = false;

	// Track which walls the preview is currently registered with
	TArray<int32> PreviewRegisteredWallIndices;
};

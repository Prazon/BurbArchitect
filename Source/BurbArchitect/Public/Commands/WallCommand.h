// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "Actors/LotManager.h"
#include "Components/WallComponent.h"
#include "WallCommand.generated.h"

/**
 * Operation mode for wall commands
 */
UENUM()
enum class EWallOperationMode : uint8
{
	Create UMETA(DisplayName = "Create"),
	Delete UMETA(DisplayName = "Delete")
};

/**
 * Wall Command - Handles creating and destroying wall segments
 */
UCLASS()
class UWallCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	/**
	 * Initialize the wall command for CREATE operation
	 * @param bDeferRoomGen If true, room detection happens but floor/ceiling generation is deferred
	 * @param bPoolWall If true, marks this wall as a pool wall
	 */
	void Initialize(ALotManager* Lot, int32 Level, const FVector& Start, const FVector& End, float Height, class UWallPattern* Pattern, UMaterialInstance* BaseMat, bool bDeferRoomGen = false, bool bPoolWall = false);

	/**
	 * Initialize the wall command for DELETE operation
	 */
	void InitializeDelete(ALotManager* Lot, const FWallSegmentData& WallToDelete);

	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

	// Public accessors for room detection
	FORCEINLINE int32 GetLevel() const { return Level; }
	FORCEINLINE FVector GetStartLoc() const { return StartLoc; }
	FORCEINLINE FVector GetEndLoc() const { return EndLoc; }
	FORCEINLINE int32 GetWallEdgeID() const { return WallEdgeID; }

private:
	/**
	 * Trigger an immediate cutaway update on the player pawn
	 * This ensures newly created/modified walls have cutaway applied instantly
	 */
	void TriggerCutawayUpdate();

protected:
	// Reference to the lot manager
	UPROPERTY()
	ALotManager* LotManager;

	// Operation mode (create or delete)
	UPROPERTY()
	EWallOperationMode OperationMode;

	// Level/floor for the wall
	UPROPERTY()
	int32 Level;

	// Wall start location
	UPROPERTY()
	FVector StartLoc;

	// Wall end location
	UPROPERTY()
	FVector EndLoc;

	// Wall height
	UPROPERTY()
	float WallHeight;

	// Wall material
	UPROPERTY()
	UMaterialInstance* Material;

	// Wall pattern (for texture application)
	UPROPERTY()
	class UWallPattern* Pattern;

	// Wall data created by this command (legacy rendering system)
	UPROPERTY()
	FWallSegmentData WallData;

	// Wall graph edge ID (new graph system)
	UPROPERTY()
	int32 WallEdgeID = -1;

	// Whether wall was successfully created/deleted
	bool bWallCreated;

	// If true, room detection happens but floor/ceiling generation is deferred (for roof walls)
	UPROPERTY()
	bool bDeferRoomGeneration = false;

	// If true, marks this wall as a pool wall
	UPROPERTY()
	bool bIsPoolWall = false;
};

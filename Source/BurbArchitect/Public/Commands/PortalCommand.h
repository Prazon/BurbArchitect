// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Commands/BuildCommand.h"
#include "Actors/LotManager.h"
#include "Actors/PortalBase.h"
#include "Components/WallComponent.h"
#include "PortalCommand.generated.h"

/**
 * Portal Command - Handles creating and destroying portals (doors/windows)
 */
UCLASS()
class UPortalCommand : public UBuildCommand
{
	GENERATED_BODY()

public:
	/**
	 * Initialize the portal command with parameters
	 */
	void Initialize(
		ALotManager* Lot,
		TSubclassOf<APortalBase> PortalClass,
		const FVector& Location,
		const FRotator& Rotation,
		const TArray<int32>& WallArrayIndices,
		const FVector2D& InPortalSize,
		const FVector2D& InPortalOffset,
		TSoftObjectPtr<UStaticMesh> InWindowMesh,
		TSoftObjectPtr<UStaticMesh> InDoorStaticMesh,
		TSoftObjectPtr<UStaticMesh> InDoorFrameMesh
	);

	virtual void Commit() override;
	virtual void Undo() override;
	virtual void Redo() override;
	virtual FString GetDescription() const override;
	virtual bool IsValid() const override;

protected:
	// Reference to the lot manager
	UPROPERTY()
	ALotManager* LotManager;

	// Portal class to spawn
	UPROPERTY()
	TSubclassOf<APortalBase> PortalClassToSpawn;

	// Portal spawn location
	UPROPERTY()
	FVector SpawnLocation;

	// Portal spawn rotation
	UPROPERTY()
	FRotator SpawnRotation;

	// Indices of all wall sections this portal is attached to
	UPROPERTY()
	TArray<int32> AffectedWallArrayIndices;

	// Reference to the spawned portal actor
	UPROPERTY()
	APortalBase* SpawnedPortal;

	// Portal size (width x height in cm)
	UPROPERTY()
	FVector2D PortalSize;

	// Portal position offset from wall placement point (X = horizontal, Y = vertical, in cm)
	UPROPERTY()
	FVector2D PortalOffset;

	// Static mesh for windows (set from WindowItem data asset)
	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> WindowMesh;

	// Static mesh for door panel (set from DoorItem data asset)
	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> DoorStaticMesh;

	// Static mesh for door frame (set from DoorItem data asset)
	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> DoorFrameMesh;

	// Whether portal was successfully created
	bool bPortalCreated;
};

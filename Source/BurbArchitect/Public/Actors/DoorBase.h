// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/PortalBase.h"
#include "DoorBase.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;

/**
 * Door portal actor with skeletal mesh for animation and static meshes for appearance
 * Modular system: Skeletal mesh provides animation skeleton (set in Blueprint)
 * Static meshes provide visual appearance (set from DoorItem data asset)
 * Static meshes attach to skeletal mesh bones for bone-driven animation
 */
UCLASS()
class BURBARCHITECT_API ADoorBase : public APortalBase
{
	GENERATED_BODY()

public:
	ADoorBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostNetInit() override;

protected:
	virtual void BeginPlay() override;
	virtual void ApplyReplicatedProperties() override;

public:
	/** Skeletal mesh component for animation skeleton (set in Blueprint child) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Door|Skeleton")
	USkeletalMeshComponent* DoorSkeletalMesh;

	/** Static mesh for door panel appearance (attached to skeletal mesh, set from data asset) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Door|Appearance")
	UStaticMeshComponent* DoorStaticMesh;

	/** Static mesh for door frame appearance (attached to skeletal mesh, set from data asset) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Door|Appearance")
	UStaticMeshComponent* DoorFrameMesh;

	// ==================== REPLICATED MESH ASSETS ====================

	/** Replicated door panel mesh asset - set this to replicate mesh to clients */
	UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_DoorMeshAsset, Category = "Door|Replication")
	TSoftObjectPtr<UStaticMesh> DoorMeshAsset;

	/** Replicated door frame mesh asset - set this to replicate mesh to clients */
	UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_DoorFrameMeshAsset, Category = "Door|Replication")
	TSoftObjectPtr<UStaticMesh> DoorFrameMeshAsset;

	/** Called when DoorMeshAsset replicates to clients */
	UFUNCTION()
	void OnRep_DoorMeshAsset();

	/** Called when DoorFrameMeshAsset replicates to clients */
	UFUNCTION()
	void OnRep_DoorFrameMeshAsset();

	/** Apply the door panel mesh asset to the DoorStaticMesh component */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void ApplyDoorMesh();

	/** Apply the door frame mesh asset to the DoorFrameMesh component */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void ApplyDoorFrameMesh();

	// ==================== DOOR STATE ====================

	/** Whether the door is currently open (replicated) */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IsOpen, Category = "Door")
	bool bIsOpen;

	/** Called when bIsOpen replicates to clients */
	UFUNCTION()
	void OnRep_IsOpen();

	/** Open the door (calls server RPC if not authority) */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void OpenDoor();

	/** Close the door (calls server RPC if not authority) */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void CloseDoor();

	/** Toggle door state (open/close) */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void ToggleDoor();

	/** Server RPC to open the door */
	UFUNCTION(Server, Reliable)
	void Server_OpenDoor();

	/** Server RPC to close the door */
	UFUNCTION(Server, Reliable)
	void Server_CloseDoor();

protected:
	/** Internal function to set door open state and play animation */
	void SetDoorOpen(bool bOpen);
};

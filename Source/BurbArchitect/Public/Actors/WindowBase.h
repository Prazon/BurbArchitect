// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/PortalBase.h"
#include "WindowBase.generated.h"

class UStaticMeshComponent;
class UStaticMesh;

/**
 * Window portal actor with static mesh component for window frame/glass
 * Extends PortalBase to add visual mesh representation
 */
UCLASS()
class BURBARCHITECT_API AWindowBase : public APortalBase
{
	GENERATED_BODY()

public:
	AWindowBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostNetInit() override;

protected:
	virtual void BeginPlay() override;
	virtual void ApplyReplicatedProperties() override;

public:
	/** Static mesh component for window frame and glass */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Window")
	UStaticMeshComponent* WindowMesh;

	/** Replicated mesh asset reference - set this to replicate mesh to clients */
	UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_WindowMeshAsset, Category = "Window|Replication")
	TSoftObjectPtr<UStaticMesh> WindowMeshAsset;

	/** Called when WindowMeshAsset replicates to clients */
	UFUNCTION()
	void OnRep_WindowMeshAsset();

	/** Apply the mesh asset to the WindowMesh component */
	UFUNCTION(BlueprintCallable, Category = "Window")
	void ApplyWindowMesh();
};

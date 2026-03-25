// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/PlaceableObject.h"
#include "Engine/StaticMeshActor.h"
#include "Interfaces/IDeletable.h"
#include "PortalBase.generated.h"

class UWallComponent;
class ALotManager;
class UPortalBoxComponent;

UCLASS()
class BURBARCHITECT_API APortalBase : public APlaceableObject, public IDeletable
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	APortalBase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Called after all initial replication properties have been received */
	virtual void PostNetInit() override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	/** Apply all replicated visual properties - called from OnRep and PostNetInit */
	virtual void ApplyReplicatedProperties();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	ALotManager* CurrentLot;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UWallComponent* CurrentWallComponent;

	UFUNCTION(BlueprintCallable)
	void DrawPortal(UCanvas* Canvas, FVector2D PositionOnTexture);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing = OnRep_PortalSize)
	FVector2D PortalSize;

	// Portal cutout offset on wall (X = horizontal, Y = vertical)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing = OnRep_PortalOffset)
	FVector2D PortalOffset;

	/** Called when PortalSize replicates to clients */
	UFUNCTION()
	void OnRep_PortalSize();

	/** Called when PortalOffset replicates to clients */
	UFUNCTION()
	void OnRep_PortalOffset();

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	UPortalBoxComponent* Box;

	/** Whether this portal is currently selected */
	UPROPERTY(BlueprintReadOnly, Category = "Selection")
	bool bIsSelected;

	/** Select this portal (enable selection highlight) */
	UFUNCTION(BlueprintCallable, Category = "Selection")
	void Select();

	/** Unselect this portal (disable selection highlight) */
	UFUNCTION(BlueprintCallable, Category = "Selection")
	void Unselect();

	/** Toggle selection state */
	UFUNCTION(BlueprintCallable, Category = "Selection")
	void ToggleSelection();

	// ==================== IDELETABLE INTERFACE ====================

	/** Check if this portal can currently be deleted */
	virtual bool CanBeDeleted_Implementation() const override;

	/** Called before deletion to clean up references */
	virtual void OnDeleted_Implementation() override;

	/** Check if this portal is currently selected */
	virtual bool IsSelected_Implementation() const override;

private:
	UPROPERTY(VisibleAnywhere)
	USceneComponent* RootSceneComponent;
};

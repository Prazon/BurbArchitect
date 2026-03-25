// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Actors/BuildTool.h"
#include "Components/BoxComponent.h"
#include "Components/RoofComponent.h"
#include "BuildRoofTool.generated.h"

/**
 *
 */
UCLASS()
class BURBARCHITECT_API ABuildRoofTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuildRoofTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the actor is being destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Override this in child classes to specify the roof type
	virtual ERoofType GetRoofType() const { return ERoofType::Gable; }

	// Get the roof actor class to spawn based on roof type
	TSubclassOf<class ARoofBase> GetRoofActorClass() const;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void CreateRoofPreview();

	UFUNCTION(BlueprintCallable)
	void CreateRoof();

	UFUNCTION(BlueprintCallable)
	void AdjustRoofPosition(const FVector& MoveLocation, const FHitResult& CursorWorldHitResult, int32 TracedLevel);

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	// Rotation functions - override from BuildTool base class
	virtual void RotateLeft_Implementation() override;
	virtual void RotateRight_Implementation() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	USceneComponent* SceneComponent;

	// Reference to the preview roof actor
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	class ARoofBase* PreviewRoofActor;

	// Reference to the placed roof actor (after commit)
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	class ARoofBase* PlacedRoofActor;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UMaterialInstance* DefaultRoofMaterial;

	UPROPERTY(BlueprintReadWrite)
	URoofComponent*	DragCreateRoof;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 HitMeshSection = -1;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 DefaultRoof = -1;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float RoofThickness = 15;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float GableThickness = 20;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float Height = 298;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float FrontUnits = 3.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float BackUnits = 3.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float RightUnits = 3.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float LeftUnits = 3.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float FrontRake = 50.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float BackRake = 50.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float RightEve = 50.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float LeftEve = 50.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float EveSnap = 80.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Wall Generation", Meta = (Bitmask, BitmaskEnum = "/Script/BurbArchitect.ERoofWallFlags"))
	int32 WallFlags = 0;
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 Level = 0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector RoofDirection;

	/** Roof actor class to spawn (set in Blueprint, defaults to C++ class if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Configuration")
	TSubclassOf<class ARoofBase> RoofActorClass;

private:
	// Debounce flag to prevent double execution of CreateRoof
	bool bIsCreatingRoof = false;
};
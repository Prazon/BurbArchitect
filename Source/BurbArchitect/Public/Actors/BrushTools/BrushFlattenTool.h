// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Components/DecalComponent.h"
#include "BrushFlattenTool.generated.h"

/**
 * Terrain flattening brush tool
 * Flattens terrain to a target height within a circular brush radius
 */
UCLASS()
class BURBARCHITECT_API ABrushFlattenTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABrushFlattenTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void ShapeTerrain();

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	UPROPERTY(VisibleAnywhere, Category = "Brush")
    UDecalComponent* BrushDecal;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UMaterialInstance* DefaultTerrainMaterial;

	// Brush radius in world units
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Flatten Tool")
	float Radius = 200.0f;

	// Timer delay between terrain shape updates (in seconds)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Flatten Tool")
	float TimerDelay = 0.1f;

	FTimerHandle TimerHandle;

	// Target height for flattening (captured on first click)
	// Sampled from terrain height at cursor position when brush is first activated
	float TargetFlattenHeight = 0.0f;
};

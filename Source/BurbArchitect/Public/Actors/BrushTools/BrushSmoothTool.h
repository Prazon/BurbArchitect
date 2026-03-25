// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Actors/BuildTool.h"
#include "Components/DecalComponent.h"
#include "BrushSmoothTool.generated.h"

/**
 * Terrain smoothing brush tool
 * Uses Laplacian smoothing to blend terrain heights toward neighbor averages
 */
UCLASS()
class BURBARCHITECT_API ABrushSmoothTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABrushSmoothTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void SmoothTerrain();

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	UPROPERTY(VisibleAnywhere, Category = "Brush")
    UDecalComponent* BrushDecal;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UMaterialInstance* DefaultTerrainMaterial;

	// Brush radius in world units
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Smooth Tool")
	float Radius = 500.0f;

	// Smoothing strength (0.0 = no effect, 1.0 = full average)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Smooth Tool", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothingStrength = 0.5f;

	// Timer delay between terrain smooth updates (in seconds)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Smooth Tool")
	float TimerDelay = 0.1f;

	FTimerHandle TimerHandle;
};

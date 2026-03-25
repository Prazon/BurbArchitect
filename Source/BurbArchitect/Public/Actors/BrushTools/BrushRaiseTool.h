// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Actors/BuildTool.h"
// #include "Components/FloorComponent.h"
#include "Components/DecalComponent.h"
#include "BrushRaiseTool.generated.h"

/**
 * 
 */
UCLASS()
class BURBARCHITECT_API ABrushRaiseTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABrushRaiseTool();

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
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	UPROPERTY(VisibleAnywhere, Category = "Brush")
    UDecalComponent* BrushDecal;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UMaterialInstance* DefaultTerrainMaterial;

	FTimerHandle TimerHandle;

	// Brush radius in world units
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Brush")
	float Radius;

	// Timer delay between terrain updates (in seconds)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Brush")
	float TimerDelay = 0.1f;

	// Number of segments for brush decal visualization
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Brush")
	int32 SegmentCount;
};

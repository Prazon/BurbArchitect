// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTools/BuildWallTool.h"
#include "Actors/BuildTool.h"
#include "Components/WallComponent.h"
#include "BuildDiagonalRoomTool.generated.h"

UCLASS()
class BURBARCHITECT_API ABuildDiagonalRoomTool : public ABuildWallTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuildDiagonalRoomTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Size of the diamond room in tiles (distance from center to each corner)
	// Example: DiamondRadius = 1 creates a small 3x3 diamond
	//          DiamondRadius = 2 creates a larger 5x5 diamond
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Diagonal Room Tool")
	int32 DiamondRadius = 1;

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

protected:
	// Helper to generate preview walls for the diamond shape from drag rectangle
	void GenerateDiamondPreviewFromDrag(const FVector& StartLocation, const FVector& EndLocation, int32 Level);

	// Helper to calculate the 4 corner positions of the diamond from a bounding rectangle
	void CalculateDiamondCornersFromRect(const FVector& MinCorner, const FVector& MaxCorner, FVector& OutNorth, FVector& OutEast, FVector& OutSouth, FVector& OutWest) const;

	// Helper to generate diagonal wall segments between two corners
	void GenerateDiagonalWallSegments(const FVector& StartCorner, const FVector& EndCorner, int32 Level, TArray<UWallComponent*>& OutWalls);
};

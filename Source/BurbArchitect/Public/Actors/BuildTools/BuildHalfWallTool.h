// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Components/WallComponent.h"
#include "BuildHalfWallTool.generated.h"

/**
 * Half Wall Tool - Creates decorative half-height walls
 *
 * IMPORTANT: Half walls are DECORATIVE ONLY and do NOT participate in room detection.
 * - Height: 50% of standard wall height (150 units vs 300 units)
 * - WallEdgeID: -1 (no WallGraphComponent edge created)
 * - Room Detection: These walls do NOT block rooms or create boundaries
 * - Use Case: Visual dividers, railings, decorative elements
 *
 * Unlike BuildWallTool which creates both WallGraph edges (for room detection) and
 * WallComponent rendering, this tool ONLY creates WallComponent rendering by directly
 * calling CommitWallSection() and bypassing BuildServer entirely.
 */
UCLASS()
class BURBARCHITECT_API ABuildHalfWallTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuildHalfWallTool();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void CreateWallPreview(int32 Level, int32 Index, FVector StartLocation, FVector Direction);

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	UPROPERTY(BlueprintReadWrite)
	UWallComponent* CurrentWallComponent;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Wall Tool")
	class UWallPattern* DefaultWallPattern;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Wall Tool")
	UMaterialInstance* BaseMaterial;

	UPROPERTY(BlueprintReadWrite)
	TArray<UWallComponent*>	DragCreateWallArray;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float WallHeight = 150;  // Half height - default full height is 300

protected:
	// Current traced level - updated during Move() and used by Drag()
	UPROPERTY()
	int32 CurrentTracedLevel = 0;

	// Cache for reusing preview wall components during drag
	// Key: encoded grid coordinates (StartRow << 48 | StartColumn << 32 | EndRow << 16 | EndColumn)
	TMap<int64, UWallComponent*> PreviewWallCache;

	// Helper function to clean up preview cache
	void ClearPreviewCache();

	// Helper to generate cache key from grid coordinates
	static int64 GenerateWallCacheKey(int32 StartRow, int32 StartColumn, int32 EndRow, int32 EndColumn);

	// Helper to snap drag direction to nearest axis-aligned or diagonal direction
	FVector SnapToAllowedDirection(const FVector& Direction) const;

	// Helper to constrain cursor position to snapped direction (updates dynamically)
	FVector ConstrainToLockedDirection(const FVector& StartPos, const FVector& CurrentPos) const;
};

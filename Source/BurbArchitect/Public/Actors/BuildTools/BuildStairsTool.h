// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Components/StairsComponent.h"
#include "Components/WallComponent.h"
#include "UObject/NoExportTypes.h"
#include "BuildStairsTool.generated.h"

/**
 * 
 */
UCLASS()
class BURBARCHITECT_API ABuildStairsTool : public ABuildTool
{
	GENERATED_BODY()

public:

	// Sets default values for this actor's properties
	ABuildStairsTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when actor is being destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	UFUNCTION(BlueprintCallable)
	void CreateStairsPreview();

	UFUNCTION(BlueprintCallable)
	void CreateStairs();

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	// Rotation during preview (rotates preview actor)
	virtual void RotateLeft_Implementation() override;
	virtual void RotateRight_Implementation() override;
	
	TArray<FStairModuleStructure> StairStructures;

	/** Number of stair treads to reach one level (12 treads = 300 unit DefaultWallHeight)
	 * IMPORTANT: Stair tread mesh socket offsets must provide 25 units vertical rise per step:
	 * - "Back_Socket" to "Front_Socket" offset should be approximately (25, 0, 25) for standard rise
	 * - Total vertical travel: TreadsCount * 25 = 300 units (one level)
	 */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int TreadsCount = 12;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int DefaultLandingIndex = 4;  // First valid landing position (first multiple of StepsPerSection=4)
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int LastLandingIndex = -1;

	// Blueprint class of stairs actor to spawn (allows custom stairs implementations)
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Stairs")
	TSubclassOf<class AStairsBase> StairsActorClass;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Stair Treads")
	UStaticMesh* StairTreadMesh;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Stair Treads")
	UStaticMesh* StairLandingMesh;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Stair Treads")
	FName StairTreadSocketRight = "None";
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Stair Treads")
	FName StairTreadSocketLeft = "None";
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Stair Treads")
	FName StairTreadPlugRight = "None";
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Stair Treads")
	FName StairTreadPlugLeft = "None";
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	USceneComponent* SceneComponent;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UMaterialInstance* DefaultStairsMaterial;

	UPROPERTY(BlueprintReadWrite)
	class AStairsBase* DragCreateStairs;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 HitMeshSection = -1;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 DefaultStairs = -1;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float StairsThickness = 15;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float Height = 300;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 Level = 0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector StairsDirection;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int TreadSize = 25;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int LandingSize = 95;
};

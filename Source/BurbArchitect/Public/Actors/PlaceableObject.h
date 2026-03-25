// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "PlaceableObject.generated.h"

/**
 * Object placement mode enum
 * Determines how the object snaps and validates placement
 */
UENUM(BlueprintType)
enum class EObjectPlacementMode : uint8
{
	Floor           UMETA(DisplayName = "Floor"),           // Standard floor placement (chairs, tables)
	WallHanging     UMETA(DisplayName = "Wall Hanging"),    // Snaps to wall surfaces (posters, shelves, wall lights)
	CeilingFixture  UMETA(DisplayName = "Ceiling Fixture"), // Snaps to ceiling (chandeliers, lights)
	Surface         UMETA(DisplayName = "Surface")          // Future: on surfaces (countertops, desks)
};

USTRUCT(BlueprintType)
struct FLotObjectSaveData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FTransform ObjectTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TSoftClassPtr<APlaceableObject> ObjectClass;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 Level;
};

UCLASS()
class BURBARCHITECT_API APlaceableObject : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	APlaceableObject();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The current floor this object resides on in its lot.
	UPROPERTY(BlueprintReadWrite, SaveGame, Replicated)
	int32 CurrentFloor;

	// Placement mode used when this object was placed (for serialization/editing)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Replicated, Category = "Placement")
	EObjectPlacementMode PlacementMode = EObjectPlacementMode::Floor;

	// Grid coordinates where this object was placed (for floor/ceiling objects)
	UPROPERTY(BlueprintReadWrite, SaveGame, Replicated, Category = "Placement")
	int32 PlacedRow = 0;

	UPROPERTY(BlueprintReadWrite, SaveGame, Replicated, Category = "Placement")
	int32 PlacedColumn = 0;

	// For wall-mounted objects: which wall normal this is attached to
	UPROPERTY(BlueprintReadWrite, SaveGame, Replicated, Category = "Placement")
	FVector WallNormal = FVector::ZeroVector;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};

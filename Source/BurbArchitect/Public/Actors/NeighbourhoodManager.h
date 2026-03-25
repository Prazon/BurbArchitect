// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/TerrainComponent.h"
#include "NeighbourhoodManager.generated.h"

// Forward declarations
class ALotManager;

UCLASS(Blueprintable)
class BURBARCHITECT_API ANeighbourhoodManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ANeighbourhoodManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void OnConstruction(const FTransform& Transform) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	// Editor button to generate neighbourhood terrain
	UFUNCTION(CallInEditor, Category = "Neighbourhood", meta = (DisplayName = "Generate Neighbourhood Terrain"))
	void GenerateNeighbourhoodTerrain();
#endif

	// Neighbourhood size in lot tiles (defines placement grid)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neighbourhood", meta = (ClampMin = "50", ClampMax = "2048"))
	int32 NeighbourhoodSizeX = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neighbourhood", meta = (ClampMin = "50", ClampMax = "2048"))
	int32 NeighbourhoodSizeY = 512;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Neighbourhood")
	float LotTileSize = 100.0f;

	// Neighbourhood terrain material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neighbourhood")
	UMaterialInstance* NeighbourhoodTerrainMaterial;

	// Neighbourhood terrain component (1:1 tile scale, same as lots)
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Neighbourhood")
	class UTerrainComponent* TerrainComponent;

	// All lots placed in this neighbourhood
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Neighbourhood")
	TArray<ALotManager*> PlacedLots;

	// Tile occupancy map (lot tile coordinates)
	// Key = packed tile coords (Row << 16 | Col), Value = LotManager pointer
	UPROPERTY(Transient)
	TMap<int32, ALotManager*> TileClaimMap;

	// Lot claiming
	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	bool ClaimTiles(ALotManager* Lot, int32 Row, int32 Column, int32 SizeX, int32 SizeY);

	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	void ReleaseTiles(ALotManager* Lot);

	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	bool CanPlaceLot(int32 Row, int32 Column, int32 SizeX, int32 SizeY) const;

	// Terrain hole punching
	void CreateTerrainHole(int32 Row, int32 Column, int32 SizeX, int32 SizeY);
	void FillTerrainHole(int32 Row, int32 Column, int32 SizeX, int32 SizeY);

	// Get terrain height (for lot edge stitching)
	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	float GetTerrainHeightAt(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	float GetTerrainHeightAtTile(int32 Row, int32 Column) const;

	// Coordinate conversions
	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	FVector NeighbourhoodTileToWorld(int32 Row, int32 Column) const;

	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	bool WorldToNeighbourhoodTile(FVector Location, int32& OutRow, int32& OutColumn) const;

	// Spatial queries
	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	ALotManager* GetLotAtTile(int32 Row, int32 Column) const;

	UFUNCTION(BlueprintCallable, Category = "Neighbourhood")
	bool IsTileClaimed(int32 Row, int32 Column) const;
};

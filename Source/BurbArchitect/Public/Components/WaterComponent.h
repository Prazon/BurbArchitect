// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "WaterComponent.generated.h"

/**
 * Data structure for a pool water volume
 * One per pool room - uses room polygon for shape, creates 3D volume
 */
USTRUCT(BlueprintType)
struct FPoolWaterData
{
	GENERATED_BODY()

	// Room ID this pool water belongs to
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	int32 RoomID = -1;

	// Mesh section index in WaterComponent
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	int32 SectionIndex = -1;

	// Floor level of the pool (basement level)
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	int32 Level = 0;

	// Polygon vertices defining pool boundary (from FRoomData::BoundaryVertices)
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	TArray<FVector> BoundaryVertices;

	// Water surface height (ground level floor Z - where you'd walk)
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	float WaterSurfaceZ = 0.0f;

	// Pool floor height (basement/pool level floor Z)
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	float PoolFloorZ = 0.0f;

	// Material instance for this pool's water
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	UMaterialInstanceDynamic* WaterMaterial = nullptr;

	// Is this pool water committed/visible
	UPROPERTY(BlueprintReadWrite, Category = "Pool Water")
	bool bCommitted = false;

	// Constructor
	FPoolWaterData()
	{
		RoomID = -1;
		SectionIndex = -1;
		Level = 0;
		WaterSurfaceZ = 0.0f;
		PoolFloorZ = 0.0f;
		WaterMaterial = nullptr;
		bCommitted = false;
	}

	// Equality operator (by RoomID)
	bool operator==(const FPoolWaterData& Other) const
	{
		return RoomID == Other.RoomID;
	}

	bool operator!=(const FPoolWaterData& Other) const
	{
		return !(*this == Other);
	}
};

/**
 * Water Component - Manages pool water volumes
 * Uses polygon-based triangulation like floors
 * One mesh section per pool with top surface + side walls
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UWaterComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UWaterComponent(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	// ========== Pool Water Management ==========

	/**
	 * Generate a 3D water volume for a pool room
	 * Creates top surface + side walls using room polygon boundary
	 * @param RoomID - Room identifier for this pool
	 * @param BoundaryVertices - Polygon vertices defining pool boundary (from FRoomData)
	 * @param WaterSurfaceZ - Z height of water surface (ground level)
	 * @param PoolFloorZ - Z height of pool floor (basement level)
	 * @param BaseMaterial - Base material to create dynamic instance from
	 * @return Generated pool water data
	 */
	UFUNCTION(BlueprintCallable, Category = "Water")
	FPoolWaterData GeneratePoolWater(int32 RoomID, const TArray<FVector>& BoundaryVertices,
	                                  float WaterSurfaceZ, float PoolFloorZ,
	                                  UMaterialInstance* BaseMaterial);

	/**
	 * Update an existing pool water volume (e.g., when room polygon changes)
	 * @param RoomID - Room identifier to update
	 * @param BoundaryVertices - New polygon vertices
	 * @return True if pool was found and updated
	 */
	UFUNCTION(BlueprintCallable, Category = "Water")
	bool UpdatePoolWater(int32 RoomID, const TArray<FVector>& BoundaryVertices);

	/**
	 * Remove pool water by room ID
	 * @param RoomID - Room identifier to remove
	 * @return True if pool was found and removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Water")
	bool RemovePoolWater(int32 RoomID);

	/**
	 * Find pool water data by room ID (O(1) lookup)
	 * @param RoomID - Room identifier to find
	 * @return Pointer to pool data, or nullptr if not found
	 */
	FPoolWaterData* FindPoolWater(int32 RoomID);

	/**
	 * Check if a pool water volume exists for a room
	 * @param RoomID - Room identifier to check
	 * @return True if pool water exists for this room
	 */
	UFUNCTION(BlueprintCallable, Category = "Water")
	bool HasPoolWater(int32 RoomID) const;

	/**
	 * Destroy all pool water volumes
	 */
	UFUNCTION(BlueprintCallable, Category = "Water")
	void DestroyAllWater();

	// ========== Data Storage ==========

	// Array of all pool water volumes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	TArray<FPoolWaterData> PoolWaterArray;

	// O(1) lookup: RoomID -> index in PoolWaterArray
	TMap<int32, int32> PoolWaterMap;

	// Default water material (applied if none specified)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	UMaterialInstance* DefaultWaterMaterial;

	// Enable collision for water surfaces
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bCreateCollision = false;

private:
	/**
	 * Generate the 3D mesh for a pool water volume
	 * Creates top surface triangles + side wall quads
	 * @param PoolData - Pool data to generate mesh for
	 */
	void GeneratePoolWaterMesh(FPoolWaterData& PoolData);

	/**
	 * Triangulate a polygon using ear clipping algorithm
	 * @param Vertices - 2D polygon vertices (Z ignored for triangulation)
	 * @return Triangle indices (e.g., [0,1,2, 0,2,3, ...])
	 */
	TArray<int32> TriangulatePolygon(const TArray<FVector>& Vertices);

	/**
	 * Check if a vertex is an "ear" (can be clipped)
	 * @param Vertices - Polygon vertices
	 * @param Indices - Current vertex indices
	 * @param EarIndex - Index to check in Indices array
	 * @return True if this vertex forms a valid ear
	 */
	bool IsEar(const TArray<FVector>& Vertices, const TArray<int32>& Indices, int32 EarIndex);

	/**
	 * Check if a point is inside a triangle (2D, ignoring Z)
	 */
	static bool IsPointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C);

	// Next available section index for pool water
	int32 NextPoolSectionIndex = 0;
};

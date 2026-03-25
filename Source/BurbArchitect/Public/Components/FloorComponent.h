// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Components/WallComponent.h"
#include "Components/ActorComponent.h"
#include "FloorComponent.generated.h"

class UFloorPattern;

//Floor Corner States
UENUM(BlueprintType)
enum class ECornerType : uint8
{
	TopRight = 0,
	TopLeft,
	BottomRight,
	BottomLeft,
};

//Floor Triangle Type - each tile can have 4 independent triangles
UENUM(BlueprintType)
enum class ETriangleType : uint8
{
	Top = 0,
	Right = 1,
	Bottom = 2,
	Left = 3,
	None = 255
};

//Floor Section States
USTRUCT(BlueprintType)
struct FTileSectionState
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool Top;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool Bottom;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool Right;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool Left;

	//Constructor
	FTileSectionState()
	{
		Top = true;
		Bottom = true;
		Right = true;
		Left = true;
	}
	
	void AddCorner(const ECornerType Corner)
	{
		switch (Corner)
		{
			case ECornerType::TopRight:
				Right = false; Top = false;
				break;
			case ECornerType::TopLeft:
				Left = false; Top = false;
				break;
			case ECornerType::BottomRight:
				Right = false; Bottom = false;
				break;
			case ECornerType::BottomLeft:
				Left = false; Bottom = false;
				break;
		}
	}
};

//Lightweight floor triangle data - each triangle is independent with its own pattern
//NOTE: "FloorTileData" name kept for backward compatibility, but now represents a single triangle
USTRUCT(BlueprintType)
struct FFloorTileData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Floor Data")
	int32 Row = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Floor Data")
	int32 Column = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Floor Data")
	int32 Level = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	ETriangleType Triangle = ETriangleType::Top;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	UFloorPattern* Pattern = nullptr;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	int32 SwatchIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	FTileSectionState TileSectionState;  // DEPRECATED: Kept for backward compatibility, ignored in new system

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	bool bCommitted = false;

	// Whether this floor tile is part of a pool (similar to bIsPoolWall on FWallEdge)
	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	bool bIsPool = false;

	// Whether this floor tile is a pool edge tile (cement/concrete around pool perimeter)
	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	bool bIsPoolEdge = false;

	// Constructor
	FFloorTileData()
	{
		Row = -1;
		Column = -1;
		Level = 0;
		Triangle = ETriangleType::Top;
		Pattern = nullptr;
		SwatchIndex = 0;
		bCommitted = false;
		bIsPool = false;
		bIsPoolEdge = false;
	}

	// Comparison operators (includes Triangle for per-triangle storage system)
	bool operator==(const FFloorTileData& Other) const
	{
		return Row == Other.Row && Column == Other.Column &&
		       Level == Other.Level && Triangle == Other.Triangle;
	}

	bool operator!=(const FFloorTileData& Other) const
	{
		return !(*this == Other);
	}
};

//Definition of a single floor segment (legacy - will be phased out)
USTRUCT(BlueprintType)
struct FFloorSegmentData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Floor Data")
	int32 Level = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	int32 ArrayIndex = -1;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	int32 SectionIndex = -1;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	int32 Row = -1;

	UPROPERTY(BlueprintReadWrite, Category = "Floor Data")
	int32 Column = -1;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Floor Data")
	FVector StartLoc;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Floor Data")
	TArray<FVector> CornerLocs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Data")
	float Thickness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Data")
	float Width;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Data")
	bool bCommitted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Data")
	FTileSectionState TileSectionState;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Data")
	TArray<FVector> Vertices;
	
	//Floor Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material")
	UMaterialInstanceDynamic* FloorMaterial;

	//Applied Floor Pattern (tracks which pattern was applied to this floor)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Data")
	UFloorPattern* AppliedPattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material")
	bool bTop = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material")
	bool bBottom = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material")
	bool bRight = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material")
	bool bLeft = true;
	
	//Constructor
	FFloorSegmentData()
	{
		ArrayIndex = -1;
		SectionIndex = -1;
		bCommitted = false;
		StartLoc = FVector(0,0,0);
		Thickness = 10;
		FloorMaterial = nullptr;
		AppliedPattern = nullptr;
	}
	
	//Compare
	bool operator==(const FFloorSegmentData & other) const
	{
		return (other.StartLoc == StartLoc && other.ArrayIndex == ArrayIndex && other.SectionIndex == SectionIndex);
	}
	//Compare
	bool operator!=(const FFloorSegmentData & other) const
	{
		return (other.StartLoc != StartLoc && other.ArrayIndex != ArrayIndex && other.SectionIndex != SectionIndex);
	}
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UFloorComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

protected:
	
	UFloorComponent(const FObjectInitializer& ObjectInitializer);
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable)
	int32 GetSectionIDFromHitResult(const FHitResult& HitResult) const;
	
	UFUNCTION(BlueprintCallable)
	void ConstructFloor();
	
	UFUNCTION(BlueprintCallable)
	void DestroyFloor();
	
	UFUNCTION(BlueprintCallable)
	void DestroyFloorSection(FFloorSegmentData& FloorSection);
		
	UFUNCTION(BlueprintCallable)
	void RemoveFloorSection(int FloorArrayIndex);
	
	UFUNCTION(BlueprintCallable)
	void CommitFloor(UMaterialInstance* DefaultFloorMaterial, bool Top, bool Bottom, bool Right, bool Left);

	UFUNCTION(BlueprintCallable)
	void CommitFloorSection(FFloorSegmentData InFloorData, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial);
	
	UFUNCTION(BlueprintCallable)
	bool FindExistingFloorSection(int32 Level, const FVector& TileCenter, FFloorSegmentData& OutFloor);

	UFUNCTION(BlueprintCallable)
	void UpdateFloorSection(FFloorSegmentData& InFloorData, const FTileSectionState& NewTileSectionState, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial);

	UFUNCTION(BlueprintCallable)
	void GenerateFloorMesh();

	//UFUNCTION(BlueprintCallable)
	FFloorSegmentData GenerateFloorSection(int32 Level, const FVector& TileCenter, UMaterialInstance* OptionalMaterial, const FTileSectionState& TileSectionState);

	UFUNCTION(BlueprintCallable)
	FFloorSegmentData GenerateFloorMeshSection(FFloorSegmentData InFloorData, UMaterialInstance* OptionalMaterial);

	// Merged mesh system functions
	// Helper to create unique spatial key from grid coordinates + triangle
	static inline int32 MakeGridKey(int32 Level, int32 Row, int32 Column, ETriangleType Triangle)
	{
		// Pack into 32-bit key: 8 bits level (0-255), 10 bits row (0-1023), 10 bits column (0-1023), 2 bits triangle (0-3)
		// Triangle: 0=Top, 1=Right, 2=Bottom, 3=Left
		return (Level << 24) | ((Row & 0x3FF) << 14) | ((Column & 0x3FF) << 4) | (static_cast<uint8>(Triangle) & 0x3);
	}

	// Legacy key maker for backward compatibility (defaults to Top triangle)
	static inline int32 MakeGridKey(int32 Level, int32 Row, int32 Column)
	{
		return MakeGridKey(Level, Row, Column, ETriangleType::Top);
	}

	// Find floor triangle at grid coordinates (O(1) lookup)
	FFloorTileData* FindFloorTile(int32 Level, int32 Row, int32 Column, ETriangleType Triangle) const;

	// Legacy finder for backward compatibility (finds Top triangle)
	FFloorTileData* FindFloorTile(int32 Level, int32 Row, int32 Column) const;

	// Add floor triangle to spatial map
	void AddFloorTile(const FFloorTileData& TileData, UMaterialInstance* Material = nullptr);

	// Remove floor triangle from spatial map
	void RemoveFloorTile(int32 Level, int32 Row, int32 Column, ETriangleType Triangle);

	// Legacy remover for backward compatibility (removes Top triangle)
	void RemoveFloorTile(int32 Level, int32 Row, int32 Column);

	// Check if ANY triangle exists at this tile location (checks all 4 triangles)
	bool HasAnyFloorTile(int32 Level, int32 Row, int32 Column) const;

	// Get which triangles exist at this tile location (returns TileSectionState)
	FTileSectionState GetExistingTriangles(int32 Level, int32 Row, int32 Column) const;

	// Get all triangle data at this tile location (returns array of pointers to existing triangles)
	TArray<FFloorTileData*> GetAllTrianglesAtTile(int32 Level, int32 Row, int32 Column);

	// Mark level as needing mesh rebuild
	void MarkLevelDirty(int32 Level);

	// Begin batch operation (suppresses invalidation)
	UFUNCTION(BlueprintCallable)
	void BeginBatchOperation();

	// End batch operation (rebuilds all dirty levels)
	UFUNCTION(BlueprintCallable)
	void EndBatchOperation();

	// Rebuild merged mesh for a specific level
	void RebuildLevel(int32 Level);

	// Calculate filled mask for a tile to determine which neighbors exist
	// Returns bitmask: Bit 0=Top, Bit 1=Right, Bit 2=Bottom, Bit 3=Left
	uint8 CalculateFilledMask(int32 Row, int32 Column, int32 Level) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFloorSegmentData FloorData;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FFloorSegmentData> FloorDataArray;

	//Wall Free Indices to Reuse
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int> FloorFreeIndices;

	// New merged mesh system data structures
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FFloorTileData> FloorTileDataArray;

	// O(1) spatial lookup: Key = (Level << 24) | (Row << 12) | Column
	// Maps grid coordinates to index in FloorTileDataArray
	TMap<int32, int32> FloorSpatialMap;

	// Track which levels need mesh rebuild
	TSet<int32> DirtyLevels;

	// Material per level for merged mesh system
	TMap<int32, UMaterialInstance*> LevelMaterials;

	// Track which mesh sections belong to which level
	// Key: Level index, Value: Array of section indices used by that level
	TMap<int32, TArray<int32>> LevelToSectionIndices;

	// Next available section index (increments sequentially to avoid gaps)
	int32 NextSectionIndex = 0;

	// Cache material instances per section to preserve parameters (grid state, visibility) across rebuilds
	// Key: Section index, Value: Material instance for that section
	TMap<int32, UMaterialInstanceDynamic*> SectionMaterialCache;

	// Batch operation flag - suppresses invalidation until EndBatchOperation()
	bool bInBatchOperation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bValidPlacement;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCommitted;

	//Floor Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material", Meta = (ExposeOnSpawn = true))
	UMaterialInstance* FloorMaterial;

	// Floor thickness configuration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Configuration")
	float FloorThickness = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Configuration")
	float CeilingFloorThickness = 10.0f;

	// Z-offset for ceiling floor top surface to prevent z-fighting with wall tops/terrain
	// Set to 0.1 to raise floor slightly above wall tops (prevents flickering)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Configuration")
	float FloorTopOffset = 0.1f;

	// Mesh information
	bool bGlobalMeshDataUpdated = false;
	bool bTop = true;
	bool bBottom = true;
	bool bRight = true;
	bool bLeft = true;
	bool bCreateCollision = true;

private:
	// Cache of shared material instances to reduce shader compilation overhead
	// Key: base material pointer, Value: shared dynamic material instance
	TMap<UMaterialInstance*, UMaterialInstanceDynamic*> SharedMaterialInstances;

	// Helper to get or create a shared material instance
	UMaterialInstanceDynamic* GetOrCreateSharedMaterialInstance(UMaterialInstance* BaseMaterial);

	// Triangulate a polygon using ear clipping algorithm
	// Returns triangle indices (e.g., [0,1,2, 0,2,3, ...])
	TArray<int32> TriangulatePolygon(const TArray<FVector>& Vertices);
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Components/ActorComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "RoofComponent.generated.h"

class ALotManager;

// Forward declarations
class UWallComponent;
class UWallPattern;
struct FWallSegmentData;

UENUM(BlueprintType)
enum class ERoofType : uint8
{
	Gable    UMETA(DisplayName = "Gable Roof"),
	Hip      UMETA(DisplayName = "Hip Roof"),
	Shed     UMETA(DisplayName = "Shed Roof")
};

UENUM(BlueprintType, Meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ERoofWallFlags : uint8
{
	None = 0 UMETA(Hidden),
	Front = 1 << 0 UMETA(DisplayName = "Front Wall"),
	Back = 1 << 1 UMETA(DisplayName = "Back Wall"),
	Left = 1 << 2 UMETA(DisplayName = "Left Wall"),
	Right = 1 << 3 UMETA(DisplayName = "Right Wall"),
};
ENUM_CLASS_FLAGS(ERoofWallFlags);

UENUM(BlueprintType)
enum class EScaleToolType : uint8
{
	Peak,
	Edge,
	FrontRake,
	BackRake,
	RightEve,
	LeftEve,
	FrontWall,
	BackWall,
	LeftWall,
	RightWall,
	Submit,
	Empty  // This helps in managing the size of the array
};

//Definition of the
USTRUCT(BlueprintType)
struct FRoofDimensions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Type")
	ERoofType RoofType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Type", Meta = (Bitmask, BitmaskEnum = "/Script/BurbArchitect.ERoofWallFlags"))
	int32 WallFlags;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float FrontDistance;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float BackDistance;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float RightDistance;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float LeftDistance;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float FrontRake;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float BackRake;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float RightEve;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float LeftEve;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Dimensions")
	float Height;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Dimensions")
	float Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof Dimensions")
	bool bUsePitchInsteadOfHeight;

	// Calculated properties
	float GetWidth() const { return LeftDistance + RightDistance; }
	float GetLength() const { return FrontDistance + BackDistance; }
	
	// Default constructor
	FRoofDimensions()
		: RoofType(ERoofType::Gable), WallFlags(0), FrontDistance(0.0f), BackDistance(0.0f), RightDistance(0.0f), LeftDistance(0.0f), FrontRake(0.0f), BackRake(0.0f), RightEve(0), LeftEve(0), Height(0.0f), Pitch(30.0f), bUsePitchInsteadOfHeight(false)
	{
	}

	//Constructor
	FRoofDimensions(float InFront, float InBack, float InRight, float InLeft, float InFrontRake, float InBackRake, float InRightEve, float InLeftEve,float InHeight) :
	RoofType(ERoofType::Gable), // Initialize to default, will be overridden by tool
	WallFlags(0),
	FrontDistance(InFront), BackDistance(InBack), RightDistance(InRight), LeftDistance(InLeft),
	FrontRake(InFrontRake), BackRake(InBackRake), RightEve(InRightEve), LeftEve(InLeftEve), Height(InHeight),
	Pitch(30.0f), bUsePitchInsteadOfHeight(false)
	{
	}

	//Compare ==
	bool operator==(const FRoofDimensions & other) const
	{
		return (other.FrontDistance == FrontDistance && other.BackDistance == BackDistance  && other.RightDistance == RightDistance
			&& other.LeftDistance == LeftDistance && other.FrontRake == FrontRake && other.BackRake == BackRake && other.Height == Height);
	}
	
	//Compare !=
	bool operator!=(const FRoofDimensions & other) const
	{
		return (other.FrontDistance != FrontDistance && other.BackDistance != BackDistance  && other.RightDistance != RightDistance
			&& other.LeftDistance != LeftDistance && other.FrontRake != FrontRake && other.BackRake != BackRake && other.Height != Height);
	}
};

//Definition of a single Roof segment
USTRUCT(BlueprintType)
struct FRoofSegmentData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Roof Data")
	int32 RoofArrayIndex = -1;
	
	UPROPERTY(BlueprintReadWrite, Category = "Roof Data")
	int32 SectionIndex;

	UPROPERTY(BlueprintReadWrite, Category = "Roof Data")
	int32 Level;	

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Data")
	FVector Location;
	
	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Data")
	FVector Direction;
	
	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Roof Data")
	FRoofDimensions Dimensions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Data")
	float RoofThickness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Data")
	float GableThickness;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Data")
	TArray<FVector> Vertices;
	
	//Roof Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UMaterialInstanceDynamic* RoofMaterial;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCommitted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCutOut;

	// Reference to LotManager for wall generation (not serialized)
	UPROPERTY(Transient)
	ALotManager* LotManager;

	// Indices of walls created by this roof for cleanup on undo/delete (not serialized)
	UPROPERTY(Transient)
	TArray<int32> CreatedWallIndices;

	//Constructor
	FRoofSegmentData(): Dimensions(0,0,0,0,0,0,0,0,0), RoofMaterial(nullptr), LotManager(nullptr)
	{
		RoofArrayIndex = -1;
		SectionIndex = -1;
		Level = 0;
		bCommitted = false;
		bCutOut = false;
		Location = FVector(0, 0, 0);
		Dimensions = FRoofDimensions(0,0,0,0,0,0,0,0,0);
		RoofThickness = 15;
		GableThickness = 20;
	}

	//Compare only X,Y and Level for locations
	bool operator==(const FRoofSegmentData & other) const
	{
		return ((other.Location == Location && other.Dimensions == Dimensions)
			&& other.Level == Level && other.RoofArrayIndex == RoofArrayIndex && other.SectionIndex == SectionIndex);
	}
	
	//Compare only X,Y and Level for locations
	bool operator!=(const FRoofSegmentData & other) const
	{
		return ((other.Location != Location && other.Dimensions == Dimensions)
			&& other.Level != Level && other.RoofArrayIndex != RoofArrayIndex && other.SectionIndex != SectionIndex);
	}
};

struct FRoofVertices
{
	FVector GablePeakFront;
	FVector GablePeakBack;
	FVector GableFrontRight;
	FVector GableBackRight;
	FVector GableFrontLeft;
	FVector GableBackLeft;
	
	FVector PeakFront;
	FVector PeakBack;
	FVector FrontRight;
	FVector BackRight;
	FVector FrontLeft;
	FVector BackLeft;
	
	FVector RightSlopeDirection;
	FVector LeftSlopeDirection;
};

/**
 * Base Roof Component for managing procedural roof geometry
 *
 * Architecture Notes:
 * - This base class can manage multiple roof segments via RoofDataArray (legacy multi-roof support)
 * - For single-instance roofs, use specialized child classes:
 *   - UGableRoofComponent: Two slopes with triangular gable ends
 *   - UHipRoofComponent: Four slopes converging to ridge/peak
 *   - UShedRoofComponent: Single sloped surface/ half gabled roof
 * - Child classes override GenerateRoofMeshSection() to call their specific generation method
 * - One component instance per placed roof provides better organization and performance
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API URoofComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

protected:

	URoofComponent(const FObjectInitializer& ObjectInitializer);
	
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable)
	void RegenerateRoofSection(FRoofSegmentData InRoofData, bool bRecursive);

	UFUNCTION(BlueprintCallable)
	int32 GetSectionIDFromHitResult(const FHitResult& HitResult) const;
	
	UFUNCTION(BlueprintCallable)
	void DestroyRoof();

	UFUNCTION(BlueprintCallable)
	void CommitRoofSection(FRoofSegmentData InRoofData, UMaterialInstance* DefaultRoofMaterial);

	UFUNCTION(BlueprintCallable)
	bool FindExistingRoofSection(const FVector& TileCornerStart, const FRoofDimensions& RoofDimensions, FRoofSegmentData& OutRoof);
	
	UFUNCTION(BlueprintCallable)
	FRoofSegmentData GenerateRoofSection(const FVector& Location, const FVector& Direction, const FRoofDimensions& RoofDimensions, const float RoofThickness, const float GableThickness);

	// Simplified interface for single-instance roof components
	UFUNCTION(BlueprintCallable)
	void UpdateSingleRoof(const FVector& Location, const FVector& Direction, const FRoofDimensions& RoofDimensions, const float RoofThickness = 15.0f, const float GableThickness = 20.0f);

	UFUNCTION(BlueprintCallable)
	void DestroyRoofSection(FRoofSegmentData InRoofData);

	UFUNCTION(BlueprintCallable)
	void RemoveRoofSection(int RoofArrayIndex);

	UFUNCTION(BlueprintCallable)
	bool SetRoofDataByIndex(int32 Index, FRoofSegmentData NewRoofData);

	UFUNCTION(BlueprintCallable)
	FRoofSegmentData& GetRoofDataByIndex(int32 RoofIndex);

	void GenerateRoofMesh();

	// Virtual method for child classes to override with type-specific generation
	virtual FRoofSegmentData GenerateRoofMeshSection(FRoofSegmentData InRoofData);

	// Type-specific roof generation methods (protected for child class access)
	FRoofSegmentData GenerateGableRoofMesh(FRoofSegmentData InRoofData);
	FRoofSegmentData GenerateHipRoofMesh(FRoofSegmentData InRoofData);
	FRoofSegmentData GenerateShedRoofMesh(FRoofSegmentData InRoofData);

	// UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	// void RegenerateEverySection();

	FVector FindPerpendicularVector(FVector& Direction);

	static FRoofVertices CalculateRoofVertices(const FVector& Location, const FVector& FrontDirection, const FRoofDimensions& Dimensions);

	// Helper methods for pitch/height calculations
	static float CalculateHeightFromPitch(float Pitch, float Width);
	static float CalculatePitchFromHeight(float Height, float Width);

	// Helper methods for wall vertex trimming to roof slope
	static float CalculateRoofHeightAtPosition(const FVector2D& Position, const FRoofVertices& RoofVerts, ERoofType RoofType);
	void TrimCommittedWallToRoof(ALotManager* InLotManager, int32 WallArrayIndex, const FRoofVertices& RoofVerts, ERoofType RoofType, float BaseZ);

	// Wall generation methods for auto-creating supporting walls
	void GeneratePerimeterWalls(ALotManager* InLotManager, const FRoofVertices& RoofVerts, const FVector& BaseLocation, int32 Level, ERoofType RoofType, UWallPattern* WallPattern, TArray<int32>& OutCreatedWallIndices);
	void GenerateGableEndWalls(ALotManager* InLotManager, const FRoofVertices& RoofVerts, const FVector& BaseLocation, int32 Level, float GableThickness, const FRoofDimensions& RoofDimensions, UWallPattern* WallPattern, TArray<int32>& OutCreatedWallIndices);
	void GenerateShedEndWalls(ALotManager* InLotManager, const FRoofVertices& RoofVerts, const FVector& BaseLocation, int32 Level, const FRoofDimensions& RoofDimensions, UWallPattern* WallPattern, TArray<int32>& OutCreatedWallIndices);

	//Roof System informations
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FRoofSegmentData RoofData;

	//Roof System informations
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FRoofSegmentData> RoofDataArray;
	
	//Wall Free Indices to Reuse
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int> RoofFreeIndices;

	//If the Roof is valid place
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bValidPlacement;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCommitted;
	
	//Roof Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material", Meta = (ExposeOnSpawn = true))
	UMaterialInstance* RoofMaterial;

	// Reference to LotManager for wall generation (not serialized)
	UPROPERTY(Transient)
	ALotManager* LotManager;

private:

	bool bCreateCollision = true;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "UObject/NoExportTypes.h"
#include "StairsComponent.generated.h"

class ALotManager;

UENUM(BlueprintType)
enum class EScaleStairsToolType : uint8
{
	StartingStep,
	EndingStep,
	SelectionAdjustment,
	Empty
};

UENUM(BlueprintType)
enum class EStairModuleType : uint8
{
	Tread           UMETA(DisplayName = "Tread"),
	Landing         UMETA(DisplayName = "Landing")
};

UENUM(BlueprintType)
enum class ETurningSocket : uint8
{
	Idle   = 0      UMETA(DisplayName = "Idle"),
	Right  = 1     UMETA(DisplayName = "Right"),
	Left   = 2      UMETA(DisplayName = "Left")
};

//Definition of the 
USTRUCT(BlueprintType)
struct FStairModuleStructure
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Stairs Structure")
	EStairModuleType StairType;
	
	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Stairs Structure")
	ETurningSocket TurningSocket;

	float GetTurningSocketValue() const
	{
		switch (TurningSocket)
		{
		case ETurningSocket::Right:
			return -1.0f;
		case ETurningSocket::Left:
			return 1.0f;
		case ETurningSocket::Idle:
		default:
			return 0.0f;
		}
	}

	void SetTurningSocketValue(const float InTurningSocket)
	{
		if (InTurningSocket == -1.0f)
		{
			TurningSocket = ETurningSocket::Right;
		}
		else
		{
			TurningSocket = ETurningSocket::Left;
		}
	}
	
	// Default constructor
	FStairModuleStructure()
		: StairType(EStairModuleType::Tread), TurningSocket(ETurningSocket::Idle)
	{
	}

	// Parameterized constructor
	FStairModuleStructure(EStairModuleType InModuleType, ETurningSocket InTurningSocket)
		: StairType(InModuleType), TurningSocket(InTurningSocket)
	{
	}

	bool operator==(const FStairModuleStructure & other) const
	{
		return (other.StairType == StairType && other.TurningSocket == TurningSocket);
	}

	bool operator!=(const FStairModuleStructure & other) const
	{
		return (other.StairType != StairType || other.TurningSocket != TurningSocket);
	}
};

//Definition of a single Stairs segment
USTRUCT(BlueprintType)
struct FStairsOffsets
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Stairs Data")
	FVector OffsetTread;
	
	UPROPERTY(BlueprintReadWrite, Category = "Stairs Data")
	FVector OffsetTreadLanding;
	
	UPROPERTY(BlueprintReadWrite, Category = "Stairs Data")
	FVector OffsetLandingTreadRight;
	
	UPROPERTY(BlueprintReadWrite, Category = "Stairs Data")
	FVector OffsetLandingTransform;
};

//Definition of a single Stairs segment
USTRUCT(BlueprintType)
struct FStairsSegmentData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Stairs Data")
	int32 StairsArrayIndex = -1;
	
	UPROPERTY(BlueprintReadWrite, Category = "Stairs Data")
	int32 SectionIndex;

	UPROPERTY(BlueprintReadWrite, Category = "Stairs Data")
	int32 Level;	

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Stairs Data")
	FVector StartLoc;
	
	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Stairs Data")
	FVector Direction;
	
	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Stairs Data")
	FVector RoofTunnelLoc;
	
	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Stairs Data")
	TArray<FStairModuleStructure> Structures;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Data")
	float StairsThickness;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stairs Data")
	TArray<FVector> Vertices;
	
	//Stairs Material
	TArray<UStaticMeshComponent*> StairModules;
	
	//Stairs Tread Mesh
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Data")
	UStaticMesh* StairTreadMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs Data")
	UStaticMesh* StairLandingMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UMaterialInterface* TreadMaterial;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UMaterialInterface* LandingMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCommitted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCutOut;
	
	//Constructor
	FStairsSegmentData(): TreadMaterial(nullptr), LandingMaterial(nullptr)
	{
		StairsArrayIndex = -1;
		SectionIndex = -1;
		Level = 0;
		bCommitted = false;
		bCutOut = false;
		StartLoc = FVector(0, 0, 0);
		RoofTunnelLoc = FVector(0, 0, 0);
		Structures = {};
		StairsThickness = 15;
	}

	//Compare only X,Y and Level for locations
	bool operator==(const FStairsSegmentData & other) const
	{
		return ((other.StartLoc == StartLoc && other.RoofTunnelLoc == RoofTunnelLoc && other.Structures == Structures)
			&& other.Level == Level && other.StairsArrayIndex == StairsArrayIndex && other.SectionIndex == SectionIndex);
	}
	
	//Compare only X,Y and Level for locations
	bool operator!=(const FStairsSegmentData & other) const
	{
		return ((other.StartLoc != StartLoc && other.RoofTunnelLoc != RoofTunnelLoc && other.Structures == Structures)
			&& other.Level != Level && other.StairsArrayIndex != StairsArrayIndex && other.SectionIndex != SectionIndex);
	}
};

/**
 * 
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UStairsComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

protected:

	UStairsComponent(const FObjectInitializer& ObjectInitializer);
	
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	UFUNCTION(BlueprintCallable)
	int32 GetSectionIDFromHitResult(const FHitResult& HitResult) const;
	
	UFUNCTION(BlueprintCallable)
	void DestroyStairs();
	
	UFUNCTION(BlueprintCallable)
	bool FindExistingStairsSection(const FVector& TileCornerStart, const TArray<FStairModuleStructure>& Structures, FStairsSegmentData& OutStairs);
	
	UFUNCTION(BlueprintCallable)
	int FindNearestStairTread(UStaticMeshComponent* HitMesh);

	UFUNCTION(BlueprintCallable)
	void GenerateStairs();

	UFUNCTION(BlueprintCallable)
	FStairsSegmentData GenerateStairsMeshSection(FStairsSegmentData InStairsData);

	UFUNCTION(BlueprintCallable)
	void CommitStairsSection(FStairsSegmentData InStairsData, UMaterialInstance* TreadsMaterial, UMaterialInstance* LandingsMaterial);
	
	UFUNCTION(BlueprintCallable)
	FStairsSegmentData GenerateStairsSection(const FVector& Location, const FVector& Direction, const TArray<FStairModuleStructure>& Structures, const TArray<UStaticMeshComponent*> StairModules, const float StairsThickness);
	
	UFUNCTION(BlueprintCallable)
	void DestroyStairsSection(FStairsSegmentData InStairsData);
		
	UFUNCTION(BlueprintCallable)
	void RemoveStairsSection(int StairsArrayIndex);
	
	UFUNCTION(BlueprintCallable)
	bool SetStairsDataByIndex(int32 Index, FStairsSegmentData NewStairsData);
	
	UFUNCTION(BlueprintCallable)
	FStairsSegmentData& GetStairsDataByIndex(int32 StairsIndex);
	
	//Stairs System Information
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FStairsSegmentData StairsData;
	
	//Stairs System Information
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FStairsSegmentData> StairsDataArray;
	
	//Wall Free Indices to Reuse
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int> StairsFreeIndices;

	//If the Stairs is valid place
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bValidPlacement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCommitted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stairs")
	UMaterialInterface* PreviewMaterial;

	// Reference to LotManager for grid operations (not serialized)
	UPROPERTY(Transient)
	class ALotManager* LotManager;

private:

	bool bCreateCollision = true;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Components/ActorComponent.h"
#include "Actors/PortalBase.h"
#include "Components/RoofComponent.h"
#include "WallComponent.generated.h"

class UCanvasRenderTarget2D;
class UWallPattern;

// Store roof trimming data for walls that need to be trimmed to roof slope
USTRUCT()
struct FRoofTrimData
{
	GENERATED_BODY()

	FRoofVertices RoofVerts;
	ERoofType RoofType;
	float BaseZ;

	FRoofTrimData() : RoofType(ERoofType::Gable), BaseZ(0.0f) {}
};

USTRUCT(BlueprintType)
struct FWallTextures
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture* FrontFaceTexture;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture* BackFaceTexture;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture* RightFaceTexture;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture* LeftFaceTexture;
	
	FWallTextures() : FrontFaceTexture(nullptr), BackFaceTexture(nullptr), RightFaceTexture(nullptr), LeftFaceTexture(nullptr) {}
	FWallTextures(UTexture* FrontFaceTexture, UTexture* BackFaceTexture, UTexture* RightFaceTexture, UTexture* LeftFaceTexture) : FrontFaceTexture(FrontFaceTexture), BackFaceTexture(BackFaceTexture), RightFaceTexture(RightFaceTexture), LeftFaceTexture(LeftFaceTexture) {}
};

// Represents a chain of connected, aligned wall segments for editing operations
// Used for Sims 4-style room editing (select/move/delete entire wall chains)
USTRUCT(BlueprintType)
struct FWallChains
{
	GENERATED_BODY()

	// Floor level
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Level;

	// Start and end points of the entire chain
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector StartLoc;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector EndLoc;

	// Normalized direction of this wall chain (for alignment checking)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Direction;

	// Indices into WallComponent->WallDataArray for all walls in this chain
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int32> WallIndices;

	FWallChains() : Level(0), StartLoc(FVector::ZeroVector), EndLoc(FVector::ZeroVector), Direction(FVector::ZeroVector) {}

	bool operator==(const FWallChains& other) const
	{
		return StartLoc == other.StartLoc && EndLoc == other.EndLoc && Level == other.Level;
	}

	bool operator!=(const FWallChains& other) const
	{
		return !(*this == other);
	}
};

//Wall Corner data
// USTRUCT(BlueprintType)
// struct FWallConnection
// {
// 	GENERATED_BODY()
// 	
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite)
// 	FVector Location;
// 	
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite)
// 	FVector Direction;
// 	
// 	FWallConnection() : Location(0), Direction(0) {}
// 	FWallConnection(const FVector& Location) : Location(Location) {}
// 	FWallConnection(const FVector& Location, const FVector& Direction) : Location(Location), Direction(Direction) {}
//
// 	bool operator==(const FWallConnection other) const
// 	{
// 		return Location == other.Location;
// 	}
// 	
// 	bool operator!=(const FWallConnection other) const
// 	{
// 		return Location != other.Location && Direction != other.Direction;
// 	}
// };

//Wall intersection data
USTRUCT(BlueprintType)
struct FWallGraphPositionEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float XPos;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float YPos;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Level;
};
	
//Wall intersection data
USTRUCT(BlueprintType)
struct FWallGraphLineEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 LayerId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 FromId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Room1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ToId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Room2;
	
};

//Wall Offset data
USTRUCT(BlueprintType)
struct FConnectedWallOffsets
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WallPlugin|Data")
	float ConnectedStartPosY;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WallPlugin|Data")
	float ConnectedStartNegY;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WallPlugin|Data")
	float ConnectedEndPosY;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WallPlugin|Data")
	float ConnectedEndNegY;

	FConnectedWallOffsets() : ConnectedStartPosY(0), ConnectedStartNegY(0), ConnectedEndPosY(0), ConnectedEndNegY(0) {}
	FConnectedWallOffsets(float A, float B, float C, float D) : ConnectedStartPosY(A), ConnectedStartNegY(B), ConnectedEndPosY(C), ConnectedEndNegY(D) {}
};

//Definition of a single wall segment
USTRUCT(BlueprintType)
struct FWallSegmentData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Wall Data")
	int32 WallArrayIndex = -1;
	
	UPROPERTY(BlueprintReadWrite, Category = "Wall Data")
	int32 SectionIndex;

	// REMOVED: RoomID1, RoomID2, Level, StartRow, StartColumn, EndRow, EndColumn
	// These are now owned by WallGraphComponent (FWallEdge)
	// Query WallGraph for this data instead of storing it here

	// Reference to wall graph edge (for querying spatial/logic data)
	UPROPERTY(BlueprintReadWrite, Category = "Wall Data")
	int32 WallEdgeID = -1;

	// Level is still needed for rendering at the correct height
	UPROPERTY(BlueprintReadWrite, Category = "Wall Data")
	int32 Level;

	UPROPERTY(EditAnywhere , BlueprintReadWrite, Category="Wall Data")
	FVector StartLoc;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Data")
	FVector EndLoc;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Data")
	float Height;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Data")
	float EndHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Data")
	float Thickness;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Data")
	FRotator WallRotation;
	
	FProcMeshSection* WallSectionData;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Data")
	TArray<FVector> Vertices;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Data")
	TArray<FVector> Face1Vertices;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Data")
	TArray<FVector> Face2Vertices;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Data")
	TArray<int32> Triangles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Data")
	TArray<int32> Face1Triangles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Data")
	TArray<int32> Face2Triangles;
	
	//Wall Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UMaterialInstanceDynamic* WallMaterial;

	//Wall Textures
	FWallTextures WallTextures;

	//Applied Wall Pattern (tracks which pattern was applied to this wall)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall Data")
	UWallPattern* AppliedPattern;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<APortalBase*> PortalArray;
	
	// Used for rendering portal cutouts
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UCanvasRenderTarget2D* RenderTarget;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCommitted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCutOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsInCutawayMode;

	// Transition visibility states for cutaway optimization (updated via material parameters, no mesh regeneration)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bShowStartTransition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bShowEndTransition;

	//For Dynamic changes to the connected Walls. Because they need to reload Meshdata too.
	TArray<FVector> ConnectedWallsAtStartDir;
	
	TArray<FVector> ConnectedWallsAtEndDir;

	TArray<int> ConnectedWallsSections;

	// Mesh information
	FConnectedWallOffsets ConnectedWallOffsets;

	//Constructor
	FWallSegmentData(): WallSectionData(nullptr), WallMaterial(nullptr), AppliedPattern(nullptr), RenderTarget(nullptr)
	{
		WallArrayIndex = -1;
		SectionIndex = -1;
		WallEdgeID = -1;
		Level = 0;
		bCommitted = false;
		bCutOut = false;
		bIsInCutawayMode = false;
		bShowStartTransition = false;
		bShowEndTransition = false;
		StartLoc = FVector(0, 0, 0);
		EndLoc = FVector(0, 0, 0);
		Height = 300;
		EndHeight = 300;
		Thickness = 20;
		ConnectedWallsAtEndDir = {};
		ConnectedWallsAtStartDir = {};
		Triangles = {};
		Vertices = {};
	}

	//Compare only X,Y and Level for locations
	bool operator==(const FWallSegmentData & other) const
	{
		return ((other.StartLoc.X == StartLoc.X && other.EndLoc.X == EndLoc.X && other.StartLoc.Y == StartLoc.Y && other.EndLoc.Y == EndLoc.Y )
			&& other.Level == Level && other.WallArrayIndex == WallArrayIndex && other.SectionIndex == SectionIndex);
	}
	//Compare only X,Y and Level for locations
	bool operator!=(const FWallSegmentData & other) const
	{
		return ((other.StartLoc.X != StartLoc.X && other.EndLoc.X != EndLoc.X && other.StartLoc.Y != StartLoc.Y && other.EndLoc.Y != EndLoc.Y )
			&& other.Level != Level && other.WallArrayIndex != WallArrayIndex && other.SectionIndex != SectionIndex);
	}
};


/*
 *	This handy class handles drawing all the walls on a lot as a single continuous UWallComponent per floor/level
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BURBARCHITECT_API UWallComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

protected:
	UWallComponent(const FObjectInitializer& ObjectInitializer);
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Static alpha values for marking different wall face types (25% increments for material logic)
	static constexpr uint8 ALPHA_ALWAYS_VISIBLE = 255;    // 100% - Baseboard, caps, etc.
	static constexpr uint8 ALPHA_TRANSITION_START = 191;  // 75%  - START transition face
	static constexpr uint8 ALPHA_TRANSITION_END = 127;    // 50%  - END transition face
	static constexpr uint8 ALPHA_CENTER_FACE = 63;        // 25%  - CENTER face (hidden in cutaway)


	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable)
	void RegenerateWallSection(FWallSegmentData InWallData, bool bRecursive);

	// Helper to reapply roof trimming from stored data (called after regeneration)
	void ApplyRoofTrimming(int32 WallArrayIndex);

	// Updates RoomID1 and RoomID2 for all walls based on current tile RoomIDs
	// Call this after room detection to keep wall RoomIDs in sync without mesh regeneration
	UFUNCTION(BlueprintCallable)
	void UpdateAllWallRoomIDs();

	// Regenerates all wall meshes (updates vertex colors based on current RoomIDs)
	UFUNCTION(BlueprintCallable)
	void RegenerateAllWalls();

	UFUNCTION(BlueprintCallable)
	int32 GetSectionIDFromHitResult(const FHitResult& HitResult) const;

	UFUNCTION(BlueprintCallable)
	int32 GetWallArrayIndexFromHitLocation(const FVector& HitLocation, int32 Level) const;

	UFUNCTION(BlueprintCallable)
	TArray<int32> GetMultiSectionIDFromHitResult(FVector Location, FVector BoxExtents, FQuat Rotation);
	
	UFUNCTION(BlueprintCallable)
	void RenderPortals();

	UFUNCTION(BlueprintCallable)
	void RenderPortalsForWalls(const TArray<int32>& WallIndices);

	FVector2D GetPortalLocationRelativeToWall(FWallSegmentData WallSegment, APortalBase* Portal);

	// Check if a portal should render a cutout on this wall (based on alignment)
	bool ShouldRenderPortalOnWall(const FWallSegmentData& Wall, const APortalBase* Portal) const;

	UFUNCTION(BlueprintCallable)
	void DestroyWall();

	UFUNCTION(BlueprintCallable)
	void CommitWallSection(FWallSegmentData InWallData, UWallPattern* Pattern, UMaterialInstance* BaseMaterial, const FWallTextures& DefaultWallTextures = FWallTextures());

	void ChangeWallTextureParameters(const FWallSegmentData& InWallData, const FWallTextures& DefaultWallTextures);

	UFUNCTION(BlueprintCallable)
	FWallSegmentData GenerateWallSection(int32 Level, const FVector& TileCornerStart, const FVector& TileCornerEnd, float InWallHeight);

	/**
	 * Generate a wall section with varying heights (for triangular/sloped walls)
	 * @param Level - The floor level
	 * @param TileCornerStart - Start position (bottom vertex at ceiling level)
	 * @param TileCornerEnd - End position (bottom vertex at ceiling level)
	 * @param StartHeight - Height at start position (0 = flat at ceiling, positive = extends upward)
	 * @param EndHeight - Height at end position (0 = flat at ceiling, positive = extends upward)
	 */
	FWallSegmentData GenerateWallSection(int32 Level, const FVector& TileCornerStart, const FVector& TileCornerEnd, float StartHeight, float EndHeight);

	UFUNCTION(BlueprintCallable)
	void DestroyWallSection(FWallSegmentData InWallData);
	
	UFUNCTION(BlueprintCallable)
	void RemoveWallSection(int WallArrayIndex);

	UFUNCTION(BlueprintCallable)
	FWallSegmentData& GetWallDataByIndex(int32 WallIndex);

	// Check if a material instance is shared (returns true if it's in the shared cache)
	bool IsSharedMaterial(UMaterialInstanceDynamic* Material) const;

	void GenerateWallMesh();
	
	bool IsInsideWall(const FVector& Point, UStaticMeshComponent* MeshComponent);
	
	FWallSegmentData GenerateWallMeshSection(FWallSegmentData InWallData);
	
	FVector CalculateIntersection(const FVector& MainDirection, const FVector& MainPoint, const FVector& TargetDirection, const FVector& TargetPoint, const float SpacePoint);

	void CalculateVectorAngles(const FVector& Dir1, const FVector& Dir2, float& OutRightAngle, float& OutLeftAngle);

	// Update transition visibility states and material parameters (optimization - no mesh regeneration)
	UFUNCTION(BlueprintCallable)
	void UpdateWallTransitionState(int32 WallIndex);
	
	//Wall System informations
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FWallSegmentData WallData;

	//Wall System informations
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FWallSegmentData> WallDataArray;

	//Wall Free Indices to Reuse
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int> WallFreeIndices;

	//Array of portals (windows/doors) which need to cut holes in the walls
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<APortalBase*> PortalArray;
	
	//If the wall is valid place
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bValidPlacement;
	
	//Wall Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Material", Meta = (ExposeOnSpawn = true))
	UMaterialInstance* WallMaterial;
	
	//Wall Textures
	FWallTextures* WallTextures;

	// Map of WallArrayIndex -> Roof trim data for walls under roofs
	// Stores trimming parameters to reapply after wall regeneration
	UPROPERTY()
	TMap<int32, FRoofTrimData> WallRoofTrimData;

private:

	bool bCreateCollision = true;

	// Cache of shared material instances to reduce shader compilation overhead
	// Key: base material pointer, Value: shared dynamic material instance
	TMap<UMaterialInstance*, UMaterialInstanceDynamic*> SharedMaterialInstances;

	// Helper to get or create a shared material instance
	UMaterialInstanceDynamic* GetOrCreateSharedMaterialInstance(UMaterialInstance* BaseMaterial);

	// Helper functions for wall occlusion checks
	bool HasWallAbove(const FWallSegmentData& Wall) const;
	bool HasWallBelow(const FWallSegmentData& Wall) const;
};

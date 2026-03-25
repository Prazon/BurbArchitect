// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/WallComponent.h"

#include "Actors/LotManager.h"
#include "Actors/PortalBase.h"
#include "Components/PortalBoxComponent.h"
#include "Data/WallPattern.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Kismet/KismetMathLibrary.h"
#include "BurbArchitectDebug.h"
#include "DrawDebugHelpers.h"

class ALotManager;

UWallComponent::UWallComponent(const FObjectInitializer& ObjectInitializer) : UProceduralMeshComponent(ObjectInitializer)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	WallData.StartLoc = FVector(0, 0, 0);
	WallData.EndLoc = FVector(100, 0, 0);
	WallData.Height = 275.0f;
	WallData.EndHeight = 275.0f;
	WallData.Thickness = 20.0f;
	bUseComplexAsSimpleCollision = true;

	// Disable custom depth - walls are permanent geometry
	bRenderCustomDepth = false;
}

// Called when the game starts
void UWallComponent::BeginPlay()
{
	Super::BeginPlay();
	SetGenerateOverlapEvents(true);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block); // Wall trace channel for portal tools
}

// Called every frame
void UWallComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// ...
}

UMaterialInstanceDynamic* UWallComponent::GetOrCreateSharedMaterialInstance(UMaterialInstance* BaseMaterial)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}

	// Check if we already have a shared instance for this material
	UMaterialInstanceDynamic** ExistingInstance = SharedMaterialInstances.Find(BaseMaterial);
	if (ExistingInstance && *ExistingInstance)
	{
		return *ExistingInstance;
	}

	// Create new shared instance and cache it
	UMaterialInstanceDynamic* NewInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (NewInstance)
	{
		SharedMaterialInstances.Add(BaseMaterial, NewInstance);
	}

	return NewInstance;
}

void UWallComponent::CommitWallSection(FWallSegmentData InWallData, UWallPattern* Pattern, UMaterialInstance* BaseMaterial, const FWallTextures& DefaultWallTextures)
{
	if (!WallDataArray.IsValidIndex(InWallData.WallArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("CommitWallSection: Invalid WallArrayIndex %d (array size: %d)"), InWallData.WallArrayIndex, WallDataArray.Num());
		return;
	}

	WallDataArray[InWallData.WallArrayIndex].bCommitted = true;

	// Store the WallGraph edge ID link
	WallDataArray[InWallData.WallArrayIndex].WallEdgeID = InWallData.WallEdgeID;

	// Store the wall textures in the wall data (for backward compatibility)
	WallDataArray[InWallData.WallArrayIndex].WallTextures = DefaultWallTextures;

	// Store the applied pattern
	WallDataArray[InWallData.WallArrayIndex].AppliedPattern = Pattern;

	// Determine which base material to use
	// Priority: Pattern's BaseMaterial > Tool's BaseMaterial
	UMaterialInstance* MaterialToUse = BaseMaterial;
	if (Pattern && Pattern->BaseMaterial)
	{
		MaterialToUse = Pattern->BaseMaterial;
	}

	// Always create unique material instance for each wall to support per-level visibility control
	// This is necessary because SetCurrentLevel needs to set VisibilityLevel parameter independently per wall
	WallDataArray[InWallData.WallArrayIndex].WallMaterial = CreateDynamicMaterialInstance(
		WallDataArray[InWallData.WallArrayIndex].SectionIndex,
		Cast<UMaterialInterface>(MaterialToUse));

	// Set color swatch and detail normal parameters to off by default (WallPatternTool will enable them per-face)
	if (WallDataArray[InWallData.WallArrayIndex].WallMaterial)
	{
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", 0.0f);
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetScalarParameterValue("bUseColourMaskA", 0.0f);
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", 0.0f);
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetScalarParameterValue("bUseColourMaskB", 0.0f);
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetScalarParameterValue("bUseDetailNormalA", 0.0f);
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetScalarParameterValue("bUseDetailNormalB", 0.0f);
	}

	// Apply pattern textures if pattern is provided
	if (Pattern && WallDataArray[InWallData.WallArrayIndex].WallMaterial)
	{
		UMaterialInstanceDynamic* MID = WallDataArray[InWallData.WallArrayIndex].WallMaterial;

		// Apply textures to both A and B faces (can be customized per-face later via WallPatternTool)
		if (Pattern->BaseTexture)
		{
			MID->SetTextureParameterValue("WallCoveringA", Pattern->BaseTexture);
			MID->SetTextureParameterValue("WallCoveringB", Pattern->BaseTexture);
		}
		if (Pattern->NormalMap)
		{
			MID->SetTextureParameterValue("WallNormalA", Pattern->NormalMap);
			MID->SetTextureParameterValue("WallNormalB", Pattern->NormalMap);
		}
		// Apply detail normal if enabled
		MID->SetScalarParameterValue("bUseDetailNormalA", Pattern->bUseDetailNormal ? 1.0f : 0.0f);
		MID->SetScalarParameterValue("bUseDetailNormalB", Pattern->bUseDetailNormal ? 1.0f : 0.0f);
		if (Pattern->bUseDetailNormal && Pattern->DetailNormal)
		{
			MID->SetTextureParameterValue("DetailNormalA", Pattern->DetailNormal);
			MID->SetTextureParameterValue("DetailNormalB", Pattern->DetailNormal);
			MID->SetScalarParameterValue("DetailNormalIntensityA", Pattern->DetailNormalIntensity);
			MID->SetScalarParameterValue("DetailNormalIntensityB", Pattern->DetailNormalIntensity);
		}
		else
		{
			MID->SetScalarParameterValue("DetailNormalIntensityA", 0.0f);
			MID->SetScalarParameterValue("DetailNormalIntensityB", 0.0f);
		}
		if (Pattern->RoughnessMap)
		{
			MID->SetTextureParameterValue("WallRoughnessA", Pattern->RoughnessMap);
			MID->SetTextureParameterValue("WallRoughnessB", Pattern->RoughnessMap);
		}
	}

	SetMaterial(WallDataArray[InWallData.WallArrayIndex].SectionIndex, WallDataArray[InWallData.WallArrayIndex].WallMaterial);
	RegenerateWallSection(WallDataArray[InWallData.WallArrayIndex], true);

	// Invalidate affected rooms in the cache when a wall is added
	if (ALotManager* OurLot = Cast<ALotManager>(GetOwner()))
	{
		// Query RoomIDs from WallGraph using WallEdgeID
		if (InWallData.WallEdgeID != -1 && OurLot->WallGraph)
		{
			const FWallEdge* Edge = OurLot->WallGraph->Edges.Find(InWallData.WallEdgeID);
			if (Edge)
			{
				if (Edge->Room1 > 0)
					OurLot->InvalidateRoom(Edge->Room1);
				if (Edge->Room2 > 0)
					OurLot->InvalidateRoom(Edge->Room2);
			}
		}
	}

	//RegenerateEverySection();
}

void UWallComponent::ChangeWallTextureParameters(const FWallSegmentData& InWallData, const FWallTextures& DefaultWallTextures)
{
	// Validate array index
	if (!WallDataArray.IsValidIndex(InWallData.WallArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("ChangeWallTextureParameters: Invalid WallArrayIndex %d (array size: %d)"), InWallData.WallArrayIndex, WallDataArray.Num());
		return;
	}

	// Check if wall is currently using a shared material - if so, we need to create a unique instance
	// because we're about to set wall-specific texture parameters
	UMaterialInstanceDynamic* CurrentMaterial = WallDataArray[InWallData.WallArrayIndex].WallMaterial;
	bool bIsSharedMaterial = false;

	// Check if this material is in our shared materials cache (meaning it's shared)
	for (const auto& Pair : SharedMaterialInstances)
	{
		if (Pair.Value == CurrentMaterial)
		{
			bIsSharedMaterial = true;
			break;
		}
	}

	// If using shared material, create a unique instance for this wall
	if (bIsSharedMaterial && CurrentMaterial)
	{
		UMaterialInstanceDynamic* UniqueMaterial = UMaterialInstanceDynamic::Create(CurrentMaterial->Parent, this);
		WallDataArray[InWallData.WallArrayIndex].WallMaterial = UniqueMaterial;
	}

	// Now apply the textures to the (now unique) material
	if (DefaultWallTextures.RightFaceTexture != nullptr)
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetTextureParameterValue("WallCoveringA", Cast<UTexture>(DefaultWallTextures.RightFaceTexture));
	if (DefaultWallTextures.FrontFaceTexture != nullptr)
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetTextureParameterValue("WallCoveringA", Cast<UTexture>(DefaultWallTextures.FrontFaceTexture));
	if (DefaultWallTextures.LeftFaceTexture != nullptr)
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetTextureParameterValue("WallCoveringB", Cast<UTexture>(DefaultWallTextures.LeftFaceTexture));
	if (DefaultWallTextures.BackFaceTexture != nullptr)
		WallDataArray[InWallData.WallArrayIndex].WallMaterial->SetTextureParameterValue("WallCoveringB", Cast<UTexture>(DefaultWallTextures.BackFaceTexture));

	// Store the updated textures in the wall data struct to keep it in sync
	WallDataArray[InWallData.WallArrayIndex].WallTextures = DefaultWallTextures;

	SetMaterial(WallDataArray[InWallData.WallArrayIndex].SectionIndex, WallDataArray[InWallData.WallArrayIndex].WallMaterial);
	RegenerateWallSection(WallDataArray[InWallData.WallArrayIndex], true);
	bRenderCustomDepth = false;
	//RegenerateEverySection();
}

//Destroy this wall segment
void UWallComponent::DestroyWall()
{
	// Component removal handled by LotManager's singular component reference
	for (int i = 0; i<5 ; i++)
	{
		ClearMeshSection(i);
	}
	DestroyComponent();
}

void UWallComponent::GenerateWallMesh()
{
	float Width = WallData.Thickness; 
	float HalfWidth = Width / 2;
	float Height = WallData.Height;
	FVector WallDirection = WallData.EndLoc - WallData.StartLoc;
	WallDirection.Z = 0.0f; // Ignore the Z component for 2D rotation
	WallDirection.Normalize();

	FVector end(UKismetMathLibrary::VSize(   WallData.EndLoc * FVector(1,1,0) - WallData.StartLoc * FVector(1,1,0)) , 0 ,0 );
	
	// Combined mesh arrays
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	auto AddFaceToMesh = [&](const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector>& Norms, const TArray<FVector2D>& UV, const FVector& NormalDirection, bool bIsEdgeFace = false)
	{
		int32 VertexBase = Vertices.Num();
		Vertices.Append(Verts);
		for (int32 Index : Tris)
		{
			Triangles.Add(VertexBase + Index);
		}
		for (int i = 0; i < Verts.Num(); i++)
		{
			Normals.Add(NormalDirection);
			UVs.Add(UV[i]);
			VertexColors.Add(bIsEdgeFace ? FColor::Red : FColor::White);
			Tangents.Add(FProcMeshTangent());
		}
	};
	
	// All required Vertices 
	// Use StartOffset and EndOffset to position the vertices
	FVector VertNegXNegYNegZ(0, -HalfWidth , 0 );
	FVector VertNegXPosYNegZ(0, HalfWidth , 0);
	FVector VertNegXNegYPosZ(0, -HalfWidth, Height);
	FVector VertNegXPosYPosZ(0, HalfWidth, Height);
	FVector VertPosXNegYNegZ(end.X, -HalfWidth , 0);
	FVector VertPosXPosYNegZ(end.X, HalfWidth, 0);
	FVector VertPosXNegYPosZ(end.X, -HalfWidth, Height);
	FVector VertPosXPosYPosZ(end.X, HalfWidth, Height);

	//Underside face
	//This should only be made on walls that are on above ground floors and dont have a wall beneath them/have their bottom face visible
	TArray<FVector> NegZVerts;
	TArray<int32> NegZTriangles;
	TArray<FVector> NegZNormals;
	TArray<FVector2D> NegZUV;
	TArray<FColor> NegZVertexColor;
	TArray<FProcMeshTangent> NegZTangents;
	NegZVerts.Add(VertPosXPosYNegZ);
	NegZVerts.Add(VertPosXNegYNegZ);
	NegZVerts.Add(VertNegXPosYNegZ);
	NegZVerts.Add(VertNegXNegYNegZ);
	NegZTriangles.Add(1);
	NegZTriangles.Add(0);
	NegZTriangles.Add(2);
	NegZTriangles.Add(1);
	NegZTriangles.Add(2);
	NegZTriangles.Add(3);
	NegZUV.Add(FVector2D(0, 1));
	NegZUV.Add(FVector2D(0, 0));
	NegZUV.Add(FVector2D(1, 1));
	NegZUV.Add(FVector2D(1, 0));
	NegZNormals.Add(FVector(0, 0, -1));
	NegZNormals.Add(FVector(0, 0, -1));
	NegZNormals.Add(FVector(0, 0, -1));
	NegZNormals.Add(FVector(0, 0, -1));
	
	//Top side face
	//This should only render if theres no wall above this one on subsequent floors/ is visible
	TArray<FVector> PosZVerts;
	TArray<int32> PosZTriangles;
	TArray<FVector> PosZNormals;
	TArray<FVector2D> PosZUV;
	TArray<FColor> PosZVertexColor;
	TArray<FProcMeshTangent> PosZTangents;
	PosZVerts.Add(VertPosXPosYPosZ);
	PosZVerts.Add(VertPosXNegYPosZ);
	PosZVerts.Add(VertNegXPosYPosZ);
	PosZVerts.Add(VertNegXNegYPosZ);
	PosZTriangles.Add(0);
	PosZTriangles.Add(1);
	PosZTriangles.Add(2);
	PosZTriangles.Add(1);
	PosZTriangles.Add(3);
	PosZTriangles.Add(2);
	PosZUV.Add(FVector2D(0, 1));
	PosZUV.Add(FVector2D(0, 0));
	PosZUV.Add(FVector2D(1, 1));
	PosZUV.Add(FVector2D(1, 0));
	PosZNormals.Add(FVector(0, 0, 1));
	PosZNormals.Add(FVector(0, 0, 1));
	PosZNormals.Add(FVector(0, 0, 1));
	PosZNormals.Add(FVector(0, 0, 1));
	
	//Start Location face
	TArray<FVector> NegXVerts;
	TArray<int32> NegXTriangles;
	TArray<FVector> NegXNormals;
	TArray<FVector2D> NegXUV;
	TArray<FColor> NegXVertexColor;
	TArray<FProcMeshTangent> NegXTangents;
	NegXVerts.Add(VertNegXPosYNegZ);
	NegXVerts.Add(VertNegXNegYNegZ);
	NegXVerts.Add(VertNegXPosYPosZ);
	NegXVerts.Add(VertNegXNegYPosZ);
	NegXTriangles.Add(1);
	NegXTriangles.Add(0);
	NegXTriangles.Add(2);
	NegXTriangles.Add(1);
	NegXTriangles.Add(2);
	NegXTriangles.Add(3);
	NegXUV.Add(FVector2D(0, 1));
	NegXUV.Add(FVector2D(0, 0));
	NegXUV.Add(FVector2D(1, 1));
	NegXUV.Add(FVector2D(1, 0));
	NegXNormals.Add(FVector(-1, 0, 0));
	NegXNormals.Add(FVector(-1, 0, 0));
	NegXNormals.Add(FVector(-1, 0, 0));
	NegXNormals.Add(FVector(-1, 0, 0));
	
	//End Location face
	TArray<FVector> PosXVerts;
	TArray<int32> PosXTriangles;
	TArray<FVector> PosXNormals;
	TArray<FVector2D> PosXUV;
	TArray<FColor> PosXVertexColor;
	TArray<FProcMeshTangent> PosXTangents;
	PosXVerts.Add(VertPosXPosYNegZ);
	PosXVerts.Add(VertPosXNegYNegZ);
	PosXVerts.Add(VertPosXPosYPosZ);
	PosXVerts.Add(VertPosXNegYPosZ);
	PosXTriangles.Add(0);
	PosXTriangles.Add(1);
	PosXTriangles.Add(2);
	PosXTriangles.Add(1);
	PosXTriangles.Add(3);
	PosXTriangles.Add(2);
	PosXUV.Add(FVector2D(0, 1));
	PosXUV.Add(FVector2D(0, 0));
	PosXUV.Add(FVector2D(1, 1));
	PosXUV.Add(FVector2D(1, 0));
	PosXNormals.Add(FVector(1, 0, 0));
	PosXNormals.Add(FVector(1, 0, 0));
	PosXNormals.Add(FVector(1, 0, 0));
	PosXNormals.Add(FVector(1, 0, 0));
	
	//posY WallSurface
	TArray<FVector> PosYVerts;
	TArray<int32> PosYTriangles;
	TArray<FVector> PosYNormals;
	TArray<FVector2D> PosYUV;
	TArray<FColor> PosYVertexColor;
	TArray<FProcMeshTangent> PosYTangents;
	PosYVerts.Add(VertNegXPosYNegZ);
	PosYVerts.Add(VertNegXPosYPosZ);
	PosYVerts.Add(VertPosXPosYNegZ);
	PosYVerts.Add(VertPosXPosYPosZ);
	PosYTriangles.Add(1);
	PosYTriangles.Add(0);
	PosYTriangles.Add(2);
	PosYTriangles.Add(1);
	PosYTriangles.Add(2);
	PosYTriangles.Add(3);
	PosYUV.Add(FVector2D(0, 1));
	PosYUV.Add(FVector2D(0, 0));
	PosYUV.Add(FVector2D(1, 1));
	PosYUV.Add(FVector2D(1, 0));
	PosYNormals.Add(FVector(0, 1, 0));
	PosYNormals.Add(FVector(0, 1, 0));
	PosYNormals.Add(FVector(0, 1, 0));
	PosYNormals.Add(FVector(0, 1, 0));
	
	//negY WallSurface
	TArray<FVector> NegYVerts;
	TArray<int32> NegYTriangles;
	TArray<FVector> NegYNormals;
	TArray<FVector2D> NegYUV;
	TArray<FColor> NegYVertexColor;
	TArray<FProcMeshTangent> NegYTangents;
	NegYVerts.Add(VertNegXNegYNegZ);
	NegYVerts.Add(VertNegXNegYPosZ);
	NegYVerts.Add(VertPosXNegYNegZ);
	NegYVerts.Add(VertPosXNegYPosZ);
	NegYTriangles.Add(0);
	NegYTriangles.Add(1);
	NegYTriangles.Add(2);
	NegYTriangles.Add(1);
	NegYTriangles.Add(3);
	NegYTriangles.Add(2);
	NegYUV.Add(FVector2D(0, 1));
	NegYUV.Add(FVector2D(0, 0));
	NegYUV.Add(FVector2D(1, 1));
	NegYUV.Add(FVector2D(1, 0));
	NegYNormals.Add(FVector(0, -1, 0));
	NegYNormals.Add(FVector(0, -1, 0));
	NegYNormals.Add(FVector(0, -1, 0));
	NegYNormals.Add(FVector(0, -1, 0));
	
	// Assuming you've generated the faces for NegZ, PosZ, NegX, PosX, PosY, NegY using the previous pattern...
	// Add each face to the combined mesh

	// Note: For preview walls (GenerateWallMesh), we don't have full WallData with level info,
	// so we'll show all faces for preview purposes. The actual committed walls will have proper culling.

	//Underside - commented out as it's rarely visible
	//AddFaceToMesh(NegZVerts, NegZTriangles, NegZNormals, NegZUV, FVector(0, 0, -1));

	//TopSide
	AddFaceToMesh(PosZVerts, PosZTriangles, PosZNormals, PosZUV, FVector(0, 0, 1), true);
	//Start Face
	AddFaceToMesh(NegXVerts, NegXTriangles, NegXNormals, NegXUV, FVector(-1, 0, 0), true);
	//End Face
	AddFaceToMesh(PosXVerts, PosXTriangles, PosXNormals, PosXUV, FVector(1, 0, 0), true);
	//PosY Surface
	AddFaceToMesh(PosYVerts, PosYTriangles, PosYNormals, PosYUV, FVector(0, 1, 0));
	//NegY Surface
	AddFaceToMesh(NegYVerts, NegYTriangles, NegYNormals, NegYUV, FVector(0, -1, 0));
	// Check if section exists and has valid vertex count
	FProcMeshSection* Section = GetProcMeshSection(0);
	if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != Vertices.Num())
	{
		CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
	}
	else
	{
		UpdateMeshSection(0, Vertices, Normals, UVs, VertexColors, Tangents);
	}
	SetMaterial(0, WallMaterial);
	
	// Apply the rotation to the wall components
	FRotator WallRotation = WallDirection.Rotation();
	this->SetRelativeRotation(WallRotation);
}

bool UWallComponent::IsInsideWall(const FVector& Point, UStaticMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return false;
	}

	// Use ray tracing to check if the point is inside the wall
	// You can implement more sophisticated methods like ray casting or polygon intersection tests
	// This is a simplified example

	// Transform the point to the local space of the mesh component
	FVector LocalPoint = MeshComponent->GetComponentTransform().InverseTransformPosition(Point);

	// Perform ray tracing to determine if the point is inside the mesh
	// For simplicity, we'll use a simple ray casting algorithm
	FHitResult HitResult;
	FVector Start = LocalPoint;
	FVector End = FVector(Start.X, Start.Y, -10000.0f); // A point below the mesh
	FCollisionQueryParams Params;
	Params.AddIgnoredComponent(MeshComponent);
	bool bHit = MeshComponent->LineTraceComponent(HitResult, Start, End, Params);

	// If the ray intersects an odd number of times with the mesh, the point is inside
	return bHit && HitResult.bBlockingHit && (HitResult.FaceIndex % 2 != 0);
}

FWallSegmentData UWallComponent::GenerateWallMeshSection(FWallSegmentData InWallData)
{
	// Update RoomID values from adjacent tiles BEFORE generating mesh
	// Use deterministic grid sampling to figure out which face points where
	if (ALotManager* OurLot = Cast<ALotManager>(GetOwner()))
	{
		TArray<FTileData> AdjacentTiles = OurLot->GetTilesAdjacentToWall(InWallData.StartLoc, InWallData.EndLoc, InWallData.Level);

		if (AdjacentTiles.Num() >= 2)
		{
			// Calculate wall normal (right-hand perpendicular, deterministic)
			FVector WallDirection = (InWallData.EndLoc - InWallData.StartLoc).GetSafeNormal();
			FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();
			FVector WallCenter = (InWallData.StartLoc + InWallData.EndLoc) * 0.5f;

			// Sample two points perpendicular to the wall
			float SampleOffset = 50.0f; // Half a tile size
			FVector SamplePosNormal = WallCenter + (WallNormal * SampleOffset);
			FVector SampleNegNormal = WallCenter - (WallNormal * SampleOffset);

			// Find which tiles these sample points are in
			int32 RowPos, ColPos, RowNeg, ColNeg;
			bool bFoundPos = OurLot->LocationToTile(SamplePosNormal, RowPos, ColPos);
			bool bFoundNeg = OurLot->LocationToTile(SampleNegNormal, RowNeg, ColNeg);

			if (bFoundPos && bFoundNeg)
			{
				// Get the tiles at these grid coordinates
				FTileData TilePosNormal = OurLot->FindTileByGridCoords(RowPos, ColPos, InWallData.Level);
				FTileData TileNegNormal = OurLot->FindTileByGridCoords(RowNeg, ColNeg, InWallData.Level);

				// RoomIDs are now stored in WallGraph, not in WallSegmentData
				// The WallGraph edge should already be created with the correct room assignments
				// This code is no longer needed as room assignment happens in WallGraphComponent
			}
			else
			{
				// Fallback - room assignment handled by WallGraph
			}
		}
		else if (AdjacentTiles.Num() == 1)
		{
			// Diagonal wall - room assignment handled by WallGraph
		}
	}

	int32 NewSectionIndex = GetNumSections();
	if(InWallData.SectionIndex != -1)
	{
		NewSectionIndex = InWallData.SectionIndex;
	}
	InWallData.SectionIndex = NewSectionIndex;
	float Width = InWallData.Thickness;
	float HalfWidth = Width / 2;
	float Height = InWallData.Height;
	float EndHeight = InWallData.EndHeight;
	float SpacePoint = 45.0f; //~24

	FVector WallDirection = InWallData.EndLoc - InWallData.StartLoc;
	WallDirection.Z = 0.0f; // Ignore the Z component for 2D rotation
	WallDirection.Normalize();
	FRotator WallRotation = WallDirection.Rotation();

	FVector MovementRight(SpacePoint, -1 * HalfWidth, 0.0f);
	FVector MovementLeft(SpacePoint, HalfWidth, 0.0f);

	FVector MainPointStart(InWallData.StartLoc);
	FVector RightPointStart(MainPointStart + WallRotation.RotateVector(MovementRight));
	FVector LeftPointStart(MainPointStart + WallRotation.RotateVector(MovementLeft));

	FVector MainPointEnd(InWallData.EndLoc);
	FVector LeftPointEnd(MainPointEnd + WallRotation.RotateVector(MovementLeft * -1));
	FVector RightPointEnd(MainPointEnd + WallRotation.RotateVector(MovementRight * -1));

	// nearest neighbors connector Start :
	if (InWallData.ConnectedWallsAtStartDir.Num() != 0)
	{
		float MinRightAngle = FLT_MAX;
		float MinLeftAngle = FLT_MAX;
		FVector NearestRightWall;
		FVector NearestLeftWall;
		
		for (FVector WallFound : InWallData.ConnectedWallsAtStartDir)
		{
			float AngleRight;
			float AngleLeft;

			CalculateVectorAngles(WallDirection, WallFound, AngleRight, AngleLeft);
			
			// Check if this wall is to the right
			if (AngleRight < MinRightAngle)
			{
				MinRightAngle = AngleRight;
				NearestRightWall = WallFound;
			}
			// Check if this wall is to the left
			if (AngleLeft < MinLeftAngle)
			{
				MinLeftAngle = AngleLeft;
				NearestLeftWall = WallFound;
			}
		}

		FVector FoundWallLeftPoint = FVector(MainPointStart + NearestRightWall.Rotation().RotateVector(MovementLeft));
		FVector FoundWallRightPoint = FVector(MainPointStart + NearestLeftWall.Rotation().RotateVector(MovementRight));
		
		RightPointStart = CalculateIntersection(WallDirection, RightPointStart, NearestRightWall, FoundWallLeftPoint, SpacePoint);
		LeftPointStart = CalculateIntersection(WallDirection, LeftPointStart, NearestLeftWall, FoundWallRightPoint, SpacePoint);
		
	} else
	{
		RightPointStart = MainPointStart + WallRotation.RotateVector(FVector::LeftVector * HalfWidth);
		LeftPointStart = MainPointStart + WallRotation.RotateVector(FVector::RightVector * HalfWidth);
	}

	// Mitered center point at start edge (for baseboard cap convergence)
	// EXACT duplicate of edge mitering logic but for center line (Y offset = 0)
	FVector MovementCenter(SpacePoint, 0.0f, 0.0f);
	FVector CenterPointStart(MainPointStart + WallRotation.RotateVector(MovementCenter));

	if (InWallData.ConnectedWallsAtStartDir.Num() != 0)
	{
		float MinRightAngle = FLT_MAX;
		float MinLeftAngle = FLT_MAX;
		FVector NearestRightWall;
		FVector NearestLeftWall;

		for (FVector WallFound : InWallData.ConnectedWallsAtStartDir)
		{
			float AngleRight;
			float AngleLeft;

			CalculateVectorAngles(WallDirection, WallFound, AngleRight, AngleLeft);

			// Check if this wall is to the right
			if (AngleRight < MinRightAngle)
			{
				MinRightAngle = AngleRight;
				NearestRightWall = WallFound;
			}
			// Check if this wall is to the left
			if (AngleLeft < MinLeftAngle)
			{
				MinLeftAngle = AngleLeft;
				NearestLeftWall = WallFound;
			}
		}

		FVector FoundWallCenterPoint = FVector(MainPointStart + NearestRightWall.Rotation().RotateVector(MovementCenter));

		CenterPointStart = CalculateIntersection(WallDirection, CenterPointStart, NearestRightWall, FoundWallCenterPoint, SpacePoint);
	}
	else
	{
		CenterPointStart = MainPointStart;
	}

	// nearest neighbors connector End :
	if (InWallData.ConnectedWallsAtEndDir.Num() != 0)
	{
		float MinRightAngle = FLT_MAX;
		float MinLeftAngle = FLT_MAX;
		FVector NearestRightWall;
		FVector NearestLeftWall;
		
		for (FVector WallFound : InWallData.ConnectedWallsAtEndDir)
		{
			float AngleRight;
			float AngleLeft;

			CalculateVectorAngles(WallDirection, WallFound, AngleRight, AngleLeft);
			
			// Check if this wall is to the right
			if (AngleRight < MinRightAngle)
			{
				MinRightAngle = AngleRight;
				NearestRightWall = WallFound;
			}
			// Check if this wall is to the left
			if (AngleLeft < MinLeftAngle)
			{
				MinLeftAngle = AngleLeft;
				NearestLeftWall = WallFound;
			}
		}
		
		FVector FoundWallLeftPoint = FVector(MainPointEnd + NearestRightWall.Rotation().RotateVector(MovementLeft * -1));
		FVector FoundWallRightPoint = FVector(MainPointEnd + NearestLeftWall.Rotation().RotateVector(MovementRight * -1));
		
		RightPointEnd = CalculateIntersection(WallDirection, RightPointEnd, NearestRightWall, FoundWallLeftPoint, SpacePoint * -1);
		LeftPointEnd = CalculateIntersection(WallDirection, LeftPointEnd, NearestLeftWall, FoundWallRightPoint,  SpacePoint * -1);

	} else
	{
		RightPointEnd = MainPointEnd + WallRotation.RotateVector(FVector::RightVector * HalfWidth);
		LeftPointEnd = MainPointEnd + WallRotation.RotateVector(FVector::LeftVector * HalfWidth);
	}

	// Mitered center point at end edge (for baseboard cap convergence)
	// EXACT duplicate of edge mitering logic but for center line (Y offset = 0)
	FVector CenterPointEnd(MainPointEnd + WallRotation.RotateVector(MovementCenter * -1));

	if (InWallData.ConnectedWallsAtEndDir.Num() != 0)
	{
		float MinRightAngle = FLT_MAX;
		float MinLeftAngle = FLT_MAX;
		FVector NearestRightWall;
		FVector NearestLeftWall;

		for (FVector WallFound : InWallData.ConnectedWallsAtEndDir)
		{
			float AngleRight;
			float AngleLeft;

			CalculateVectorAngles(WallDirection, WallFound, AngleRight, AngleLeft);

			// Check if this wall is to the right
			if (AngleRight < MinRightAngle)
			{
				MinRightAngle = AngleRight;
				NearestRightWall = WallFound;
			}
			// Check if this wall is to the left
			if (AngleLeft < MinLeftAngle)
			{
				MinLeftAngle = AngleLeft;
				NearestLeftWall = WallFound;
			}
		}

		FVector FoundWallCenterPoint = FVector(MainPointEnd + NearestRightWall.Rotation().RotateVector(MovementCenter * -1));

		CenterPointEnd = CalculateIntersection(WallDirection, CenterPointEnd, NearestRightWall, FoundWallCenterPoint, SpacePoint * -1);
	}
	else
	{
		CenterPointEnd = MainPointEnd;
	}

	FVector MainPointStartUp(MainPointStart      + (Height * FVector::UpVector));
	FVector RightPointStartUp(RightPointStart    + (Height * FVector::UpVector));
	FVector LeftPointStartUp(LeftPointStart      + (Height * FVector::UpVector));

	FVector MainPointEndUp(MainPointEnd     + (EndHeight * FVector::UpVector));
	FVector LeftPointEndUp(LeftPointEnd     + (EndHeight * FVector::UpVector));
	FVector RightPointEndUp(RightPointEnd   + (EndHeight * FVector::UpVector));

	// Cutaway geometry vertices
	const float BaseboardHeight = 30.0f;
	FVector BaseboardTopLeft = LeftPointStart + (BaseboardHeight * FVector::UpVector);
	FVector BaseboardTopRight = RightPointStart + (BaseboardHeight * FVector::UpVector);

	// End side baseboard vertices
	FVector BaseboardTopLeftEnd = LeftPointEnd + (BaseboardHeight * FVector::UpVector);
	FVector BaseboardTopRightEnd = RightPointEnd + (BaseboardHeight * FVector::UpVector);

	// Baseboard center points
	// Main faces use center of their diagonal bottom edges (already uses mitered edge points)
	FVector BaseboardTopCenterPosY = ((LeftPointStart + RightPointEnd) * 0.5f) + (BaseboardHeight * FVector::UpVector);
	FVector BaseboardTopCenterNegY = ((RightPointStart + LeftPointEnd) * 0.5f) + (BaseboardHeight * FVector::UpVector);
	// Edge faces use mitered center lines (eliminates gaps at 3+ wall junctions)
	FVector BaseboardTopCenterStart = CenterPointStart + (BaseboardHeight * FVector::UpVector);
	FVector BaseboardTopCenterEnd = CenterPointEnd + (BaseboardHeight * FVector::UpVector);

	// === DEBUG VISUALIZATION FOR ALL WALL VERTICES ===
	// Controlled by console command: burb.debug.walls
	if (BurbArchitectDebug::IsWallDebugEnabled() && GetWorld())
	{
		TArray<TPair<FVector, FString>> DebugVertices;

		// Bottom corners (Red)
		DebugVertices.Add(TPair<FVector, FString>(LeftPointStart, TEXT("LPS")));
		DebugVertices.Add(TPair<FVector, FString>(RightPointStart, TEXT("RPS")));
		DebugVertices.Add(TPair<FVector, FString>(LeftPointEnd, TEXT("LPE")));
		DebugVertices.Add(TPair<FVector, FString>(RightPointEnd, TEXT("RPE")));

		// Top corners (Blue)
		DebugVertices.Add(TPair<FVector, FString>(LeftPointStartUp, TEXT("LPSU")));
		DebugVertices.Add(TPair<FVector, FString>(RightPointStartUp, TEXT("RPSU")));
		DebugVertices.Add(TPair<FVector, FString>(LeftPointEndUp, TEXT("LPEU")));
		DebugVertices.Add(TPair<FVector, FString>(RightPointEndUp, TEXT("RPEU")));

		// Baseboard corners (Yellow)
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopLeft, TEXT("BTL")));
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopRight, TEXT("BTR")));
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopLeftEnd, TEXT("BTLE")));
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopRightEnd, TEXT("BTRE")));

		// Convergence points (Green)
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopCenterStart, TEXT("BTCS")));
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopCenterPosY, TEXT("BTCPY")));
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopCenterEnd, TEXT("BTCE")));
		DebugVertices.Add(TPair<FVector, FString>(BaseboardTopCenterNegY, TEXT("BTCNY")));

		for (int32 i = 0; i < DebugVertices.Num(); i++)
		{
			FColor DebugColor = FColor::White;
			if (i < 4) DebugColor = FColor::Red;      // Bottom corners
			else if (i < 8) DebugColor = FColor::Blue;   // Top corners
			else if (i < 12) DebugColor = FColor::Yellow; // Baseboard corners
			else DebugColor = FColor::Green;              // Convergence points

			DrawDebugSphere(GetWorld(), DebugVertices[i].Key, 8.0f, 8, DebugColor, false, 15.0f, 0, 2.0f);
			DrawDebugString(GetWorld(), DebugVertices[i].Key + FVector(0, 0, 15), DebugVertices[i].Value, nullptr, DebugColor, 15.0f, true, 2.5f);
		}
	}
	
	// Combined mesh arrays
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	
	enum class EWallFaceType
	{
		BaseboardSection,        // Baseboard triangles (main faces) - Cyan - alpha controls visibility, portal-compatible
		UpperWall,               // Upper wall triangles (main faces) - White - alpha controls visibility, portal-compatible
		Cap,                     // Any cap face (internal faces only visible if wall is cut away)- Green - alpha controls visibility, portal-compatible
		BaseboardEdge,           // Baseboard edge faces (always visible) - Magenta - no portal rendering
		EdgeFace                 // Upper edge/Top/Bottom faces (no portal rendering) - Red - alpha controls visibility
	};

	auto AddFaceToMesh = [&](const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector2D>& UV, const FVector& NormalDirection, EWallFaceType FaceType, uint8 Alpha = 255)
	{
		int32 VertexBase = Vertices.Num();
		Vertices.Append(Verts);
		for (int32 Index : Tris)
		{
			Triangles.Add(VertexBase + Index);
		}

		// Assign colors based on face type (RGB for portal compatibility, Alpha for cutaway control)
		FColor VertexColor = FColor::White;
		switch (FaceType)
		{
			case EWallFaceType::BaseboardSection:
				VertexColor = FColor::Cyan; // (0, 255, 255) - Baseboard sections
				break;
			case EWallFaceType::UpperWall:
				VertexColor = FColor::White; // (255, 255, 255) - Upper wall (portal-compatible)
				break;
			case EWallFaceType::Cap:
				VertexColor = FColor::Green; // (0, 255, 0) - All caps (portal-compatible)
				break;
			case EWallFaceType::BaseboardEdge:
				VertexColor = FColor::Magenta; // (255, 0, 255) - Baseboard edges (no portals, always visible)
				break;
			case EWallFaceType::EdgeFace:
				VertexColor = FColor::Red; // (255, 0, 0) - Upper edge/Top/Bottom faces (no portals)
				break;
		}

		// Apply alpha channel to vertex color
		VertexColor.A = Alpha;

		for (int i = 0; i < Verts.Num(); i++)
		{
			Normals.Add(NormalDirection);
			UVs.Add(UV[i]);
			VertexColors.Add(VertexColor);
			Tangents.Add(FProcMeshTangent());
		}
	};
	
	//Underside face
	TArray<FVector> NegZVerts;
	TArray<int32> NegZTriangles;
	TArray<FVector2D> NegZUV;
	NegZVerts.Add(RightPointEnd);    // 0
	NegZVerts.Add(LeftPointEnd);     // 1
	NegZVerts.Add(LeftPointStart);   // 2
	NegZVerts.Add(RightPointStart);  // 3
	NegZTriangles.Add(1);
	NegZTriangles.Add(0);
	NegZTriangles.Add(2);
	NegZTriangles.Add(3);
	NegZTriangles.Add(1);
	NegZTriangles.Add(2);
	NegZUV.Add(FVector2D(0, 1));
	NegZUV.Add(FVector2D(0, 0));
	NegZUV.Add(FVector2D(1, 1));
	NegZUV.Add(FVector2D(1, 0));
	FVector NegZNormal = FVector(0,0,-1);

	NegZVerts.Add(MainPointEnd);       // 4
	NegZVerts.Add(MainPointStart);     // 5
	NegZTriangles.Add(1);
	NegZTriangles.Add(4);
	NegZTriangles.Add(0);
	NegZTriangles.Add(3);
	NegZTriangles.Add(2);
	NegZTriangles.Add(5);
	NegZUV.Add(FVector2D(0, 1));
	NegZUV.Add(FVector2D(0, 0));
	
	//Top face
	TArray<FVector> PosZVerts;
	TArray<int32> PosZTriangles;
	TArray<FVector2D> PosZUV;
	PosZVerts.Add(RightPointEndUp);   // 0
	PosZVerts.Add(LeftPointEndUp);    // 1
	PosZVerts.Add(LeftPointStartUp);  // 2
	PosZVerts.Add(RightPointStartUp); // 3
	PosZTriangles.Add(0);
	PosZTriangles.Add(1);
	PosZTriangles.Add(2);
	PosZTriangles.Add(1);
	PosZTriangles.Add(3);
	PosZTriangles.Add(2);
	PosZUV.Add(FVector2D(0, 1));
	PosZUV.Add(FVector2D(0, 0));
	PosZUV.Add(FVector2D(1, 1));
	PosZUV.Add(FVector2D(1, 0));
	FVector PosZNormal = FVector(0,0,1);

	PosZVerts.Add(MainPointEndUp);       // 4
	PosZVerts.Add(MainPointStartUp);     // 5
	PosZTriangles.Add(0);
	PosZTriangles.Add(4);
	PosZTriangles.Add(1);
	PosZTriangles.Add(3);
	PosZTriangles.Add(5);
	PosZTriangles.Add(2);
	PosZUV.Add(FVector2D(0, 1));
	PosZUV.Add(FVector2D(0, 0));
	
	//Start Edge face (Cutaway geometry)
	TArray<FVector> NegXVerts;
	TArray<int32> NegXTriangles;
	TArray<FVector2D> NegXUV;

	// 7 vertices: the cutaway profile across the thickness
	NegXVerts.Add(LeftPointStart);          // 0 - Bottom outer
	NegXVerts.Add(RightPointStart);         // 1 - Bottom inner
	NegXVerts.Add(BaseboardTopLeft);        // 2 - Baseboard top outer
	NegXVerts.Add(BaseboardTopRight);       // 3 - Baseboard top inner
	NegXVerts.Add(BaseboardTopCenterStart); // 4 - Baseboard center (convergence)
	NegXVerts.Add(LeftPointStartUp);        // 5 - Top outer
	NegXVerts.Add(RightPointStartUp);       // 6 - Top inner

	// Baseboard triangles (3 triangles) - REVERSED winding for edge face
	NegXTriangles.Add(0); NegXTriangles.Add(2); NegXTriangles.Add(4); // Triangle 1
	NegXTriangles.Add(0); NegXTriangles.Add(4); NegXTriangles.Add(1); // Triangle 2
	NegXTriangles.Add(4); NegXTriangles.Add(3); NegXTriangles.Add(1); // Triangle 3

	// Main wall triangles (3 triangles with diagonal cuts) - REVERSED winding
	NegXTriangles.Add(5); NegXTriangles.Add(4); NegXTriangles.Add(2); // Triangle 4 - Left side
	NegXTriangles.Add(6); NegXTriangles.Add(3); NegXTriangles.Add(4); // Triangle 5 - Right side
	NegXTriangles.Add(6); NegXTriangles.Add(4); NegXTriangles.Add(5); // Triangle 6 - Top diagonal (RPSU LPSU BTCS) - reversed winding

	// UVs for 7 vertices
	float baseboardUVHeight3 = BaseboardHeight / Height;
	NegXUV.Add(FVector2D(0.0f, 1.0f));                      // 0 - Bottom outer
	NegXUV.Add(FVector2D(1.0f, 1.0f));                      // 1 - Bottom inner
	NegXUV.Add(FVector2D(0.0f, 1.0f - baseboardUVHeight3)); // 2 - Baseboard top outer
	NegXUV.Add(FVector2D(1.0f, 1.0f - baseboardUVHeight3)); // 3 - Baseboard top inner
	NegXUV.Add(FVector2D(0.5f, 1.0f - baseboardUVHeight3)); // 4 - Center
	NegXUV.Add(FVector2D(0.0f, 0.0f));                      // 5 - Top outer
	NegXUV.Add(FVector2D(1.0f, 0.0f));                      // 6 - Top inner

	// Calculate normal from first triangle (0,2,4) using winding order - reversed to point outward
	FVector NegXNormal = FVector::CrossProduct((NegXVerts[4] - NegXVerts[0]), (NegXVerts[2] - NegXVerts[0])).GetSafeNormal();
	
	//End Edge face (Cutaway geometry)
	TArray<FVector> PosXVerts;
	TArray<int32> PosXTriangles;
	TArray<FVector2D> PosXUV;

	// 7 vertices (mirrored from start edge)
	PosXVerts.Add(RightPointEnd);         // 0 - Bottom inner
	PosXVerts.Add(LeftPointEnd);          // 1 - Bottom outer
	PosXVerts.Add(BaseboardTopRightEnd);  // 2 - Baseboard top inner
	PosXVerts.Add(BaseboardTopLeftEnd);   // 3 - Baseboard top outer
	PosXVerts.Add(BaseboardTopCenterEnd); // 4 - Baseboard center (convergence)
	PosXVerts.Add(RightPointEndUp);       // 5 - Top inner
	PosXVerts.Add(LeftPointEndUp);        // 6 - Top outer

	// Baseboard triangles (3 triangles) - REVERSED winding from NegX (not rotated!)
	PosXTriangles.Add(4); PosXTriangles.Add(2); PosXTriangles.Add(0); // Triangle 1 - reversed from NegX's 0,2,4
	PosXTriangles.Add(1); PosXTriangles.Add(4); PosXTriangles.Add(0); // Triangle 2 - reversed from NegX's 0,4,1
	PosXTriangles.Add(1); PosXTriangles.Add(3); PosXTriangles.Add(4); // Triangle 3 - reversed from NegX's 4,3,1

	// Main wall triangles (3 triangles with diagonal cuts) - REVERSED winding from NegX
	PosXTriangles.Add(2); PosXTriangles.Add(4); PosXTriangles.Add(5); // Triangle 4 - Left side, reversed from NegX's 5,4,2
	PosXTriangles.Add(4); PosXTriangles.Add(3); PosXTriangles.Add(6); // Triangle 5 - Right side, reversed from NegX's 6,3,4
	PosXTriangles.Add(5); PosXTriangles.Add(4); PosXTriangles.Add(6); // Triangle 6 - Top diagonal (LPEU RPEU BTCE) - reversed from NegX's 6,4,5

	// UVs for 7 vertices
	float baseboardUVHeight4 = BaseboardHeight / Height;
	PosXUV.Add(FVector2D(0.0f, 1.0f));                      // 0 - Bottom inner
	PosXUV.Add(FVector2D(1.0f, 1.0f));                      // 1 - Bottom outer
	PosXUV.Add(FVector2D(0.0f, 1.0f - baseboardUVHeight4)); // 2 - Baseboard top inner
	PosXUV.Add(FVector2D(1.0f, 1.0f - baseboardUVHeight4)); // 3 - Baseboard top outer
	PosXUV.Add(FVector2D(0.5f, 1.0f - baseboardUVHeight4)); // 4 - Center
	PosXUV.Add(FVector2D(0.0f, 0.0f));                      // 5 - Top inner
	PosXUV.Add(FVector2D(1.0f, 0.0f));                      // 6 - Top outer

	// Calculate normal from first triangle (4,2,0) using winding order - reversed to point outward
	FVector PosXNormal = FVector::CrossProduct((PosXVerts[0] - PosXVerts[4]), (PosXVerts[2] - PosXVerts[4])).GetSafeNormal();
	
	// Calculate UV offsets for extended render target
	// The render target is wider than the wall (20 unit margin on each side)
	// so we need to map the wall surface to the center portion of the texture
	const float RenderTargetMargin = 20.0f;
	float WallWidth = (MainPointStart - MainPointEnd).Length();
	float ExtendedWidth = WallWidth + (RenderTargetMargin * 2.0f);
	float UVMin = RenderTargetMargin / ExtendedWidth;
	float UVMax = (WallWidth + RenderTargetMargin) / ExtendedWidth;

	//posY Surface (Cutaway geometry)
	TArray<FVector> PosYVerts;
	TArray<int32> PosYTriangles;
	TArray<FVector2D> PosYUV;

	// Add 7 vertices for cutaway geometry
	PosYVerts.Add(LeftPointStart);          // 0 - Bottom left
	PosYVerts.Add(RightPointEnd);           // 1 - Bottom right
	PosYVerts.Add(BaseboardTopLeft);        // 2 - Baseboard top left
	PosYVerts.Add(BaseboardTopRightEnd);    // 3 - Baseboard top right
	PosYVerts.Add(BaseboardTopCenterPosY);  // 4 - Baseboard center (convergence point)
	PosYVerts.Add(LeftPointStartUp);        // 5 - Top left
	PosYVerts.Add(RightPointEndUp);         // 6 - Top right

	// Baseboard triangles (3 triangles)
	PosYTriangles.Add(0); PosYTriangles.Add(4); PosYTriangles.Add(2); // Triangle 1
	PosYTriangles.Add(0); PosYTriangles.Add(1); PosYTriangles.Add(4); // Triangle 2
	PosYTriangles.Add(4); PosYTriangles.Add(1); PosYTriangles.Add(3); // Triangle 3

	// Main wall triangles (4 triangles with diagonal cuts)
	PosYTriangles.Add(5); PosYTriangles.Add(2); PosYTriangles.Add(4); // Triangle 4 - Left side
	PosYTriangles.Add(6); PosYTriangles.Add(4); PosYTriangles.Add(3); // Triangle 5 - Right side
	PosYTriangles.Add(4); PosYTriangles.Add(6); PosYTriangles.Add(5); // Triangle 6 - Top diagonal (BTCPY RPEU LPSU) - reversed winding

	// UVs for 7 vertices - map texture top to bottom
	float baseboardUVHeight = BaseboardHeight / Height;
	PosYUV.Add(FVector2D(UVMin, 1.0f));                                      // 0 - Bottom left
	PosYUV.Add(FVector2D(UVMax, 1.0f));                                      // 1 - Bottom right
	PosYUV.Add(FVector2D(UVMin, 1.0f - baseboardUVHeight));                  // 2 - Baseboard top left
	PosYUV.Add(FVector2D(UVMax, 1.0f - baseboardUVHeight));                  // 3 - Baseboard top right
	PosYUV.Add(FVector2D((UVMin + UVMax) * 0.5f, 1.0f - baseboardUVHeight)); // 4 - Center
	PosYUV.Add(FVector2D(UVMin, 0.0f));                                      // 5 - Top left
	PosYUV.Add(FVector2D(UVMax, 0.0f));                                      // 6 - Top right

	// Calculate normal from first triangle (0,4,2) using winding order - reversed to point outward
	FVector PosYNormal = FVector::CrossProduct((PosYVerts[2] - PosYVerts[0]), (PosYVerts[4] - PosYVerts[0])).GetSafeNormal();

	//negY Surface (Cutaway geometry)
	TArray<FVector> NegYVerts;
	TArray<int32> NegYTriangles;
	TArray<FVector2D> NegYUV;

	// Add 7 vertices (mirror of PosY)
	NegYVerts.Add(RightPointStart);         // 0 - Bottom left (right side)
	NegYVerts.Add(LeftPointEnd);            // 1 - Bottom right (left side)
	NegYVerts.Add(BaseboardTopRight);       // 2 - Baseboard top left
	NegYVerts.Add(BaseboardTopLeftEnd);     // 3 - Baseboard top right
	NegYVerts.Add(BaseboardTopCenterNegY);  // 4 - Baseboard center
	NegYVerts.Add(RightPointStartUp);       // 5 - Top left
	NegYVerts.Add(LeftPointEndUp);          // 6 - Top right

	// Baseboard triangles (reversed winding for back face)
	NegYTriangles.Add(0); NegYTriangles.Add(2); NegYTriangles.Add(4);
	NegYTriangles.Add(0); NegYTriangles.Add(4); NegYTriangles.Add(1);
	NegYTriangles.Add(4); NegYTriangles.Add(3); NegYTriangles.Add(1);

	// Main wall triangles (reversed winding for back face)
	NegYTriangles.Add(5); NegYTriangles.Add(4); NegYTriangles.Add(2);
	NegYTriangles.Add(6); NegYTriangles.Add(3); NegYTriangles.Add(4);
	NegYTriangles.Add(6); NegYTriangles.Add(4); NegYTriangles.Add(5); // Top diagonal (LPEU BTCNY RPSU) - reversed winding

	// UVs (same as PosY)
	float baseboardUVHeight2 = BaseboardHeight / Height;
	NegYUV.Add(FVector2D(UVMin, 1.0f));
	NegYUV.Add(FVector2D(UVMax, 1.0f));
	NegYUV.Add(FVector2D(UVMin, 1.0f - baseboardUVHeight2));
	NegYUV.Add(FVector2D(UVMax, 1.0f - baseboardUVHeight2));
	NegYUV.Add(FVector2D((UVMin + UVMax) * 0.5f, 1.0f - baseboardUVHeight2));
	NegYUV.Add(FVector2D(UVMin, 0.0f));
	NegYUV.Add(FVector2D(UVMax, 0.0f));

	// Calculate normal from first triangle (0,2,4) using winding order - reversed to point outward
	FVector NegYNormal = FVector::CrossProduct((NegYVerts[4] - NegYVerts[0]), (NegYVerts[2] - NegYVerts[0])).GetSafeNormal();

	// === STATIC ALPHA MARKERS FOR TRANSITION FACES ===
	// Instead of dynamically calculating transition visibility, we use static alpha markers
	// that identify which face type each triangle is. The material will handle visibility.
	//
	// ALPHA_TRANSITION_START (191/75%)  = START edge transition faces
	// ALPHA_TRANSITION_END   (127/50%)  = END edge transition faces
	// ALPHA_CENTER_FACE      (63/25%)   = Center diagonal faces (always hidden in cutaway)
	// ALPHA_ALWAYS_VISIBLE   (255/100%) = Baseboard, caps (always visible)
	//
	// Material parameters (ShowStartTransition, ShowEndTransition) control actual visibility

	// Assuming you've generated the faces for NegZ, PosZ, NegX, PosX, PosY, NegY using the previous pattern...
	// Add each face to the combined mesh

	// Underside/Bottom face - only add if NOT on ground floor AND no wall below
	// Ground floor walls (Level 0) should NEVER show bottom face as it's underground
	// Bottom face is always hidden in cutaway mode (controlled by material's EnableCutaway parameter)
	if (InWallData.Level > 0 && !HasWallBelow(InWallData))
	{
		// EdgeFaces hide in cutaway - use ALPHA_CENTER_FACE so material hides them
		AddFaceToMesh(NegZVerts, NegZTriangles, NegZUV, NegZNormal, EWallFaceType::EdgeFace, ALPHA_CENTER_FACE);
	}

	// Top face - split into transition-aware zones to prevent triangle gaps in partial cutaway modes
	// The top face geometry already has start/end/center triangles from the mitered center points.
	// By assigning matching alpha markers, these zones appear/disappear in sync with the
	// transition faces on PosY/NegY, sealing the top edge gaps that would otherwise be visible.
	// START zone: triangle at start edge (vertices 3=RPSU, 5=MPSU, 2=LPSU)
	TArray<int32> PosZStartTris = {3, 5, 2};
	AddFaceToMesh(PosZVerts, PosZStartTris, PosZUV, PosZNormal, EWallFaceType::EdgeFace, ALPHA_TRANSITION_START);
	// END zone: triangle at end edge (vertices 0=RPEU, 4=MPEU, 1=LPEU)
	TArray<int32> PosZEndTris = {0, 4, 1};
	AddFaceToMesh(PosZVerts, PosZEndTris, PosZUV, PosZNormal, EWallFaceType::EdgeFace, ALPHA_TRANSITION_END);
	// CENTER zone: main quad (always hidden in cutaway)
	TArray<int32> PosZCenterTris = {0, 1, 2, 1, 3, 2};
	AddFaceToMesh(PosZVerts, PosZCenterTris, PosZUV, PosZNormal, EWallFaceType::EdgeFace, ALPHA_CENTER_FACE);

	if (InWallData.ConnectedWallsAtStartDir.Num() == 0)
	{
		//Start Edge Face (NegX) - Only exists when no connected wall
		// Baseboard triangles: 0-2 (indices 0,2,4 / 0,4,1 / 4,3,1) - Always visible
		TArray<int32> NegXBaseboardTris = {0,2,4, 0,4,1, 4,3,1};
		AddFaceToMesh(NegXVerts, NegXBaseboardTris, NegXUV, NegXNormal, EWallFaceType::BaseboardEdge, ALPHA_ALWAYS_VISIBLE);

		// Upper wall triangles: 3-5 - Hidden in cutaway mode (use ALPHA_CENTER_FACE)
		TArray<int32> NegXUpperTris = {5,4,2, 6,3,4, 6,4,5};
		AddFaceToMesh(NegXVerts, NegXUpperTris, NegXUV, NegXNormal, EWallFaceType::EdgeFace, ALPHA_CENTER_FACE);
	}
	if (InWallData.ConnectedWallsAtEndDir.Num() == 0)
	{
		//End Edge Face (PosX) - Only exists when no connected wall
		// Baseboard triangles: 0-2 (indices 4,2,0 / 1,4,0 / 1,3,4) - Always visible
		TArray<int32> PosXBaseboardTris = {4,2,0, 1,4,0, 1,3,4};
		AddFaceToMesh(PosXVerts, PosXBaseboardTris, PosXUV, PosXNormal, EWallFaceType::BaseboardEdge, ALPHA_ALWAYS_VISIBLE);

		// Upper wall triangles: 3-5 - Hidden in cutaway mode (use ALPHA_CENTER_FACE)
		TArray<int32> PosXUpperTris = {2,4,5, 4,3,6, 5,4,6};
		AddFaceToMesh(PosXVerts, PosXUpperTris, PosXUV, PosXNormal, EWallFaceType::EdgeFace, ALPHA_CENTER_FACE);
	}

	//PosY Surface (adjacent to RoomID1) - Split into baseboard and upper wall
	// Baseboard triangles: 0-2 (indices 0,4,2 / 0,1,4 / 4,1,3) - Always visible in cutaway
	TArray<int32> PosYBaseboardTris = {0,4,2, 0,1,4, 4,1,3};
	AddFaceToMesh(PosYVerts, PosYBaseboardTris, PosYUV, PosYNormal, EWallFaceType::BaseboardSection, ALPHA_ALWAYS_VISIBLE);

	// Upper wall triangle at START edge: triangle 4 (indices 5,2,4) - Static marker for START transitions
	TArray<int32> PosYUpperStartTris = {5,2,4};
	AddFaceToMesh(PosYVerts, PosYUpperStartTris, PosYUV, PosYNormal, EWallFaceType::UpperWall, ALPHA_TRANSITION_START);

	// Upper wall triangle at END edge: triangle 5 (indices 6,4,3) - Static marker for END transitions
	TArray<int32> PosYUpperEndTris = {6,4,3};
	AddFaceToMesh(PosYVerts, PosYUpperEndTris, PosYUV, PosYNormal, EWallFaceType::UpperWall, ALPHA_TRANSITION_END);

	// Top center diagonal: triangle 6 (indices 4,6,5) - Center face (always hidden in cutaway)
	TArray<int32> PosYTopCenterTris = {4,6,5};
	AddFaceToMesh(PosYVerts, PosYTopCenterTris, PosYUV, PosYNormal, EWallFaceType::UpperWall, ALPHA_CENTER_FACE);

	//NegY Surface (adjacent to RoomID2) - Split into baseboard and upper wall
	// Baseboard triangles: 0-2 (indices 0,2,4 / 0,4,1 / 4,3,1) - Always visible in cutaway
	TArray<int32> NegYBaseboardTris = {0,2,4, 0,4,1, 4,3,1};
	AddFaceToMesh(NegYVerts, NegYBaseboardTris, NegYUV, NegYNormal, EWallFaceType::BaseboardSection, ALPHA_ALWAYS_VISIBLE);

	// Upper wall triangle at START edge: triangle 4 (indices 5,4,2) - Static marker for START transitions
	TArray<int32> NegYUpperStartTris = {5,4,2};
	AddFaceToMesh(NegYVerts, NegYUpperStartTris, NegYUV, NegYNormal, EWallFaceType::UpperWall, ALPHA_TRANSITION_START);

	// Upper wall triangle at END edge: triangle 5 (indices 6,3,4) - Static marker for END transitions
	TArray<int32> NegYUpperEndTris = {6,3,4};
	AddFaceToMesh(NegYVerts, NegYUpperEndTris, NegYUV, NegYNormal, EWallFaceType::UpperWall, ALPHA_TRANSITION_END);

	// Top center diagonal: triangle 6 (indices 6,4,5) - Center face (always hidden in cutaway)
	TArray<int32> NegYTopCenterTris = {6,4,5};
	AddFaceToMesh(NegYVerts, NegYTopCenterTris, NegYUV, NegYNormal, EWallFaceType::UpperWall, ALPHA_CENTER_FACE);

	// === CAP FACES (to prevent seeing through cutaway walls) ===

	// Cap 1: Baseboard Top Face (8 vertices, 6 triangles - outer rectangle to inner diamond)
	// This fully seals the horizontal top surface of the baseboard section
	TArray<FVector> BaseboardCapVerts;
	TArray<int32> BaseboardCapTriangles;
	TArray<FVector2D> BaseboardCapUV;

	// Outer rectangle vertices (4 corners)
	BaseboardCapVerts.Add(BaseboardTopLeft);        // 0 - Outer corner (start/PosY)
	BaseboardCapVerts.Add(BaseboardTopRight);       // 1 - Outer corner (start/NegY)
	BaseboardCapVerts.Add(BaseboardTopLeftEnd);     // 2 - Outer corner (end/PosY)
	BaseboardCapVerts.Add(BaseboardTopRightEnd);    // 3 - Outer corner (end/NegY)

	// Inner diamond vertices (4 convergence points)
	BaseboardCapVerts.Add(BaseboardTopCenterStart); // 4 - Inner center (start edge)
	BaseboardCapVerts.Add(BaseboardTopCenterPosY);  // 5 - Inner center (PosY edge)
	BaseboardCapVerts.Add(BaseboardTopCenterEnd);   // 6 - Inner center (end edge)
	BaseboardCapVerts.Add(BaseboardTopCenterNegY);  // 7 - Inner center (NegY edge)

	// 6 triangles: 3 for start edge + 3 for end edge (split center diamond along 7-5 diagonal)
	// All triangles reversed for correct upward-facing winding
	// Start edge triangles
	BaseboardCapTriangles.Add(4); BaseboardCapTriangles.Add(7); BaseboardCapTriangles.Add(1); // Reversed: start center to NegY center to right corner
	BaseboardCapTriangles.Add(5); BaseboardCapTriangles.Add(7); BaseboardCapTriangles.Add(4); // Reversed: PosY center to NegY center to start center (half of center diamond)
	BaseboardCapTriangles.Add(0); BaseboardCapTriangles.Add(5); BaseboardCapTriangles.Add(4); // Reversed: left corner to PosY center to start center
	// End edge triangles
	BaseboardCapTriangles.Add(6); BaseboardCapTriangles.Add(2); BaseboardCapTriangles.Add(7); // Reversed: end center to left corner to NegY center
	BaseboardCapTriangles.Add(5); BaseboardCapTriangles.Add(6); BaseboardCapTriangles.Add(7); // Reversed: PosY center to end center to NegY center (other half of center diamond)
	BaseboardCapTriangles.Add(3); BaseboardCapTriangles.Add(6); BaseboardCapTriangles.Add(5); // Reversed: right corner to end center to PosY center

	// UVs for 8 vertices (outer rectangle corners + inner diamond points)
	// Project vertices onto wall plane to get correct UV coordinates for portal cutouts
	for (int32 i = 0; i < BaseboardCapVerts.Num(); i++)
	{
		// Project vertex position onto the wall to get U coordinate
		FVector ToVertex = BaseboardCapVerts[i] - InWallData.StartLoc;
		float U = FVector::DotProduct(ToVertex, WallDirection) / WallWidth;
		float UScaled = UVMin + (U * (UVMax - UVMin));

		// V coordinate is based on height (Z component)
		float V = (BaseboardCapVerts[i].Z - InWallData.StartLoc.Z) / Height;

		BaseboardCapUV.Add(FVector2D(UScaled, 1.0f - V)); // Flip V to match texture orientation
	}

	FVector BaseboardCapNormal = FVector(0, 0, 1); // Facing up
	AddFaceToMesh(BaseboardCapVerts, BaseboardCapTriangles, BaseboardCapUV, BaseboardCapNormal, EWallFaceType::Cap, ALPHA_ALWAYS_VISIBLE);

	// === DEBUG VISUALIZATION ===
	// Draw vertex indices for baseboard cap
	// Controlled by console command: burb.debug.walls
	if (BurbArchitectDebug::IsWallDebugEnabled() && GetWorld())
	{
		for (int32 i = 0; i < BaseboardCapVerts.Num(); i++)
		{
			FVector WorldPos = BaseboardCapVerts[i];

			// Draw sphere at vertex position
			DrawDebugSphere(GetWorld(), WorldPos, 5.0f, 8, FColor::Yellow, false, 10.0f, 0, 2.0f);

			// Draw vertex index as text
			FString VertexLabel = FString::Printf(TEXT("%d"), i);
			DrawDebugString(GetWorld(), WorldPos + FVector(0, 0, 10), VertexLabel, nullptr, FColor::White, 10.0f, true, 2.0f);
		}

		// Draw triangle edges
		for (int32 i = 0; i < BaseboardCapTriangles.Num(); i += 3)
		{
			int32 V0 = BaseboardCapTriangles[i];
			int32 V1 = BaseboardCapTriangles[i + 1];
			int32 V2 = BaseboardCapTriangles[i + 2];

			DrawDebugLine(GetWorld(), BaseboardCapVerts[V0], BaseboardCapVerts[V1], FColor::Cyan, false, 10.0f, 0, 1.0f);
			DrawDebugLine(GetWorld(), BaseboardCapVerts[V1], BaseboardCapVerts[V2], FColor::Cyan, false, 10.0f, 0, 1.0f);
			DrawDebugLine(GetWorld(), BaseboardCapVerts[V2], BaseboardCapVerts[V0], FColor::Cyan, false, 10.0f, 0, 1.0f);
		}
	}

	// Cap 2: Start Edge Diagonal Cap
	// This caps the diagonal cut at the start edge from top edge down to convergence points
	TArray<FVector> StartDiagCapVerts;
	TArray<int32> StartDiagCapTriangles;
	TArray<FVector2D> StartDiagCapUV;

	StartDiagCapVerts.Add(RightPointStartUp);      // 0 - RPSU (right top corner at start)
	StartDiagCapVerts.Add(BaseboardTopCenterNegY); // 1 - BTCNY (NegY convergence point)
	StartDiagCapVerts.Add(LeftPointStartUp);       // 2 - LPSU (left top corner at start)
	StartDiagCapVerts.Add(BaseboardTopCenterPosY); // 3 - BTCPY (PosY convergence point)

	// Two triangles: LPSU -> BTCNY -> RPSU (reversed), then LPSU -> BTCPY -> BTCNY
	StartDiagCapTriangles.Add(2); StartDiagCapTriangles.Add(1); StartDiagCapTriangles.Add(0);
	StartDiagCapTriangles.Add(2); StartDiagCapTriangles.Add(3); StartDiagCapTriangles.Add(1);

	// Project vertices onto wall plane to get correct UV coordinates for portal cutouts
	for (int32 i = 0; i < StartDiagCapVerts.Num(); i++)
	{
		// Project vertex position onto the wall to get U coordinate
		FVector ToVertex = StartDiagCapVerts[i] - InWallData.StartLoc;
		float U = FVector::DotProduct(ToVertex, WallDirection) / WallWidth;
		float UScaled = UVMin + (U * (UVMax - UVMin));

		// V coordinate is based on height (Z component)
		float V = (StartDiagCapVerts[i].Z - InWallData.StartLoc.Z) / Height;

		StartDiagCapUV.Add(FVector2D(UScaled, 1.0f - V)); // Flip V to match texture orientation
	}

	FVector StartDiagCapNormal = FVector::CrossProduct((StartDiagCapVerts[1] - StartDiagCapVerts[0]), (StartDiagCapVerts[2] - StartDiagCapVerts[0])).GetSafeNormal();
	AddFaceToMesh(StartDiagCapVerts, StartDiagCapTriangles, StartDiagCapUV, StartDiagCapNormal, EWallFaceType::Cap, ALPHA_TRANSITION_START);

	// Cap 3: End Edge Diagonal Cap
	// This caps the diagonal cut at the end edge from top edge down to convergence points
	TArray<FVector> EndDiagCapVerts;
	TArray<int32> EndDiagCapTriangles;
	TArray<FVector2D> EndDiagCapUV;

	EndDiagCapVerts.Add(RightPointEndUp);          // 0 - RPEU (right top corner at end)
	EndDiagCapVerts.Add(BaseboardTopCenterNegY);   // 1 - BTCNY (NegY convergence point)
	EndDiagCapVerts.Add(LeftPointEndUp);           // 2 - LPEU (left top corner at end)
	EndDiagCapVerts.Add(BaseboardTopCenterPosY);   // 3 - BTCPY (PosY convergence point)

	// Two triangles: LPEU -> BTCNY -> RPEU (reversed), then RPEU -> BTCNY -> BTCPY
	EndDiagCapTriangles.Add(2); EndDiagCapTriangles.Add(1); EndDiagCapTriangles.Add(0);
	EndDiagCapTriangles.Add(0); EndDiagCapTriangles.Add(1); EndDiagCapTriangles.Add(3);

	// Project vertices onto wall plane to get correct UV coordinates for portal cutouts
	for (int32 i = 0; i < EndDiagCapVerts.Num(); i++)
	{
		// Project vertex position onto the wall to get U coordinate
		FVector ToVertex = EndDiagCapVerts[i] - InWallData.StartLoc;
		float U = FVector::DotProduct(ToVertex, WallDirection) / WallWidth;
		float UScaled = UVMin + (U * (UVMax - UVMin));

		// V coordinate is based on height (Z component)
		float V = (EndDiagCapVerts[i].Z - InWallData.StartLoc.Z) / Height;

		EndDiagCapUV.Add(FVector2D(UScaled, 1.0f - V)); // Flip V to match texture orientation
	}

	FVector EndDiagCapNormal = FVector::CrossProduct((EndDiagCapVerts[2] - EndDiagCapVerts[0]), (EndDiagCapVerts[1] - EndDiagCapVerts[0])).GetSafeNormal();
	AddFaceToMesh(EndDiagCapVerts, EndDiagCapTriangles, EndDiagCapUV, EndDiagCapNormal, EWallFaceType::Cap, ALPHA_TRANSITION_END);

	InWallData.Face1Triangles = PosYTriangles;
	InWallData.Face2Triangles = NegYTriangles;
	InWallData.Face1Vertices = PosYVerts;
	InWallData.Face2Vertices = NegYVerts;
	
	
	//if we need more or less verts then we can signal to regenerate the mesh
	if(!GetProcMeshSection(NewSectionIndex))
	{
		CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
	}
	else
	{
		// Check if vertex count changed or section is empty
		FProcMeshSection* Section = GetProcMeshSection(NewSectionIndex);
		if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != Vertices.Num())
		{
			CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
		}
		else
		{
			UpdateMeshSection(NewSectionIndex, Vertices, Normals, UVs, VertexColors, Tangents);
		}
	}

	// Add margin to render target to allow cutouts to extend past wall edges and overlap with adjacent walls
	// This fixes the visible seam when portals span across two aligned walls
	// (ExtendedWidth calculated above in UV section)
	InWallData.RenderTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), ExtendedWidth, Height);
	InWallData.Triangles = Triangles;
	InWallData.Vertices = Vertices;
	InWallData.WallRotation = WallRotation;

	// CRITICAL: Get WallSectionData pointer AFTER Create/UpdateMeshSection completes
	// This ensures we have a valid pointer even if CreateMeshSection reallocated internal storage
	InWallData.WallSectionData = GetProcMeshSection(InWallData.SectionIndex);

	// CRITICAL: After updating this wall's mesh, refresh WallSectionData pointers for ALL other walls
	// CreateMeshSection may have reallocated ProceduralMeshComponent's internal section array,
	// invalidating all previously stored WallSectionData pointers (dangling pointers)
	// This fixes the bug where walls lose their section pointers when other walls regenerate
	// and call CreateMeshSection (e.g. during DetectRoomsForBatch mitring updates)
	for (int32 i = 0; i < WallDataArray.Num(); i++)
	{
		if (i != InWallData.WallArrayIndex && WallDataArray[i].SectionIndex >= 0)
		{
			// Refresh the section pointer for this wall (may have been invalidated by reallocation)
			WallDataArray[i].WallSectionData = GetProcMeshSection(WallDataArray[i].SectionIndex);
		}
	}

	// Assign array index if not already set
	if(InWallData.WallArrayIndex == -1)
	{
		if (WallFreeIndices.Num() > 0)
		{
			InWallData.WallArrayIndex = WallFreeIndices.Pop();
		}else
		{
			InWallData.WallArrayIndex = WallDataArray.Add(InWallData);
		}
	}

	// Validate index before accessing array
	if (!WallDataArray.IsValidIndex(InWallData.WallArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateWallMeshSection: Invalid WallArrayIndex %d (array size: %d)"), InWallData.WallArrayIndex, WallDataArray.Num());
		// Fallback: add to array
		InWallData.WallArrayIndex = WallDataArray.Add(InWallData);
	}

	WallDataArray[InWallData.WallArrayIndex] = InWallData;
	return WallDataArray[InWallData.WallArrayIndex];
}

FVector UWallComponent::CalculateIntersection(const FVector& MainDirection, const FVector& MainPoint,
	const FVector& TargetDirection, const FVector& TargetPoint, const float SpacePoint)
{
	const float Denominator = MainDirection.X * TargetDirection.Y - MainDirection.Y * TargetDirection.X;
    
	if (FMath::Abs(Denominator) < KINDA_SMALL_NUMBER)
	{
		return MainPoint + MainDirection.Rotation().RotateVector(FVector::BackwardVector * SpacePoint);
	}
	
	float t1 = ((TargetPoint.X - MainPoint.X) * TargetDirection.Y - (TargetPoint.Y - MainPoint.Y) * TargetDirection.X) / Denominator;
    
	FVector Intersection = MainPoint + t1 * MainDirection;
    
	float Z = 2.0f;
	return FVector(Intersection.X, Intersection.Y, Intersection.Z);
}

void UWallComponent::CalculateVectorAngles(const FVector& Dir1, const FVector& Dir2, float& OutRightAngle,
	float& OutLeftAngle)
{
	// Normalize the vectors to ensure calculations are accurate for direction only
	FVector NormDir1 = Dir1.GetSafeNormal();
	FVector NormDir2 = Dir2.GetSafeNormal();

	// Calculate the angle in degrees between the two vectors
	float CosTheta = FVector::DotProduct(NormDir1, NormDir2);
	float Angle = FMath::Acos(FMath::Clamp(CosTheta, -1.0f, 1.0f)) * (180.f / PI);

	// Using cross product to determine the sign of the angle
	FVector CrossProd = FVector::CrossProduct(NormDir1, NormDir2);

	// Assuming Z-up world - determine the sign of the Z component of the cross product
	if (CrossProd.Z < 0)
	{
		OutRightAngle = Angle;       // Clockwise angle
		OutLeftAngle = 360.f - Angle;  // Counterclockwise angle
	}
	else
	{
		OutRightAngle = 360.f - Angle;  // Clockwise angle
		OutLeftAngle = Angle;           // Counterclockwise angle
	}
}

FWallSegmentData UWallComponent::GenerateWallSection(int32 Level, const FVector& TileCornerStart, const FVector& TileCornerEnd, float InWallHeight)
{
	FWallSegmentData NewWallData;
	NewWallData.Level = Level;
	NewWallData.StartLoc = TileCornerStart;
	NewWallData.EndLoc = TileCornerEnd;
	NewWallData.SectionIndex = -1;
	NewWallData.WallArrayIndex = -1;  // Initialize to -1 so GenerateWallMeshSection knows to assign a new index
	NewWallData.Height = InWallHeight;
	NewWallData.EndHeight = InWallHeight;
	NewWallData.WallTextures = FWallTextures();

	// Grid coordinates are no longer stored in FWallSegmentData
	// They are managed by WallGraphComponent instead
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (OurLot)
	{
		// Get grid coordinates for level validation only
		int32 StartRow, StartColumn;
		OurLot->LocationToTile(TileCornerStart, StartRow, StartColumn);

		// CRITICAL FIX: Derive the wall's level from the actual tiles it connects to
		// This ensures the wall's Level always matches the tiles' Level for proper room detection
		// The IsWallBetweenTiles function requires Wall.Level == Tile.Level
		FTileData StartTile = OurLot->FindTileByGridCoords(StartRow, StartColumn, Level);
		if (StartTile.TileIndex >= 0)
		{
			// Validate: warn if the passed-in Level doesn't match the tile's actual Level
			if (NewWallData.Level != StartTile.Level)
			{
				UE_LOG(LogTemp, Warning, TEXT("GenerateWallSection: Correcting wall level from %d to %d (tile's level) at grid coords (%d,%d)"),
					NewWallData.Level, StartTile.Level, StartRow, StartColumn);
			}

			// Override the wall's level to match the tile's level
			NewWallData.Level = StartTile.Level;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GenerateWallSection: Could not find tile at grid coords (%d,%d) level %d - wall may not participate in room detection correctly"),
				StartRow, StartColumn, Level);
		}
	}

	return GenerateWallMeshSection(NewWallData);
}

FWallSegmentData UWallComponent::GenerateWallSection(int32 Level, const FVector& TileCornerStart, const FVector& TileCornerEnd, float StartHeight, float EndHeight)
{
	FWallSegmentData NewWallData;
	NewWallData.Level = Level;
	NewWallData.StartLoc = TileCornerStart;
	NewWallData.EndLoc = TileCornerEnd;
	NewWallData.SectionIndex = -1;
	NewWallData.WallArrayIndex = -1;  // Initialize to -1 so GenerateWallMeshSection knows to assign a new index
	NewWallData.Height = StartHeight;
	NewWallData.EndHeight = EndHeight;
	NewWallData.WallTextures = FWallTextures();

	// Grid coordinates are no longer stored in FWallSegmentData
	// They are managed by WallGraphComponent instead
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (OurLot)
	{
		// Get grid coordinates for level validation only
		int32 StartRow, StartColumn;
		OurLot->LocationToTile(TileCornerStart, StartRow, StartColumn);

		// CRITICAL FIX: Derive the wall's level from the actual tiles it connects to
		// This ensures the wall's Level always matches the tiles' Level for proper room detection
		// The IsWallBetweenTiles function requires Wall.Level == Tile.Level
		FTileData StartTile = OurLot->FindTileByGridCoords(StartRow, StartColumn, Level);
		if (StartTile.TileIndex >= 0)
		{
			// Validate: warn if the passed-in Level doesn't match the tile's actual Level
			if (NewWallData.Level != StartTile.Level)
			{
				UE_LOG(LogTemp, Warning, TEXT("GenerateWallSection (varying heights): Correcting wall level from %d to %d (tile's level) at grid coords (%d,%d)"),
					NewWallData.Level, StartTile.Level, StartRow, StartColumn);
			}

			// Override the wall's level to match the tile's level
			NewWallData.Level = StartTile.Level;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GenerateWallSection (varying heights): Could not find tile at grid coords (%d,%d) level %d - wall may not participate in room detection correctly"),
				StartRow, StartColumn, Level);
		}
	}

	return GenerateWallMeshSection(NewWallData);
}

void UWallComponent::DestroyWallSection(FWallSegmentData InWallData)
{
	int32 FoundIndex = WallDataArray.Find(InWallData);
	if(FoundIndex != INDEX_NONE)
	{
		// Invalidate affected rooms in the cache before removing the wall
		if (ALotManager* OurLot = Cast<ALotManager>(GetOwner()))
		{
			// Get tiles adjacent to this wall to determine affected rooms
			TArray<FTileData> AdjacentTiles = OurLot->GetTilesAdjacentToWall(InWallData.StartLoc, InWallData.EndLoc, InWallData.Level);
			for (const FTileData& Tile : AdjacentTiles)
			{
				if (Tile.GetPrimaryRoomID() > 0)
					OurLot->InvalidateRoom(Tile.GetPrimaryRoomID());
			}
		}

		ClearMeshSection(InWallData.SectionIndex);
		/*
		if (InWallData.ConnectedWallsAtEnd.Num() != 0)
		{
			RegenerateWallSection(InWallData.ConnectedWallsAtEnd[0], true);
		}
		if(InWallData.ConnectedWallsAtStart.Num() != 0)
		{
			RegenerateWallSection(InWallData.ConnectedWallsAtStart[0], true);
		}*/

		// Mark as uncommitted instead of removing to preserve array indices
		WallDataArray[FoundIndex].bCommitted = false;
		WallFreeIndices.Add(FoundIndex);
	}
}

void UWallComponent::RemoveWallSection(int WallArrayIndex)
{
	if(WallDataArray.Num() > WallArrayIndex)
	{
		// Store wall info before removal for adjacent level checks
		FWallSegmentData WallToRemove = WallDataArray[WallArrayIndex];

		// Invalidate affected rooms in the cache before removing the wall
		if (ALotManager* OurLot = Cast<ALotManager>(GetOwner()))
		{
			// Get tiles adjacent to this wall to determine affected rooms
			TArray<FTileData> AdjacentTiles = OurLot->GetTilesAdjacentToWall(WallToRemove.StartLoc, WallToRemove.EndLoc, WallToRemove.Level);
			for (const FTileData& Tile : AdjacentTiles)
			{
				if (Tile.GetPrimaryRoomID() > 0)
					OurLot->InvalidateRoom(Tile.GetPrimaryRoomID());
			}
		}

		// Clear Mesh Section
		ClearMeshSection(WallDataArray[WallArrayIndex].SectionIndex);

		// Disable The Wall Data
		WallDataArray[WallArrayIndex].bCommitted = false;
		WallFreeIndices.Add(WallArrayIndex);

		// Regenerate Connected Walls
		for (auto& WallIndex : WallDataArray[WallArrayIndex].ConnectedWallsSections)
		{
			WallDataArray[WallIndex].ConnectedWallsSections.Empty();
			WallDataArray[WallIndex].ConnectedWallsAtStartDir.Empty();
			WallDataArray[WallIndex].ConnectedWallsAtEndDir.Empty();
			RegenerateWallSection(WallDataArray[WallIndex], true);
		}
	}
}

FWallSegmentData& UWallComponent::GetWallDataByIndex(int32 WallIndex)
{
	if (!WallDataArray.IsValidIndex(WallIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("GetWallDataByIndex: Invalid WallIndex %d (array size: %d)"), WallIndex, WallDataArray.Num());
		// Return a static dummy to avoid crash (not ideal but safer than crashing)
		static FWallSegmentData Dummy;
		return Dummy;
	}
	return WallDataArray[WallIndex];
}

bool UWallComponent::IsSharedMaterial(UMaterialInstanceDynamic* Material) const
{
	if (!Material)
	{
		return false;
	}

	// Check if this material is in our shared materials cache
	for (const auto& Pair : SharedMaterialInstances)
	{
		if (Pair.Value == Material)
		{
			return true;
		}
	}

	return false;
}

bool UWallComponent::HasWallAbove(const FWallSegmentData& Wall) const
{
	// Check if there's a wall directly above this one on the next level
	int32 LevelAbove = Wall.Level + 1;

	// Convert world positions to grid coordinates for this wall
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
		return false;

	int32 WallStartRow, WallStartColumn, WallEndRow, WallEndColumn;
	if (!OurLot->LocationToTile(Wall.StartLoc, WallStartRow, WallStartColumn) ||
		!OurLot->LocationToTile(Wall.EndLoc, WallEndRow, WallEndColumn))
	{
		UE_LOG(LogTemp, Verbose, TEXT("HasWallAbove: Wall has invalid grid coordinates, using position fallback"));

		// Fallback to position-based comparison
		for (const FWallSegmentData& OtherWall : WallDataArray)
		{
			if (!OtherWall.bCommitted || OtherWall.Level != LevelAbove)
				continue;

			// Use position comparison with tolerance
			const float Tolerance = 1.0f;
			bool bSamePosition =
				(FVector::Dist(OtherWall.StartLoc, Wall.StartLoc) < Tolerance &&
				 FVector::Dist(OtherWall.EndLoc, Wall.EndLoc) < Tolerance) ||
				(FVector::Dist(OtherWall.StartLoc, Wall.EndLoc) < Tolerance &&
				 FVector::Dist(OtherWall.EndLoc, Wall.StartLoc) < Tolerance);

			if (bSamePosition)
			{
				UE_LOG(LogTemp, Verbose, TEXT("HasWallAbove: Found wall above using position match"));
				return true;
			}
		}
		return false;
	}

	for (const FWallSegmentData& OtherWall : WallDataArray)
	{
		if (!OtherWall.bCommitted || OtherWall.Level != LevelAbove)
			continue;

		// Convert other wall's positions to grid coordinates
		int32 OtherStartRow, OtherStartColumn, OtherEndRow, OtherEndColumn;
		if (!OurLot->LocationToTile(OtherWall.StartLoc, OtherStartRow, OtherStartColumn) ||
			!OurLot->LocationToTile(OtherWall.EndLoc, OtherEndRow, OtherEndColumn))
			continue;

		// Check if walls have the same start/end positions using grid coordinates
		bool bSamePosition =
			(OtherStartRow == WallStartRow &&
			 OtherStartColumn == WallStartColumn &&
			 OtherEndRow == WallEndRow &&
			 OtherEndColumn == WallEndColumn) ||
			(OtherStartRow == WallEndRow &&
			 OtherStartColumn == WallEndColumn &&
			 OtherEndRow == WallStartRow &&
			 OtherEndColumn == WallStartColumn);

		if (bSamePosition)
		{
			UE_LOG(LogTemp, Verbose, TEXT("HasWallAbove: Found wall above at Level %d"), LevelAbove);
			return true;
		}
	}

	return false;
}

bool UWallComponent::HasWallBelow(const FWallSegmentData& Wall) const
{
	// Ground floor walls never have walls below
	if (Wall.Level == 0)
		return false;

	int32 LevelBelow = Wall.Level - 1;

	// Convert world positions to grid coordinates for this wall
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
		return false;

	int32 WallStartRow, WallStartColumn, WallEndRow, WallEndColumn;
	if (!OurLot->LocationToTile(Wall.StartLoc, WallStartRow, WallStartColumn) ||
		!OurLot->LocationToTile(Wall.EndLoc, WallEndRow, WallEndColumn))
	{
		// Fallback to position-based comparison
		for (const FWallSegmentData& OtherWall : WallDataArray)
		{
			if (!OtherWall.bCommitted || OtherWall.Level != LevelBelow)
				continue;

			// Use position comparison with tolerance
			const float Tolerance = 1.0f;
			bool bSamePosition =
				(FVector::Dist(OtherWall.StartLoc, Wall.StartLoc) < Tolerance &&
				 FVector::Dist(OtherWall.EndLoc, Wall.EndLoc) < Tolerance) ||
				(FVector::Dist(OtherWall.StartLoc, Wall.EndLoc) < Tolerance &&
				 FVector::Dist(OtherWall.EndLoc, Wall.StartLoc) < Tolerance);

			if (bSamePosition)
				return true;
		}
		return false;
	}

	for (const FWallSegmentData& OtherWall : WallDataArray)
	{
		if (!OtherWall.bCommitted || OtherWall.Level != LevelBelow)
			continue;

		// Convert other wall's positions to grid coordinates
		int32 OtherStartRow, OtherStartColumn, OtherEndRow, OtherEndColumn;
		if (!OurLot->LocationToTile(OtherWall.StartLoc, OtherStartRow, OtherStartColumn) ||
			!OurLot->LocationToTile(OtherWall.EndLoc, OtherEndRow, OtherEndColumn))
			continue;

		// Check if walls have the same start/end positions using grid coordinates
		bool bSamePosition =
			(OtherStartRow == WallStartRow &&
			 OtherStartColumn == WallStartColumn &&
			 OtherEndRow == WallEndRow &&
			 OtherEndColumn == WallEndColumn) ||
			(OtherStartRow == WallEndRow &&
			 OtherStartColumn == WallEndColumn &&
			 OtherEndRow == WallStartRow &&
			 OtherEndColumn == WallStartColumn);

		if (bSamePosition)
			return true;
	}

	return false;
}

//Checks ends and starts for neighbors and if it finds them regenerates wall meshes to connect corners and junctions
void UWallComponent::RegenerateWallSection(FWallSegmentData InWallData, bool bRecursive)
{
	// Validate array index before accessing
	if (!WallDataArray.IsValidIndex(InWallData.WallArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("RegenerateWallSection: Invalid WallArrayIndex %d (array size: %d)"), InWallData.WallArrayIndex, WallDataArray.Num());
		return;
	}

	ALotManager* OurLot = Cast<ALotManager>(GetOwner());

	FVector WallDirection = WallDataArray[InWallData.WallArrayIndex].EndLoc - WallDataArray[InWallData.WallArrayIndex].StartLoc;
	WallDirection.Z = 0.0f; // Ignore the Z component for 2D rotation
	WallDirection.Normalize();

	// Check for neighbors at the start location
	for (FWallSegmentData FoundWall : WallDataArray)
	{
		FVector FundWallDirection = FoundWall.EndLoc - FoundWall.StartLoc;
		FundWallDirection.Z = 0.0f;
		FundWallDirection.Normalize();

		const bool bNotSelf = FoundWall.WallArrayIndex != InWallData.WallArrayIndex ;
		const bool bIsEndWall = (FoundWall.StartLoc == WallDataArray[InWallData.WallArrayIndex].EndLoc || FoundWall.EndLoc == WallDataArray[InWallData.WallArrayIndex].EndLoc) ;
		const bool bIsStartWall = (FoundWall.StartLoc == WallDataArray[InWallData.WallArrayIndex].StartLoc || FoundWall.EndLoc == WallDataArray[InWallData.WallArrayIndex].StartLoc);
		const int32 GetNegIfBothStart = 1 - 2 * static_cast<int>(InWallData.StartLoc != FoundWall.StartLoc);
		const int32 GetNegIfBothEnd = 1 - 2 * static_cast<int>(InWallData.EndLoc != FoundWall.EndLoc);

		// Decorative wall connection logic:
		// - Regular walls (WallEdgeID >= 0) should NOT connect to decorative walls
		// - Decorative walls (WallEdgeID == -1) CAN connect to other decorative walls with matching height
		// This allows half walls to mitre with each other without affecting regular wall corners
		const bool bCurrentIsDecorative = (WallDataArray[InWallData.WallArrayIndex].WallEdgeID == -1);
		const bool bFoundIsDecorative = (FoundWall.WallEdgeID == -1);

		// Height tolerance for matching decorative walls (e.g., half walls at 150 units)
		const float HeightTolerance = 1.0f;
		const bool bHeightsMatch = FMath::IsNearlyEqual(
			WallDataArray[InWallData.WallArrayIndex].Height,
			FoundWall.Height,
			HeightTolerance);

		// Determine if we should skip this wall for connection purposes
		bool bShouldSkipConnection = false;
		if (bCurrentIsDecorative)
		{
			// Current wall is decorative - only connect to other decorative walls with matching height
			bShouldSkipConnection = !bFoundIsDecorative || !bHeightsMatch;
		}
		else
		{
			// Current wall is regular - skip all decorative walls (original behavior)
			bShouldSkipConnection = bFoundIsDecorative;
		}

		if ( FoundWall.bCommitted && !bShouldSkipConnection && bIsStartWall && bNotSelf && !WallDataArray[InWallData.WallArrayIndex].ConnectedWallsAtStartDir.Contains(FundWallDirection * GetNegIfBothStart))
		{
			WallDataArray[InWallData.WallArrayIndex].ConnectedWallsAtStartDir.Add(FundWallDirection * GetNegIfBothStart);
			WallDataArray[InWallData.WallArrayIndex].ConnectedWallsSections.Add(FoundWall.WallArrayIndex);

			if(bRecursive)
			{
				RegenerateWallSection(WallDataArray[FoundWall.WallArrayIndex], false);
			}
		}
		else if ( FoundWall.bCommitted && !bShouldSkipConnection && bIsEndWall && bNotSelf && !WallDataArray[InWallData.WallArrayIndex].ConnectedWallsAtEndDir.Contains(FundWallDirection * GetNegIfBothEnd))
		{
			WallDataArray[InWallData.WallArrayIndex].ConnectedWallsAtEndDir.Add(FundWallDirection * GetNegIfBothEnd);
			WallDataArray[InWallData.WallArrayIndex].ConnectedWallsSections.Add(FoundWall.WallArrayIndex);

			if(bRecursive)
			{
				RegenerateWallSection(WallDataArray[FoundWall.WallArrayIndex], false);
			}
		}
	}

	GenerateWallMeshSection(WallDataArray[InWallData.WallArrayIndex]);

	// NEW: Reapply roof trimming if this wall has trim data (must happen AFTER mesh regeneration)
	ApplyRoofTrimming(InWallData.WallArrayIndex);

	// CRITICAL FIX: Reapply material after mesh regeneration
	// When CreateMeshSection is called during GenerateWallMeshSection (vertex count changed due to mitring),
	// the new mesh section has no material applied. The material exists in WallDataArray[].WallMaterial
	// but must be explicitly set on the ProceduralMeshComponent section.
	// Without this, regenerated walls have invalid materials and portal cutouts fail.
	if (WallDataArray.IsValidIndex(InWallData.WallArrayIndex) &&
	    WallDataArray[InWallData.WallArrayIndex].WallMaterial)
	{
		SetMaterial(WallDataArray[InWallData.WallArrayIndex].SectionIndex,
		            WallDataArray[InWallData.WallArrayIndex].WallMaterial);
	}
}

void UWallComponent::ApplyRoofTrimming(int32 WallArrayIndex)
{
	// Reapply roof trimming from stored data (called after wall regeneration)

	if (!WallRoofTrimData.Contains(WallArrayIndex))
	{
		return; // No trim data for this wall
	}

	if (!WallDataArray.IsValidIndex(WallArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyRoofTrimming: Invalid wall array index %d"), WallArrayIndex);
		return;
	}

	const FRoofTrimData& TrimData = WallRoofTrimData[WallArrayIndex];
	FWallSegmentData& WallSegment = WallDataArray[WallArrayIndex];
	int32 SectionIndex = WallSegment.SectionIndex;

	// Get the procedural mesh section
	FProcMeshSection* MeshSection = GetProcMeshSection(SectionIndex);
	if (!MeshSection)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyRoofTrimming: Failed to get mesh section %d for wall %d"),
			SectionIndex, WallArrayIndex);
		return;
	}

	// Trim vertices in the mesh section
	bool bModified = false;
	for (int32 i = 0; i < MeshSection->ProcVertexBuffer.Num(); i++)
	{
		FProcMeshVertex& Vertex = MeshSection->ProcVertexBuffer[i];

		// Only modify vertices that are above the base (top vertices)
		if (Vertex.Position.Z > TrimData.BaseZ + 10.0f)
		{
			// Calculate roof height at this vertex's XY position
			FVector2D Position2D(Vertex.Position.X, Vertex.Position.Y);
			float RoofHeight = URoofComponent::CalculateRoofHeightAtPosition(Position2D, TrimData.RoofVerts, TrimData.RoofType);

			// Clamp vertex Z to not exceed roof height, but preserve baseboard geometry
			// Baseboard is 30 units tall, so trimmed wall must not go below BaseZ + 30
			const float BaseboardHeight = 30.0f;
			float MinTrimHeight = TrimData.BaseZ + BaseboardHeight;
			float ClampedRoofHeight = FMath::Max(RoofHeight, MinTrimHeight);

			if (Vertex.Position.Z > ClampedRoofHeight)
			{
				Vertex.Position.Z = ClampedRoofHeight;
				bModified = true;
			}
		}
	}

	// Update the mesh section if we modified vertices
	if (bModified)
	{
		// Extract vertex components from ProcVertexBuffer
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;

		Vertices.Reserve(MeshSection->ProcVertexBuffer.Num());
		Normals.Reserve(MeshSection->ProcVertexBuffer.Num());
		UV0.Reserve(MeshSection->ProcVertexBuffer.Num());
		VertexColors.Reserve(MeshSection->ProcVertexBuffer.Num());
		Tangents.Reserve(MeshSection->ProcVertexBuffer.Num());

		for (const FProcMeshVertex& Vertex : MeshSection->ProcVertexBuffer)
		{
			Vertices.Add(Vertex.Position);
			Normals.Add(Vertex.Normal);
			UV0.Add(Vertex.UV0);
			VertexColors.Add(Vertex.Color);
			Tangents.Add(Vertex.Tangent);
		}

		UpdateMeshSection(SectionIndex, Vertices, Normals, UV0, VertexColors, Tangents);
		UE_LOG(LogTemp, Log, TEXT("ApplyRoofTrimming: Reapplied trimming to wall %d after regeneration"), WallArrayIndex);
	}
}

void UWallComponent::UpdateAllWallRoomIDs()
{
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
		return;

	int32 UpdatedCount = 0;

	// Iterate through all committed walls and update their RoomIDs from adjacent tiles
	for (FWallSegmentData& Wall : WallDataArray)
	{
		if (!Wall.bCommitted)
			continue;

		TArray<FTileData> AdjacentTiles = OurLot->GetTilesAdjacentToWall(Wall.StartLoc, Wall.EndLoc, Wall.Level);

		if (AdjacentTiles.Num() >= 2)
		{
			// Use deterministic grid sampling - same logic as GenerateWallMeshSection
			FVector WallDirection = (Wall.EndLoc - Wall.StartLoc).GetSafeNormal();
			FVector WallNormal = FVector(-WallDirection.Y, WallDirection.X, 0.0f).GetSafeNormal();
			FVector WallCenter = (Wall.StartLoc + Wall.EndLoc) * 0.5f;

			// Sample two points perpendicular to the wall
			float SampleOffset = 50.0f; // Half a tile size
			FVector SamplePosNormal = WallCenter + (WallNormal * SampleOffset);
			FVector SampleNegNormal = WallCenter - (WallNormal * SampleOffset);

			// Find which tiles these sample points are in
			int32 RowPos, ColPos, RowNeg, ColNeg;
			bool bFoundPos = OurLot->LocationToTile(SamplePosNormal, RowPos, ColPos);
			bool bFoundNeg = OurLot->LocationToTile(SampleNegNormal, RowNeg, ColNeg);

			if (bFoundPos && bFoundNeg)
			{
				// Get the tiles at these grid coordinates
				FTileData TilePosNormal = OurLot->FindTileByGridCoords(RowPos, ColPos, Wall.Level);
				FTileData TileNegNormal = OurLot->FindTileByGridCoords(RowNeg, ColNeg, Wall.Level);

				// Update WallGraph edge with room assignments (only for walls not already assigned)
				if (Wall.WallEdgeID != -1 && OurLot->WallGraph)
				{
					FWallEdge* Edge = OurLot->WallGraph->Edges.Find(Wall.WallEdgeID);
					if (Edge)
					{
						Edge->Room1 = TilePosNormal.GetPrimaryRoomID(); // PosY face
						Edge->Room2 = TileNegNormal.GetPrimaryRoomID(); // NegY face
					}
				}
			}
			else
			{
				// Fallback - update WallGraph edge
				if (Wall.WallEdgeID != -1 && OurLot->WallGraph)
				{
					FWallEdge* Edge = OurLot->WallGraph->Edges.Find(Wall.WallEdgeID);
					if (Edge)
					{
						Edge->Room1 = AdjacentTiles[0].GetPrimaryRoomID();
						Edge->Room2 = AdjacentTiles[1].GetPrimaryRoomID();
					}
				}
			}
		}
		else if (AdjacentTiles.Num() == 1)
		{
			// Diagonal wall - update WallGraph edge
			if (Wall.WallEdgeID != -1 && OurLot->WallGraph)
			{
				FWallEdge* Edge = OurLot->WallGraph->Edges.Find(Wall.WallEdgeID);
				if (Edge)
				{
					Edge->Room1 = AdjacentTiles[0].GetPrimaryRoomID();
					Edge->Room2 = AdjacentTiles[0].GetPrimaryRoomID();
				}
			}
		}

		UpdatedCount++;
	}

	UE_LOG(LogTemp, Warning, TEXT("UpdateAllWallRoomIDs: Updated RoomIDs for %d walls"), UpdatedCount);
}

void UWallComponent::RegenerateAllWalls()
{
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
		return;

	int32 RegeneratedCount = 0;

	// NOTE: UpdateAllWallRoomIDs() removed - room assignments now done correctly by
	// RoomManager::AssignRoomToBoundaryEdges() using geometric centroid-based approach

	// Regenerate all wall meshes to update vertex colors
	// NOTE: We call GenerateWallMeshSection but don't use the return value
	// This preserves existing WallSegmentData fields like WallEdgeID
	for (FWallSegmentData& Wall : WallDataArray)
	{
		if (!Wall.bCommitted)
			continue;

		GenerateWallMeshSection(Wall);
		RegeneratedCount++;
	}

	UE_LOG(LogTemp, Warning, TEXT("RegenerateAllWalls: Regenerated %d walls with updated vertex colors"), RegeneratedCount);
}

int32 UWallComponent::GetSectionIDFromHitResult(const FHitResult& HitResult) const
{
	if (!HitResult.Component.IsValid() || HitResult.Component.Get() != this)
	{
		// Hit result is not on this component
		return -2;
	}

	// Convert world-space impact point to component-local space for comparison with SectionLocalBox
	FVector LocalImpactPoint = GetComponentTransform().InverseTransformPosition(HitResult.ImpactPoint);
	FVector BoxExtents(4.0f, 4.0f, 4.0f); // Adjust the extents as needed
	FBox ImpactBox(LocalImpactPoint - BoxExtents, LocalImpactPoint + BoxExtents);

	// Iterate over each section to find the one containing the hit point
	for (const FWallSegmentData& Data : WallDataArray)
	{
	// for (int32 SectionIndex = 0; SectionIndex < GetNumSections(); ++SectionIndex) // fized bug
	// {
		// Skip if WallSectionData is invalid (can happen after undo/redo)
		if (!Data.WallSectionData)
		{
			continue;
		}

		// Get bounds of this section
		FBox& SectionBounds = Data.WallSectionData->SectionLocalBox;
		// Check if the impact point is within the bounds of this section
		if (ImpactBox.Intersect(SectionBounds))
		{
			return Data.WallArrayIndex;
		}
	}

	// Hit point is not within any section bounds
	return -1;
}

int32 UWallComponent::GetWallArrayIndexFromHitLocation(const FVector& HitLocation, int32 Level) const
{
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		UE_LOG(LogTemp, Error, TEXT("GetWallArrayIndexFromHitLocation: No LotManager found"));
		return -1;
	}

	// Find the closest committed wall on the specified level
	float MinDistance = FLT_MAX;
	int32 ClosestWallIndex = -1;

	for (const FWallSegmentData& Wall : WallDataArray)
	{
		// Skip uncommitted walls or walls on different levels
		if (!Wall.bCommitted || Wall.Level != Level)
		{
			continue;
		}

		// Calculate distance from hit point to wall segment
		FVector WallStart = Wall.StartLoc;
		FVector WallEnd = Wall.EndLoc;
		FVector WallDirection = WallEnd - WallStart;
		float WallLength = WallDirection.Size();

		if (WallLength < KINDA_SMALL_NUMBER)
		{
			continue; // Skip zero-length walls
		}

		WallDirection /= WallLength; // Normalize

		// Project hit point onto wall line
		FVector HitToStart = HitLocation - WallStart;
		float ProjectionLength = FVector::DotProduct(HitToStart, WallDirection);

		// Clamp to wall segment bounds
		ProjectionLength = FMath::Clamp(ProjectionLength, 0.0f, WallLength);

		// Find closest point on wall segment
		FVector ClosestPoint = WallStart + WallDirection * ProjectionLength;

		// Calculate distance (only XY plane, ignore Z)
		FVector Delta = HitLocation - ClosestPoint;
		Delta.Z = 0.0f;
		float Distance = Delta.Size();

		// Check if this is the closest wall so far
		// Also check if distance is reasonable (within wall thickness + tolerance)
		const float MaxDistance = Wall.Thickness + 50.0f; // Thickness plus tolerance
		if (Distance < MinDistance && Distance < MaxDistance)
		{
			MinDistance = Distance;
			ClosestWallIndex = Wall.WallArrayIndex;
		}
	}

	if (ClosestWallIndex == -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetWallArrayIndexFromHitLocation: No wall found near hit location (%s) on level %d"),
			*HitLocation.ToString(), Level);
	}

	return ClosestWallIndex;
}

TArray<int32> UWallComponent::GetMultiSectionIDFromHitResult(FVector Location, FVector BoxExtents, FQuat Rotation)
{
    TArray<int32> Sections;
    // BoxExtents is already half-extents from GetScaledBoxExtent(), so use it directly
    FVector BoxHalfExtents = BoxExtents;

    // Define the original box corners based on half extents
    TArray<FVector> Corners;
    Corners.Add(FVector(-BoxHalfExtents.X, -BoxHalfExtents.Y, -BoxHalfExtents.Z));
    Corners.Add(FVector(BoxHalfExtents.X, -BoxHalfExtents.Y, -BoxHalfExtents.Z));
    Corners.Add(FVector(BoxHalfExtents.X, BoxHalfExtents.Y, -BoxHalfExtents.Z));
    Corners.Add(FVector(-BoxHalfExtents.X, BoxHalfExtents.Y, -BoxHalfExtents.Z));
    Corners.Add(FVector(-BoxHalfExtents.X, -BoxHalfExtents.Y, BoxHalfExtents.Z));
    Corners.Add(FVector(BoxHalfExtents.X, -BoxHalfExtents.Y, BoxHalfExtents.Z));
    Corners.Add(FVector(BoxHalfExtents.X, BoxHalfExtents.Y, BoxHalfExtents.Z));
    Corners.Add(FVector(-BoxHalfExtents.X, BoxHalfExtents.Y, BoxHalfExtents.Z));

    // Rotate and translate each corner
    for (FVector& Corner : Corners)
    {
        Corner = Rotation.RotateVector(Corner) + Location;
    }

    // Create a new bounding box from the transformed corners
    FBox ImpactBox(Corners[0], Corners[0]);
    for (const FVector& Corner : Corners)
    {
        ImpactBox += Corner;
    }

    // Check intersections with wall section bounds - iterate through WallDataArray, not mesh sections
    for (const FWallSegmentData& Wall : WallDataArray)
    {
        // Skip uncommitted walls or walls without valid section data
        if (!Wall.bCommitted || !Wall.WallSectionData)
        {
            continue;
        }

        const FBox& SectionBounds = Wall.WallSectionData->SectionLocalBox;

        if (ImpactBox.Intersect(SectionBounds))
        {
            Sections.Add(Wall.WallArrayIndex);  // Return WallArrayIndex, not SectionIndex!
        }
    }

    return Sections;
}

void UWallComponent::RenderPortals()
{
	for (FWallSegmentData& FoundWall : WallDataArray)
	{
		// Skip uncommitted walls
		if (!FoundWall.bCommitted)
		{
			continue;
		}

		// Validate array index
		if (!WallDataArray.IsValidIndex(FoundWall.WallArrayIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("RenderPortals: Skipping wall with invalid WallArrayIndex %d"), FoundWall.WallArrayIndex);
			continue;
		}

		// Verify the wall has a valid material instance
		if (!WallDataArray[FoundWall.WallArrayIndex].WallMaterial)
		{
			UE_LOG(LogTemp, Warning, TEXT("RenderPortals: Wall at index %d has no material instance, skipping portal rendering"), FoundWall.WallArrayIndex);
			continue;
		}

		// Check if render target exists BEFORE trying to use it
		if (!FoundWall.RenderTarget)
		{
			UE_LOG(LogTemp, Error, TEXT("RenderPortals: Wall at index %d (Level %d) has no render target! Portals cannot be rendered."),
				FoundWall.WallArrayIndex, FoundWall.Level);
			continue;
		}

		// ALWAYS clear the render target first - this is critical for proper undo functionality
		// When a portal is removed (undo), the wall may have 0 portals but still needs the cutout cleared
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FoundWall.RenderTarget);

		// If wall has portals, render them
		if (FoundWall.PortalArray.Num() > 0)
		{
			// Log portal rendering for debugging
			UE_LOG(LogTemp, Log, TEXT("RenderPortals: Rendering %d portal(s) for wall at index %d (Level %d)"),
				FoundWall.PortalArray.Num(), FoundWall.WallArrayIndex, FoundWall.Level);

			UCanvas *canvas;
			FDrawToRenderTargetContext context;
			FVector2D size;

			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, FoundWall.RenderTarget, canvas, size, context);
			if (canvas)
			{
				for (APortalBase* Portal : FoundWall.PortalArray)
				{
					if (Portal)
					{
						// Only render portal cutout if the portal is aligned with this wall
						// This prevents rendering on perpendicular corner walls
						if (ShouldRenderPortalOnWall(FoundWall, Portal))
						{
							FVector2D loc = GetPortalLocationRelativeToWall(FoundWall, Portal);
							Portal->DrawPortal(canvas, loc);
						}
					}
				}
			}
			UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, context);

			UE_LOG(LogTemp, Log, TEXT("RenderPortals: Successfully rendered portals for wall at index %d (Level %d) - Cuts texture applied"),
				FoundWall.WallArrayIndex, FoundWall.Level);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("RenderPortals: Cleared cutouts for wall %d (Level %d) - no portals"),
				FoundWall.WallArrayIndex, FoundWall.Level);
		}

		// Update material parameter with the render target (either with portals or cleared)
		WallDataArray[FoundWall.WallArrayIndex].WallMaterial->SetTextureParameterValue(FName("Cuts"), FoundWall.RenderTarget);
		SetMaterial(WallDataArray[FoundWall.WallArrayIndex].SectionIndex, WallDataArray[FoundWall.WallArrayIndex].WallMaterial);
	}
}

void UWallComponent::RenderPortalsForWalls(const TArray<int32>& WallIndices)
{
	for (int32 WallIndex : WallIndices)
	{
		// Validate index
		if (!WallDataArray.IsValidIndex(WallIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("RenderPortalsForWalls: Invalid WallIndex %d (array size: %d)"), WallIndex, WallDataArray.Num());
			continue;
		}

		FWallSegmentData& FoundWall = WallDataArray[WallIndex];

		// Skip uncommitted walls
		if (!FoundWall.bCommitted)
		{
			continue;
		}

		// Check if render target exists
		if (!FoundWall.RenderTarget)
		{
			UE_LOG(LogTemp, Warning, TEXT("RenderPortalsForWalls: Wall at index %d (Level %d) has no render target!"), WallIndex, FoundWall.Level);
			continue;
		}

		// Verify the wall has a valid material instance
		if (!FoundWall.WallMaterial)
		{
			UE_LOG(LogTemp, Warning, TEXT("RenderPortalsForWalls: Wall at index %d has no material instance"), WallIndex);
			continue;
		}

		UCanvas* Canvas;
		FDrawToRenderTargetContext Context;
		FVector2D Size;

		// Clear the render target first
		UKismetRenderingLibrary::ClearRenderTarget2D(this, FoundWall.RenderTarget);

		// If wall has portals, render them
		if (FoundWall.PortalArray.Num() > 0)
		{
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, FoundWall.RenderTarget, Canvas, Size, Context);
			if (Canvas)
			{
				for (APortalBase* Portal : FoundWall.PortalArray)
				{
					if (Portal)
					{
						// Only render portal cutout if the portal is aligned with this wall
						// This prevents rendering on perpendicular corner walls
						if (ShouldRenderPortalOnWall(FoundWall, Portal))
						{
							FVector2D Loc = GetPortalLocationRelativeToWall(FoundWall, Portal);
							Portal->DrawPortal(Canvas, Loc);
						}
					}
				}
			}
			UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

			UE_LOG(LogTemp, Verbose, TEXT("RenderPortalsForWalls: Rendered %d portal(s) for wall %d (Level %d)"),
				FoundWall.PortalArray.Num(), WallIndex, FoundWall.Level);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("RenderPortalsForWalls: Cleared cutouts for wall %d (Level %d) - no portals"),
				WallIndex, FoundWall.Level);
		}

		// Update material parameter
		FoundWall.WallMaterial->SetTextureParameterValue(FName("Cuts"), FoundWall.RenderTarget);
		SetMaterial(FoundWall.SectionIndex, FoundWall.WallMaterial);
	}
}

FVector2D UWallComponent::GetPortalLocationRelativeToWall(FWallSegmentData WallSegment, APortalBase* Portal)
{
	// World space positions of the wall's start and end points
	FVector WallStart = WallSegment.StartLoc;
	FVector WallEnd = WallSegment.EndLoc;

	// World space position of the portal (use Box component location to include X/Y offset)
	FVector PortalPosition = Portal->Box ? Portal->Box->GetComponentLocation() : Portal->GetActorLocation();

	// Vector from wall's start to the portal, establishing a relative position
	FVector PositionRelativeToWallStart = PortalPosition - WallStart;

	// Normalized direction vector along the wall, defines the local X-axis
	FVector WallDirection = WallEnd - WallStart;
	WallDirection.Normalize();

	// Establish the local Z-axis as the cross product of the wall direction and the world's up vector
	FVector UpVector = FVector(0, 0, 1);  // Assuming Z is up
	FVector LocalZAxis = UKismetMathLibrary::Cross_VectorVector(WallDirection, UpVector);
	LocalZAxis.Normalize();

	// Calculate the local Y-axis as the cross product of the Z and X axes
	FVector LocalYAxis = UKismetMathLibrary::Cross_VectorVector(LocalZAxis, WallDirection);

	// Project the relative position onto the new local axes
	float LocalX = UKismetMathLibrary::Dot_VectorVector(PositionRelativeToWallStart, WallDirection);
	float LocalY = UKismetMathLibrary::Dot_VectorVector(PositionRelativeToWallStart, LocalYAxis);

	// Add margin offset to account for extended render target (20 units margin on left side)
	const float RenderTargetMargin = 20.0f;
	LocalX += RenderTargetMargin;

	// Apply portal offset from data asset (shifts cutout position on wall)
	// PortalOffset.X = horizontal shift along wall, PortalOffset.Y = vertical shift
	// Box->SetRelativeLocation is for visual debugging only
	// Cutout calculation needs explicit offset because it's in wall-relative coordinate frame
	LocalX += Portal->PortalOffset.X;
	float LocalYWithOffset = LocalY - Portal->PortalOffset.Y; // Subtract because Y increases downward in texture space

	// Return the projected local coordinates, assuming it's projected onto the XY plane of the wall
	return FVector2D(LocalX, WallSegment.Height - LocalYWithOffset);
}

bool UWallComponent::ShouldRenderPortalOnWall(const FWallSegmentData& Wall, const APortalBase* Portal) const
{
	if (!Portal)
	{
		return false;
	}

	// Get wall direction (ignoring Z component for 2D alignment check)
	FVector WallDirection = Wall.EndLoc - Wall.StartLoc;
	WallDirection.Z = 0.0f;
	WallDirection.Normalize();

	// Get portal's forward direction from its rotation (this is the direction the portal faces)
	FVector PortalForward = Portal->GetActorRotation().Vector();
	PortalForward.Z = 0.0f;
	PortalForward.Normalize();

	// Calculate the dot product to determine alignment
	// If portal is aligned with wall: |dot| should be close to 1
	// If portal is perpendicular to wall: |dot| should be close to 0
	float DotProduct = FVector::DotProduct(WallDirection, PortalForward);
	float AbsDot = FMath::Abs(DotProduct);

	// Only render portal on walls that are roughly aligned with the portal's orientation
	// Threshold of 0.9 allows for ~25 degree variation from alignment
	// This prevents rendering on perpendicular walls AND sharp corner walls (e.g., 30-45 degree angles)
	// Lower values (0.7 = ~45°) were too permissive and caused cutouts on adjacent triangle corner walls
	const float AlignmentThreshold = 0.9f;
	return AbsDot >= AlignmentThreshold;
}

void UWallComponent::UpdateWallTransitionState(int32 WallIndex)
{
	if (!WallDataArray.IsValidIndex(WallIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("UpdateWallTransitionState: Invalid WallIndex %d"), WallIndex);
		return;
	}

	FWallSegmentData& Wall = WallDataArray[WallIndex];
	if (!Wall.bCommitted || !Wall.WallMaterial)
	{
		return;  // Skip uncommitted walls or walls without materials
	}

	// Calculate transition visibility based on connected walls' cutaway states
	// Transitions should be visible when any connected wall at that edge is NOT in cutaway mode
	// Material will handle final visibility based on EnableCutaway parameter
	bool bShowStart = false;
	bool bShowEnd = false;

	for (int32 ConnectedIndex : Wall.ConnectedWallsSections)
	{
		if (WallDataArray.IsValidIndex(ConnectedIndex))
		{
			const FWallSegmentData& ConnectedWall = WallDataArray[ConnectedIndex];

			// Check if this wall connects at our start location
			bool bConnectsAtStart =
				(ConnectedWall.StartLoc.Equals(Wall.StartLoc, 1.0f) ||
				 ConnectedWall.EndLoc.Equals(Wall.StartLoc, 1.0f));

			if (bConnectsAtStart && !ConnectedWall.bIsInCutawayMode)
			{
				bShowStart = true;  // Show transition at start
			}

			// Check if this wall connects at our end location
			bool bConnectsAtEnd =
				(ConnectedWall.StartLoc.Equals(Wall.EndLoc, 1.0f) ||
				 ConnectedWall.EndLoc.Equals(Wall.EndLoc, 1.0f));

			if (bConnectsAtEnd && !ConnectedWall.bIsInCutawayMode)
			{
				bShowEnd = true;  // Show transition at end
			}
		}
	}

	// Update stored transition states
	Wall.bShowStartTransition = bShowStart;
	Wall.bShowEndTransition = bShowEnd;

	// Update material parameters (no mesh regeneration needed!)
	Wall.WallMaterial->SetScalarParameterValue("ShowStartTransition", bShowStart ? 1.0f : 0.0f);
	Wall.WallMaterial->SetScalarParameterValue("ShowEndTransition", bShowEnd ? 1.0f : 0.0f);
}
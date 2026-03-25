// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/RoofComponent.h"

#include "Actors/LotManager.h"
#include "Components/RoomManagerComponent.h"
#include "Components/WallComponent.h"
#include "Components/WallGraphComponent.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Subsystems/BuildServer.h"



URoofComponent::URoofComponent(const FObjectInitializer &ObjectInitializer) : UProceduralMeshComponent(ObjectInitializer)
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	RoofData.Location = FVector(0, 0, 0);
	RoofData.SectionIndex = 0; // Single instance components use section 0
	RoofData.RoofArrayIndex = 0; // Single instance components use array index 0

	bCommitted = false;

	// Disable custom depth - roofs are permanent geometry
	bRenderCustomDepth = false;
}

void URoofComponent::BeginPlay()
{
    Super::BeginPlay();
	SetGenerateOverlapEvents(true);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block); // Primitives trace channel for deletion tool
}

void URoofComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void URoofComponent::RegenerateRoofSection(FRoofSegmentData InRoofData, bool bRecursive)
{
}

int32 URoofComponent::GetSectionIDFromHitResult(const FHitResult& HitResult) const
{
	if (!HitResult.Component.IsValid() || HitResult.Component.Get() != this)
	{
		// Hit result is not on this component
		return -2;
	}

	// Convert world-space impact point to component-local space for comparison with vertex bounds
	FVector LocalImpactPoint = GetComponentTransform().InverseTransformPosition(HitResult.ImpactPoint);
	FVector BoxExtents(4.0f, 4.0f, 4.0f); // Adjust the extents as needed
	FBox ImpactBox(LocalImpactPoint - BoxExtents, LocalImpactPoint + BoxExtents);
	//DrawDebugBox(GetWorld(), LocalImpactPoint, BoxExtents, FColor::Green, false, 1, 0, 1);
	
	// Iterate over each section to find the one containing the hit point
	for (const FRoofSegmentData& Data : RoofDataArray)
	{
		// for (int32 SectionIndex = 0; SectionIndex < GetNumSections(); ++SectionIndex) // fized bug
		// {
		// Get bounds of this section
		FBox SectionBounds(Data.Vertices);
		// Check if the impact point is within the bounds of this section
		if (ImpactBox.Intersect(SectionBounds))
		{
			return Data.RoofArrayIndex;
		}
	}
	
	// Hit point is not within any section bounds
	return -1;
}

void URoofComponent::DestroyRoof()
{
	// Clean up all created walls before destroying the roof
	if (LotManager && LotManager->WallComponent)
	{
		// Remove walls created for the single roof instance
		for (int32 WallIndex : RoofData.CreatedWallIndices)
		{
			if (WallIndex >= 0)
			{
				LotManager->WallComponent->RemoveWallSection(WallIndex);
			}
		}
		RoofData.CreatedWallIndices.Empty();

		// Also clean up walls from any roof segments in the array (legacy support)
		for (FRoofSegmentData& Segment : RoofDataArray)
		{
			for (int32 WallIndex : Segment.CreatedWallIndices)
			{
				if (WallIndex >= 0)
				{
					LotManager->WallComponent->RemoveWallSection(WallIndex);
				}
			}
			Segment.CreatedWallIndices.Empty();
		}
	}

	// Clear mesh sections
	for (int i = 0; i<5 ; i++)
	{
		ClearMeshSection(i);
	}
	DestroyComponent();
}

void URoofComponent::CommitRoofSection(FRoofSegmentData InRoofData, UMaterialInstance *DefaultRoofMaterial)
{
    RoofDataArray[InRoofData.RoofArrayIndex].bCommitted = true;
	//Set material on both sides of Roof to default
	RoofDataArray[InRoofData.RoofArrayIndex].RoofMaterial = CreateDynamicMaterialInstance(RoofDataArray[InRoofData.RoofArrayIndex].SectionIndex, Cast<UMaterialInterface>(DefaultRoofMaterial));
	SetMaterial(RoofDataArray[InRoofData.RoofArrayIndex].SectionIndex, RoofDataArray[InRoofData.RoofArrayIndex].RoofMaterial);
	RegenerateRoofSection(RoofDataArray[InRoofData.RoofArrayIndex], true);
	bRenderCustomDepth = false;
	//RegenerateEverySection();
}

bool URoofComponent::FindExistingRoofSection(const FVector &TileCornerStart, const FRoofDimensions& RoofDimensions, FRoofSegmentData &OutRoof)
{
	for (FRoofSegmentData FoundRoofComponent : RoofDataArray)
	{
		if (FoundRoofComponent.bCommitted == true && ((FoundRoofComponent.Location == TileCornerStart && FoundRoofComponent.Dimensions == RoofDimensions)))
		{
			// Found an existing Roof segment with matching start and end s
			OutRoof = FoundRoofComponent;
			return true;
		}
	}
	// No existing Roof segment found with the given start and end s
	return false;
}

FRoofSegmentData URoofComponent::GenerateRoofSection(const FVector& Location, const FVector& Direction, const FRoofDimensions& RoofDimensions, const float RoofThickness, const float GableThickness)
{
	FRoofSegmentData NewRoofData;
	NewRoofData.Location = Location;
	NewRoofData.Dimensions = RoofDimensions;
	NewRoofData.Direction = Direction;
    NewRoofData.RoofThickness = RoofThickness;
    NewRoofData.GableThickness = GableThickness;
	NewRoofData.SectionIndex = -1;

	return GenerateRoofMeshSection(NewRoofData);
}

void URoofComponent::UpdateSingleRoof(const FVector& Location, const FVector& Direction, const FRoofDimensions& RoofDimensions, const float RoofThickness, const float GableThickness)
{
	// Simplified method for single-instance roof components
	// Updates the component's RoofData and regenerates the mesh
	RoofData.Location = Location;
	RoofData.Direction = Direction;
	RoofData.Dimensions = RoofDimensions;
	RoofData.RoofThickness = RoofThickness;
	RoofData.GableThickness = GableThickness;
	RoofData.SectionIndex = 0; // Single instance always uses section 0

	// Generate the mesh - child classes will override GenerateRoofMeshSection to use their specific type
	RoofData = GenerateRoofMeshSection(RoofData);

	// Ensure the data is in the array at index 0 for compatibility
	if (RoofDataArray.Num() == 0)
	{
		RoofDataArray.Add(RoofData);
	}
	else
	{
		RoofDataArray[0] = RoofData;
	}
}

void URoofComponent::DestroyRoofSection(FRoofSegmentData InRoofData)
{
	int32 FoundIndex = RoofDataArray.Find(InRoofData);
	if(FoundIndex != INDEX_NONE)
	{
		// Clean up walls created by this roof section
		if (LotManager && LotManager->WallComponent)
		{
			for (int32 WallIndex : InRoofData.CreatedWallIndices)
			{
				if (WallIndex >= 0)
				{
					LotManager->WallComponent->RemoveWallSection(WallIndex);
				}
			}
		}

		ClearMeshSection(InRoofData.SectionIndex);
		/*
		if (InRoofData.ConnectedRoofsAtEnd.Num() != 0)
		{
			RegenerateRoofSection(InRoofData.ConnectedRoofsAtEnd[0], true);
		}
		if(InRoofData.ConnectedRoofsAtStart.Num() != 0)
		{
			RegenerateRoofSection(InRoofData.ConnectedRoofsAtStart[0], true);
		}*/
		RoofDataArray.Remove(InRoofData);
	}
}

void URoofComponent::RemoveRoofSection(int RoofArrayIndex)
{
	if(RoofDataArray.Num() > RoofArrayIndex)
	{
		// Clean up walls created by this roof section
		if (LotManager && LotManager->WallComponent)
		{
			for (int32 WallIndex : RoofDataArray[RoofArrayIndex].CreatedWallIndices)
			{
				if (WallIndex >= 0)
				{
					LotManager->WallComponent->RemoveWallSection(WallIndex);
				}
			}
			RoofDataArray[RoofArrayIndex].CreatedWallIndices.Empty();
		}

		// Clear Mesh Section
		ClearMeshSection(RoofDataArray[RoofArrayIndex].SectionIndex);

		// Disable The Roof Data
		RoofDataArray[RoofArrayIndex].bCommitted = false;
		RoofFreeIndices.Add(RoofArrayIndex);
	}
}

bool URoofComponent::SetRoofDataByIndex(int32 Index, FRoofSegmentData NewRoofData)
{
	if(RoofDataArray.IsValidIndex(Index))
	{
		RoofDataArray[Index] = NewRoofData;
		return true;
	}
	return false;
}

FRoofSegmentData &URoofComponent::GetRoofDataByIndex(int32 RoofIndex)
{
    return RoofDataArray[RoofIndex];
}

void URoofComponent::GenerateRoofMesh()
{
	FRoofVertices RoofVertices = CalculateRoofVertices(FVector(0,0,0), RoofData.Direction, RoofData.Dimensions);

	float RoofThickness = RoofData.RoofThickness;
	float HalfGableThickness = RoofData.GableThickness / 2;

	FVector RoofRiseVector(       RoofThickness * FVector::UpVector);
	FVector RightEdgeShiftVector(RoofThickness * FVector::CrossProduct(RoofVertices.RightSlopeDirection, RoofData.Direction));
	FVector LeftEdgeShiftVector( RoofThickness * FVector::CrossProduct(RoofVertices.LeftSlopeDirection,  -RoofData.Direction));

	FVector GableAdvanceVector = RoofData.Direction * HalfGableThickness;
	FVector GableRetreatVector = -RoofData.Direction * HalfGableThickness;
	
	// Combined mesh arrays
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	
	auto AddFaceToMesh = [&](const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector2D>& UV, const FVector& NormalDirection)
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
			VertexColors.Add(FColor::White);
			Tangents.Add(FProcMeshTangent());
		}
	};

	//Gable Front face
	TArray<FVector> GableFrontVerts;
	TArray<int32> GableFrontTriangles;
	TArray<FVector2D> GableFrontUV;
	GableFrontVerts.Add(RoofVertices.GablePeakFront  + GableAdvanceVector);
	GableFrontVerts.Add(RoofVertices.GableFrontRight + GableAdvanceVector);
	GableFrontVerts.Add(RoofVertices.GableFrontLeft  + GableAdvanceVector);
	
	GableFrontTriangles.Add(2);
	GableFrontTriangles.Add(1);
	GableFrontTriangles.Add(0);
	
	GableFrontTriangles.Add(0);
	GableFrontTriangles.Add(1);
	GableFrontTriangles.Add(2);
	
	GableFrontUV.Add(FVector2D(0, 1));
	GableFrontUV.Add(FVector2D(0, 0));
	GableFrontUV.Add(FVector2D(1, 1));
	GableFrontUV.Add(FVector2D(1, 0));
	FVector GableFrontNormal = FVector::CrossProduct((GableFrontVerts[1] - GableFrontVerts[0]), (GableFrontVerts[2] - GableFrontVerts[0])).GetSafeNormal();
	
	//Gable Back face
	TArray<FVector> GableBackVerts;
	TArray<int32> GableBackTriangles;
	TArray<FVector2D> GableBackUV;
	GableBackVerts.Add(RoofVertices.GablePeakBack  + GableRetreatVector);
	GableBackVerts.Add(RoofVertices.GableBackRight + GableRetreatVector);
	GableBackVerts.Add(RoofVertices.GableBackLeft  + GableRetreatVector);
	
	GableBackTriangles.Add(2);
	GableBackTriangles.Add(1);
	GableBackTriangles.Add(0);
	
	GableBackTriangles.Add(0);
	GableBackTriangles.Add(1);
	GableBackTriangles.Add(2);
	
	GableBackUV.Add(FVector2D(0, 1));
	GableBackUV.Add(FVector2D(0, 0));
	GableBackUV.Add(FVector2D(1, 1));
	GableBackUV.Add(FVector2D(1, 0));
	FVector GableBackNormal = FVector::CrossProduct((GableBackVerts[1] - GableBackVerts[0]), (GableBackVerts[2] - GableBackVerts[0])).GetSafeNormal();

    //Underside face
	TArray<FVector> NegZVerts;
	TArray<int32> NegZTriangles;
	TArray<FVector2D> NegZUV;
	NegZVerts.Add(RoofVertices.PeakFront);    // 0
	NegZVerts.Add(RoofVertices.PeakBack);     // 1
    NegZVerts.Add(RoofVertices.FrontRight);   // 2
	NegZVerts.Add(RoofVertices.FrontLeft);    // 3
	NegZVerts.Add(RoofVertices.BackRight);    // 4
	NegZVerts.Add(RoofVertices.BackLeft);     // 5
	
	NegZTriangles.Add(4);
	NegZTriangles.Add(0);
	NegZTriangles.Add(1);
	NegZTriangles.Add(4);
	NegZTriangles.Add(2);
	NegZTriangles.Add(0);
	NegZTriangles.Add(1);
	NegZTriangles.Add(3);
	NegZTriangles.Add(5);
	NegZTriangles.Add(1);
	NegZTriangles.Add(0);
	NegZTriangles.Add(3);
	
	NegZTriangles.Add(0);
	NegZTriangles.Add(2);
	NegZTriangles.Add(4);
	NegZTriangles.Add(1);
	NegZTriangles.Add(0);
	NegZTriangles.Add(4);
	NegZTriangles.Add(3);
	NegZTriangles.Add(0);
	NegZTriangles.Add(1);
	NegZTriangles.Add(5);
	NegZTriangles.Add(3);
	NegZTriangles.Add(1);
	
    NegZUV.Add(FVector2D(0, 0.5)); 
	NegZUV.Add(FVector2D(1, 0.5)); 
	NegZUV.Add(FVector2D(0, 1));   
	NegZUV.Add(FVector2D(0, 0));
	NegZUV.Add(FVector2D(1, 1));  
	NegZUV.Add(FVector2D(1, 0));
	FVector NegZNormal = FVector(0,0,-1);

	//Top face
    TArray<FVector> PosZVerts;
	TArray<int32> PosZTriangles;
	TArray<FVector2D> PosZUV;
	PosZVerts.Add(RoofVertices.PeakFront  + RoofRiseVector);    // 0
	PosZVerts.Add(RoofVertices.PeakBack   + RoofRiseVector);    // 1
    PosZVerts.Add(RoofVertices.FrontRight + RightEdgeShiftVector);    // 2
	PosZVerts.Add(RoofVertices.FrontLeft  + LeftEdgeShiftVector);    // 3
	PosZVerts.Add(RoofVertices.BackRight  + RightEdgeShiftVector);    // 4
	PosZVerts.Add(RoofVertices.BackLeft   + LeftEdgeShiftVector);    // 5
		
	PosZTriangles.Add(4);
	PosZTriangles.Add(0);
	PosZTriangles.Add(1);
	PosZTriangles.Add(4);
	PosZTriangles.Add(2);
	PosZTriangles.Add(0);
	PosZTriangles.Add(1);
	PosZTriangles.Add(3);
	PosZTriangles.Add(5);
	PosZTriangles.Add(1);
	PosZTriangles.Add(0);
	PosZTriangles.Add(3);
	
	PosZTriangles.Add(0);
	PosZTriangles.Add(2);
	PosZTriangles.Add(4);
	PosZTriangles.Add(1);
	PosZTriangles.Add(0);
	PosZTriangles.Add(4);
	PosZTriangles.Add(3);
	PosZTriangles.Add(0);
	PosZTriangles.Add(1);
	PosZTriangles.Add(5);
	PosZTriangles.Add(3);
	PosZTriangles.Add(1);
	
    PosZUV.Add(FVector2D(0, 0.5)); 
	PosZUV.Add(FVector2D(1, 0.5)); 
	PosZUV.Add(FVector2D(0, 1));   
	PosZUV.Add(FVector2D(0, 0));
	PosZUV.Add(FVector2D(1, 1));  
	PosZUV.Add(FVector2D(1, 0));
	FVector PosZNormal = FVector(0,0, 1);
	
	//Underside
	AddFaceToMesh(NegZVerts, NegZTriangles, NegZUV, FVector(0, 0, -1));
	
	//TopSide
	AddFaceToMesh(PosZVerts, PosZTriangles, PosZUV, FVector(0, 0, 1));

	//Gable Front Surface
	AddFaceToMesh(GableFrontVerts, GableFrontTriangles, GableFrontUV, FVector(0, 0, 1));
	
	//Gable Front Surface
	AddFaceToMesh(GableBackVerts, GableBackTriangles, GableBackUV, FVector(0, 0, 1));

	// Check if section exists and has valid vertex count
	FProcMeshSection* Section = GetProcMeshSection(0);
	if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != Vertices.Num())
	{
		CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
		UpdateBounds();             // Recalculate world bounds for VSM
		MarkRenderStateDirty();     // Invalidate cached shadow data
	}
	else
	{
		UpdateMeshSection(0, Vertices, Normals, UVs, VertexColors, Tangents);
		UpdateBounds();             // Recalculate world bounds for VSM
		MarkRenderStateDirty();     // Invalidate cached shadow data
	}

	SetMaterial(0, RoofMaterial);
	// this->SetRelativeRotation(RoofRotation);
}

FRoofSegmentData URoofComponent::GenerateRoofMeshSection(FRoofSegmentData InRoofData)
{
	// Base implementation routes to appropriate generation method based on roof type
	// Child component classes (UGableRoofComponent, UHipRoofComponent, etc.) override this
	// to directly call their specific generation method for better performance
	switch (InRoofData.Dimensions.RoofType)
	{
		case ERoofType::Gable:
			return GenerateGableRoofMesh(InRoofData);
		case ERoofType::Hip:
			return GenerateHipRoofMesh(InRoofData);
		case ERoofType::Shed:
			return GenerateShedRoofMesh(InRoofData);
		default:
			return GenerateGableRoofMesh(InRoofData);
	}
}

FRoofSegmentData URoofComponent::GenerateGableRoofMesh(FRoofSegmentData InRoofData)
{
    int32 NewSectionIndex = GetNumSections();
	if(InRoofData.SectionIndex != -1)
	{
		NewSectionIndex = InRoofData.SectionIndex;
	}

	InRoofData.SectionIndex = NewSectionIndex;

	float RoofThickness = InRoofData.RoofThickness;
	float HalfGableThickness = InRoofData.GableThickness / 2;
	
	// CalculateRoofVertices returns vertices in WORLD SPACE
	FRoofVertices RoofVertices = CalculateRoofVertices(InRoofData.Location, InRoofData.Direction, InRoofData.Dimensions);

	FVector RoofRiseVector(      RoofThickness * FVector::UpVector);
	FVector RightEdgeShiftVector(RoofThickness * FVector::CrossProduct(RoofVertices.RightSlopeDirection, InRoofData.Direction));
	FVector LeftEdgeShiftVector( RoofThickness * FVector::CrossProduct(RoofVertices.LeftSlopeDirection,  -InRoofData.Direction));

	FVector GableAdvanceVector = InRoofData.Direction * HalfGableThickness;
	FVector GableRetreatVector = -InRoofData.Direction * HalfGableThickness;

	// Calculate rake extensions to apply to ALL geometry (slopes and fascia)
	FVector FrontRakeExtension = InRoofData.Direction * InRoofData.Dimensions.FrontRake;
	FVector BackRakeExtension = -InRoofData.Direction * InRoofData.Dimensions.BackRake;

	// Get component location for world-to-local conversion
	FVector ComponentLocation = GetComponentLocation();
	
	// Combined mesh arrays
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	
	auto AddFaceToMesh = [&](const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector2D>& UV, const FVector& NormalDirection)
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
			VertexColors.Add(FColor::White);
			Tangents.Add(FProcMeshTangent());
		}
	};

    //Underside face (extended by rake for overhang)
	TArray<FVector> NegZVerts;
	TArray<int32> NegZTriangles;
	TArray<FVector2D> NegZUV;
	NegZVerts.Add(RoofVertices.PeakFront + FrontRakeExtension);    // 0 - Peak front extended
	NegZVerts.Add(RoofVertices.PeakBack + BackRakeExtension);      // 1 - Peak back extended
    NegZVerts.Add(RoofVertices.FrontRight + FrontRakeExtension);   // 2 - Right front extended
	NegZVerts.Add(RoofVertices.FrontLeft + FrontRakeExtension);    // 3 - Left front extended
	NegZVerts.Add(RoofVertices.BackRight + BackRakeExtension);     // 4 - Right back extended
	NegZVerts.Add(RoofVertices.BackLeft + BackRakeExtension);      // 5 - Left back extended
	NegZTriangles.Add(4);
	NegZTriangles.Add(0);
	NegZTriangles.Add(1);
	NegZTriangles.Add(4);
	NegZTriangles.Add(2);
	NegZTriangles.Add(0);
	NegZTriangles.Add(1);
	NegZTriangles.Add(3);
	NegZTriangles.Add(5);
	NegZTriangles.Add(1);
	NegZTriangles.Add(0);
	NegZTriangles.Add(3);
    NegZUV.Add(FVector2D(0, 0.5)); 
	NegZUV.Add(FVector2D(1, 0.5)); 
	NegZUV.Add(FVector2D(0, 1));   
	NegZUV.Add(FVector2D(0, 0));
	NegZUV.Add(FVector2D(1, 1));  
	NegZUV.Add(FVector2D(1, 0));
	FVector NegZNormal = FVector(0,0,-1);
	
	
	//Top face (two sloped surfaces meeting at ridge, extended by rake for overhang)
    TArray<FVector> PosZVerts;
	TArray<int32> PosZTriangles;
	TArray<FVector2D> PosZUV;
	PosZVerts.Add(RoofVertices.PeakFront  + RoofRiseVector + FrontRakeExtension);         // 0 - Ridge front extended
	PosZVerts.Add(RoofVertices.PeakBack   + RoofRiseVector + BackRakeExtension);          // 1 - Ridge back extended
    PosZVerts.Add(RoofVertices.FrontRight + RightEdgeShiftVector + FrontRakeExtension);   // 2 - Right slope front extended
	PosZVerts.Add(RoofVertices.FrontLeft + LeftEdgeShiftVector + FrontRakeExtension);     // 3 - Left slope front extended
	PosZVerts.Add(RoofVertices.BackRight + RightEdgeShiftVector + BackRakeExtension);     // 4 - Right slope back extended
	PosZVerts.Add(RoofVertices.BackLeft  + LeftEdgeShiftVector + BackRakeExtension);      // 5 - Left slope back extended

	// Right slope quad (0,2,4,1) - triangulate with consistent counter-clockwise winding from outside
	PosZTriangles.Add(0); PosZTriangles.Add(2); PosZTriangles.Add(4);  // Triangle 1: ridge front, right front, right back
	PosZTriangles.Add(0); PosZTriangles.Add(4); PosZTriangles.Add(1);  // Triangle 2: ridge front, right back, ridge back

	// Left slope quad (0,1,5,3) - triangulate with consistent counter-clockwise winding from outside
	PosZTriangles.Add(0); PosZTriangles.Add(1); PosZTriangles.Add(5);  // Triangle 3: ridge front, ridge back, left back
	PosZTriangles.Add(0); PosZTriangles.Add(5); PosZTriangles.Add(3);  // Triangle 4: ridge front, left back, left front

    PosZUV.Add(FVector2D(0, 0.5));  // 0 - Ridge front
	PosZUV.Add(FVector2D(1, 0.5));  // 1 - Ridge back
	PosZUV.Add(FVector2D(0, 1));    // 2 - Right front
	PosZUV.Add(FVector2D(0, 0));    // 3 - Left front
	PosZUV.Add(FVector2D(1, 1));    // 4 - Right back
	PosZUV.Add(FVector2D(1, 0));    // 5 - Left back

	// Normal points upward (average of the two slopes)
	FVector PosZNormal = FVector(0,0, 1);

	//posY Surface (Right edge, extended by rake for overhang)
	TArray<FVector> PosYVerts;
	TArray<int32> PosYTriangles;
	TArray<FVector2D> PosYUV;
    PosYVerts.Add(RoofVertices.FrontRight + RightEdgeShiftVector + FrontRakeExtension);    // 0 - Top Front extended
	PosYVerts.Add(RoofVertices.BackRight  + RightEdgeShiftVector + BackRakeExtension);     // 1 - Top Back extended
    PosYVerts.Add(RoofVertices.FrontRight + FrontRakeExtension);                           // 2 - Bottom Front extended
	PosYVerts.Add(RoofVertices.BackRight + BackRakeExtension);                             // 3 - Bottom Back extended
	PosYTriangles.Add(0);
	PosYTriangles.Add(2);
	PosYTriangles.Add(3);
	PosYTriangles.Add(1);
	PosYTriangles.Add(0);
	PosYTriangles.Add(3);
	PosYUV.Add(FVector2D(0, 1));
	PosYUV.Add(FVector2D(1, 1));
	PosYUV.Add(FVector2D(0, 0));
	PosYUV.Add(FVector2D(1, 0));
	FVector PosYNormal = FVector::CrossProduct((PosYVerts[1] - PosYVerts[0]), (PosYVerts[2] - PosYVerts[0])).GetSafeNormal();

	//negY Surface (Left edge, extended by rake for overhang)
	TArray<FVector> NegYVerts;
	TArray<int32> NegYTriangles;
	TArray<FVector2D> NegYUV;
	NegYVerts.Add(RoofVertices.FrontLeft  + LeftEdgeShiftVector + FrontRakeExtension);     // 0 - Top Front extended
	NegYVerts.Add(RoofVertices.BackLeft   + LeftEdgeShiftVector + BackRakeExtension);      // 1 - Top Back extended
	NegYVerts.Add(RoofVertices.FrontLeft + FrontRakeExtension);                            // 2 - Bottom Front extended
	NegYVerts.Add(RoofVertices.BackLeft + BackRakeExtension);                              // 3 - Bottom Back extended
	NegYTriangles.Add(2);
	NegYTriangles.Add(0);
	NegYTriangles.Add(1);
	NegYTriangles.Add(3);
	NegYTriangles.Add(2);
	NegYTriangles.Add(1);
	NegYUV.Add(FVector2D(0, 1));
	NegYUV.Add(FVector2D(1, 1));
	NegYUV.Add(FVector2D(0, 0));
	NegYUV.Add(FVector2D(1, 0));
	FVector NegYNormal = FVector::CrossProduct((NegYVerts[2] - NegYVerts[0]), (NegYVerts[1] - NegYVerts[0])).GetSafeNormal();

	//Front Fascia (Front edge - the thickness strip at the front, uses rake extension)
	TArray<FVector> FrontFasciaVerts;
	TArray<int32> FrontFasciaTriangles;
	TArray<FVector2D> FrontFasciaUV;

	// Right side of fascia (extended forward)
	FrontFasciaVerts.Add(RoofVertices.PeakFront + RoofRiseVector + FrontRakeExtension);         // 0 - Top peak
	FrontFasciaVerts.Add(RoofVertices.FrontRight + RightEdgeShiftVector + FrontRakeExtension);  // 1 - Top right
	FrontFasciaVerts.Add(RoofVertices.PeakFront + FrontRakeExtension);                          // 2 - Bottom peak
	FrontFasciaVerts.Add(RoofVertices.FrontRight + FrontRakeExtension);                         // 3 - Bottom right

	// Left side of fascia (extended forward)
	FrontFasciaVerts.Add(RoofVertices.FrontLeft + LeftEdgeShiftVector + FrontRakeExtension);    // 4 - Top left
	FrontFasciaVerts.Add(RoofVertices.FrontLeft + FrontRakeExtension);                          // 5 - Bottom left

	// Right quad
	FrontFasciaTriangles.Add(0); FrontFasciaTriangles.Add(3); FrontFasciaTriangles.Add(1);
	FrontFasciaTriangles.Add(0); FrontFasciaTriangles.Add(2); FrontFasciaTriangles.Add(3);

	// Left quad
	FrontFasciaTriangles.Add(0); FrontFasciaTriangles.Add(4); FrontFasciaTriangles.Add(5);
	FrontFasciaTriangles.Add(0); FrontFasciaTriangles.Add(5); FrontFasciaTriangles.Add(2);

	FrontFasciaUV.Add(FVector2D(0.5f, 1)); FrontFasciaUV.Add(FVector2D(1, 1));
	FrontFasciaUV.Add(FVector2D(0.5f, 0)); FrontFasciaUV.Add(FVector2D(1, 0));
	FrontFasciaUV.Add(FVector2D(0, 1)); FrontFasciaUV.Add(FVector2D(0, 0));

	FVector FrontFasciaNormal = InRoofData.Direction;

	//Back Fascia (Back edge - the thickness strip at the back, uses rake extension)
	TArray<FVector> BackFasciaVerts;
	TArray<int32> BackFasciaTriangles;
	TArray<FVector2D> BackFasciaUV;

	// Right side of fascia (extended backward)
	BackFasciaVerts.Add(RoofVertices.PeakBack + RoofRiseVector + BackRakeExtension);          // 0 - Top peak
	BackFasciaVerts.Add(RoofVertices.BackRight + RightEdgeShiftVector + BackRakeExtension);   // 1 - Top right
	BackFasciaVerts.Add(RoofVertices.PeakBack + BackRakeExtension);                           // 2 - Bottom peak
	BackFasciaVerts.Add(RoofVertices.BackRight + BackRakeExtension);                          // 3 - Bottom right

	// Left side of fascia (extended backward)
	BackFasciaVerts.Add(RoofVertices.BackLeft + LeftEdgeShiftVector + BackRakeExtension);     // 4 - Top left
	BackFasciaVerts.Add(RoofVertices.BackLeft + BackRakeExtension);                           // 5 - Bottom left

	// Right quad (reverse winding for back face)
	BackFasciaTriangles.Add(0); BackFasciaTriangles.Add(1); BackFasciaTriangles.Add(3);
	BackFasciaTriangles.Add(0); BackFasciaTriangles.Add(3); BackFasciaTriangles.Add(2);

	// Left quad (reverse winding for back face)
	BackFasciaTriangles.Add(0); BackFasciaTriangles.Add(2); BackFasciaTriangles.Add(5);
	BackFasciaTriangles.Add(0); BackFasciaTriangles.Add(5); BackFasciaTriangles.Add(4);

	BackFasciaUV.Add(FVector2D(0.5f, 1)); BackFasciaUV.Add(FVector2D(1, 1));
	BackFasciaUV.Add(FVector2D(0.5f, 0)); BackFasciaUV.Add(FVector2D(1, 0));
	BackFasciaUV.Add(FVector2D(0, 1)); BackFasciaUV.Add(FVector2D(0, 0));

	FVector BackFasciaNormal = -InRoofData.Direction;

    //Underside
	AddFaceToMesh(NegZVerts, NegZTriangles, NegZUV, NegZNormal);

	//Top face
	AddFaceToMesh(PosZVerts, PosZTriangles, PosZUV, PosZNormal);

	//Right edge
	AddFaceToMesh(PosYVerts, PosYTriangles, PosYUV, PosYNormal);

	//Left edge
	AddFaceToMesh(NegYVerts, NegYTriangles, NegYUV, NegYNormal);

	//Front fascia
	AddFaceToMesh(FrontFasciaVerts, FrontFasciaTriangles, FrontFasciaUV, FrontFasciaNormal);

	//Back fascia
	AddFaceToMesh(BackFasciaVerts, BackFasciaTriangles, BackFasciaUV, BackFasciaNormal);

	// CRITICAL: Convert all vertices from world space to local space
	// ProceduralMeshComponent expects vertices relative to component origin
	for (FVector& Vertex : Vertices)
	{
		Vertex = Vertex - ComponentLocation;
	}

	// Check if section exists and has valid vertices with matching count
	FProcMeshSection* Section = GetProcMeshSection(NewSectionIndex);
	if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != Vertices.Num())
	{
		UE_LOG(LogTemp, Log, TEXT("RoofComponent::GenerateGableRoofMesh - Creating section %d (Section: %s, CurrentVerts: %d, NewVerts: %d)"),
			NewSectionIndex, Section ? TEXT("Valid") : TEXT("Null"), Section ? Section->ProcVertexBuffer.Num() : 0, Vertices.Num());
		CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
		UpdateBounds();             // Recalculate world bounds for VSM
		MarkRenderStateDirty();     // Invalidate cached shadow data
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("RoofComponent::GenerateGableRoofMesh - Updating section %d with %d vertices"), NewSectionIndex, Vertices.Num());
		UpdateMeshSection(NewSectionIndex, Vertices, Normals, UVs, VertexColors, Tangents);
		UpdateBounds();             // Recalculate world bounds for VSM
		MarkRenderStateDirty();     // Invalidate cached shadow data
	}

	InRoofData.Vertices = Vertices;

	// Generate supporting walls for the gable roof
	if (InRoofData.LotManager && !InRoofData.bCommitted)
	{
		// Clear previous walls if regenerating
		InRoofData.CreatedWallIndices.Empty();

		// Generate perimeter walls (4 walls around the base)
		// Legacy path: nullptr uses LotManager's DefaultWallPattern
		GeneratePerimeterWalls(InRoofData.LotManager, RoofVertices, InRoofData.Location, InRoofData.Level, InRoofData.Dimensions.RoofType, nullptr, InRoofData.CreatedWallIndices);

		// Generate gable end walls (triangular walls at front and back)
		// Legacy path: nullptr uses LotManager's DefaultWallPattern
		GenerateGableEndWalls(InRoofData.LotManager, RoofVertices, InRoofData.Location, InRoofData.Level, InRoofData.GableThickness, InRoofData.Dimensions, nullptr, InRoofData.CreatedWallIndices);
	}

	if(!RoofDataArray.Contains(InRoofData))
	{
		if (RoofFreeIndices.Num() > 0)
		{
			InRoofData.RoofArrayIndex = RoofFreeIndices.Pop();
			// Ensure array is large enough for the reused index
			if (RoofDataArray.Num() <= InRoofData.RoofArrayIndex)
			{
				RoofDataArray.SetNum(InRoofData.RoofArrayIndex + 1);
			}
			RoofDataArray[InRoofData.RoofArrayIndex] = InRoofData;
		}
		else
		{
			InRoofData.RoofArrayIndex = RoofDataArray.Add(InRoofData);
			// No need to assign again - Add already inserted it
		}
	}
	else
	{
		// Validate index before accessing array
		if (InRoofData.RoofArrayIndex >= 0 && InRoofData.RoofArrayIndex < RoofDataArray.Num())
		{
			RoofDataArray[InRoofData.RoofArrayIndex] = InRoofData;
		}
		else
		{
			// Index is invalid - treat as new data
			UE_LOG(LogTemp, Warning, TEXT("RoofComponent: Invalid RoofArrayIndex (%d) detected, adding as new entry"), InRoofData.RoofArrayIndex);
			InRoofData.RoofArrayIndex = RoofDataArray.Add(InRoofData);
		}
	}

	// Validate index before returning
	if (InRoofData.RoofArrayIndex >= 0 && InRoofData.RoofArrayIndex < RoofDataArray.Num())
	{
		return RoofDataArray[InRoofData.RoofArrayIndex];
	}
	else
	{
		// Should never happen after the fixes above, but just in case
		UE_LOG(LogTemp, Error, TEXT("RoofComponent: Critical error - RoofArrayIndex (%d) out of bounds (array size: %d)"), InRoofData.RoofArrayIndex, RoofDataArray.Num());
		return InRoofData;
	}
}

FRoofVertices URoofComponent::CalculateRoofVertices(const FVector& Location, const FVector& FrontDirection, const FRoofDimensions& Dimensions)
{
	const FVector RightDirection = FVector::CrossProduct(FrontDirection, FVector::UpVector).GetSafeNormal();
	const FVector AdjustPeak = RightDirection * ((Dimensions.RightDistance - Dimensions.LeftDistance) / 2) ;

	const FVector Front(FrontDirection * Dimensions.FrontDistance);
	const FVector Back(-FrontDirection * Dimensions.BackDistance);

	FRoofVertices RoofVertices;

	RoofVertices.GablePeakFront  = Location + AdjustPeak + (FVector::UpVector * Dimensions.Height) + Front;
	RoofVertices.GablePeakBack   = Location + AdjustPeak + (FVector::UpVector * Dimensions.Height) + Back;
	RoofVertices.GableFrontRight = Location + RightDirection * Dimensions.RightDistance + Front;
	RoofVertices.GableBackRight  = Location + RightDirection * Dimensions.RightDistance + Back;
	RoofVertices.GableFrontLeft  = Location + -RightDirection  * Dimensions.LeftDistance  + Front;
	RoofVertices.GableBackLeft   = Location + -RightDirection  * Dimensions.LeftDistance  + Back;
	
	RoofVertices.RightSlopeDirection = RoofVertices.GableFrontRight - RoofVertices.GablePeakFront;
	RoofVertices.RightSlopeDirection.Normalize();
	RoofVertices.LeftSlopeDirection  = RoofVertices.GableFrontLeft  - RoofVertices.GablePeakFront;
	RoofVertices.LeftSlopeDirection.Normalize();

	const FVector FrontRake(FrontDirection * Dimensions.FrontRake);
	const FVector BackRake(-FrontDirection * Dimensions.BackRake);

	RoofVertices.PeakFront  = RoofVertices.GablePeakFront  + FrontRake;
	RoofVertices.PeakBack   = RoofVertices.GablePeakBack   + BackRake;

	// Extend eave vertices down the slope past the gable base, then raise them to sit on top of baseboard
	// Calculate the non-normalized slope vectors (from peak to base corner)
	FVector RightSlopeFull = RoofVertices.GableFrontRight - RoofVertices.GablePeakFront;
	FVector LeftSlopeFull = RoofVertices.GableFrontLeft - RoofVertices.GablePeakFront;

	// Calculate how far to extend along the full slope based on eve distance
	float RightSlopeLength = RightSlopeFull.Size();
	float LeftSlopeLength = LeftSlopeFull.Size();

	FVector RightEveExtension = (RightSlopeFull / RightSlopeLength) * (RightSlopeLength + Dimensions.RightEve);
	FVector LeftEveExtension = (LeftSlopeFull / LeftSlopeLength) * (LeftSlopeLength + Dimensions.LeftEve);

	// Calculate base eave positions (extended down slope by eve distance)
	FVector FrontRightBase = RoofVertices.GablePeakFront + RightEveExtension;
	FVector BackRightBase  = RoofVertices.GablePeakBack  + RightEveExtension;
	FVector FrontLeftBase  = RoofVertices.GablePeakFront + LeftEveExtension;
	FVector BackLeftBase   = RoofVertices.GablePeakBack  + LeftEveExtension;

	// Raise eave vertices by baseboard height so roof sits ON TOP of baseboard cap
	// This prevents baseboard geometry from sticking through the roof
	// NOTE: Rake extension will be applied in GenerateGableRoofMesh to both slopes AND fascia
	const float BaseboardHeight = 30.0f;
	FVector BaseboardOffset = BaseboardHeight * FVector::UpVector;

	RoofVertices.FrontRight = FrontRightBase + BaseboardOffset;
	RoofVertices.BackRight  = BackRightBase  + BaseboardOffset;
	RoofVertices.FrontLeft  = FrontLeftBase  + BaseboardOffset;
	RoofVertices.BackLeft   = BackLeftBase   + BaseboardOffset;

	return RoofVertices;
}

FRoofSegmentData URoofComponent::GenerateHipRoofMesh(FRoofSegmentData InRoofData)
{
	int32 NewSectionIndex = GetNumSections();
	if(InRoofData.SectionIndex != -1)
	{
		NewSectionIndex = InRoofData.SectionIndex;
	}

	InRoofData.SectionIndex = NewSectionIndex;

	float RoofThickness = InRoofData.RoofThickness;
	FVector Location = InRoofData.Location;
	FVector Direction = InRoofData.Direction;
	FRoofDimensions Dims = InRoofData.Dimensions;

	// Get component location for world-to-local conversion
	FVector ComponentLocation = GetComponentLocation();

	// Calculate right direction perpendicular to forward direction
	FVector RightDir = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();

	// Calculate base corners
	FVector FL = Location - RightDir * Dims.LeftDistance - Direction * Dims.FrontDistance;
	FVector FR = Location + RightDir * Dims.RightDistance - Direction * Dims.FrontDistance;
	FVector BR = Location + RightDir * Dims.RightDistance + Direction * Dims.BackDistance;
	FVector BL = Location - RightDir * Dims.LeftDistance + Direction * Dims.BackDistance;

	// Calculate dimensions
	float TotalWidth = Dims.LeftDistance + Dims.RightDistance;
	float TotalLength = Dims.FrontDistance + Dims.BackDistance;
	float MinDimension = FMath::Min(TotalWidth, TotalLength) / 2.0f;

	// Determine if we have a ridge line or a single peak
	FVector Peak, TL, TR, BL_Ridge, BR_Ridge;
	bool bHasRidge = !FMath::IsNearlyEqual(TotalWidth, TotalLength, 1.0f);

	if (TotalWidth < TotalLength)
	{
		// Ridge runs front-to-back
		// Calculate ridge inset based on width (distance from front/back edges to ridge)
		float InsetDist = TotalWidth / 2.0f;

		// Calculate ridge points relative to center Location (not corners)
		// This keeps the ridge stable when left/right distances change
		TL = Location + Direction * (-Dims.FrontDistance + InsetDist) + FVector::UpVector * Dims.Height;
		TR = Location + Direction * (-Dims.FrontDistance + InsetDist) + FVector::UpVector * Dims.Height;
		BL_Ridge = Location + Direction * (Dims.BackDistance - InsetDist) + FVector::UpVector * Dims.Height;
		BR_Ridge = Location + Direction * (Dims.BackDistance - InsetDist) + FVector::UpVector * Dims.Height;

		// Weld ridge vertices to ensure they're perfectly aligned
		// All ridge points should be at same height
		float RidgeHeight = (TL.Z + TR.Z + BL_Ridge.Z + BR_Ridge.Z) / 4.0f;
		TL.Z = TR.Z = BL_Ridge.Z = BR_Ridge.Z = RidgeHeight;

		// Left ridge points should be at same X,Y (welded together along centerline)
		TL.X = BL_Ridge.X = (TL.X + BL_Ridge.X) / 2.0f;
		TL.Y = BL_Ridge.Y = (TL.Y + BL_Ridge.Y) / 2.0f;

		// Right ridge points should be at same X,Y (welded together along centerline)
		TR.X = BR_Ridge.X = (TR.X + BR_Ridge.X) / 2.0f;
		TR.Y = BR_Ridge.Y = (TR.Y + BR_Ridge.Y) / 2.0f;
	}
	else if (TotalLength < TotalWidth)
	{
		// Ridge runs left-to-right
		// Calculate ridge inset based on length (distance from left/right edges to ridge)
		float InsetDist = TotalLength / 2.0f;

		// Calculate ridge points relative to center Location (not corners)
		// This keeps the ridge stable when front/back distances change
		TL = Location + RightDir * (-Dims.LeftDistance + InsetDist) + FVector::UpVector * Dims.Height;
		TR = Location + RightDir * (Dims.RightDistance - InsetDist) + FVector::UpVector * Dims.Height;
		BL_Ridge = Location + RightDir * (-Dims.LeftDistance + InsetDist) + FVector::UpVector * Dims.Height;
		BR_Ridge = Location + RightDir * (Dims.RightDistance - InsetDist) + FVector::UpVector * Dims.Height;

		// Weld ridge vertices to ensure they're perfectly aligned
		// All ridge points should be at same height
		float RidgeHeight = (TL.Z + TR.Z + BL_Ridge.Z + BR_Ridge.Z) / 4.0f;
		TL.Z = TR.Z = BL_Ridge.Z = BR_Ridge.Z = RidgeHeight;

		// Front ridge points should be at same X,Y (welded together along centerline)
		TL.X = TR.X = (TL.X + TR.X) / 2.0f;
		TL.Y = TR.Y = (TL.Y + TR.Y) / 2.0f;

		// Back ridge points should be at same X,Y (welded together along centerline)
		BL_Ridge.X = BR_Ridge.X = (BL_Ridge.X + BR_Ridge.X) / 2.0f;
		BL_Ridge.Y = BR_Ridge.Y = (BL_Ridge.Y + BR_Ridge.Y) / 2.0f;
	}
	else
	{
		// Square base - single peak point
		Peak = Location + FVector::UpVector * Dims.Height;
		TL = TR = BL_Ridge = BR_Ridge = Peak;
		bHasRidge = false;
	}

	// Add overhangs to base corners
	FVector FrontOverhang = -Direction * Dims.FrontRake;
	FVector BackOverhang = Direction * Dims.BackRake;
	FVector LeftOverhang = -RightDir * Dims.LeftEve;
	FVector RightOverhang = RightDir * Dims.RightEve;

	FL += FrontOverhang + LeftOverhang;
	FR += FrontOverhang + RightOverhang;
	BR += BackOverhang + RightOverhang;
	BL += BackOverhang + LeftOverhang;

	// Combined mesh arrays
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	auto AddFaceToMesh = [&](const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector2D>& UV, const FVector& NormalDirection)
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
			VertexColors.Add(FColor::White);
			Tangents.Add(FProcMeshTangent());
		}
	};

	// Generate 4 hip faces with thickness
	if (bHasRidge && TotalWidth < TotalLength)
	{
		// Front and back are triangular, left and right are trapezoids

		// Front Hip Face (Triangle) - following Gable pattern
		FVector FrontPeak = (TL + TR) / 2.0f;
		FVector FrontNormal = FVector::CrossProduct(FR - FL, FrontPeak - FL).GetSafeNormal();
		TArray<FVector> FrontVerts;
		FrontVerts.Add(FL);
		FrontVerts.Add(FR);
		FrontVerts.Add(FrontPeak + FrontNormal * RoofThickness);  // Outer peak
		FrontVerts.Add(FL);
		FrontVerts.Add(FR);
		FrontVerts.Add(FrontPeak);  // Inner peak
		TArray<int32> FrontTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> FrontUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(FrontVerts, FrontTriangles, FrontUV, FrontNormal);

		// Back Hip Face (Triangle)
		FVector BackPeak = (BL_Ridge + BR_Ridge) / 2.0f;
		FVector BackNormal = FVector::CrossProduct(BL - BR, BackPeak - BR).GetSafeNormal();
		TArray<FVector> BackVerts;
		BackVerts.Add(BR);
		BackVerts.Add(BL);
		BackVerts.Add(BackPeak + BackNormal * RoofThickness);  // Outer peak
		BackVerts.Add(BR);
		BackVerts.Add(BL);
		BackVerts.Add(BackPeak);  // Inner peak
		TArray<int32> BackTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> BackUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(BackVerts, BackTriangles, BackUV, BackNormal);

		// Left Hip Face (Trapezoid)
		FVector LeftNormal = FVector::CrossProduct(BL - FL, BL_Ridge - FL).GetSafeNormal();
		TArray<FVector> LeftVerts;
		LeftVerts.Add(TL + LeftNormal * RoofThickness);      // 0 - Outer top front
		LeftVerts.Add(BL_Ridge + LeftNormal * RoofThickness); // 1 - Outer top back
		LeftVerts.Add(FL);                                    // 2 - Inner bottom front
		LeftVerts.Add(BL);                                    // 3 - Inner bottom back
		LeftVerts.Add(TL);                                    // 4 - Inner top front
		LeftVerts.Add(BL_Ridge);                              // 5 - Inner top back
		TArray<int32> LeftTriangles = {0, 2, 3, 1, 0, 3, 0, 4, 5, 1, 0, 5, 2, 0, 4, 5, 3, 2};
		TArray<FVector2D> LeftUV = {FVector2D(0, 1), FVector2D(1, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1)};
		AddFaceToMesh(LeftVerts, LeftTriangles, LeftUV, LeftNormal);

		// Right Hip Face (Trapezoid)
		FVector RightNormal = FVector::CrossProduct(TR - FR, BR - FR).GetSafeNormal();
		TArray<FVector> RightVerts;
		RightVerts.Add(TR + RightNormal * RoofThickness);      // 0 - Outer top front
		RightVerts.Add(BR_Ridge + RightNormal * RoofThickness); // 1 - Outer top back
		RightVerts.Add(FR);                                     // 2 - Inner bottom front
		RightVerts.Add(BR);                                     // 3 - Inner bottom back
		RightVerts.Add(TR);                                     // 4 - Inner top front
		RightVerts.Add(BR_Ridge);                               // 5 - Inner top back
		TArray<int32> RightTriangles = {0, 2, 3, 1, 0, 3, 0, 4, 5, 1, 0, 5, 2, 0, 4, 5, 3, 2};
		TArray<FVector2D> RightUV = {FVector2D(0, 1), FVector2D(1, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1)};
		AddFaceToMesh(RightVerts, RightTriangles, RightUV, RightNormal);
	}
	else if (bHasRidge && TotalLength < TotalWidth)
	{
		// Left and right are triangular, front and back are trapezoids

		// Left Hip Face (Triangle) - following Gable pattern
		FVector LeftPeak = (TL + BL_Ridge) / 2.0f;
		FVector LeftNormal = FVector::CrossProduct(BL - FL, LeftPeak - FL).GetSafeNormal();
		TArray<FVector> LeftVerts;
		LeftVerts.Add(FL);
		LeftVerts.Add(BL);
		LeftVerts.Add(LeftPeak + LeftNormal * RoofThickness);  // Outer peak
		LeftVerts.Add(FL);
		LeftVerts.Add(BL);
		LeftVerts.Add(LeftPeak);  // Inner peak
		TArray<int32> LeftTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> LeftUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(LeftVerts, LeftTriangles, LeftUV, LeftNormal);

		// Right Hip Face (Triangle)
		FVector RightPeak = (TR + BR_Ridge) / 2.0f;
		FVector RightNormal = FVector::CrossProduct(RightPeak - FR, BR - FR).GetSafeNormal();
		TArray<FVector> RightVerts;
		RightVerts.Add(FR);
		RightVerts.Add(BR);
		RightVerts.Add(RightPeak + RightNormal * RoofThickness);  // Outer peak
		RightVerts.Add(FR);
		RightVerts.Add(BR);
		RightVerts.Add(RightPeak);  // Inner peak
		TArray<int32> RightTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> RightUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(RightVerts, RightTriangles, RightUV, RightNormal);

		// Front Hip Face (Trapezoid)
		FVector FrontNormal = FVector::CrossProduct(FR - FL, TR - FL).GetSafeNormal();
		TArray<FVector> FrontVerts;
		FrontVerts.Add(TL + FrontNormal * RoofThickness);      // 0 - Outer top left
		FrontVerts.Add(TR + FrontNormal * RoofThickness);      // 1 - Outer top right
		FrontVerts.Add(FL);                                     // 2 - Inner bottom left
		FrontVerts.Add(FR);                                     // 3 - Inner bottom right
		FrontVerts.Add(TL);                                     // 4 - Inner top left
		FrontVerts.Add(TR);                                     // 5 - Inner top right
		TArray<int32> FrontTriangles = {0, 2, 3, 1, 0, 3, 0, 4, 5, 1, 0, 5, 2, 0, 4, 5, 3, 2};
		TArray<FVector2D> FrontUV = {FVector2D(0, 1), FVector2D(1, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1)};
		AddFaceToMesh(FrontVerts, FrontTriangles, FrontUV, FrontNormal);

		// Back Hip Face (Trapezoid)
		FVector BackNormal = FVector::CrossProduct(BL - BR, BL_Ridge - BR).GetSafeNormal();
		TArray<FVector> BackVerts;
		BackVerts.Add(BR_Ridge + BackNormal * RoofThickness);   // 0 - Outer top right
		BackVerts.Add(BL_Ridge + BackNormal * RoofThickness);   // 1 - Outer top left
		BackVerts.Add(BR);                                       // 2 - Inner bottom right
		BackVerts.Add(BL);                                       // 3 - Inner bottom left
		BackVerts.Add(BR_Ridge);                                 // 4 - Inner top right
		BackVerts.Add(BL_Ridge);                                 // 5 - Inner top left
		TArray<int32> BackTriangles = {0, 2, 3, 1, 0, 3, 0, 4, 5, 1, 0, 5, 2, 0, 4, 5, 3, 2};
		TArray<FVector2D> BackUV = {FVector2D(0, 1), FVector2D(1, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1)};
		AddFaceToMesh(BackVerts, BackTriangles, BackUV, BackNormal);
	}
	else
	{
		// Square base - 4 triangular faces meeting at peak
		// Calculate the single outer peak point by averaging all 4 normal directions and offsetting upward
		FVector FrontNormal = FVector::CrossProduct(FR - FL, Peak - FL).GetSafeNormal();
		FVector RightNormal = FVector::CrossProduct(BR - FR, Peak - FR).GetSafeNormal();
		FVector BackNormal = FVector::CrossProduct(BL - BR, Peak - BR).GetSafeNormal();
		FVector LeftNormal = FVector::CrossProduct(FL - BL, Peak - BL).GetSafeNormal();

		// Average all normals to get a unified upward direction, then offset by thickness
		FVector AverageNormal = (FrontNormal + RightNormal + BackNormal + LeftNormal).GetSafeNormal();
		FVector OuterPeak = Peak + AverageNormal * RoofThickness;

		// Front Face - all faces share the same outer peak
		TArray<FVector> FrontVerts;
		FrontVerts.Add(FL);
		FrontVerts.Add(FR);
		FrontVerts.Add(OuterPeak);  // Shared outer peak
		FrontVerts.Add(FL);
		FrontVerts.Add(FR);
		FrontVerts.Add(Peak);  // Inner peak
		TArray<int32> FrontTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> FrontUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(FrontVerts, FrontTriangles, FrontUV, FrontNormal);

		// Right Face
		TArray<FVector> RightVerts;
		RightVerts.Add(FR);
		RightVerts.Add(BR);
		RightVerts.Add(OuterPeak);  // Shared outer peak
		RightVerts.Add(FR);
		RightVerts.Add(BR);
		RightVerts.Add(Peak);  // Inner peak
		TArray<int32> RightTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> RightUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(RightVerts, RightTriangles, RightUV, RightNormal);

		// Back Face
		TArray<FVector> BackVerts;
		BackVerts.Add(BR);
		BackVerts.Add(BL);
		BackVerts.Add(OuterPeak);  // Shared outer peak
		BackVerts.Add(BR);
		BackVerts.Add(BL);
		BackVerts.Add(Peak);  // Inner peak
		TArray<int32> BackTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> BackUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(BackVerts, BackTriangles, BackUV, BackNormal);

		// Left Face
		TArray<FVector> LeftVerts;
		LeftVerts.Add(BL);
		LeftVerts.Add(FL);
		LeftVerts.Add(OuterPeak);  // Shared outer peak
		LeftVerts.Add(BL);
		LeftVerts.Add(FL);
		LeftVerts.Add(Peak);  // Inner peak
		TArray<int32> LeftTriangles = {0, 3, 5, 1, 0, 5, 0, 2, 4, 3, 0, 4};
		TArray<FVector2D> LeftUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1), FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(LeftVerts, LeftTriangles, LeftUV, LeftNormal);
	}

	// =====================================================
	// UNDERSIDE GEOMETRY - faces visible from below
	// Uses inner vertices (no thickness offset) with reversed winding
	// =====================================================
	FVector UndersideNormal = FVector(0, 0, -1);

	if (bHasRidge && TotalWidth < TotalLength)
	{
		// Ridge runs front-to-back: front/back triangles, left/right trapezoids
		FVector FrontPeak = (TL + TR) / 2.0f;
		FVector BackPeak = (BL_Ridge + BR_Ridge) / 2.0f;

		// Front underside (triangle) - reversed winding
		TArray<FVector> FrontUnderVerts = {FL, FR, FrontPeak};
		TArray<int32> FrontUnderTris = {0, 2, 1};  // Reversed
		TArray<FVector2D> FrontUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(FrontUnderVerts, FrontUnderTris, FrontUnderUV, UndersideNormal);

		// Back underside (triangle) - reversed winding
		TArray<FVector> BackUnderVerts = {BR, BL, BackPeak};
		TArray<int32> BackUnderTris = {0, 2, 1};  // Reversed
		TArray<FVector2D> BackUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(BackUnderVerts, BackUnderTris, BackUnderUV, UndersideNormal);

		// Left underside (trapezoid) - reversed winding
		TArray<FVector> LeftUnderVerts = {FL, BL, BL_Ridge, TL};
		TArray<int32> LeftUnderTris = {0, 1, 2, 0, 2, 3};  // Reversed
		TArray<FVector2D> LeftUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
		AddFaceToMesh(LeftUnderVerts, LeftUnderTris, LeftUnderUV, UndersideNormal);

		// Right underside (trapezoid) - reversed winding
		TArray<FVector> RightUnderVerts = {FR, BR, BR_Ridge, TR};
		TArray<int32> RightUnderTris = {0, 2, 1, 0, 3, 2};  // Reversed
		TArray<FVector2D> RightUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
		AddFaceToMesh(RightUnderVerts, RightUnderTris, RightUnderUV, UndersideNormal);
	}
	else if (bHasRidge && TotalLength < TotalWidth)
	{
		// Ridge runs left-to-right: left/right triangles, front/back trapezoids
		FVector LeftPeak = (TL + BL_Ridge) / 2.0f;
		FVector RightPeak = (TR + BR_Ridge) / 2.0f;

		// Left underside (triangle) - reversed winding
		TArray<FVector> LeftUnderVerts = {FL, BL, LeftPeak};
		TArray<int32> LeftUnderTris = {0, 2, 1};  // Reversed
		TArray<FVector2D> LeftUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(LeftUnderVerts, LeftUnderTris, LeftUnderUV, UndersideNormal);

		// Right underside (triangle) - reversed winding
		TArray<FVector> RightUnderVerts = {FR, BR, RightPeak};
		TArray<int32> RightUnderTris = {0, 1, 2};  // Note: FR->BR->RightPeak needs this winding
		TArray<FVector2D> RightUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(RightUnderVerts, RightUnderTris, RightUnderUV, UndersideNormal);

		// Front underside (trapezoid) - reversed winding
		TArray<FVector> FrontUnderVerts = {FL, FR, TR, TL};
		TArray<int32> FrontUnderTris = {0, 2, 1, 0, 3, 2};  // Reversed
		TArray<FVector2D> FrontUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
		AddFaceToMesh(FrontUnderVerts, FrontUnderTris, FrontUnderUV, UndersideNormal);

		// Back underside (trapezoid) - reversed winding
		TArray<FVector> BackUnderVerts = {BL, BR, BR_Ridge, BL_Ridge};
		TArray<int32> BackUnderTris = {0, 1, 2, 0, 2, 3};  // Reversed
		TArray<FVector2D> BackUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};
		AddFaceToMesh(BackUnderVerts, BackUnderTris, BackUnderUV, UndersideNormal);
	}
	else
	{
		// Square base - 4 triangular underside faces meeting at peak
		// Front underside - reversed winding
		TArray<FVector> FrontUnderVerts = {FL, FR, Peak};
		TArray<int32> FrontUnderTris = {0, 2, 1};
		TArray<FVector2D> FrontUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(FrontUnderVerts, FrontUnderTris, FrontUnderUV, UndersideNormal);

		// Right underside - reversed winding
		TArray<FVector> RightUnderVerts = {FR, BR, Peak};
		TArray<int32> RightUnderTris = {0, 2, 1};
		TArray<FVector2D> RightUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(RightUnderVerts, RightUnderTris, RightUnderUV, UndersideNormal);

		// Back underside - reversed winding
		TArray<FVector> BackUnderVerts = {BR, BL, Peak};
		TArray<int32> BackUnderTris = {0, 2, 1};
		TArray<FVector2D> BackUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(BackUnderVerts, BackUnderTris, BackUnderUV, UndersideNormal);

		// Left underside - reversed winding
		TArray<FVector> LeftUnderVerts = {BL, FL, Peak};
		TArray<int32> LeftUnderTris = {0, 2, 1};
		TArray<FVector2D> LeftUnderUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(0.5f, 1)};
		AddFaceToMesh(LeftUnderVerts, LeftUnderTris, LeftUnderUV, UndersideNormal);
	}

	// CRITICAL: Convert all vertices from world space to local space
	// ProceduralMeshComponent expects vertices relative to component origin
	for (FVector& Vertex : Vertices)
	{
		Vertex = Vertex - ComponentLocation;
	}

	// Create or update mesh section
	if(!GetProcMeshSection(NewSectionIndex))
	{
		CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
		UpdateBounds();             // Recalculate world bounds for VSM
		MarkRenderStateDirty();     // Invalidate cached shadow data
	}
	else
	{
		if(GetProcMeshSection(NewSectionIndex)->ProcVertexBuffer.Num() != Vertices.Num())
		{
			CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
			UpdateBounds();             // Recalculate world bounds for VSM
			MarkRenderStateDirty();     // Invalidate cached shadow data
		}
		else
		{
			UpdateMeshSection(NewSectionIndex, Vertices, Normals, UVs, VertexColors, Tangents);
			UpdateBounds();             // Recalculate world bounds for VSM
			MarkRenderStateDirty();     // Invalidate cached shadow data
		}
	}

	InRoofData.Vertices = Vertices;

	// Generate supporting walls for the hip roof
	if (InRoofData.LotManager && !InRoofData.bCommitted)
	{
		// Clear previous walls if regenerating
		InRoofData.CreatedWallIndices.Empty();

		// Hip roofs have all sides sloped, so only perimeter walls are needed
		FRoofVertices RoofVerts = CalculateRoofVertices(InRoofData.Location, InRoofData.Direction, InRoofData.Dimensions);
		// Legacy path: nullptr uses LotManager's DefaultWallPattern
		GeneratePerimeterWalls(InRoofData.LotManager, RoofVerts, InRoofData.Location, InRoofData.Level, InRoofData.Dimensions.RoofType, nullptr, InRoofData.CreatedWallIndices);
	}

	if(!RoofDataArray.Contains(InRoofData))
	{
		if (RoofFreeIndices.Num() > 0)
		{
			InRoofData.RoofArrayIndex = RoofFreeIndices.Pop();
			// Ensure array is large enough for the reused index
			if (RoofDataArray.Num() <= InRoofData.RoofArrayIndex)
			{
				RoofDataArray.SetNum(InRoofData.RoofArrayIndex + 1);
			}
			RoofDataArray[InRoofData.RoofArrayIndex] = InRoofData;
		}
		else
		{
			InRoofData.RoofArrayIndex = RoofDataArray.Add(InRoofData);
			// No need to assign again - Add already inserted it
		}
	}
	else
	{
		// Validate index before accessing array
		if (InRoofData.RoofArrayIndex >= 0 && InRoofData.RoofArrayIndex < RoofDataArray.Num())
		{
			RoofDataArray[InRoofData.RoofArrayIndex] = InRoofData;
		}
		else
		{
			// Index is invalid - treat as new data
			UE_LOG(LogTemp, Warning, TEXT("RoofComponent: Invalid RoofArrayIndex (%d) detected in GenerateHipRoofMesh, adding as new entry"), InRoofData.RoofArrayIndex);
			InRoofData.RoofArrayIndex = RoofDataArray.Add(InRoofData);
		}
	}

	// Validate index before returning
	if (InRoofData.RoofArrayIndex >= 0 && InRoofData.RoofArrayIndex < RoofDataArray.Num())
	{
		return RoofDataArray[InRoofData.RoofArrayIndex];
	}
	else
	{
		// Should never happen after the fixes above, but just in case
		UE_LOG(LogTemp, Error, TEXT("RoofComponent: Critical error in GenerateHipRoofMesh - RoofArrayIndex (%d) out of bounds (array size: %d)"), InRoofData.RoofArrayIndex, RoofDataArray.Num());
		return InRoofData;
	}
}

FRoofSegmentData URoofComponent::GenerateShedRoofMesh(FRoofSegmentData InRoofData)
{
	int32 NewSectionIndex = GetNumSections();
	if(InRoofData.SectionIndex != -1)
	{
		NewSectionIndex = InRoofData.SectionIndex;
	}

	InRoofData.SectionIndex = NewSectionIndex;

	float RoofThickness = InRoofData.RoofThickness;
	FVector Location = InRoofData.Location;
	FVector Direction = InRoofData.Direction;
	FRoofDimensions Dims = InRoofData.Dimensions;

	// Get component location for world-to-local conversion
	FVector ComponentLocation = GetComponentLocation();

	// Calculate right direction perpendicular to forward direction
	FVector RightDir = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();

	// Calculate base corners (front = high, back = low)
	FVector FrontLeft = Location - RightDir * Dims.LeftDistance - Direction * Dims.FrontDistance;
	FVector FrontRight = Location + RightDir * Dims.RightDistance - Direction * Dims.FrontDistance;
	FVector BackLeft = Location - RightDir * Dims.LeftDistance + Direction * Dims.BackDistance;
	FVector BackRight = Location + RightDir * Dims.RightDistance + Direction * Dims.BackDistance;

	// Front edge is high, back edge is low
	FrontLeft.Z += Dims.Height;
	FrontRight.Z += Dims.Height;
	// Back edges stay at base height

	// Calculate slope direction for eave overhangs
	FVector SlopeDir = (BackLeft - FrontLeft).GetSafeNormal();

	// Add overhangs
	FVector FrontOverhang = -Direction * Dims.FrontRake;
	FVector BackOverhang = Direction * Dims.BackRake;

	FrontLeft += FrontOverhang - RightDir * Dims.LeftEve;
	FrontRight += FrontOverhang + RightDir * Dims.RightEve;
	BackLeft += BackOverhang - RightDir * Dims.LeftEve;
	BackRight += BackOverhang + RightDir * Dims.RightEve;

	// Combined mesh arrays
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	auto AddFaceToMesh = [&](const TArray<FVector>& Verts, const TArray<int32>& Tris, const TArray<FVector2D>& UV, const FVector& NormalDirection)
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
			VertexColors.Add(FColor::White);
			Tangents.Add(FProcMeshTangent());
		}
	};

	// Calculate slope normal
	FVector SlopeNormal = FVector::CrossProduct(RightDir, SlopeDir).GetSafeNormal();

	// Top sloped surface - simple quad with correct winding
	FVector ThicknessOffset = SlopeNormal * RoofThickness;
	TArray<FVector> TopSlopeVerts;
	TopSlopeVerts.Add(FrontLeft + ThicknessOffset);   // 0
	TopSlopeVerts.Add(FrontRight + ThicknessOffset);  // 1
	TopSlopeVerts.Add(BackRight + ThicknessOffset);   // 2
	TopSlopeVerts.Add(BackLeft + ThicknessOffset);    // 3

	TArray<int32> TopSlopeTriangles = {0, 2, 1, 0, 3, 2};  // Reversed winding for outward normal
	TArray<FVector2D> TopSlopeUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};

	// Bottom sloped surface - simple quad with correct winding
	TArray<FVector> BottomSlopeVerts;
	BottomSlopeVerts.Add(FrontLeft);   // 0
	BottomSlopeVerts.Add(FrontRight);  // 1
	BottomSlopeVerts.Add(BackRight);   // 2
	BottomSlopeVerts.Add(BackLeft);    // 3

	TArray<int32> BottomSlopeTriangles = {0, 1, 2, 0, 2, 3};  // Correct winding for inward normal
	TArray<FVector2D> BottomSlopeUV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};

	// Left Edge Face
	TArray<FVector> LeftEndVerts;
	LeftEndVerts.Add(TopSlopeVerts[3]);     // Back Left Top
	LeftEndVerts.Add(TopSlopeVerts[0]);     // Front Left Top
	LeftEndVerts.Add(BottomSlopeVerts[3]);  // Back Left Bottom
	LeftEndVerts.Add(BottomSlopeVerts[0]);  // Front Left Bottom
	TArray<int32> EndTriangles = {0, 3, 2, 1, 3, 0};  // Reversed winding
	TArray<FVector2D> EndUV = {FVector2D(0, 1), FVector2D(1, 1), FVector2D(0, 0), FVector2D(1, 0)};

	// Right Edge Face
	TArray<FVector> RightEndVerts;
	RightEndVerts.Add(TopSlopeVerts[1]);     // Front Right Top
	RightEndVerts.Add(TopSlopeVerts[2]);     // Back Right Top
	RightEndVerts.Add(BottomSlopeVerts[1]);  // Front Right Bottom
	RightEndVerts.Add(BottomSlopeVerts[2]);  // Back Right Bottom

	// Front Fascia Face (high edge)
	TArray<FVector> FrontFasciaVerts;
	FrontFasciaVerts.Add(TopSlopeVerts[0]);     // Front Left Top
	FrontFasciaVerts.Add(TopSlopeVerts[1]);     // Front Right Top
	FrontFasciaVerts.Add(BottomSlopeVerts[0]);  // Front Left Bottom
	FrontFasciaVerts.Add(BottomSlopeVerts[1]);  // Front Right Bottom

	// Back Fascia Face (low edge)
	TArray<FVector> BackFasciaVerts;
	BackFasciaVerts.Add(TopSlopeVerts[2]);     // Back Right Top
	BackFasciaVerts.Add(TopSlopeVerts[3]);     // Back Left Top
	BackFasciaVerts.Add(BottomSlopeVerts[2]);  // Back Right Bottom
	BackFasciaVerts.Add(BottomSlopeVerts[3]);  // Back Left Bottom

	// Add all faces
	AddFaceToMesh(TopSlopeVerts, TopSlopeTriangles, TopSlopeUV, SlopeNormal);
	AddFaceToMesh(BottomSlopeVerts, BottomSlopeTriangles, BottomSlopeUV, -SlopeNormal);
	AddFaceToMesh(LeftEndVerts, EndTriangles, EndUV, -RightDir);
	AddFaceToMesh(RightEndVerts, EndTriangles, EndUV, RightDir);
	AddFaceToMesh(FrontFasciaVerts, EndTriangles, EndUV, -Direction);
	AddFaceToMesh(BackFasciaVerts, EndTriangles, EndUV, Direction);

	// CRITICAL: Convert all vertices from world space to local space
	// ProceduralMeshComponent expects vertices relative to component origin
	for (FVector& Vertex : Vertices)
	{
		Vertex = Vertex - ComponentLocation;
	}

	// Create or update mesh section
	if(!GetProcMeshSection(NewSectionIndex))
	{
		CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
		UpdateBounds();             // Recalculate world bounds for VSM
		MarkRenderStateDirty();     // Invalidate cached shadow data
	}
	else
	{
		// Check if vertex count changed or section is empty
		FProcMeshSection* Section = GetProcMeshSection(NewSectionIndex);
		if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != Vertices.Num())
		{
			CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
			UpdateBounds();             // Recalculate world bounds for VSM
			MarkRenderStateDirty();     // Invalidate cached shadow data
		}
		else
		{
			UpdateMeshSection(NewSectionIndex, Vertices, Normals, UVs, VertexColors, Tangents);
			UpdateBounds();             // Recalculate world bounds for VSM
			MarkRenderStateDirty();     // Invalidate cached shadow data
		}
	}

	InRoofData.Vertices = Vertices;

	// Generate supporting walls for the shed roof
	if (InRoofData.LotManager && !InRoofData.bCommitted)
	{
		// Clear previous walls if regenerating
		InRoofData.CreatedWallIndices.Empty();

		// Shed roofs need perimeter walls + shed end walls (triangular walls on left/right)
		FRoofVertices RoofVerts = CalculateRoofVertices(InRoofData.Location, InRoofData.Direction, InRoofData.Dimensions);
		// Legacy path: nullptr uses LotManager's DefaultWallPattern
		GeneratePerimeterWalls(InRoofData.LotManager, RoofVerts, InRoofData.Location, InRoofData.Level, InRoofData.Dimensions.RoofType, nullptr, InRoofData.CreatedWallIndices);
		GenerateShedEndWalls(InRoofData.LotManager, RoofVerts, InRoofData.Location, InRoofData.Level, InRoofData.Dimensions, nullptr, InRoofData.CreatedWallIndices);
	}

	if(!RoofDataArray.Contains(InRoofData))
	{
		if (RoofFreeIndices.Num() > 0)
		{
			InRoofData.RoofArrayIndex = RoofFreeIndices.Pop();
			// Ensure array is large enough for the reused index
			if (RoofDataArray.Num() <= InRoofData.RoofArrayIndex)
			{
				RoofDataArray.SetNum(InRoofData.RoofArrayIndex + 1);
			}
			RoofDataArray[InRoofData.RoofArrayIndex] = InRoofData;
		}
		else
		{
			InRoofData.RoofArrayIndex = RoofDataArray.Add(InRoofData);
			// No need to assign again - Add already inserted it
		}
	}
	else
	{
		// Validate index before accessing array
		if (InRoofData.RoofArrayIndex >= 0 && InRoofData.RoofArrayIndex < RoofDataArray.Num())
		{
			RoofDataArray[InRoofData.RoofArrayIndex] = InRoofData;
		}
		else
		{
			// Index is invalid - treat as new data
			UE_LOG(LogTemp, Warning, TEXT("RoofComponent: Invalid RoofArrayIndex (%d) detected in GenerateShedRoofMesh, adding as new entry"), InRoofData.RoofArrayIndex);
			InRoofData.RoofArrayIndex = RoofDataArray.Add(InRoofData);
		}
	}

	// Validate index before returning
	if (InRoofData.RoofArrayIndex >= 0 && InRoofData.RoofArrayIndex < RoofDataArray.Num())
	{
		return RoofDataArray[InRoofData.RoofArrayIndex];
	}
	else
	{
		// Should never happen after the fixes above, but just in case
		UE_LOG(LogTemp, Error, TEXT("RoofComponent: Critical error in GenerateShedRoofMesh - RoofArrayIndex (%d) out of bounds (array size: %d)"), InRoofData.RoofArrayIndex, RoofDataArray.Num());
		return InRoofData;
	}
}

float URoofComponent::CalculateHeightFromPitch(float Pitch, float Width)
{
	// Height = (Width / 2) * tan(Pitch)
	// Pitch is in degrees
	return (Width / 2.0f) * FMath::Tan(FMath::DegreesToRadians(Pitch));
}

float URoofComponent::CalculatePitchFromHeight(float Height, float Width)
{
	// Pitch = atan(Height / (Width / 2))
	// Returns pitch in degrees
	return FMath::RadiansToDegrees(FMath::Atan(Height / (Width / 2.0f)));
}

float URoofComponent::CalculateRoofHeightAtPosition(const FVector2D& Position, const FRoofVertices& RoofVerts, ERoofType RoofType)
{
	// Calculate the roof Z height at a given XY position
	// Uses bilinear interpolation on the roof plane

	switch (RoofType)
	{
	case ERoofType::Gable:
		{
			// Gable roof has two sloped planes divided by the peak ridge
			// Determine which side of the peak we're on
			FVector2D PeakFront2D(RoofVerts.PeakFront.X, RoofVerts.PeakFront.Y);
			FVector2D PeakBack2D(RoofVerts.PeakBack.X, RoofVerts.PeakBack.Y);
			FVector2D RidgeDirection = (PeakBack2D - PeakFront2D).GetSafeNormal();
			FVector2D RidgeNormal(-RidgeDirection.Y, RidgeDirection.X); // Perpendicular to ridge

			FVector2D ToPeak = Position - PeakFront2D;
			float SideSign = FVector2D::DotProduct(ToPeak, RidgeNormal);

			// Determine which quad (left or right slope) we're in
			FVector CornerA, CornerB, PeakA, PeakB;
			if (SideSign >= 0.0f) // Right side
			{
				CornerA = RoofVerts.FrontRight;
				CornerB = RoofVerts.BackRight;
				PeakA = RoofVerts.PeakFront;
				PeakB = RoofVerts.PeakBack;
			}
			else // Left side
			{
				CornerA = RoofVerts.FrontLeft;
				CornerB = RoofVerts.BackLeft;
				PeakA = RoofVerts.PeakFront;
				PeakB = RoofVerts.PeakBack;
			}

			// Bilinear interpolation on the quad (CornerA, CornerB, PeakB, PeakA)
			// First, find position within quad using parametric coordinates
			FVector2D A2D(CornerA.X, CornerA.Y);
			FVector2D B2D(CornerB.X, CornerB.Y);
			FVector2D PA2D(PeakA.X, PeakA.Y);
			FVector2D PB2D(PeakB.X, PeakB.Y);

			// Simple approach: interpolate along front-back direction, then peak-eave direction
			FVector2D FrontEdge = A2D - PA2D;
			FVector2D BackEdge = B2D - PB2D;
			FVector2D FrontToBack = B2D - A2D;

			float FrontBackT = 0.5f; // Default to middle
			if (FrontToBack.SizeSquared() > 0.001f)
			{
				FVector2D ToPos = Position - A2D;
				FrontBackT = FMath::Clamp(FVector2D::DotProduct(ToPos, FrontToBack) / FrontToBack.SizeSquared(), 0.0f, 1.0f);
			}

			// Interpolate between front edge and back edge
			FVector2D EdgePoint = FMath::Lerp(A2D, B2D, FrontBackT);
			FVector2D PeakPoint = FMath::Lerp(PA2D, PB2D, FrontBackT);
			float EdgeZ = FMath::Lerp(CornerA.Z, CornerB.Z, FrontBackT);
			float PeakZ = FMath::Lerp(PeakA.Z, PeakB.Z, FrontBackT);

			// Interpolate from peak to edge
			FVector2D PeakToEdge = EdgePoint - PeakPoint;
			float PeakEdgeT = 0.5f;
			if (PeakToEdge.SizeSquared() > 0.001f)
			{
				FVector2D ToPosFromPeak = Position - PeakPoint;
				PeakEdgeT = FMath::Clamp(FVector2D::DotProduct(ToPosFromPeak, PeakToEdge) / PeakToEdge.SizeSquared(), 0.0f, 1.0f);
			}

			return FMath::Lerp(PeakZ, EdgeZ, PeakEdgeT);
		}

	case ERoofType::Shed:
		{
			// Shed roof has a single slope from front (high) to back (low)
			FVector2D FrontLeft2D(RoofVerts.GableFrontLeft.X, RoofVerts.GableFrontLeft.Y);
			FVector2D BackLeft2D(RoofVerts.GableBackLeft.X, RoofVerts.GableBackLeft.Y);
			FVector2D FrontToBack = BackLeft2D - FrontLeft2D;

			float FrontBackT = 0.5f;
			if (FrontToBack.SizeSquared() > 0.001f)
			{
				FVector2D ToPos = Position - FrontLeft2D;
				FrontBackT = FMath::Clamp(FVector2D::DotProduct(ToPos, FrontToBack) / FrontToBack.SizeSquared(), 0.0f, 1.0f);
			}

			return FMath::Lerp(RoofVerts.GableFrontLeft.Z, RoofVerts.GableBackLeft.Z, FrontBackT);
		}

	case ERoofType::Hip:
		// Hip roof - more complex, use similar quad interpolation
		// For now, use gable logic as fallback
		return CalculateRoofHeightAtPosition(Position, RoofVerts, ERoofType::Gable);

	default:
		return RoofVerts.PeakFront.Z; // Fallback to peak height
	}
}

void URoofComponent::TrimCommittedWallToRoof(ALotManager* InLotManager, int32 WallArrayIndex, const FRoofVertices& RoofVerts, ERoofType RoofType, float BaseZ)
{
	// Trim committed wall mesh vertices to conform to roof slope
	// This operates on the wall AFTER it's been committed and mesh sections created

	UE_LOG(LogTemp, Log, TEXT("TrimCommittedWallToRoof: Attempting to trim wall array index %d"), WallArrayIndex);

	if (!InLotManager || !InLotManager->WallComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("TrimCommittedWallToRoof: Invalid LotManager or WallComponent"));
		return;
	}

	UWallComponent* WallComp = InLotManager->WallComponent;
	if (!WallComp->WallDataArray.IsValidIndex(WallArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("TrimCommittedWallToRoof: Invalid wall array index %d (array size: %d)"),
			WallArrayIndex, WallComp->WallDataArray.Num());
		return;
	}

	FWallSegmentData& WallData = WallComp->WallDataArray[WallArrayIndex];
	int32 SectionIndex = WallData.SectionIndex;

	UE_LOG(LogTemp, Log, TEXT("TrimCommittedWallToRoof: Wall %d has section index %d"), WallArrayIndex, SectionIndex);

	// Get the procedural mesh section
	FProcMeshSection* MeshSection = WallComp->GetProcMeshSection(SectionIndex);
	if (!MeshSection)
	{
		UE_LOG(LogTemp, Error, TEXT("TrimCommittedWallToRoof: Failed to get mesh section %d"), SectionIndex);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("TrimCommittedWallToRoof: Mesh section has %d vertices"), MeshSection->ProcVertexBuffer.Num());

	// Trim vertices in the mesh section
	bool bModified = false;
	int32 VerticesModified = 0;
	for (int32 i = 0; i < MeshSection->ProcVertexBuffer.Num(); i++)
	{
		FProcMeshVertex& Vertex = MeshSection->ProcVertexBuffer[i];

		// Only modify vertices that are above the base (top vertices)
		if (Vertex.Position.Z > BaseZ + 10.0f) // Small epsilon to identify top vertices
		{
			// Calculate roof height at this vertex's XY position
			FVector2D Position2D(Vertex.Position.X, Vertex.Position.Y);
			float RoofHeight = CalculateRoofHeightAtPosition(Position2D, RoofVerts, RoofType);

			// Clamp vertex Z to not exceed roof height, but preserve baseboard geometry
			// Baseboard is 30 units tall, so trimmed wall must not go below BaseZ + 30
			const float BaseboardHeight = 30.0f;
			float MinTrimHeight = BaseZ + BaseboardHeight;
			float ClampedRoofHeight = FMath::Max(RoofHeight, MinTrimHeight);

			if (Vertex.Position.Z > ClampedRoofHeight)
			{
				float OldZ = Vertex.Position.Z;
				Vertex.Position.Z = ClampedRoofHeight;
				bModified = true;
				VerticesModified++;

				if (VerticesModified <= 2) // Log first few for debugging
				{
					UE_LOG(LogTemp, Log, TEXT("TrimCommittedWallToRoof: Trimmed vertex %d from Z=%.2f to Z=%.2f (baseboard preserved at %.2f)"),
						i, OldZ, ClampedRoofHeight, MinTrimHeight);
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TrimCommittedWallToRoof: Modified %d vertices (bModified=%d)"), VerticesModified, bModified);

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

		WallComp->UpdateMeshSection(SectionIndex, Vertices, Normals, UV0, VertexColors, Tangents);
		UE_LOG(LogTemp, Log, TEXT("TrimCommittedWallToRoof: Successfully updated mesh section %d"), SectionIndex);

		// STORE trim data in WallComponent for future regenerations
		FRoofTrimData TrimData;
		TrimData.RoofVerts = RoofVerts;
		TrimData.RoofType = RoofType;
		TrimData.BaseZ = BaseZ;
		WallComp->WallRoofTrimData.Add(WallArrayIndex, TrimData);
		UE_LOG(LogTemp, Log, TEXT("TrimCommittedWallToRoof: Stored trim data for wall %d"), WallArrayIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TrimCommittedWallToRoof: No vertices were modified for wall %d"), WallArrayIndex);
	}
}

FVector URoofComponent::FindPerpendicularVector(FVector& Direction)
{
	Direction.Normalize();

	return FVector::CrossProduct(Direction, FVector(1,0,0));
}

void URoofComponent::GeneratePerimeterWalls(ALotManager* InLotManager, const FRoofVertices& RoofVerts, const FVector& BaseLocation, int32 Level, ERoofType RoofType, UWallPattern* WallPattern, TArray<int32>& OutCreatedWallIndices)
{
	if (!InLotManager || !InLotManager->WallComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("GeneratePerimeterWalls: LotManager or WallComponent is null"));
		return;
	}

	// Get BuildServer for proper wall creation (creates WallGraph edges + rendering)
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("GeneratePerimeterWalls: BuildServer subsystem not found"));
		return;
	}

	// Use provided wall pattern or fallback to LotManager's default
	UWallPattern* PatternToUse = WallPattern ? WallPattern : InLotManager->DefaultWallPattern;

	// Get the gable base corners (without overhang adjustments)
	FVector FL = RoofVerts.GableFrontLeft;
	FVector FR = RoofVerts.GableFrontRight;
	FVector BR = RoofVerts.GableBackRight;
	FVector BL = RoofVerts.GableBackLeft;

	// BaseLocation represents the ceiling tiles the roof sits on
	float FloorZ = BaseLocation.Z;

	// CRITICAL: Snap to tile grid like regular walls do
	// This ensures perimeter walls align with the building grid
	FVector FL_Snapped, FR_Snapped, BR_Snapped, BL_Snapped;

	if (!InLotManager->LocationToTileCorner(Level, FL, FL_Snapped) ||
		!InLotManager->LocationToTileCorner(Level, FR, FR_Snapped) ||
		!InLotManager->LocationToTileCorner(Level, BR, BR_Snapped) ||
		!InLotManager->LocationToTileCorner(Level, BL, BL_Snapped))
	{
		UE_LOG(LogTemp, Warning, TEXT("GeneratePerimeterWalls: Failed to snap corners to tile grid"));
		return;
	}

	// Create floor positions for each snapped corner
	FVector FL_Floor = FVector(FL_Snapped.X, FL_Snapped.Y, FloorZ);
	FVector FR_Floor = FVector(FR_Snapped.X, FR_Snapped.Y, FloorZ);
	FVector BR_Floor = FVector(BR_Snapped.X, BR_Snapped.Y, FloorZ);
	FVector BL_Floor = FVector(BL_Snapped.X, BL_Snapped.Y, FloorZ);

	// CRITICAL: Create individual tile-sized STANDARD wall segments (not one continuous wall)
	// Each segment is GridTileSize wide at uniform 300 height for proper UV tiling
	// Roof geometry will hide portions that extend above the slope
	const float StandardWallHeight = 300.0f;
	const float GridTileSize = InLotManager->GridTileSize;

	// Collect wall indices for batch trimming (avoid regeneration during loop)
	TArray<int32> WallIndicesToTrim;

	// Helper to create tile-sized standard wall segments along an edge
	auto CreateSegmentedPerimeterWall = [&](const FVector& EdgeStart, const FVector& EdgeEnd)
	{
		FVector EdgeDirection = EdgeEnd - EdgeStart;
		float EdgeLength = EdgeDirection.Size2D();
		EdgeDirection.Normalize();

		int32 NumSegments = FMath::Max(1, FMath::RoundToInt(EdgeLength / GridTileSize));

		for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; SegmentIndex++)
		{
			float SegmentStartDist = SegmentIndex * GridTileSize;
			float SegmentEndDist = FMath::Min((SegmentIndex + 1) * GridTileSize, EdgeLength);

			FVector SegStart = EdgeStart + (EdgeDirection * SegmentStartDist);
			FVector SegEnd = EdgeStart + (EdgeDirection * SegmentEndDist);

			FVector SegStartSnapped, SegEndSnapped;
			if (!InLotManager->LocationToTileCorner(Level, SegStart, SegStartSnapped) ||
				!InLotManager->LocationToTileCorner(Level, SegEnd, SegEndSnapped))
			{
				continue;
			}

			FVector SegStartFloor = FVector(SegStartSnapped.X, SegStartSnapped.Y, FloorZ);
			FVector SegEndFloor = FVector(SegEndSnapped.X, SegEndSnapped.Y, FloorZ);

			// IMPORTANT: Use BuildServer->BuildWall() to create walls properly
			// This creates both WallGraph edges (for room detection) and rendering mesh
			// Direct WallComponent calls bypass WallGraph and break room detection
			// Defer room floor/ceiling generation - roof will mark rooms and generate manually
			int32 WallCountBefore = InLotManager->WallComponent->WallDataArray.Num();
			BuildServer->BuildWall(Level, SegStartFloor, SegEndFloor, StandardWallHeight, PatternToUse, InLotManager->DefaultWallMaterial, true);
			int32 WallCountAfter = InLotManager->WallComponent->WallDataArray.Num();

			if (WallCountAfter > WallCountBefore)
			{
				int32 NewWallIndex = WallCountAfter - 1;
				OutCreatedWallIndices.Add(NewWallIndex);
				WallIndicesToTrim.Add(NewWallIndex);
			}
		}
	};

	// Create segmented standard walls for each perimeter edge
	CreateSegmentedPerimeterWall(FL_Floor, FR_Floor);  // Front
	CreateSegmentedPerimeterWall(FR_Floor, BR_Floor);  // Right
	CreateSegmentedPerimeterWall(BR_Floor, BL_Floor);  // Back
	CreateSegmentedPerimeterWall(BL_Floor, FL_Floor);  // Left

	// Trim ALL walls AFTER all commits are done (prevents regeneration from overwriting previous trims)
	for (int32 WallIdx : WallIndicesToTrim)
	{
		TrimCommittedWallToRoof(InLotManager, WallIdx, RoofVerts, RoofType, FloorZ);
	}

	// POST-CREATION FIX: Ensure all perimeter walls have room assignments
	// Room detection runs when the loop closes, but some walls may still have unassigned sides
	if (InLotManager->RoomManager && InLotManager->WallGraph)
	{
		// Find roof room(s) at this level
		for (const auto& RoomPair : InLotManager->RoomManager->Rooms)
		{
			const FRoomData& Room = RoomPair.Value;
			if (Room.Level != Level)
				continue;

			// For each perimeter wall, check if it needs room assignment
			for (int32 WallIdx : WallIndicesToTrim)
			{
				if (!InLotManager->WallComponent->WallDataArray.IsValidIndex(WallIdx))
					continue;

				const FWallSegmentData& WallData = InLotManager->WallComponent->WallDataArray[WallIdx];
				if (WallData.WallEdgeID == -1)
					continue;

				const FWallEdge* Edge = InLotManager->WallGraph->Edges.Find(WallData.WallEdgeID);
				if (!Edge)
					continue;

				// Fix walls that have unassigned sides
				if (Edge->Room1 == 0 || Edge->Room2 == 0)
				{
					InLotManager->WallGraph->AssignRoomToWallByGeometry(WallData.WallEdgeID, Room.RoomID, Room.Centroid);
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("GeneratePerimeterWalls: Created and trimmed %d wall segments"), WallIndicesToTrim.Num());
}

void URoofComponent::GenerateGableEndWalls(ALotManager* InLotManager, const FRoofVertices& RoofVerts, const FVector& BaseLocation, int32 Level, float GableThickness, const FRoofDimensions& RoofDimensions, UWallPattern* WallPattern, TArray<int32>& OutCreatedWallIndices)
{
	if (!InLotManager || !InLotManager->WallComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateGableEndWalls: LotManager or WallComponent is null"));
		return;
	}

	// Get BuildServer for proper wall creation (creates WallGraph edges + rendering)
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateGableEndWalls: BuildServer subsystem not found"));
		return;
	}

	// Use provided wall pattern or fallback to LotManager's default
	UWallPattern* PatternToUse = WallPattern ? WallPattern : InLotManager->DefaultWallPattern;

	// BaseLocation.Z is the ceiling level the roof sits on
	float FloorZ = BaseLocation.Z;
	const float StandardWallHeight = 300.0f;
	const float GridTileSize = InLotManager->GridTileSize;

	// Collect wall indices for batch trimming (avoid regeneration during loop)
	TArray<int32> WallIndicesToTrim;

	// Track front vs back gable walls separately for correct room assignment
	TArray<int32> FrontGableWallIndices;
	TArray<int32> BackGableWallIndices;
	FVector FrontGableCentroid = FVector::ZeroVector;
	FVector BackGableCentroid = FVector::ZeroVector;

	// Lambda to create segmented gable wall and track indices
	auto CreateSegmentedGableWall = [&](const FVector& EdgeStart, const FVector& EdgeEnd, TArray<int32>& OutGableWallIndices) {
		FVector EdgeDirection = EdgeEnd - EdgeStart;
		float EdgeLength = EdgeDirection.Size2D();
		EdgeDirection.Normalize();

		int32 NumSegments = FMath::Max(1, FMath::RoundToInt(EdgeLength / GridTileSize));

		for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; SegmentIndex++)
		{
			// Calculate segment positions at GridTileSize increments
			float SegmentStartDist = SegmentIndex * GridTileSize;
			float SegmentEndDist = FMath::Min((SegmentIndex + 1) * GridTileSize, EdgeLength);

			FVector SegStart = EdgeStart + EdgeDirection * SegmentStartDist;
			FVector SegEnd = EdgeStart + EdgeDirection * SegmentEndDist;

			// Snap to tile corners
			FVector SnappedSegStart, SnappedSegEnd;
			if (!InLotManager->LocationToTileCorner(Level, SegStart, SnappedSegStart) ||
				!InLotManager->LocationToTileCorner(Level, SegEnd, SnappedSegEnd))
			{
				continue;
			}

			// Create standard wall segment at uniform 300-unit height
			FVector SegStartFloor = FVector(SnappedSegStart.X, SnappedSegStart.Y, FloorZ);
			FVector SegEndFloor = FVector(SnappedSegEnd.X, SnappedSegEnd.Y, FloorZ);

			// IMPORTANT: Use BuildServer->BuildWall() to create walls properly
			// This creates both WallGraph edges (for room detection) and rendering mesh
			// Defer room floor/ceiling generation - roof will mark rooms and generate manually
			int32 WallCountBefore = InLotManager->WallComponent->WallDataArray.Num();
			BuildServer->BuildWall(Level, SegStartFloor, SegEndFloor, StandardWallHeight, PatternToUse, InLotManager->DefaultWallMaterial, true);
			int32 WallCountAfter = InLotManager->WallComponent->WallDataArray.Num();

			if (WallCountAfter > WallCountBefore)
			{
				int32 NewWallIndex = WallCountAfter - 1;
				OutCreatedWallIndices.Add(NewWallIndex);
				WallIndicesToTrim.Add(NewWallIndex);
				OutGableWallIndices.Add(NewWallIndex);
			}
		}
	};

	int32 WallsCreated = 0;

	// Front Gable Triangle - only if Front wall flag is set
	if (EnumHasAnyFlags(static_cast<ERoofWallFlags>(RoofDimensions.WallFlags), ERoofWallFlags::Front))
	{
		// Walls positioned BEFORE the rake extension for overhang
		FVector OriginalGableFrontLeft = RoofVerts.GableFrontLeft;
		FVector OriginalGableFrontRight = RoofVerts.GableFrontRight;
		FVector OriginalGablePeakFront = RoofVerts.GablePeakFront;

		// CRITICAL: Snap to tile grid like regular walls do
		FVector SnappedFrontLeft, SnappedFrontRight, SnappedPeakFront;
		if (!InLotManager->LocationToTileCorner(Level, OriginalGableFrontLeft, SnappedFrontLeft) ||
			!InLotManager->LocationToTileCorner(Level, OriginalGableFrontRight, SnappedFrontRight) ||
			!InLotManager->LocationToTileCorner(Level, OriginalGablePeakFront, SnappedPeakFront))
		{
			UE_LOG(LogTemp, Warning, TEXT("GenerateGableEndWalls: Failed to snap front gable corners to tile grid"));
			return;
		}

		// Calculate front gable triangle centroid for correct room assignment
		// This point is in the CENTER of the triangular gable area
		FrontGableCentroid = (SnappedFrontLeft + SnappedFrontRight + SnappedPeakFront) / 3.0f;

		// Create segmented walls from corners to peak
		CreateSegmentedGableWall(SnappedFrontLeft, SnappedPeakFront, FrontGableWallIndices);
		CreateSegmentedGableWall(SnappedFrontRight, SnappedPeakFront, FrontGableWallIndices);

		WallsCreated++;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("GenerateGableEndWalls: Skipping front gable (Front wall flag not set)"));
	}

	// Back Gable Triangle - only if Back wall flag is set
	if (EnumHasAnyFlags(static_cast<ERoofWallFlags>(RoofDimensions.WallFlags), ERoofWallFlags::Back))
	{
		// Gable vertices now include rake extension, but walls should stop BEFORE rake
		// Remove rake offset to get original gable positions
		FVector BackRakeOffset(-InLotManager->GetActorForwardVector() * RoofDimensions.BackRake);
		FVector OriginalGableBackLeft = RoofVerts.GableBackLeft - BackRakeOffset;
		FVector OriginalGableBackRight = RoofVerts.GableBackRight - BackRakeOffset;
		FVector OriginalGablePeakBack = RoofVerts.PeakBack - BackRakeOffset;

		// CRITICAL: Snap to tile grid like regular walls do
		FVector SnappedBackLeft, SnappedBackRight, SnappedPeakBack;
		if (!InLotManager->LocationToTileCorner(Level, OriginalGableBackLeft, SnappedBackLeft) ||
			!InLotManager->LocationToTileCorner(Level, OriginalGableBackRight, SnappedBackRight) ||
			!InLotManager->LocationToTileCorner(Level, OriginalGablePeakBack, SnappedPeakBack))
		{
			UE_LOG(LogTemp, Warning, TEXT("GenerateGableEndWalls: Failed to snap back gable corners to tile grid"));
			return;
		}

		// Calculate back gable triangle centroid for correct room assignment
		// This point is in the CENTER of the triangular gable area
		BackGableCentroid = (SnappedBackLeft + SnappedBackRight + SnappedPeakBack) / 3.0f;

		// Create segmented walls from corners to peak
		CreateSegmentedGableWall(SnappedBackLeft, SnappedPeakBack, BackGableWallIndices);
		CreateSegmentedGableWall(SnappedBackRight, SnappedPeakBack, BackGableWallIndices);

		WallsCreated++;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("GenerateGableEndWalls: Skipping back gable (Back wall flag not set)"));
	}

	// Trim ALL walls AFTER all commits are done (prevents regeneration from overwriting previous trims)
	for (int32 WallIdx : WallIndicesToTrim)
	{
		TrimCommittedWallToRoof(InLotManager, WallIdx, RoofVerts, RoofDimensions.RoofType, FloorZ);
	}

	// POST-CREATION FIX: Assign room IDs to gable walls that weren't captured by room detection
	// Gable walls are created AFTER the perimeter walls close the loop and trigger room detection,
	// so they may have Room1=0 and Room2=0. This causes WallPatternTool to treat them as "freestanding".
	//
	// IMPORTANT: Use the gable triangle centroid (not room centroid) for the half-plane test.
	// The room centroid is at the center of the entire room, which may be on the wrong side
	// of diagonal gable walls. The gable triangle centroid is always inside the gable area.
	if (InLotManager->RoomManager && InLotManager->WallGraph)
	{
		// Find roof room(s) at this level
		for (const auto& RoomPair : InLotManager->RoomManager->Rooms)
		{
			const FRoomData& Room = RoomPair.Value;
			if (Room.Level != Level)
				continue;

			// Helper lambda to assign room to a set of gable walls using the appropriate centroid
			auto AssignRoomToGableWalls = [&](const TArray<int32>& GableWallIndices, const FVector& GableCentroid) {
				for (int32 WallIdx : GableWallIndices)
				{
					if (!InLotManager->WallComponent->WallDataArray.IsValidIndex(WallIdx))
						continue;

					const FWallSegmentData& WallData = InLotManager->WallComponent->WallDataArray[WallIdx];
					if (WallData.WallEdgeID == -1)
						continue;

					const FWallEdge* Edge = InLotManager->WallGraph->Edges.Find(WallData.WallEdgeID);
					if (!Edge)
						continue;

					// Only fix walls that have no room assignments (freestanding)
					if (Edge->Room1 == 0 || Edge->Room2 == 0)
					{
						InLotManager->WallGraph->AssignRoomToWallByGeometry(WallData.WallEdgeID, Room.RoomID, GableCentroid);
					}
				}
			};

			// Assign front gable walls using the front gable triangle centroid
			if (FrontGableWallIndices.Num() > 0 && !FrontGableCentroid.IsZero())
			{
				AssignRoomToGableWalls(FrontGableWallIndices, FrontGableCentroid);
			}

			// Assign back gable walls using the back gable triangle centroid
			if (BackGableWallIndices.Num() > 0 && !BackGableCentroid.IsZero())
			{
				AssignRoomToGableWalls(BackGableWallIndices, BackGableCentroid);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("GenerateGableEndWalls: Post-processed %d front + %d back gable walls for room assignment"),
			FrontGableWallIndices.Num(), BackGableWallIndices.Num());
	}

	UE_LOG(LogTemp, Log, TEXT("GenerateGableEndWalls: Created %d gable end wall groups with %d trimmed segments"), WallsCreated, WallIndicesToTrim.Num());
}

void URoofComponent::GenerateShedEndWalls(ALotManager* InLotManager, const FRoofVertices& RoofVerts, const FVector& BaseLocation, int32 Level, const FRoofDimensions& RoofDimensions, UWallPattern* WallPattern, TArray<int32>& OutCreatedWallIndices)
{
	if (!InLotManager || !InLotManager->WallComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateShedEndWalls: LotManager or WallComponent is null"));
		return;
	}

	// Get BuildServer for proper wall creation (creates WallGraph edges + rendering)
	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Error, TEXT("GenerateShedEndWalls: BuildServer subsystem not found"));
		return;
	}

	// Use provided wall pattern or fallback to LotManager's default
	UWallPattern* PatternToUse = WallPattern ? WallPattern : InLotManager->DefaultWallPattern;

	// BaseLocation.Z is the ceiling level the roof sits on
	float FloorZ = BaseLocation.Z;
	const float StandardWallHeight = 300.0f;
	const float GridTileSize = InLotManager->GridTileSize;

	// Collect wall indices for batch trimming (avoid regeneration during loop)
	TArray<int32> WallIndicesToTrim;

	// Track front vs diagonal (left/right) walls separately for correct room assignment
	TArray<int32> FrontWallIndices;
	TArray<int32> LeftWallIndices;
	TArray<int32> RightWallIndices;
	FVector ShedCentroid = FVector::ZeroVector;

	// Lambda to create segmented shed wall and track indices
	auto CreateSegmentedShedWall = [&](const FVector& EdgeStart, const FVector& EdgeEnd, TArray<int32>& OutShedWallIndices) {
		FVector EdgeDirection = EdgeEnd - EdgeStart;
		float EdgeLength = EdgeDirection.Size2D();
		EdgeDirection.Normalize();

		int32 NumSegments = FMath::Max(1, FMath::RoundToInt(EdgeLength / GridTileSize));

		for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; SegmentIndex++)
		{
			// Calculate segment positions at GridTileSize increments
			float SegmentStartDist = SegmentIndex * GridTileSize;
			float SegmentEndDist = FMath::Min((SegmentIndex + 1) * GridTileSize, EdgeLength);

			FVector SegStart = EdgeStart + EdgeDirection * SegmentStartDist;
			FVector SegEnd = EdgeStart + EdgeDirection * SegmentEndDist;

			// Snap to tile corners
			FVector SnappedSegStart, SnappedSegEnd;
			if (!InLotManager->LocationToTileCorner(Level, SegStart, SnappedSegStart) ||
				!InLotManager->LocationToTileCorner(Level, SegEnd, SnappedSegEnd))
			{
				continue;
			}

			// Create standard wall segment at uniform 300-unit height
			FVector SegStartFloor = FVector(SnappedSegStart.X, SnappedSegStart.Y, FloorZ);
			FVector SegEndFloor = FVector(SnappedSegEnd.X, SnappedSegEnd.Y, FloorZ);

			// IMPORTANT: Use BuildServer->BuildWall() to create walls properly
			// This creates both WallGraph edges (for room detection) and rendering mesh
			// Defer room floor/ceiling generation - roof will mark rooms and generate manually
			int32 WallCountBefore = InLotManager->WallComponent->WallDataArray.Num();
			BuildServer->BuildWall(Level, SegStartFloor, SegEndFloor, StandardWallHeight, PatternToUse, InLotManager->DefaultWallMaterial, true);
			int32 WallCountAfter = InLotManager->WallComponent->WallDataArray.Num();

			if (WallCountAfter > WallCountBefore)
			{
				int32 NewWallIndex = WallCountAfter - 1;
				OutCreatedWallIndices.Add(NewWallIndex);
				WallIndicesToTrim.Add(NewWallIndex);
				OutShedWallIndices.Add(NewWallIndex);
			}
		}
	};

	int32 WallsCreated = 0;

	// CRITICAL: Snap all corners to tile grid like regular walls do
	FVector SnappedFrontLeft, SnappedFrontRight, SnappedBackLeft, SnappedBackRight;
	if (!InLotManager->LocationToTileCorner(Level, RoofVerts.GableFrontLeft, SnappedFrontLeft) ||
		!InLotManager->LocationToTileCorner(Level, RoofVerts.GableFrontRight, SnappedFrontRight) ||
		!InLotManager->LocationToTileCorner(Level, RoofVerts.GableBackLeft, SnappedBackLeft) ||
		!InLotManager->LocationToTileCorner(Level, RoofVerts.GableBackRight, SnappedBackRight))
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateShedEndWalls: Failed to snap corners to tile grid"));
		return;
	}

	// Calculate shed quadrilateral centroid for correct room assignment
	// This point is in the CENTER of the shed roof area (works for all shed walls)
	ShedCentroid = (SnappedFrontLeft + SnappedFrontRight + SnappedBackLeft + SnappedBackRight) / 4.0f;

	// Front Tall Wall - only if Front wall flag is set
	if (EnumHasAnyFlags(static_cast<ERoofWallFlags>(RoofDimensions.WallFlags), ERoofWallFlags::Front))
	{
		// Create segmented wall along front edge
		CreateSegmentedShedWall(SnappedFrontLeft, SnappedFrontRight, FrontWallIndices);
		WallsCreated++;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("GenerateShedEndWalls: Skipping front wall (Front wall flag not set)"));
	}

	// Left Shed Triangle - only if Left wall flag is set
	if (EnumHasAnyFlags(static_cast<ERoofWallFlags>(RoofDimensions.WallFlags), ERoofWallFlags::Left))
	{
		// Create segmented diagonal wall from high front to low back
		CreateSegmentedShedWall(SnappedFrontLeft, SnappedBackLeft, LeftWallIndices);
		WallsCreated++;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("GenerateShedEndWalls: Skipping left side (Left wall flag not set)"));
	}

	// Right Shed Triangle - only if Right wall flag is set
	if (EnumHasAnyFlags(static_cast<ERoofWallFlags>(RoofDimensions.WallFlags), ERoofWallFlags::Right))
	{
		// Create segmented diagonal wall from high front to low back
		CreateSegmentedShedWall(SnappedFrontRight, SnappedBackRight, RightWallIndices);
		WallsCreated++;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("GenerateShedEndWalls: Skipping right side (Right wall flag not set)"));
	}

	// Trim ALL walls AFTER all commits are done (prevents regeneration from overwriting previous trims)
	for (int32 WallIdx : WallIndicesToTrim)
	{
		TrimCommittedWallToRoof(InLotManager, WallIdx, RoofVerts, RoofDimensions.RoofType, FloorZ);
	}

	// POST-CREATION FIX: Assign room IDs to shed walls that weren't captured by room detection
	// Shed walls are created AFTER the perimeter walls close the loop and trigger room detection,
	// so they may have Room1=0 and Room2=0. This causes WallPatternTool to treat them as "freestanding".
	//
	// IMPORTANT: Use the shed quadrilateral centroid (not room centroid) for the half-plane test.
	// This ensures consistent room assignment for diagonal walls.
	if (InLotManager->RoomManager && InLotManager->WallGraph && !ShedCentroid.IsZero())
	{
		// Find roof room(s) at this level
		for (const auto& RoomPair : InLotManager->RoomManager->Rooms)
		{
			const FRoomData& Room = RoomPair.Value;
			if (Room.Level != Level)
				continue;

			// Helper lambda to assign room to a set of shed walls using the shed centroid
			auto AssignRoomToShedWalls = [&](const TArray<int32>& ShedWallIndices) {
				for (int32 WallIdx : ShedWallIndices)
				{
					if (!InLotManager->WallComponent->WallDataArray.IsValidIndex(WallIdx))
						continue;

					const FWallSegmentData& WallData = InLotManager->WallComponent->WallDataArray[WallIdx];
					if (WallData.WallEdgeID == -1)
						continue;

					const FWallEdge* Edge = InLotManager->WallGraph->Edges.Find(WallData.WallEdgeID);
					if (!Edge)
						continue;

					// Only fix walls that have no room assignments (freestanding)
					if (Edge->Room1 == 0 || Edge->Room2 == 0)
					{
						InLotManager->WallGraph->AssignRoomToWallByGeometry(WallData.WallEdgeID, Room.RoomID, ShedCentroid);
					}
				}
			};

			// Assign all shed walls using the shed quadrilateral centroid
			AssignRoomToShedWalls(FrontWallIndices);
			AssignRoomToShedWalls(LeftWallIndices);
			AssignRoomToShedWalls(RightWallIndices);
		}

		UE_LOG(LogTemp, Log, TEXT("GenerateShedEndWalls: Post-processed %d front + %d left + %d right shed walls for room assignment"),
			FrontWallIndices.Num(), LeftWallIndices.Num(), RightWallIndices.Num());
	}

	UE_LOG(LogTemp, Log, TEXT("GenerateShedEndWalls: Created %d shed wall groups with %d trimmed segments"), WallsCreated, WallIndicesToTrim.Num());
}

// void URoofComponent::RegenerateEverySection()
// {
// }

// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/WaterComponent.h"
#include "Actors/LotManager.h"

UWaterComponent::UWaterComponent(const FObjectInitializer& ObjectInitializer)
	: UProceduralMeshComponent(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	bCreateCollision = false;
	bRenderCustomDepth = false;
}

void UWaterComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                    FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

// ========== Pool Water Implementation ==========

FPoolWaterData UWaterComponent::GeneratePoolWater(int32 RoomID, const TArray<FVector>& BoundaryVertices,
                                                   float WaterSurfaceZ, float PoolFloorZ,
                                                   UMaterialInstance* BaseMaterial)
{
	FPoolWaterData PoolData;

	// Validate input
	if (RoomID < 0 || BoundaryVertices.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("WaterComponent::GeneratePoolWater - Invalid RoomID (%d) or insufficient vertices (%d)"),
			RoomID, BoundaryVertices.Num());
		return PoolData;
	}

	// Check if pool already exists for this room
	if (FPoolWaterData* ExistingPool = FindPoolWater(RoomID))
	{
		UpdatePoolWater(RoomID, BoundaryVertices);
		return *ExistingPool;
	}

	// Initialize pool data
	PoolData.RoomID = RoomID;
	PoolData.BoundaryVertices = BoundaryVertices;
	PoolData.WaterSurfaceZ = WaterSurfaceZ;
	PoolData.PoolFloorZ = PoolFloorZ;
	PoolData.SectionIndex = NextPoolSectionIndex++;
	PoolData.bCommitted = true;

	// Create dynamic material instance
	UMaterialInstance* MaterialToUse = BaseMaterial ? BaseMaterial : DefaultWaterMaterial;
	if (MaterialToUse)
	{
		PoolData.WaterMaterial = UMaterialInstanceDynamic::Create(MaterialToUse, this);
	}

	// Generate the 3D mesh
	GeneratePoolWaterMesh(PoolData);

	// Add to storage and spatial map
	int32 ArrayIndex = PoolWaterArray.Add(PoolData);
	PoolWaterMap.Add(RoomID, ArrayIndex);

	UE_LOG(LogTemp, Log, TEXT("WaterComponent::GeneratePoolWater - Created pool water for RoomID %d with %d vertices, Section %d"),
		RoomID, BoundaryVertices.Num(), PoolData.SectionIndex);

	return PoolData;
}

bool UWaterComponent::UpdatePoolWater(int32 RoomID, const TArray<FVector>& BoundaryVertices)
{
	FPoolWaterData* PoolData = FindPoolWater(RoomID);
	if (!PoolData)
	{
		UE_LOG(LogTemp, Warning, TEXT("WaterComponent::UpdatePoolWater - No pool found for RoomID %d"), RoomID);
		return false;
	}

	// Update vertices
	PoolData->BoundaryVertices = BoundaryVertices;

	// Regenerate mesh
	GeneratePoolWaterMesh(*PoolData);

	UE_LOG(LogTemp, Log, TEXT("WaterComponent::UpdatePoolWater - Updated pool water for RoomID %d"), RoomID);
	return true;
}

bool UWaterComponent::RemovePoolWater(int32 RoomID)
{
	int32* IndexPtr = PoolWaterMap.Find(RoomID);
	if (!IndexPtr)
	{
		return false;
	}

	int32 ArrayIndex = *IndexPtr;
	if (!PoolWaterArray.IsValidIndex(ArrayIndex))
	{
		PoolWaterMap.Remove(RoomID);
		return false;
	}

	// Clear the mesh section
	FPoolWaterData& PoolData = PoolWaterArray[ArrayIndex];
	if (PoolData.SectionIndex >= 0)
	{
		ClearMeshSection(PoolData.SectionIndex);
	}

	// Remove from array (swap with last to maintain indices)
	int32 LastIndex = PoolWaterArray.Num() - 1;
	if (ArrayIndex != LastIndex)
	{
		// Update the swapped element's map entry
		int32 SwappedRoomID = PoolWaterArray[LastIndex].RoomID;
		PoolWaterMap[SwappedRoomID] = ArrayIndex;
	}

	PoolWaterArray.RemoveAtSwap(ArrayIndex);
	PoolWaterMap.Remove(RoomID);

	UE_LOG(LogTemp, Log, TEXT("WaterComponent::RemovePoolWater - Removed pool water for RoomID %d"), RoomID);
	return true;
}

FPoolWaterData* UWaterComponent::FindPoolWater(int32 RoomID)
{
	int32* IndexPtr = PoolWaterMap.Find(RoomID);
	if (!IndexPtr || !PoolWaterArray.IsValidIndex(*IndexPtr))
	{
		return nullptr;
	}
	return &PoolWaterArray[*IndexPtr];
}

bool UWaterComponent::HasPoolWater(int32 RoomID) const
{
	return PoolWaterMap.Contains(RoomID);
}

void UWaterComponent::DestroyAllWater()
{
	// Clear all mesh sections
	for (const FPoolWaterData& PoolData : PoolWaterArray)
	{
		if (PoolData.SectionIndex >= 0)
		{
			ClearMeshSection(PoolData.SectionIndex);
		}
	}

	PoolWaterArray.Empty();
	PoolWaterMap.Empty();
	NextPoolSectionIndex = 0;
}

void UWaterComponent::GeneratePoolWaterMesh(FPoolWaterData& PoolData)
{
	const TArray<FVector>& BoundaryVerts = PoolData.BoundaryVertices;
	const int32 NumVerts = BoundaryVerts.Num();

	if (NumVerts < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("WaterComponent::GeneratePoolWaterMesh - Need at least 3 vertices, got %d"), NumVerts);
		return;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	// Reserve approximate space
	// Top surface: NumVerts vertices
	// Side walls: NumVerts * 4 vertices (4 per quad)
	Vertices.Reserve(NumVerts + NumVerts * 4);
	Normals.Reserve(NumVerts + NumVerts * 4);
	UVs.Reserve(NumVerts + NumVerts * 4);

	// ========== TOP SURFACE ==========
	// Add top surface vertices at water surface Z
	int32 TopStartIndex = Vertices.Num();
	for (int32 i = 0; i < NumVerts; i++)
	{
		FVector TopVert = BoundaryVerts[i];
		TopVert.Z = PoolData.WaterSurfaceZ;
		Vertices.Add(TopVert);
		Normals.Add(FVector(0, 0, 1)); // Up

		// Planar UV projection based on world XY
		UVs.Add(FVector2D(TopVert.X / 100.0f, TopVert.Y / 100.0f));
		VertexColors.Add(FColor::White);
	}

	// Calculate signed area to detect polygon winding
	// In Unreal's coordinate system (X forward, Y right, Z up), viewed from +Z:
	// - Positive signed area = Clockwise (CW) winding
	// - Negative signed area = Counter-Clockwise (CCW) winding
	// This is opposite of standard 2D math because Unreal's Y axis points "right" not "up"
	float SignedArea = 0.0f;
	for (int32 i = 0; i < NumVerts; i++)
	{
		int32 NextI = (i + 1) % NumVerts;
		SignedArea += (BoundaryVerts[i].X * BoundaryVerts[NextI].Y) - (BoundaryVerts[NextI].X * BoundaryVerts[i].Y);
	}
	bool bIsClockwise = SignedArea > 0;

	// Triangulate the top surface polygon
	TArray<int32> TopTriangles = TriangulatePolygon(BoundaryVerts);

	// Offset triangle indices and add to main array
	// If polygon is clockwise, reverse triangle winding so faces point up
	for (int32 i = 0; i < TopTriangles.Num(); i += 3)
	{
		if (bIsClockwise)
		{
			// Reverse winding: swap indices 1 and 2
			Triangles.Add(TopStartIndex + TopTriangles[i]);
			Triangles.Add(TopStartIndex + TopTriangles[i + 2]);
			Triangles.Add(TopStartIndex + TopTriangles[i + 1]);
		}
		else
		{
			Triangles.Add(TopStartIndex + TopTriangles[i]);
			Triangles.Add(TopStartIndex + TopTriangles[i + 1]);
			Triangles.Add(TopStartIndex + TopTriangles[i + 2]);
		}
	}

	// ========== SIDE WALLS ==========
	// For each edge, create a quad (2 triangles)
	float WaterHeight = PoolData.WaterSurfaceZ - PoolData.PoolFloorZ;

	for (int32 i = 0; i < NumVerts; i++)
	{
		int32 NextI = (i + 1) % NumVerts;

		FVector TopLeft = BoundaryVerts[i];
		TopLeft.Z = PoolData.WaterSurfaceZ;

		FVector TopRight = BoundaryVerts[NextI];
		TopRight.Z = PoolData.WaterSurfaceZ;

		FVector BottomLeft = BoundaryVerts[i];
		BottomLeft.Z = PoolData.PoolFloorZ;

		FVector BottomRight = BoundaryVerts[NextI];
		BottomRight.Z = PoolData.PoolFloorZ;

		// Calculate edge direction and outward normal (pointing out of the water, visible from above)
		// For CCW polygon, outward is Cross(Up, EdgeDir)
		// For CW polygon, outward is Cross(EdgeDir, Up) = -Cross(Up, EdgeDir)
		FVector EdgeDir = (TopRight - TopLeft).GetSafeNormal();
		FVector OutwardNormal = bIsClockwise
			? FVector::CrossProduct(EdgeDir, FVector(0, 0, 1)).GetSafeNormal()
			: FVector::CrossProduct(FVector(0, 0, 1), EdgeDir).GetSafeNormal();

		// Edge length for UV calculation
		float EdgeLength = FVector::Dist(TopLeft, TopRight);

		// Add 4 vertices for this quad
		int32 QuadStartIndex = Vertices.Num();

		// Top-Left
		Vertices.Add(TopLeft);
		Normals.Add(OutwardNormal);
		UVs.Add(FVector2D(0, 0));
		VertexColors.Add(FColor::White);

		// Top-Right
		Vertices.Add(TopRight);
		Normals.Add(OutwardNormal);
		UVs.Add(FVector2D(EdgeLength / 100.0f, 0));
		VertexColors.Add(FColor::White);

		// Bottom-Right
		Vertices.Add(BottomRight);
		Normals.Add(OutwardNormal);
		UVs.Add(FVector2D(EdgeLength / 100.0f, WaterHeight / 100.0f));
		VertexColors.Add(FColor::White);

		// Bottom-Left
		Vertices.Add(BottomLeft);
		Normals.Add(OutwardNormal);
		UVs.Add(FVector2D(0, WaterHeight / 100.0f));
		VertexColors.Add(FColor::White);

		// Two triangles for the quad (winding order for outward-facing)
		// Flip winding for CW polygons since the outward direction is reversed
		if (bIsClockwise)
		{
			// CW polygon: Triangle 1: TL, BL, TR
			Triangles.Add(QuadStartIndex + 0); // Top-Left
			Triangles.Add(QuadStartIndex + 3); // Bottom-Left
			Triangles.Add(QuadStartIndex + 1); // Top-Right

			// Triangle 2: TR, BL, BR
			Triangles.Add(QuadStartIndex + 1); // Top-Right
			Triangles.Add(QuadStartIndex + 3); // Bottom-Left
			Triangles.Add(QuadStartIndex + 2); // Bottom-Right
		}
		else
		{
			// CCW polygon: Triangle 1: TL, TR, BL
			Triangles.Add(QuadStartIndex + 0); // Top-Left
			Triangles.Add(QuadStartIndex + 1); // Top-Right
			Triangles.Add(QuadStartIndex + 3); // Bottom-Left

			// Triangle 2: TR, BR, BL
			Triangles.Add(QuadStartIndex + 1); // Top-Right
			Triangles.Add(QuadStartIndex + 2); // Bottom-Right
			Triangles.Add(QuadStartIndex + 3); // Bottom-Left
		}
	}

	// Create or update the mesh section
	CreateMeshSection(PoolData.SectionIndex, Vertices, Triangles, Normals,
	                  UVs, VertexColors, Tangents, bCreateCollision);

	// Apply material
	if (PoolData.WaterMaterial)
	{
		SetMaterial(PoolData.SectionIndex, PoolData.WaterMaterial);
	}

	UE_LOG(LogTemp, Log, TEXT("WaterComponent::GeneratePoolWaterMesh - Generated mesh with %d vertices, %d triangles for Section %d"),
		Vertices.Num(), Triangles.Num() / 3, PoolData.SectionIndex);
}

TArray<int32> UWaterComponent::TriangulatePolygon(const TArray<FVector>& Vertices)
{
	TArray<int32> Triangles;
	const int32 NumVerts = Vertices.Num();

	if (NumVerts < 3)
	{
		return Triangles;
	}

	// Simple case: triangle
	if (NumVerts == 3)
	{
		Triangles.Add(0);
		Triangles.Add(1);
		Triangles.Add(2);
		return Triangles;
	}

	// Simple case: quad
	if (NumVerts == 4)
	{
		Triangles.Add(0);
		Triangles.Add(1);
		Triangles.Add(2);
		Triangles.Add(0);
		Triangles.Add(2);
		Triangles.Add(3);
		return Triangles;
	}

	// Ear clipping algorithm for general polygons
	TArray<int32> Indices;
	Indices.Reserve(NumVerts);
	for (int32 i = 0; i < NumVerts; i++)
	{
		Indices.Add(i);
	}

	int32 SafetyCounter = NumVerts * NumVerts; // Prevent infinite loops
	while (Indices.Num() > 3 && SafetyCounter > 0)
	{
		SafetyCounter--;
		bool bFoundEar = false;

		for (int32 i = 0; i < Indices.Num(); i++)
		{
			if (IsEar(Vertices, Indices, i))
			{
				// Add triangle
				int32 Prev = (i + Indices.Num() - 1) % Indices.Num();
				int32 Next = (i + 1) % Indices.Num();

				Triangles.Add(Indices[Prev]);
				Triangles.Add(Indices[i]);
				Triangles.Add(Indices[Next]);

				// Remove the ear vertex
				Indices.RemoveAt(i);
				bFoundEar = true;
				break;
			}
		}

		if (!bFoundEar)
		{
			// Fallback: polygon may be malformed, try fan triangulation
			UE_LOG(LogTemp, Warning, TEXT("WaterComponent::TriangulatePolygon - Ear clipping failed, using fan triangulation"));
			Triangles.Empty();
			for (int32 i = 1; i < NumVerts - 1; i++)
			{
				Triangles.Add(0);
				Triangles.Add(i);
				Triangles.Add(i + 1);
			}
			return Triangles;
		}
	}

	// Add final triangle
	if (Indices.Num() == 3)
	{
		Triangles.Add(Indices[0]);
		Triangles.Add(Indices[1]);
		Triangles.Add(Indices[2]);
	}

	return Triangles;
}

bool UWaterComponent::IsEar(const TArray<FVector>& Vertices, const TArray<int32>& Indices, int32 EarIndex)
{
	const int32 NumIndices = Indices.Num();
	if (NumIndices < 3)
	{
		return false;
	}

	int32 PrevIdx = (EarIndex + NumIndices - 1) % NumIndices;
	int32 NextIdx = (EarIndex + 1) % NumIndices;

	const FVector& Prev = Vertices[Indices[PrevIdx]];
	const FVector& Curr = Vertices[Indices[EarIndex]];
	const FVector& Next = Vertices[Indices[NextIdx]];

	// Check if triangle is convex (counter-clockwise winding)
	FVector2D A(Prev.X, Prev.Y);
	FVector2D B(Curr.X, Curr.Y);
	FVector2D C(Next.X, Next.Y);

	float Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	if (Cross <= 0)
	{
		return false; // Reflex vertex, not an ear
	}

	// Check that no other vertices are inside this triangle
	for (int32 i = 0; i < NumIndices; i++)
	{
		if (i == PrevIdx || i == EarIndex || i == NextIdx)
		{
			continue;
		}

		const FVector& Test = Vertices[Indices[i]];
		FVector2D P(Test.X, Test.Y);

		if (IsPointInTriangle(P, A, B, C))
		{
			return false; // Another vertex is inside, not an ear
		}
	}

	return true;
}

bool UWaterComponent::IsPointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	// Barycentric coordinate method
	float D1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
	float D2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
	float D3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);

	bool HasNeg = (D1 < 0) || (D2 < 0) || (D3 < 0);
	bool HasPos = (D1 > 0) || (D2 > 0) || (D3 > 0);

	return !(HasNeg && HasPos);
}

// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StairsComponent.h"

#include "Actors/LotManager.h"
#include "Engine/StaticMeshSocket.h"

UStairsComponent::UStairsComponent(const FObjectInitializer& ObjectInitializer) : UProceduralMeshComponent(ObjectInitializer)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	StairsData.StartLoc = FVector(0, 0, 0);
	StairsData.RoofTunnelLoc = FVector(0, 0, 0);

	bCommitted = false;

	// Disable custom depth - stairs are permanent geometry
	bRenderCustomDepth = false;
}

void UStairsComponent::BeginPlay()
{
	Super::BeginPlay();

	// PreviewMaterial is now a configurable UPROPERTY - set it in Blueprint or via defaults

}

void UStairsComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

int32 UStairsComponent::GetSectionIDFromHitResult(const FHitResult& HitResult) const
{
	if (!HitResult.Component.IsValid() || HitResult.Component.Get() != this)
	{
		// Hit result is not on this component
		return -2;
	}
	
	FVector LocalImpactPoint = HitResult.ImpactPoint;
	FVector BoxExtents(4.0f, 4.0f, 4.0f); // Adjust the extents as needed
	FBox ImpactBox(LocalImpactPoint - BoxExtents, LocalImpactPoint + BoxExtents);
	//DrawDebugBox(GetWorld(), LocalImpactPoint, BoxExtents, FColor::Green, false, 1, 0, 1);
	
	// Iterate over each section to find the one containing the hit point
	for (const FStairsSegmentData& Data : StairsDataArray)
	{
		// for (int32 SectionIndex = 0; SectionIndex < GetNumSections(); ++SectionIndex) // fized bug
		// {
		// Get bounds of this section
		FBox SectionBounds(Data.Vertices);
		// Check if the impact point is within the bounds of this section
		if (ImpactBox.Intersect(SectionBounds))
		{
			return Data.StairsArrayIndex;
		}
	}
	
	// Hit point is not within any section bounds
	return -1;
}

void UStairsComponent::DestroyStairs()
{
	// Clear any existing stair treads
	for (UMeshComponent* Tread : StairsData.StairModules)
	{
		if (Tread)
		{
			Tread->DestroyComponent();
		}
	}

	StairsData.StairModules.Empty();
}

void UStairsComponent::CommitStairsSection(FStairsSegmentData InStairsData, UMaterialInstance* TreadsMaterial, UMaterialInstance* LandingsMaterial)
{
	StairsDataArray[InStairsData.StairsArrayIndex].bCommitted = true;
	//Set material on both sides of wall to default
	StairsDataArray[InStairsData.StairsArrayIndex].TreadMaterial = CreateDynamicMaterialInstance(StairsDataArray[InStairsData.StairsArrayIndex].SectionIndex, Cast<UMaterialInterface>(TreadsMaterial));
	SetMaterial(StairsDataArray[InStairsData.StairsArrayIndex].SectionIndex, StairsDataArray[InStairsData.StairsArrayIndex].TreadMaterial);
	bRenderCustomDepth = false;
	//RegenerateEverySection();
}

bool UStairsComponent::FindExistingStairsSection(const FVector& TileCornerStart,
	const TArray<FStairModuleStructure>& OurStructures, FStairsSegmentData& OutStairs)
{
	// for (FStairsSegmentData FoundStairsComponent : StairsDataArray)
	// {
	// 	if (FoundStairsComponent.bCommitted == true && ((FoundStairsComponent.StartLoc == TileCornerStart && FoundStairsComponent.Structures == OurStructures)))
	// 	{
	// 		// Found an existing Stairs segment with matching start and end s
	// 		OutStairs = FoundStairsComponent;
	// 		return true;
	// 	}
	// }
	// No existing Stairs segment found with the given start and end s
	return false;
}

int UStairsComponent::FindNearestStairTread(UStaticMeshComponent* HitMesh)
{
	if (StairsData.StairModules.Num() == 0) return -1;

	constexpr int Index = -1;

	// Check if the hit mesh is part of our stair treads
	for (int i = 0; i < StairsData.StairModules.Num(); ++i)
	{
		if (StairsData.StairModules[i] == HitMesh)
		{
			// We found the tread that was touched!
			return i;
		}
	}
	
	return Index;
}

// for (int i = 0; i < StairsData.StairModules.Num(); ++i)
// {
// 	FVector MeshLocation = StairsData.StairModules[i]->GetComponentLocation();
// 	const float Distance = FVector::Dist(MeshLocation, TargetLocation);
//
// 	if (Distance < MinDistance)
// 	{
// 		MinDistance = Distance;
// 		Index = i;
// 	}
// }

FStairsSegmentData UStairsComponent::GenerateStairsSection(const FVector& Start, const FVector& End,
	const TArray<FStairModuleStructure>& OurStructures, const TArray<UStaticMeshComponent*> StairModules, const float StairsThickness)
{
	FStairsSegmentData NewStairsData;
	NewStairsData.StartLoc = Start;
	NewStairsData.RoofTunnelLoc = End;
	NewStairsData.Structures = OurStructures;
	NewStairsData.StairsThickness = StairsThickness;
	NewStairsData.SectionIndex = -1;
	NewStairsData.StairModules = StairModules;
	
	return GenerateStairsMeshSection(NewStairsData);
}

void UStairsComponent::DestroyStairsSection(FStairsSegmentData InStairsData)
{
	int32 FoundIndex = StairsDataArray.Find(InStairsData);
	if(FoundIndex != INDEX_NONE)
	{
		ClearMeshSection(InStairsData.SectionIndex);
		/*
		if (InStairsData.ConnectedStairssAtEnd.Num() != 0)
		{
			RegenerateStairsSection(InStairsData.ConnectedStairssAtEnd[0], true);
		}
		if(InStairsData.ConnectedStairssAtStart.Num() != 0)
		{
			RegenerateStairsSection(InStairsData.ConnectedStairssAtStart[0], true);
		}*/
		StairsDataArray.Remove(InStairsData);
	}
}

void UStairsComponent::RemoveStairsSection(int StairsArrayIndex)
{
	if(StairsDataArray.Num() > StairsArrayIndex)
	{
		// Clear Mesh Section
		ClearMeshSection(StairsDataArray[StairsArrayIndex].SectionIndex);

		// Disable The Wall Data
		StairsDataArray[StairsArrayIndex].bCommitted = false;
		StairsFreeIndices.Add(StairsArrayIndex);
	}
}

bool UStairsComponent::SetStairsDataByIndex(int32 Index, FStairsSegmentData NewStairsData)
{
	if(StairsDataArray.IsValidIndex(Index))
	{
		StairsDataArray[Index] = NewStairsData;
		return true;
	}
	return false;
}

FStairsSegmentData& UStairsComponent::GetStairsDataByIndex(int32 StairsIndex)
{
	if (!StairsDataArray.IsValidIndex(StairsIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("StairsComponent::GetStairsDataByIndex - Invalid index %d (array size: %d)"), StairsIndex, StairsDataArray.Num());
		static FStairsSegmentData InvalidData;
		return InvalidData;
	}
	return StairsDataArray[StairsIndex];
}

void UStairsComponent::GenerateStairs()
{
	// Safety checks for mesh generation
	if (StairsData.Structures.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsComponent::GenerateStairs - No structures defined"));
		return;
	}

	if (!StairsData.StairTreadMesh || !StairsData.StairLandingMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsComponent::GenerateStairs - Missing StairTreadMesh or StairLandingMesh"));
		return;
	}

	int LandingIndex = -1;

	for (int i = 0; i < StairsData.Structures.Num(); ++i)
	{
		FString MeshName = "Stair_" + StaticEnum<EStairModuleType>()->GetNameStringByValue(static_cast<int64>(StairsData.Structures[i].StairType)) + FString::FromInt(i);
		UStaticMeshComponent* StairMesh = NewObject<UStaticMeshComponent>(this, *MeshName);

		// Get the appropriate mesh based on stair type
		UStaticMesh* MeshToUse = (StairsData.Structures[i].StairType == EStairModuleType::Tread)
			? StairsData.StairTreadMesh
			: StairsData.StairLandingMesh;

		StairMesh->SetStaticMesh(MeshToUse);
		StairMesh->RegisterComponent();
		StairMesh->SetRelativeRotation(StairsData.Direction.Rotation());
		StairMesh->SetRelativeLocation(StairsData.StartLoc);
		StairMesh->SetMaterial(0, PreviewMaterial);
		StairsData.StairModules.Add(StairMesh);

		// UE_LOG(LogTemp, Warning, TEXT("TheBurbsLog: Flooded with %s %s"), *StaticEnum<EStairModuleType>()->GetNameStringByValue(static_cast<int64>(StairsData.Structures[i].StairType)), *StaticEnum<ETurningSocket>()->GetNameStringByValue(static_cast<int64>(StairsData.Structures[i].TurningSocket)));
		if (StairsData.Structures[i].StairType == EStairModuleType::Landing && StairsData.Structures[i].TurningSocket == ETurningSocket::Idle)
		{
			LandingIndex = i;
		}
	}

	// Safety check: ensure we have modules before calculating offsets
	if (StairsData.StairModules.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsComponent::GenerateStairs - No stair modules generated, cannot calculate offsets"));
		return;
	}

	FStairsOffsets StairsOffsets;

	// Helper function to calculate offset between sockets
	auto CalculateOffsetBetweenSockets = [&](const FName& SocketA, const FName& SocketB, UStaticMeshComponent* MeshA, UStaticMeshComponent* MeshB = nullptr) -> FVector {
		if(MeshB == nullptr)
			MeshB = MeshA;
		const FTransform SocketATransform = MeshA->GetSocketTransform(SocketA);
		const FTransform SocketBTransform = MeshB->GetSocketTransform(SocketB);
		return SocketATransform.GetRelativeTransform(SocketBTransform).GetLocation();
	};

	// Calculate Offsets
	StairsOffsets.OffsetTread = CalculateOffsetBetweenSockets(TEXT("Back_Socket"), TEXT("Front_Socket"), StairsData.StairModules[0]);
	StairsOffsets.OffsetTreadLanding = FVector::ZeroVector;
	StairsOffsets.OffsetLandingTreadRight = FVector::ZeroVector;
	StairsOffsets.OffsetLandingTransform = FVector::ZeroVector;
	
	// const int LandingIndex = Structures.Find(FStairsStructure(EStairModuleType::Landing, ETurningSocket::Idle));
	
	if (LandingIndex != -1) // Structures
	{
		StairsOffsets.OffsetTreadLanding = CalculateOffsetBetweenSockets(TEXT("Back_Socket"), TEXT("Front_Socket"),StairsData.StairModules[LandingIndex], StairsData.StairModules[0]);
		StairsOffsets.OffsetLandingTreadRight = CalculateOffsetBetweenSockets(TEXT("Left_Socket"), TEXT("Front_Socket"),StairsData.StairModules[LandingIndex]);
		StairsOffsets.OffsetLandingTransform = CalculateOffsetBetweenSockets(TEXT("Left_Socket"), TEXT("Right_Socket"),StairsData.StairModules[LandingIndex]);
	}

	// BURB-40: Recalculate step height so total rise stays constant when landings are added.
	// With all treads (no landings), total rise = (N-1) * OffsetTread.Z.
	// When landings replace treads, they contribute zero vertical rise, so the remaining
	// tread risers must each be taller to preserve the same total height (one floor).
	// Module 0 is placed at StartLoc (no offset from a predecessor), so only modules
	// at index 1+ produce offset transitions — count treads there as actual risers.
	const float OriginalStepZ = StairsOffsets.OffsetTread.Z;
	const float OriginalTotalRise = static_cast<float>(StairsData.Structures.Num() - 1) * OriginalStepZ;

	int32 NumRiserPositions = 0;
	for (int32 idx = 1; idx < StairsData.Structures.Num(); ++idx)
	{
		if (StairsData.Structures[idx].StairType == EStairModuleType::Tread)
		{
			NumRiserPositions++;
		}
	}

	const float DesiredStepZ = (NumRiserPositions > 0)
		? OriginalTotalRise / static_cast<float>(NumRiserPositions)
		: OriginalStepZ;

	// Apply corrected step height to the default tread offset
	StairsOffsets.OffsetTread.Z = DesiredStepZ;

	// Transferring Modules — position each module relative to its predecessor
	UStaticMeshComponent* LastStairModule = nullptr;
	for (int i = 0; i < StairsData.Structures.Num(); ++i)
	{
		const EStairModuleType StairType = StairsData.Structures[i].StairType;
		const ETurningSocket TurningSocket = StairsData.Structures[i].TurningSocket;
		const float TurningSocketValue = StairsData.Structures[i].GetTurningSocketValue();
		
		FVector Offset = StairsOffsets.OffsetTread;

		if(StairType == EStairModuleType::Tread && TurningSocket != ETurningSocket::Idle)
		{
			// First tread after a landing turn — lateral shift + full riser height
			Offset = FVector(0, StairsOffsets.OffsetTreadLanding.Y + TurningSocketValue * StairsOffsets.OffsetLandingTreadRight.Y, DesiredStepZ);
			StairsData.StairModules[i]->SetRelativeRotation(FVector(0,TurningSocketValue,0).Rotation());
		}
		else if (StairType == EStairModuleType::Landing && TurningSocket != ETurningSocket::Idle)
		{
			Offset = TurningSocketValue * StairsOffsets.OffsetLandingTransform;
			Offset.Z = 0.0f; // Landings are flat — no vertical rise
			StairsData.StairModules[i]->SetRelativeRotation(FVector(0,TurningSocketValue,0).Rotation());
		}
		else if (StairType == EStairModuleType::Landing && TurningSocket == ETurningSocket::Idle)
		{
			Offset = StairsOffsets.OffsetTreadLanding;
			Offset.Z = 0.0f; // Landings are flat — no vertical rise
		}

		if (LastStairModule)
		{
			StairsData.StairModules[i]->SetRelativeLocation(Offset);
			StairsData.StairModules[i]->AttachToComponent(LastStairModule, FAttachmentTransformRules::KeepRelativeTransform);
		}
		else
		{
			// First module attaches to the StairsComponent itself so it follows actor transform
			StairsData.StairModules[i]->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
			StairsData.StairModules[i]->SetRelativeLocation(StairsData.StartLoc);
		}

		LastStairModule = StairsData.StairModules[i];
	}

	// Calculate RoofTunnelLoc (top of stairs) - with safety checks for editor preview
	if (StairsData.StairModules.Num() > 0 && LastStairModule)
	{
		// In editor preview, GetOwner() might return nullptr (no LotManager)
		// Use LotManager property if available, otherwise use default tile size
		const ALotManager* OurLot = LotManager ? LotManager : Cast<ALotManager>(GetOwner());

		if (OurLot)
		{
			// Calculate tunnel location using LotManager's grid tile size
			FVector LastModuleLocation = StairsData.StairModules.Last()->GetComponentLocation();
			FVector Direction = (LastModuleLocation - LastStairModule->GetComponentLocation()).GetSafeNormal();
			StairsData.RoofTunnelLoc = LastModuleLocation + ((OurLot->GridTileSize / 2.0f) * Direction);
		}
		else
		{
			// Fallback for editor preview: use default tile size (256 units)
			const float DefaultTileSize = 256.0f;
			FVector LastModuleLocation = StairsData.StairModules.Last()->GetComponentLocation();
			FVector Direction = (LastModuleLocation - LastStairModule->GetComponentLocation()).GetSafeNormal();
			StairsData.RoofTunnelLoc = LastModuleLocation + ((DefaultTileSize / 2.0f) * Direction);
		}
	}
	else
	{
		// No modules generated - set default
		StairsData.RoofTunnelLoc = StairsData.StartLoc;
	}
}

// Should writing comments for each part of this function
FStairsSegmentData UStairsComponent::GenerateStairsMeshSection(FStairsSegmentData InStairsData)
{
	int32 NewSectionIndex = GetNumSections();
	if(InStairsData.SectionIndex != -1)
	{
		NewSectionIndex = InStairsData.SectionIndex;
	}

	InStairsData.SectionIndex = NewSectionIndex;

    // Combined mesh arrays
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    int32 VertexOffset = 0;  // Keeps track of vertex offsets across different meshes

    // Loop through all stair treads and landings
    for (const UStaticMeshComponent* MeshComponent : InStairsData.StairModules)
    {
        if (!MeshComponent || !MeshComponent->GetStaticMesh()) continue;

        UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
        
        // Get LOD 0 mesh data (Level 0 of detail)
        FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0];
        
        const FPositionVertexBuffer& VertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
        const FStaticMeshVertexBuffer& StaticMeshVertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;
        const FRawStaticIndexBuffer& IndexBuffer = LODResources.IndexBuffer;

        // Extract vertex data and transform it to world/relative space
        for (uint32 VertexIndex = 0; VertexIndex < VertexBuffer.GetNumVertices(); ++VertexIndex)
        {
            // Convert local vertices to world space
            FVector WorldVertex = MeshComponent->GetComponentTransform().TransformPosition(FVector(VertexBuffer.VertexPosition(VertexIndex)));
            Vertices.Add(WorldVertex);
        	
        	// Extract normals (stored in StaticMeshVertexBuffer)
        	FVector Normal = MeshComponent->GetComponentTransform().TransformVector(FVector(StaticMeshVertexBuffer.VertexTangentZ(VertexIndex)));
        	Normals.Add(Normal);

        	// Extract UVs (assuming UV channel 0)
        	FVector2f UV = StaticMeshVertexBuffer.GetVertexUV(VertexIndex, 0); // Use UV channel 0
        	UVs.Add(FVector2D(UV));

        }

        // Extract triangle data and adjust indices with VertexOffset
        for (int32 Index = 0; Index < IndexBuffer.GetNumIndices(); Index += 3)
        {
            Triangles.Add(IndexBuffer.GetIndex(Index) + VertexOffset);
            Triangles.Add(IndexBuffer.GetIndex(Index + 1) + VertexOffset);
            Triangles.Add(IndexBuffer.GetIndex(Index + 2) + VertexOffset);
        }
    	
        // Update vertex offset for the next mesh
        VertexOffset += VertexBuffer.GetNumVertices();
    }

    // Create the final procedural mesh section
	// if we need more or less verts then we can signal to regenerate the mesh
	if(!GetProcMeshSection(NewSectionIndex))
	{
		CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, bCreateCollision);
	}
	else
	{
		// Check if vertex count changed or section is empty
		FProcMeshSection* Section = GetProcMeshSection(NewSectionIndex);
		if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != Vertices.Num())
		{
			CreateMeshSection(NewSectionIndex, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, bCreateCollision);
		}
		else
		{
			UpdateMeshSection(NewSectionIndex, Vertices, Normals, UVs, TArray<FColor>(), Tangents);
		}
	}
	
	InStairsData.Vertices = Vertices;
	
	if(!StairsDataArray.Contains(InStairsData))
	{
		if (StairsFreeIndices.Num() > 0)
		{
			InStairsData.StairsArrayIndex = StairsFreeIndices.Pop();
		}else
		{
			InStairsData.StairsArrayIndex = StairsDataArray.Add(InStairsData);
		}
	}
	
	StairsDataArray[InStairsData.StairsArrayIndex] = InStairsData;
	return StairsDataArray[InStairsData.StairsArrayIndex];
}

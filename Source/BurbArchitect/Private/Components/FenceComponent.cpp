// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FenceComponent.h"
#include "Actors/LotManager.h"
#include "Actors/GateBase.h"
#include "Data/FenceItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

UFenceComponent::UFenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CachedLotManager = nullptr;
}

ALotManager* UFenceComponent::GetLotManager()
{
	if (!CachedLotManager)
	{
		CachedLotManager = Cast<ALotManager>(GetOwner());
	}
	return CachedLotManager;
}

int32 UFenceComponent::GenerateFenceSegment(int32 Level, FVector StartLoc, FVector EndLoc, UFenceItem* FenceItem)
{
	if (!FenceItem)
	{
		UE_LOG(LogTemp, Warning, TEXT("FenceComponent::GenerateFenceSegment - FenceItem is null"));
		return -1;
	}

	// Create new fence segment data
	FFenceSegmentData NewFence;
	NewFence.StartLoc = StartLoc;
	NewFence.EndLoc = EndLoc;
	NewFence.Level = Level;
	NewFence.FenceItem = FenceItem;
	NewFence.bCommitted = true;

	// Add to array
	int32 FenceIndex = FenceDataArray.Add(NewFence);

	// Generate panels and posts
	RegenerateFencePanels(FenceIndex);

	return FenceIndex;
}

void UFenceComponent::RemoveFenceSegment(int32 FenceIndex)
{
	if (!FenceDataArray.IsValidIndex(FenceIndex))
	{
		return;
	}

	FFenceSegmentData& FenceData = FenceDataArray[FenceIndex];

	// Destroy all panel meshes
	for (UStaticMeshComponent* Panel : FenceData.PanelMeshes)
	{
		if (Panel && IsValid(Panel))
		{
			Panel->DestroyComponent();
		}
	}

	// Destroy all post meshes
	for (UStaticMeshComponent* Post : FenceData.PostMeshes)
	{
		if (Post && IsValid(Post))
		{
			Post->DestroyComponent();
		}
	}

	// Remove from array
	FenceDataArray.RemoveAt(FenceIndex);
}

int32 UFenceComponent::FindFenceSegmentAtLocation(FVector Location, float Tolerance)
{
	float ToleranceSq = Tolerance * Tolerance;

	for (int32 i = 0; i < FenceDataArray.Num(); i++)
	{
		const FFenceSegmentData& FenceData = FenceDataArray[i];

		// Calculate closest point on fence line segment to Location
		FVector FenceDir = (FenceData.EndLoc - FenceData.StartLoc);
		float FenceLengthSq = FenceDir.SizeSquared();

		if (FenceLengthSq < SMALL_NUMBER)
		{
			continue; // Degenerate fence (start == end)
		}

		// Project Location onto fence line
		float T = FVector::DotProduct(Location - FenceData.StartLoc, FenceDir) / FenceLengthSq;
		T = FMath::Clamp(T, 0.0f, 1.0f);

		FVector ClosestPoint = FenceData.StartLoc + (FenceDir * T);
		float DistSq = FVector::DistSquared(Location, ClosestPoint);

		if (DistSq <= ToleranceSq)
		{
			return i;
		}
	}

	return -1;
}

void UFenceComponent::AddGateToFence(int32 FenceIndex, FVector GateLocation, float GateWidth, AGateBase* Gate)
{
	if (!FenceDataArray.IsValidIndex(FenceIndex) || !Gate)
	{
		return;
	}

	FFenceSegmentData& FenceData = FenceDataArray[FenceIndex];

	// Add gate to fence's gate array
	FenceData.Gates.Add(Gate);

	// Destroy all existing panels and posts
	for (UStaticMeshComponent* Panel : FenceData.PanelMeshes)
	{
		if (Panel && IsValid(Panel))
		{
			Panel->DestroyComponent();
		}
	}
	for (UStaticMeshComponent* Post : FenceData.PostMeshes)
	{
		if (Post && IsValid(Post))
		{
			Post->DestroyComponent();
		}
	}
	FenceData.PanelMeshes.Empty();
	FenceData.PostMeshes.Empty();

	// Regenerate fence with gate cutout
	RegenerateFencePanels(FenceIndex);
}

void UFenceComponent::RemoveGateFromFence(int32 FenceIndex, AGateBase* Gate)
{
	if (!FenceDataArray.IsValidIndex(FenceIndex) || !Gate)
	{
		return;
	}

	FFenceSegmentData& FenceData = FenceDataArray[FenceIndex];

	// Remove gate from array
	FenceData.Gates.Remove(Gate);

	// Destroy all existing panels and posts
	for (UStaticMeshComponent* Panel : FenceData.PanelMeshes)
	{
		if (Panel && IsValid(Panel))
		{
			Panel->DestroyComponent();
		}
	}
	for (UStaticMeshComponent* Post : FenceData.PostMeshes)
	{
		if (Post && IsValid(Post))
		{
			Post->DestroyComponent();
		}
	}
	FenceData.PanelMeshes.Empty();
	FenceData.PostMeshes.Empty();

	// Regenerate fence without gate
	RegenerateFencePanels(FenceIndex);
}

UStaticMeshComponent* UFenceComponent::SpawnFencePanel(const FVector& Start, const FVector& End,
													  UFenceItem* FenceItem, int32 Level)
{
	if (!FenceItem || !FenceItem->FencePanelMesh.IsValid())
	{
		return nullptr;
	}

	// Create panel mesh component
	UStaticMeshComponent* PanelMesh = NewObject<UStaticMeshComponent>(GetOwner());
	if (!PanelMesh)
	{
		return nullptr;
	}

	// Load and set static mesh
	UStaticMesh* Mesh = FenceItem->FencePanelMesh.LoadSynchronous();
	if (!Mesh)
	{
		PanelMesh->DestroyComponent();
		return nullptr;
	}
	PanelMesh->SetStaticMesh(Mesh);

	// Calculate panel transform
	FVector PanelCenter = (Start + End) * 0.5f;
	FVector PanelDir = (End - Start).GetSafeNormal();
	FRotator PanelRotation = PanelDir.Rotation();

	PanelMesh->SetWorldLocation(PanelCenter);
	PanelMesh->SetWorldRotation(PanelRotation);

	// Enable collision for fences
	PanelMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PanelMesh->SetCollisionObjectType(ECC_WorldStatic);
	PanelMesh->SetCollisionResponseToAllChannels(ECR_Block);

	// Register component
	PanelMesh->RegisterComponent();
	PanelMesh->AttachToComponent(this, FAttachmentTransformRules::KeepWorldTransform);

	return PanelMesh;
}

UStaticMeshComponent* UFenceComponent::SpawnFencePost(const FVector& Location,
													 UFenceItem* FenceItem, int32 Level)
{
	if (!FenceItem || !FenceItem->FencePostMesh.IsValid())
	{
		return nullptr;
	}

	// Create post mesh component
	UStaticMeshComponent* PostMesh = NewObject<UStaticMeshComponent>(GetOwner());
	if (!PostMesh)
	{
		return nullptr;
	}

	// Load and set static mesh
	UStaticMesh* Mesh = FenceItem->FencePostMesh.LoadSynchronous();
	if (!Mesh)
	{
		PostMesh->DestroyComponent();
		return nullptr;
	}
	PostMesh->SetStaticMesh(Mesh);

	// Set post transform
	PostMesh->SetWorldLocation(Location);

	// Enable collision for posts
	PostMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PostMesh->SetCollisionObjectType(ECC_WorldStatic);
	PostMesh->SetCollisionResponseToAllChannels(ECR_Block);

	// Register component
	PostMesh->RegisterComponent();
	PostMesh->AttachToComponent(this, FAttachmentTransformRules::KeepWorldTransform);

	return PostMesh;
}

void UFenceComponent::RegenerateFencePanels(int32 FenceIndex)
{
	if (!FenceDataArray.IsValidIndex(FenceIndex))
	{
		return;
	}

	FFenceSegmentData& FenceData = FenceDataArray[FenceIndex];
	UFenceItem* FenceItem = FenceData.FenceItem;

	if (!FenceItem)
	{
		return;
	}

	// Calculate post positions (accounts for gates)
	TArray<FVector> PostPositions = CalculatePostPositions(FenceData);

	// Spawn posts
	for (const FVector& PostPos : PostPositions)
	{
		UStaticMeshComponent* Post = SpawnFencePost(PostPos, FenceItem, FenceData.Level);
		if (Post)
		{
			FenceData.PostMeshes.Add(Post);
		}
	}

	// Spawn panels between posts (skip gate openings)
	for (int32 i = 0; i < PostPositions.Num() - 1; i++)
	{
		FVector PanelStart = PostPositions[i];
		FVector PanelEnd = PostPositions[i + 1];

		// Check if this panel would overlap a gate
		bool bOverlapsGate = false;
		FVector FenceDir = (FenceData.EndLoc - FenceData.StartLoc).GetSafeNormal();

		for (AGateBase* Gate : FenceData.Gates)
		{
			if (Gate && IsValid(Gate))
			{
				float GateWidth = Gate->PortalSize.X;
				FVector GateLoc = Gate->GetActorLocation();

				// Calculate distances along fence
				float PanelStartDist = FVector::DotProduct(PanelStart - FenceData.StartLoc, FenceDir);
				float PanelEndDist = FVector::DotProduct(PanelEnd - FenceData.StartLoc, FenceDir);
				float GateDist = FVector::DotProduct(GateLoc - FenceData.StartLoc, FenceDir);
				float GateHalfWidth = GateWidth * 0.5f;

				// Check overlap
				if ((PanelStartDist <= GateDist + GateHalfWidth) && (PanelEndDist >= GateDist - GateHalfWidth))
				{
					bOverlapsGate = true;
					break;
				}
			}
		}

		// Only spawn panel if it doesn't overlap a gate
		if (!bOverlapsGate)
		{
			UStaticMeshComponent* Panel = SpawnFencePanel(PanelStart, PanelEnd, FenceItem, FenceData.Level);
			if (Panel)
			{
				FenceData.PanelMeshes.Add(Panel);
			}
		}
	}
}

TArray<FVector> UFenceComponent::CalculatePostPositions(const FFenceSegmentData& FenceData)
{
	TArray<FVector> PostPositions;

	UFenceItem* FenceItem = FenceData.FenceItem;
	if (!FenceItem)
	{
		return PostPositions;
	}

	FVector FenceDir = (FenceData.EndLoc - FenceData.StartLoc).GetSafeNormal();
	float FenceLength = FVector::Dist(FenceData.StartLoc, FenceData.EndLoc);

	// Start with end posts (if enabled)
	TSet<float> PostDistances; // Distances along fence

	if (FenceItem->bPostsAtEnds)
	{
		PostDistances.Add(0.0f);         // Start
		PostDistances.Add(FenceLength);  // End
	}

	// Add intermediate posts based on PostSpacing
	if (FenceItem->PostSpacing > 0)
	{
		// Get grid tile size from LotManager
		float GridTileSize = 100.0f; // Default
		if (ALotManager* LotManager = GetLotManager())
		{
			GridTileSize = LotManager->GridTileSize;
		}

		float PostInterval = GridTileSize * FenceItem->PostSpacing;

		for (float Dist = PostInterval; Dist < FenceLength; Dist += PostInterval)
		{
			PostDistances.Add(Dist);
		}
	}

	// Add forced posts at gate edges
	for (AGateBase* Gate : FenceData.Gates)
	{
		if (Gate && IsValid(Gate))
		{
			FVector GateLoc = Gate->GetActorLocation();
			float GateWidth = Gate->PortalSize.X;
			float GateDist = FVector::DotProduct(GateLoc - FenceData.StartLoc, FenceDir);

			// Posts at gate edges
			PostDistances.Add(GateDist - (GateWidth * 0.5f));
			PostDistances.Add(GateDist + (GateWidth * 0.5f));
		}
	}

	// Convert distances to world positions
	TArray<float> SortedDistances = PostDistances.Array();
	SortedDistances.Sort();

	for (float Dist : SortedDistances)
	{
		// Clamp to fence bounds
		Dist = FMath::Clamp(Dist, 0.0f, FenceLength);
		FVector PostPos = FenceData.StartLoc + (FenceDir * Dist);
		PostPositions.Add(PostPos);
	}

	return PostPositions;
}

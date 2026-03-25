// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/BuildGateTool.h"
#include "Actors/LotManager.h"
#include "Actors/GateBase.h"
#include "Actors/DoorBase.h"
#include "Components/FenceComponent.h"
#include "Components/PortalBoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/KismetMathLibrary.h"

ABuildGateTool::ABuildGateTool()
{
	PrimaryActorTick.bCanEverTick = true;
	HoveredFenceIndex = -1;

	// Gates snap to ground
	bSnapsToFloor = true;
}

void ABuildGateTool::BeginPlay()
{
	Super::BeginPlay();
}

void ABuildGateTool::Move_Implementation(FVector MoveLocation, bool SelectPressed,
										 FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	// Skip parent's wall detection - call grandparent (ABuildTool)
	ABuildTool::Move_Implementation(MoveLocation, SelectPressed, CursorWorldHitResult, TracedLevel);

	HoveredFenceIndex = -1;
	bValidPlacementLocation = false;

	// Detect fence segment hit
	if (CurrentLot && CurrentLot->FenceComponent && CursorWorldHitResult.bBlockingHit)
	{
		if (DetectFenceSegment(CursorWorldHitResult, HoveredFenceIndex))
		{
			// Validate gate placement
			if (ValidateGatePlacement(HoveredFenceIndex, MoveLocation))
			{
				// Snap position and rotation to fence
				TargetLocation = SnapToFence(HoveredFenceIndex, MoveLocation);
				TargetRotation = CalculateFenceRotation(HoveredFenceIndex);
				bValidPlacementLocation = true;

				SetActorLocation(TargetLocation);
				SetActorRotation(TargetRotation);
			}
		}
	}

	// Update preview gate (inherited from BuildDoorTool)
	UpdatePreviewDoor();
}

void ABuildGateTool::BroadcastRelease_Implementation()
{
	if (!bValidPlacementLocation || HoveredFenceIndex == -1 || !ClassToPlace)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildGateTool::BroadcastRelease - Invalid placement (Valid: %d, FenceIdx: %d, Class: %d)"),
			   bValidPlacementLocation, HoveredFenceIndex, ClassToPlace != nullptr);
		return;
	}

	// Spawn gate actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = CurrentLot;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGateBase* Gate = GetWorld()->SpawnActor<AGateBase>(ClassToPlace, TargetLocation, TargetRotation, SpawnParams);

	if (Gate)
	{
		// Set gate properties
		Gate->FenceSegmentIndex = HoveredFenceIndex;
		Gate->OwningLot = CurrentLot;
		Gate->PortalSize = PortalSize;
		Gate->PortalOffset = PortalOffset;
		Gate->CurrentLot = CurrentLot;

		// Apply meshes from GateItem
		if (ADoorBase* DoorGate = Cast<ADoorBase>(Gate))
		{
			if (DoorGate->DoorStaticMesh && DoorStaticMesh.IsValid())
			{
				DoorGate->DoorStaticMesh->SetStaticMesh(DoorStaticMesh.LoadSynchronous());
			}
			if (DoorGate->DoorFrameMesh && DoorFrameMesh.IsValid())
			{
				DoorGate->DoorFrameMesh->SetStaticMesh(DoorFrameMesh.LoadSynchronous());
			}
		}

		// Force posts on each side, adjust panels
		Gate->OnGatePlaced();

		UE_LOG(LogTemp, Log, TEXT("BuildGateTool::BroadcastRelease - Gate spawned successfully on fence %d"), HoveredFenceIndex);

		OnReleased();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BuildGateTool::BroadcastRelease - Failed to spawn gate actor"));
	}
}

bool ABuildGateTool::DetectFenceSegment(const FHitResult& HitResult, int32& OutFenceIndex)
{
	if (!CurrentLot || !CurrentLot->FenceComponent)
	{
		return false;
	}

	// Get hit component
	UPrimitiveComponent* HitComponent = HitResult.GetComponent();
	if (!HitComponent)
	{
		return false;
	}

	// Check if hit component is a fence panel or post
	UStaticMeshComponent* HitMesh = Cast<UStaticMeshComponent>(HitComponent);
	if (!HitMesh)
	{
		return false;
	}

	// Find fence segment containing this mesh component
	// Use hit location to find nearest fence segment
	FVector HitLocation = HitResult.ImpactPoint;
	OutFenceIndex = CurrentLot->FenceComponent->FindFenceSegmentAtLocation(HitLocation, 100.0f);

	return (OutFenceIndex != -1);
}

bool ABuildGateTool::ValidateGatePlacement(int32 FenceIndex, const FVector& Location)
{
	if (!CurrentLot || !CurrentLot->FenceComponent)
	{
		return false;
	}

	if (!CurrentLot->FenceComponent->FenceDataArray.IsValidIndex(FenceIndex))
	{
		return false;
	}

	const FFenceSegmentData& FenceData = CurrentLot->FenceComponent->FenceDataArray[FenceIndex];

	// Calculate gate width
	float GateWidth = PortalSize.X;

	// Calculate distance along fence
	FVector FenceDir = (FenceData.EndLoc - FenceData.StartLoc).GetSafeNormal();
	float FenceLength = FVector::Dist(FenceData.StartLoc, FenceData.EndLoc);
	float DistAlongFence = FVector::DotProduct(Location - FenceData.StartLoc, FenceDir);

	// Check if gate extends beyond fence bounds
	float GateHalfWidth = GateWidth * 0.5f;
	if ((DistAlongFence - GateHalfWidth) < 0.0f || (DistAlongFence + GateHalfWidth) > FenceLength)
	{
		return false; // Gate extends beyond fence
	}

	// Check for overlap with existing gates on this fence
	for (AGateBase* ExistingGate : FenceData.Gates)
	{
		if (ExistingGate && IsValid(ExistingGate))
		{
			FVector ExistingGateLoc = ExistingGate->GetActorLocation();
			float ExistingGateWidth = ExistingGate->PortalSize.X;
			float ExistingGateDist = FVector::DotProduct(ExistingGateLoc - FenceData.StartLoc, FenceDir);

			float ExistingGateHalfWidth = ExistingGateWidth * 0.5f;

			// Check overlap
			if ((DistAlongFence - GateHalfWidth) < (ExistingGateDist + ExistingGateHalfWidth) &&
				(DistAlongFence + GateHalfWidth) > (ExistingGateDist - ExistingGateHalfWidth))
			{
				return false; // Overlaps existing gate
			}
		}
	}

	return true;
}

FVector ABuildGateTool::SnapToFence(int32 FenceIndex, const FVector& CursorLocation)
{
	if (!CurrentLot || !CurrentLot->FenceComponent)
	{
		return CursorLocation;
	}

	if (!CurrentLot->FenceComponent->FenceDataArray.IsValidIndex(FenceIndex))
	{
		return CursorLocation;
	}

	const FFenceSegmentData& FenceData = CurrentLot->FenceComponent->FenceDataArray[FenceIndex];

	// Project cursor location onto fence line
	FVector FenceDir = (FenceData.EndLoc - FenceData.StartLoc).GetSafeNormal();
	float DistAlongFence = FVector::DotProduct(CursorLocation - FenceData.StartLoc, FenceDir);

	// Snap along fence using HorizontalSnap
	if (HorizontalSnap > 0.0f)
	{
		DistAlongFence = FMath::RoundToFloat(DistAlongFence / HorizontalSnap) * HorizontalSnap;
	}

	// Clamp to fence bounds (accounting for gate width)
	float GateHalfWidth = PortalSize.X * 0.5f;
	float FenceLength = FVector::Dist(FenceData.StartLoc, FenceData.EndLoc);
	DistAlongFence = FMath::Clamp(DistAlongFence, GateHalfWidth, FenceLength - GateHalfWidth);

	// Calculate snapped position
	FVector SnappedPos = FenceData.StartLoc + (FenceDir * DistAlongFence);

	// Handle vertical snapping (if not snapping to floor)
	if (bSnapsToFloor)
	{
		// Snap to fence base Z
		SnappedPos.Z = FenceData.StartLoc.Z;
	}
	else
	{
		// Snap vertically using VerticalSnap
		if (VerticalSnap > 0.0f)
		{
			float RelativeZ = SnappedPos.Z - FenceData.StartLoc.Z;
			RelativeZ = FMath::RoundToFloat(RelativeZ / VerticalSnap) * VerticalSnap;
			SnappedPos.Z = FenceData.StartLoc.Z + RelativeZ;
		}
	}

	return SnappedPos;
}

FRotator ABuildGateTool::CalculateFenceRotation(int32 FenceIndex)
{
	if (!CurrentLot || !CurrentLot->FenceComponent)
	{
		return FRotator::ZeroRotator;
	}

	if (!CurrentLot->FenceComponent->FenceDataArray.IsValidIndex(FenceIndex))
	{
		return FRotator::ZeroRotator;
	}

	const FFenceSegmentData& FenceData = CurrentLot->FenceComponent->FenceDataArray[FenceIndex];

	// Calculate fence direction
	FVector FenceDir = (FenceData.EndLoc - FenceData.StartLoc).GetSafeNormal();

	// Gate rotates to align with fence direction
	FRotator GateRotation = FenceDir.Rotation();

	return GateRotation;
}

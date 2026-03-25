// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/GateBase.h"
#include "Actors/LotManager.h"
#include "Components/FenceComponent.h"

AGateBase::AGateBase()
{
	FenceSegmentIndex = -1;
	OwningLot = nullptr;
}

void AGateBase::BeginPlay()
{
	Super::BeginPlay();
}

void AGateBase::OnGatePlaced()
{
	if (OwningLot && OwningLot->FenceComponent && FenceSegmentIndex != -1)
	{
		// Get gate width from portal size
		float GateWidth = PortalSize.X;

		// Tell fence component to add gate - forces posts, adjusts panels
		OwningLot->FenceComponent->AddGateToFence(FenceSegmentIndex, GetActorLocation(), GateWidth, this);

		UE_LOG(LogTemp, Log, TEXT("GateBase::OnGatePlaced - Gate placed on fence segment %d with width %.2f"), FenceSegmentIndex, GateWidth);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GateBase::OnGatePlaced - Invalid fence segment or LotManager reference"));
	}
}

void AGateBase::OnDeleted_Implementation()
{
	// Remove gate from fence - regenerates panels/posts
	if (OwningLot && OwningLot->FenceComponent && FenceSegmentIndex != -1)
	{
		OwningLot->FenceComponent->RemoveGateFromFence(FenceSegmentIndex, this);

		UE_LOG(LogTemp, Log, TEXT("GateBase::OnDeleted - Gate removed from fence segment %d"), FenceSegmentIndex);
	}

	// Call parent deletion logic
	Super::OnDeleted_Implementation();
}

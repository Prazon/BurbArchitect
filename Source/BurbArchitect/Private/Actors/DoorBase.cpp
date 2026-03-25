// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/DoorBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

ADoorBase::ADoorBase()
	: bIsOpen(false)
{
	PrimaryActorTick.bCanEverTick = true;

	// Create skeletal mesh component for animation skeleton (set in Blueprint)
	DoorSkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("DoorSkeletalMesh"));
	DoorSkeletalMesh->SetupAttachment(RootComponent);

	// Configure skeletal mesh - no collision, invisible (bones only)
	DoorSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DoorSkeletalMesh->SetVisibility(false); // Hide skeleton, only static meshes are visible
	DoorSkeletalMesh->SetCastShadow(false);

	// Enable animation tick
	DoorSkeletalMesh->PrimaryComponentTick.bCanEverTick = true;
	DoorSkeletalMesh->SetComponentTickEnabled(true);

	// Create static mesh component for door panel (attaches to skeletal mesh bone in Blueprint)
	DoorStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorStaticMesh"));
	DoorStaticMesh->SetupAttachment(DoorSkeletalMesh);

	// Configure door static mesh
	DoorStaticMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DoorStaticMesh->SetCollisionResponseToAllChannels(ECR_Block);
	DoorStaticMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	DoorStaticMesh->SetCastShadow(true);
	DoorStaticMesh->bCastDynamicShadow = true;

	// Create static mesh component for door frame (attaches to skeletal mesh bone in Blueprint)
	DoorFrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorFrameMesh"));
	DoorFrameMesh->SetupAttachment(DoorSkeletalMesh);

	// Configure door frame mesh
	DoorFrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Frame typically doesn't need collision
	DoorFrameMesh->SetCastShadow(true);
	DoorFrameMesh->bCastDynamicShadow = true;
}

void ADoorBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADoorBase, DoorMeshAsset);
	DOREPLIFETIME(ADoorBase, DoorFrameMeshAsset);
	DOREPLIFETIME(ADoorBase, bIsOpen);
}

void ADoorBase::PostNetInit()
{
	Super::PostNetInit();

	// PostNetInit is called after all initial replicated properties are received
	// ApplyReplicatedProperties() is called by Super, but we log here for debugging
	UE_LOG(LogTemp, Log, TEXT("DoorBase::PostNetInit - Door mesh: %s, Frame mesh: %s"),
		DoorMeshAsset.IsNull() ? TEXT("NULL") : *DoorMeshAsset.ToString(),
		DoorFrameMeshAsset.IsNull() ? TEXT("NULL") : *DoorFrameMeshAsset.ToString());
}

void ADoorBase::BeginPlay()
{
	Super::BeginPlay();
}

void ADoorBase::ApplyReplicatedProperties()
{
	Super::ApplyReplicatedProperties();

	// Apply door meshes if they were replicated
	if (!DoorMeshAsset.IsNull())
	{
		ApplyDoorMesh();
	}
	if (!DoorFrameMeshAsset.IsNull())
	{
		ApplyDoorFrameMesh();
	}
}

// ==================== MESH REPLICATION ====================

void ADoorBase::OnRep_DoorMeshAsset()
{
	ApplyDoorMesh();
}

void ADoorBase::OnRep_DoorFrameMeshAsset()
{
	ApplyDoorFrameMesh();
}

void ADoorBase::ApplyDoorMesh()
{
	if (!DoorStaticMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("DoorBase::ApplyDoorMesh - DoorStaticMesh component is NULL"));
		return;
	}

	if (DoorMeshAsset.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("DoorBase::ApplyDoorMesh - DoorMeshAsset is null"));
		return;
	}

	UStaticMesh* LoadedMesh = DoorMeshAsset.LoadSynchronous();
	if (LoadedMesh)
	{
		DoorStaticMesh->SetStaticMesh(LoadedMesh);
		UE_LOG(LogTemp, Log, TEXT("DoorBase::ApplyDoorMesh - Applied mesh: %s"), *LoadedMesh->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DoorBase::ApplyDoorMesh - Failed to load mesh from soft pointer"));
	}
}

void ADoorBase::ApplyDoorFrameMesh()
{
	if (!DoorFrameMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("DoorBase::ApplyDoorFrameMesh - DoorFrameMesh component is NULL"));
		return;
	}

	if (DoorFrameMeshAsset.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("DoorBase::ApplyDoorFrameMesh - DoorFrameMeshAsset is null"));
		return;
	}

	UStaticMesh* LoadedMesh = DoorFrameMeshAsset.LoadSynchronous();
	if (LoadedMesh)
	{
		DoorFrameMesh->SetStaticMesh(LoadedMesh);
		UE_LOG(LogTemp, Log, TEXT("DoorBase::ApplyDoorFrameMesh - Applied mesh: %s"), *LoadedMesh->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DoorBase::ApplyDoorFrameMesh - Failed to load mesh from soft pointer"));
	}
}

// ==================== DOOR STATE ====================

void ADoorBase::OnRep_IsOpen()
{
	// Called on clients when bIsOpen replicates
	// Play appropriate animation based on new state
	UE_LOG(LogTemp, Log, TEXT("DoorBase::OnRep_IsOpen - Door is now %s"), bIsOpen ? TEXT("OPEN") : TEXT("CLOSED"));

	// TODO: Play animation based on bIsOpen state
	// if (bIsOpen)
	//     DoorSkeletalMesh->PlayAnimation(OpenAnimationAsset, false);
	// else
	//     DoorSkeletalMesh->PlayAnimation(CloseAnimationAsset, false);
}

void ADoorBase::SetDoorOpen(bool bOpen)
{
	if (bIsOpen == bOpen)
		return;

	bIsOpen = bOpen;

	// TODO: Play opening/closing animation on DoorSkeletalMesh
	UE_LOG(LogTemp, Log, TEXT("DoorBase: Door %s"), bOpen ? TEXT("opening") : TEXT("closing"));
}

void ADoorBase::OpenDoor()
{
	if (bIsOpen)
		return;

	// If we have authority, set the state directly (it will replicate)
	// If not, call the server RPC
	if (HasAuthority())
	{
		SetDoorOpen(true);
	}
	else
	{
		Server_OpenDoor();
	}
}

void ADoorBase::CloseDoor()
{
	if (!bIsOpen)
		return;

	// If we have authority, set the state directly (it will replicate)
	// If not, call the server RPC
	if (HasAuthority())
	{
		SetDoorOpen(false);
	}
	else
	{
		Server_CloseDoor();
	}
}

void ADoorBase::ToggleDoor()
{
	if (bIsOpen)
		CloseDoor();
	else
		OpenDoor();
}

void ADoorBase::Server_OpenDoor_Implementation()
{
	SetDoorOpen(true);
}

void ADoorBase::Server_CloseDoor_Implementation()
{
	SetDoorOpen(false);
}

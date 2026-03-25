// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/WindowBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

AWindowBase::AWindowBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create static mesh component for window frame/glass
	WindowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WindowMesh"));
	WindowMesh->SetupAttachment(RootComponent);

	// Configure collision - windows don't need complex collision
	WindowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WindowMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	// Configure rendering
	WindowMesh->SetCastShadow(true);
	WindowMesh->bCastDynamicShadow = true;
}

void AWindowBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWindowBase, WindowMeshAsset);
}

void AWindowBase::PostNetInit()
{
	Super::PostNetInit();

	// PostNetInit is called after all initial replicated properties are received
	// ApplyReplicatedProperties() is called by Super, but we log here for debugging
	UE_LOG(LogTemp, Log, TEXT("WindowBase::PostNetInit - Window mesh asset: %s"),
		WindowMeshAsset.IsNull() ? TEXT("NULL") : *WindowMeshAsset.ToString());
}

void AWindowBase::BeginPlay()
{
	Super::BeginPlay();
}

void AWindowBase::ApplyReplicatedProperties()
{
	Super::ApplyReplicatedProperties();

	// Apply window mesh if it was replicated
	if (!WindowMeshAsset.IsNull())
	{
		ApplyWindowMesh();
	}
}

void AWindowBase::OnRep_WindowMeshAsset()
{
	ApplyWindowMesh();
}

void AWindowBase::ApplyWindowMesh()
{
	if (!WindowMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("WindowBase::ApplyWindowMesh - WindowMesh component is NULL"));
		return;
	}

	if (WindowMeshAsset.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("WindowBase::ApplyWindowMesh - WindowMeshAsset is null"));
		return;
	}

	UStaticMesh* LoadedMesh = WindowMeshAsset.LoadSynchronous();
	if (LoadedMesh)
	{
		WindowMesh->SetStaticMesh(LoadedMesh);
		UE_LOG(LogTemp, Log, TEXT("WindowBase::ApplyWindowMesh - Applied mesh: %s"), *LoadedMesh->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WindowBase::ApplyWindowMesh - Failed to load mesh from soft pointer"));
	}
}

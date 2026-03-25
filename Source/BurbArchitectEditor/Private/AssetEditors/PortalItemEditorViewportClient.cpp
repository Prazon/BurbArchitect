// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetEditors/PortalItemEditorViewportClient.h"
#include "AdvancedPreviewScene.h"
#include "Actors/PortalBase.h"
#include "Actors/WindowBase.h"
#include "Actors/DoorBase.h"
#include "Data/WindowItem.h"
#include "Data/DoorItem.h"
#include "Components/PortalBoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "UnrealClient.h"

FPortalItemEditorViewportClient::FPortalItemEditorViewportClient(FAdvancedPreviewScene& InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(nullptr, &InPreviewScene, InEditorViewportWidget)
	, AdvancedPreviewScene(&InPreviewScene)
	, PreviewPortalActor(nullptr)
	, PortalItemAsset(nullptr)
{
	// Setup viewport defaults
	SetViewLocation(FVector(-300.0f, 0.0f, 100.0f));
	SetViewRotation(FRotator(0.0f, 0.0f, 0.0f));

	// Configure viewport behavior
	SetRealtime(true);
	SetShowGrid();
}

FPortalItemEditorViewportClient::~FPortalItemEditorViewportClient()
{
	// Clean up preview actor
	if (PreviewPortalActor && PreviewPortalActor->GetWorld())
	{
		PreviewPortalActor->GetWorld()->DestroyActor(PreviewPortalActor);
		PreviewPortalActor = nullptr;
	}
}

void FPortalItemEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// Update preview scene
	if (AdvancedPreviewScene)
	{
		AdvancedPreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FPortalItemEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	// Draw reference grid and axis helpers if needed
	// The preview portal's box component will automatically render
}

void FPortalItemEditorViewportClient::SetPortalItem(UObject* InPortalItem)
{
	PortalItemAsset = InPortalItem;

	if (!AdvancedPreviewScene || !InPortalItem)
	{
		return;
	}

	UWorld* PreviewWorld = AdvancedPreviewScene->GetWorld();
	if (!PreviewWorld)
	{
		return;
	}

	// Destroy old preview actor if it exists
	if (PreviewPortalActor)
	{
		PreviewWorld->DestroyActor(PreviewPortalActor);
		PreviewPortalActor = nullptr;
	}

	// Determine the portal class to spawn and mesh to apply
	TSoftClassPtr<APortalBase> ClassToSpawn;
	FVector2D PortalSize = FVector2D(100.0f, 200.0f); // Default size
	FVector2D PortalOffset = FVector2D::ZeroVector; // Default offset
	TSoftObjectPtr<UStaticMesh> WindowMeshAsset;
	TSoftObjectPtr<UStaticMesh> DoorStaticMeshAsset;
	TSoftObjectPtr<UStaticMesh> DoorFrameMeshAsset;
	bool bIsWindow = false;

	if (UWindowItem* WindowItem = Cast<UWindowItem>(InPortalItem))
	{
		ClassToSpawn = WindowItem->ClassToSpawn;
		PortalSize = WindowItem->PortalSize;
		PortalOffset = WindowItem->PortalOffset;
		WindowMeshAsset = WindowItem->WindowMesh;
		bIsWindow = true;
	}
	else if (UDoorItem* DoorItem = Cast<UDoorItem>(InPortalItem))
	{
		ClassToSpawn = DoorItem->ClassToSpawn;
		PortalSize = DoorItem->PortalSize;
		PortalOffset = DoorItem->PortalOffset;
		DoorStaticMeshAsset = DoorItem->DoorStaticMesh;
		DoorFrameMeshAsset = DoorItem->DoorFrameMesh;
		bIsWindow = false;
	}

	// Load the class if needed
	if (ClassToSpawn.IsNull())
	{
		// Fallback to base portal class
		ClassToSpawn = APortalBase::StaticClass();
	}

	UClass* PortalClass = ClassToSpawn.LoadSynchronous();
	if (!PortalClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("PortalItemEditorViewportClient: Failed to load portal class"));
		return;
	}

	// Spawn preview portal actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	PreviewPortalActor = PreviewWorld->SpawnActor<APortalBase>(PortalClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (PreviewPortalActor)
	{
		// Set initial portal size and offset
		PreviewPortalActor->PortalSize = PortalSize;
		PreviewPortalActor->PortalOffset = PortalOffset;

		// Update box component to match portal size and offset
		if (PreviewPortalActor->Box)
		{
			PreviewPortalActor->Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
			PreviewPortalActor->Box->SetRelativeLocation(FVector(PortalOffset.X, 0.0f, PortalOffset.Y)); // Visual offset for preview

			// Force immediate transform update for asset editor preview
			PreviewPortalActor->Box->UpdateComponentToWorld();

			PreviewPortalActor->Box->SetHiddenInGame(false); // Show in preview
			PreviewPortalActor->Box->SetVisibility(true);
		}

		// Apply mesh from data asset
		if (bIsWindow)
		{
			// Cast to WindowBase and apply static mesh
			if (AWindowBase* WindowActor = Cast<AWindowBase>(PreviewPortalActor))
			{
				if (!WindowMeshAsset.IsNull())
				{
					UStaticMesh* LoadedMesh = WindowMeshAsset.LoadSynchronous();
					if (LoadedMesh && WindowActor->WindowMesh)
					{
						WindowActor->WindowMesh->SetStaticMesh(LoadedMesh);
					}
				}
			}
		}
		else
		{
			// Cast to DoorBase and apply both static meshes (door panel + frame)
			if (ADoorBase* DoorActor = Cast<ADoorBase>(PreviewPortalActor))
			{
				// Apply door panel mesh
				if (!DoorStaticMeshAsset.IsNull())
				{
					UStaticMesh* LoadedDoorMesh = DoorStaticMeshAsset.LoadSynchronous();
					if (LoadedDoorMesh && DoorActor->DoorStaticMesh)
					{
						DoorActor->DoorStaticMesh->SetStaticMesh(LoadedDoorMesh);
					}
				}

				// Apply door frame mesh
				if (!DoorFrameMeshAsset.IsNull())
				{
					UStaticMesh* LoadedFrameMesh = DoorFrameMeshAsset.LoadSynchronous();
					if (LoadedFrameMesh && DoorActor->DoorFrameMesh)
					{
						DoorActor->DoorFrameMesh->SetStaticMesh(LoadedFrameMesh);
					}
				}
			}
		}

		// Center the view on the portal
		SetViewLocation(FVector(-PortalSize.X * 2.0f, 0.0f, PortalSize.Y * 0.5f));
		SetLookAtLocation(FVector::ZeroVector);
	}
}

void FPortalItemEditorViewportClient::UpdatePreviewPortal()
{
	if (!PreviewPortalActor || !PortalItemAsset.IsValid())
	{
		return;
	}

	// Get current portal size, offset, and mesh from the data asset
	FVector2D NewPortalSize = FVector2D(100.0f, 200.0f);
	FVector2D NewPortalOffset = FVector2D::ZeroVector;
	bool bIsWindow = false;

	if (UWindowItem* WindowItem = Cast<UWindowItem>(PortalItemAsset.Get()))
	{
		NewPortalSize = WindowItem->PortalSize;
		NewPortalOffset = WindowItem->PortalOffset;
		bIsWindow = true;

		// Update window mesh if this is a WindowBase actor
		if (AWindowBase* WindowActor = Cast<AWindowBase>(PreviewPortalActor))
		{
			if (!WindowItem->WindowMesh.IsNull())
			{
				UStaticMesh* LoadedMesh = WindowItem->WindowMesh.LoadSynchronous();
				if (LoadedMesh && WindowActor->WindowMesh)
				{
					WindowActor->WindowMesh->SetStaticMesh(LoadedMesh);
				}
			}
		}
	}
	else if (UDoorItem* DoorItem = Cast<UDoorItem>(PortalItemAsset.Get()))
	{
		NewPortalSize = DoorItem->PortalSize;
		NewPortalOffset = DoorItem->PortalOffset;
		bIsWindow = false;

		// Update door static meshes if this is a DoorBase actor
		if (ADoorBase* DoorActor = Cast<ADoorBase>(PreviewPortalActor))
		{
			// Update door panel mesh
			if (!DoorItem->DoorStaticMesh.IsNull())
			{
				UStaticMesh* LoadedDoorMesh = DoorItem->DoorStaticMesh.LoadSynchronous();
				if (LoadedDoorMesh && DoorActor->DoorStaticMesh)
				{
					DoorActor->DoorStaticMesh->SetStaticMesh(LoadedDoorMesh);
				}
			}

			// Update door frame mesh
			if (!DoorItem->DoorFrameMesh.IsNull())
			{
				UStaticMesh* LoadedFrameMesh = DoorItem->DoorFrameMesh.LoadSynchronous();
				if (LoadedFrameMesh && DoorActor->DoorFrameMesh)
				{
					DoorActor->DoorFrameMesh->SetStaticMesh(LoadedFrameMesh);
				}
			}
		}
	}

	// Update preview portal size and offset
	PreviewPortalActor->PortalSize = NewPortalSize;
	PreviewPortalActor->PortalOffset = NewPortalOffset;

	// Update box component size and offset
	if (PreviewPortalActor->Box)
	{
		PreviewPortalActor->Box->SetBoxExtent(FVector(NewPortalSize.X / 2.0f, 1.0f, NewPortalSize.Y / 2.0f));
		PreviewPortalActor->Box->SetRelativeLocation(FVector(NewPortalOffset.X, 0.0f, NewPortalOffset.Y)); // Visual offset for preview

		// Force immediate transform update for asset editor preview
		PreviewPortalActor->Box->UpdateComponentToWorld();

		PreviewPortalActor->Box->RecreatePhysicsState();
		PreviewPortalActor->Box->MarkRenderStateDirty();
	}

	// Invalidate viewport to force redraw
	Invalidate();
}

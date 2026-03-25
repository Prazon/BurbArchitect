// Fill out your copyright notice in the Description page of Project Settings.

#include "Commands/PortalCommand.h"
#include "Actors/WindowBase.h"
#include "Actors/DoorBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PortalBoxComponent.h"

void UPortalCommand::Initialize(
	ALotManager* Lot,
	TSubclassOf<APortalBase> PortalClass,
	const FVector& Location,
	const FRotator& Rotation,
	const TArray<int32>& WallArrayIndices,
	const FVector2D& InPortalSize,
	const FVector2D& InPortalOffset,
	TSoftObjectPtr<UStaticMesh> InWindowMesh,
	TSoftObjectPtr<UStaticMesh> InDoorStaticMesh,
	TSoftObjectPtr<UStaticMesh> InDoorFrameMesh
)
{
	LotManager = Lot;
	PortalClassToSpawn = PortalClass;
	SpawnLocation = Location;
	SpawnRotation = Rotation;
	AffectedWallArrayIndices = WallArrayIndices;
	PortalSize = InPortalSize;
	PortalOffset = InPortalOffset;
	WindowMesh = InWindowMesh;
	DoorStaticMesh = InDoorStaticMesh;
	DoorFrameMesh = InDoorFrameMesh;
	bPortalCreated = false;
	SpawnedPortal = nullptr;
}

void UPortalCommand::Commit()
{
	if (!LotManager || !LotManager->WallComponent || !PortalClassToSpawn)
	{
		UE_LOG(LogTemp, Error, TEXT("PortalCommand: LotManager, WallComponent, or PortalClass is null"));
		return;
	}

	// Validate that we have at least one wall index
	if (AffectedWallArrayIndices.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("PortalCommand: No wall indices provided"));
		return;
	}

	// Validate all wall array indices
	for (int32 WallIndex : AffectedWallArrayIndices)
	{
		if (!LotManager->WallComponent->WallDataArray.IsValidIndex(WallIndex))
		{
			UE_LOG(LogTemp, Error, TEXT("PortalCommand: Invalid WallArrayIndex %d"), WallIndex);
			return;
		}
	}

	// Spawn the portal actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = LotManager;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedPortal = LotManager->GetWorld()->SpawnActor<APortalBase>(
		PortalClassToSpawn,
		SpawnLocation,
		SpawnRotation,
		SpawnParams
	);

	if (!SpawnedPortal)
	{
		UE_LOG(LogTemp, Error, TEXT("PortalCommand: Failed to spawn portal"));
		return;
	}

	// Setup portal references
	SpawnedPortal->CurrentLot = LotManager;
	SpawnedPortal->CurrentWallComponent = LotManager->WallComponent;

	// Apply portal size from command parameters
	if (PortalSize.X > 0.0f && PortalSize.Y > 0.0f)
	{
		SpawnedPortal->PortalSize = PortalSize;

		// Update box component to match portal size
		if (SpawnedPortal->Box)
		{
			SpawnedPortal->Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
		}
	}

	// Store portal offset for wall cutout rendering
	SpawnedPortal->PortalOffset = PortalOffset;

	// Apply offset to Box component (same as preview tools do)
	// This moves the Box to visualize where the cutout will be
	if (SpawnedPortal->Box)
	{
		SpawnedPortal->Box->SetRelativeLocation(FVector(PortalOffset.X, 0.0f, PortalOffset.Y));
		SpawnedPortal->Box->UpdateComponentToWorld();
	}

	UE_LOG(LogTemp, Log, TEXT("PortalCommand: Set PortalOffset (%.1f, %.1f) and Box offset on portal actor"),
		PortalOffset.X, PortalOffset.Y);

	// Apply meshes based on portal type using replicated properties
	// Setting the replicated property triggers OnRep on clients, and we call Apply* locally on server
	AWindowBase* WindowActor = Cast<AWindowBase>(SpawnedPortal);
	ADoorBase* DoorActor = Cast<ADoorBase>(SpawnedPortal);

	if (WindowActor)
	{
		UE_LOG(LogTemp, Log, TEXT("PortalCommand: Spawned portal is a WindowBase"));

		// Set replicated mesh property - this will replicate to clients and trigger OnRep_WindowMeshAsset
		if (!WindowMesh.IsNull())
		{
			WindowActor->WindowMeshAsset = WindowMesh;
			WindowActor->ApplyWindowMesh();  // Apply locally on server
			UE_LOG(LogTemp, Log, TEXT("PortalCommand: Set WindowMeshAsset for replication"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PortalCommand: WindowMesh soft pointer is null (not set in command)"));
		}
	}
	else if (DoorActor)
	{
		UE_LOG(LogTemp, Log, TEXT("PortalCommand: Spawned portal is a DoorBase"));

		// Set replicated door panel mesh property
		if (!DoorStaticMesh.IsNull())
		{
			DoorActor->DoorMeshAsset = DoorStaticMesh;
			DoorActor->ApplyDoorMesh();  // Apply locally on server
			UE_LOG(LogTemp, Log, TEXT("PortalCommand: Set DoorMeshAsset for replication"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PortalCommand: DoorStaticMesh soft pointer is null (not set in command)"));
		}

		// Set replicated door frame mesh property
		if (!DoorFrameMesh.IsNull())
		{
			DoorActor->DoorFrameMeshAsset = DoorFrameMesh;
			DoorActor->ApplyDoorFrameMesh();  // Apply locally on server
			UE_LOG(LogTemp, Log, TEXT("PortalCommand: Set DoorFrameMeshAsset for replication"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PortalCommand: DoorFrameMesh soft pointer is null (not set in command)"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PortalCommand: Spawned portal is neither WindowBase nor DoorBase. Class: %s"),
			SpawnedPortal ? *SpawnedPortal->GetClass()->GetName() : TEXT("NULL"));
	}

	// Force network update to ensure all replicated properties are sent to clients
	SpawnedPortal->ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("PortalCommand: Called ForceNetUpdate on portal actor"));

	// Add portal to all affected wall sections' portal arrays
	for (int32 WallIndex : AffectedWallArrayIndices)
	{
		LotManager->WallComponent->WallDataArray[WallIndex].PortalArray.Add(SpawnedPortal);
		UE_LOG(LogTemp, Log, TEXT("PortalCommand: Added portal to wall section %d"), WallIndex);
	}

	// Render portals to cut holes in walls
	LotManager->WallComponent->RenderPortals();

	bPortalCreated = true;
	bCommitted = true;

	UE_LOG(LogTemp, Log, TEXT("PortalCommand: Created portal at (%s) affecting %d wall section(s)"),
		*SpawnLocation.ToString(), AffectedWallArrayIndices.Num());
}

void UPortalCommand::Undo()
{
	if (!bPortalCreated || !LotManager || !LotManager->WallComponent || !SpawnedPortal)
	{
		UE_LOG(LogTemp, Warning, TEXT("PortalCommand: Cannot undo - portal not created or invalid references"));
		return;
	}

	// Remove portal from all affected wall sections' portal arrays
	for (int32 WallIndex : AffectedWallArrayIndices)
	{
		if (LotManager->WallComponent->WallDataArray.IsValidIndex(WallIndex))
		{
			LotManager->WallComponent->WallDataArray[WallIndex].PortalArray.Remove(SpawnedPortal);
			UE_LOG(LogTemp, Log, TEXT("PortalCommand Undo: Removed portal from wall section %d"), WallIndex);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PortalCommand Undo: Invalid WallArrayIndex %d"), WallIndex);
		}
	}

	// Re-render portals to update wall cutouts
	LotManager->WallComponent->RenderPortals();

	// Destroy the portal actor
	SpawnedPortal->Destroy();
	SpawnedPortal = nullptr;
	bPortalCreated = false;

	UE_LOG(LogTemp, Log, TEXT("PortalCommand: Destroyed portal at (%s)"), *SpawnLocation.ToString());
}

void UPortalCommand::Redo()
{
	if (!LotManager || !LotManager->WallComponent || !PortalClassToSpawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("PortalCommand: Cannot redo - invalid references"));
		return;
	}

	// Validate all wall array indices
	for (int32 WallIndex : AffectedWallArrayIndices)
	{
		if (!LotManager->WallComponent->WallDataArray.IsValidIndex(WallIndex))
		{
			UE_LOG(LogTemp, Error, TEXT("PortalCommand Redo: Invalid WallArrayIndex %d"), WallIndex);
			return;
		}
	}

	// Spawn the portal actor again
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = LotManager;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedPortal = LotManager->GetWorld()->SpawnActor<APortalBase>(
		PortalClassToSpawn,
		SpawnLocation,
		SpawnRotation,
		SpawnParams
	);

	if (!SpawnedPortal)
	{
		UE_LOG(LogTemp, Error, TEXT("PortalCommand Redo: Failed to spawn portal"));
		return;
	}

	// Setup portal references
	SpawnedPortal->CurrentLot = LotManager;
	SpawnedPortal->CurrentWallComponent = LotManager->WallComponent;

	// Apply portal size from command parameters
	if (PortalSize.X > 0.0f && PortalSize.Y > 0.0f)
	{
		SpawnedPortal->PortalSize = PortalSize;

		// Update box component to match portal size
		if (SpawnedPortal->Box)
		{
			SpawnedPortal->Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
		}
	}

	// Store portal offset for wall cutout rendering
	SpawnedPortal->PortalOffset = PortalOffset;

	// Apply offset to Box component (same as preview tools do)
	// This moves the Box to visualize where the cutout will be
	if (SpawnedPortal->Box)
	{
		SpawnedPortal->Box->SetRelativeLocation(FVector(PortalOffset.X, 0.0f, PortalOffset.Y));
		SpawnedPortal->Box->UpdateComponentToWorld();
	}

	UE_LOG(LogTemp, Log, TEXT("PortalCommand: Set PortalOffset (%.1f, %.1f) and Box offset on portal actor"),
		PortalOffset.X, PortalOffset.Y);

	// Apply meshes based on portal type using replicated properties
	// Setting the replicated property triggers OnRep on clients, and we call Apply* locally on server
	AWindowBase* WindowActor = Cast<AWindowBase>(SpawnedPortal);
	ADoorBase* DoorActor = Cast<ADoorBase>(SpawnedPortal);

	if (WindowActor)
	{
		UE_LOG(LogTemp, Log, TEXT("PortalCommand Redo: Spawned portal is a WindowBase"));

		// Set replicated mesh property - this will replicate to clients and trigger OnRep_WindowMeshAsset
		if (!WindowMesh.IsNull())
		{
			WindowActor->WindowMeshAsset = WindowMesh;
			WindowActor->ApplyWindowMesh();  // Apply locally on server
			UE_LOG(LogTemp, Log, TEXT("PortalCommand Redo: Set WindowMeshAsset for replication"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PortalCommand Redo: WindowMesh soft pointer is null (not set in command)"));
		}
	}
	else if (DoorActor)
	{
		UE_LOG(LogTemp, Log, TEXT("PortalCommand Redo: Spawned portal is a DoorBase"));

		// Set replicated door panel mesh property
		if (!DoorStaticMesh.IsNull())
		{
			DoorActor->DoorMeshAsset = DoorStaticMesh;
			DoorActor->ApplyDoorMesh();  // Apply locally on server
			UE_LOG(LogTemp, Log, TEXT("PortalCommand Redo: Set DoorMeshAsset for replication"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PortalCommand Redo: DoorStaticMesh soft pointer is null (not set in command)"));
		}

		// Set replicated door frame mesh property
		if (!DoorFrameMesh.IsNull())
		{
			DoorActor->DoorFrameMeshAsset = DoorFrameMesh;
			DoorActor->ApplyDoorFrameMesh();  // Apply locally on server
			UE_LOG(LogTemp, Log, TEXT("PortalCommand Redo: Set DoorFrameMeshAsset for replication"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PortalCommand Redo: DoorFrameMesh soft pointer is null (not set in command)"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PortalCommand Redo: Spawned portal is neither WindowBase nor DoorBase. Class: %s"),
			SpawnedPortal ? *SpawnedPortal->GetClass()->GetName() : TEXT("NULL"));
	}

	// Force network update to ensure all replicated properties are sent to clients
	SpawnedPortal->ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("PortalCommand Redo: Called ForceNetUpdate on portal actor"));

	// Add portal to all affected wall sections' portal arrays
	for (int32 WallIndex : AffectedWallArrayIndices)
	{
		LotManager->WallComponent->WallDataArray[WallIndex].PortalArray.Add(SpawnedPortal);
		UE_LOG(LogTemp, Log, TEXT("PortalCommand Redo: Added portal to wall section %d"), WallIndex);
	}

	// Render portals to cut holes in walls
	LotManager->WallComponent->RenderPortals();

	bPortalCreated = true;

	UE_LOG(LogTemp, Log, TEXT("PortalCommand: Redid portal at (%s) affecting %d wall section(s)"),
		*SpawnLocation.ToString(), AffectedWallArrayIndices.Num());
}

FString UPortalCommand::GetDescription() const
{
	return FString::Printf(TEXT("Place Portal at (%.0f, %.0f)"), SpawnLocation.X, SpawnLocation.Y);
}

bool UPortalCommand::IsValid() const
{
	// Command is valid if the lot manager and wall component still exist
	return LotManager && LotManager->WallComponent;
}

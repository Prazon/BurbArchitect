// Fill out your copyright notice in the Description page of Project Settings.

#include "Commands/RoofCommand.h"

#include "Actors/Roofs/RoofBase.h"


void URoofCommand::Initialize(ALotManager* Lot, const FVector& Loc, const FVector& Dir, const FRoofDimensions& Dims, float RoofThick, float GableThick, UMaterialInstance* Mat)
{
	LotManager = Lot;
	Location = Loc;
	Direction = Dir;
	Dimensions = Dims;
	RoofThickness = RoofThick;
	GableThickness = GableThick;
	RoofMaterial = Mat;
	bRoofCreated = false;
	CreatedRoofComponent = nullptr;
	CreatedRoofActor = nullptr;
}

void URoofCommand::Commit()
{
	if (!LotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("RoofCommand: LotManager is null"));
		return;
	}

	// Spawn a roof actor using the new system
	CreatedRoofActor = LotManager->SpawnRoofActor(Location, Direction, Dimensions, RoofThickness, GableThickness, RoofMaterial);

	if (!CreatedRoofActor)
	{
		UE_LOG(LogTemp, Error, TEXT("RoofCommand: Failed to create roof actor"));
		return;
	}

	// Commit the roof (generates supporting walls)
	CreatedRoofActor->CommitRoof();

	// Get the roof data from the actor's component for compatibility
	if (CreatedRoofActor->RoofMeshComponent)
	{
		RoofData = CreatedRoofActor->RoofMeshComponent->RoofData;
	}

	bRoofCreated = true;
	bCommitted = true;

	UE_LOG(LogTemp, Log, TEXT("RoofCommand: Created roof actor at (%s)"), *Location.ToString());
}

void URoofCommand::Undo()
{
	if (!bRoofCreated || !LotManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoofCommand: Cannot undo - roof not created"));
		return;
	}

	// Handle new actor-based roof
	if (CreatedRoofActor)
	{
		// The roof actor handles its own wall cleanup
		LotManager->RemoveRoofActor(CreatedRoofActor);
		CreatedRoofActor = nullptr;
	}
	// Handle legacy component-based roof (for backward compatibility)
	else if (CreatedRoofComponent)
	{
		// Cleanup walls created by this roof
		if (RoofData.CreatedWallIndices.Num() > 0 && LotManager->WallComponent)
		{
			for (int32 WallIndex : RoofData.CreatedWallIndices)
			{
				if (LotManager->WallComponent->WallDataArray.IsValidIndex(WallIndex))
				{
					FWallSegmentData& Wall = LotManager->WallComponent->WallDataArray[WallIndex];
					LotManager->WallComponent->DestroyWallSection(Wall);
				}
			}
			UE_LOG(LogTemp, Log, TEXT("RoofCommand: Destroyed %d walls created by roof"), RoofData.CreatedWallIndices.Num());
		}

		// Destroy the roof section
		CreatedRoofComponent->DestroyRoofSection(RoofData);

		// Remove the component from the lot manager's array
		LotManager->RemoveRoofComponent(CreatedRoofComponent);
		CreatedRoofComponent = nullptr;
	}

	bRoofCreated = false;

	UE_LOG(LogTemp, Log, TEXT("RoofCommand: Destroyed roof at (%s)"), *Location.ToString());
}

void URoofCommand::Redo()
{
	if (!LotManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoofCommand: Cannot redo - LotManager invalid"));
		return;
	}

	// Recreate roof actor
	CreatedRoofActor = LotManager->SpawnRoofActor(Location, Direction, Dimensions, RoofThickness, GableThickness, RoofMaterial);

	if (!CreatedRoofActor)
	{
		UE_LOG(LogTemp, Error, TEXT("RoofCommand: Failed to recreate roof actor on redo"));
		return;
	}

	// Commit the roof (generates supporting walls)
	CreatedRoofActor->CommitRoof();

	// Get the roof data from the actor's component for compatibility
	if (CreatedRoofActor->RoofMeshComponent)
	{
		RoofData = CreatedRoofActor->RoofMeshComponent->RoofData;
	}

	bRoofCreated = true;

	UE_LOG(LogTemp, Log, TEXT("RoofCommand: Redid roof at (%s)"), *Location.ToString());
}

FString URoofCommand::GetDescription() const
{
	return FString::Printf(TEXT("Build Roof at (%.0f, %.0f)"), Location.X, Location.Y);
}

bool URoofCommand::IsValid() const
{
	return LotManager && (LotManager->GetRoofActorCount() >= 0 || LotManager->GetRoofComponentCount() >= 0);
}

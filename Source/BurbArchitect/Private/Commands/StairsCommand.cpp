// Fill out your copyright notice in the Description page of Project Settings.

#include "Commands/StairsCommand.h"
#include "Actors/StairsBase.h"

void UStairsCommand::Initialize(ALotManager* Lot, TSubclassOf<AStairsBase> StairsClass, const FVector& Start, const FVector& Dir, const TArray<FStairModuleStructure>& Structs, float StairsThick, UStaticMesh* TreadMesh, UStaticMesh* LandingMesh)
{
	LotManager = Lot;
	StairsActorClass = StairsClass;
	StartLoc = Start;
	Direction = Dir;
	Structures = Structs;
	StairsThickness = StairsThick;
	StairTreadMesh = TreadMesh;
	StairLandingMesh = LandingMesh;
	StairsActor = nullptr;
}

void UStairsCommand::Commit()
{
	if (!LotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("StairsCommand: LotManager is null"));
		return;
	}

	// Spawn stairs actor
	StairsActor = LotManager->SpawnStairsActor(
		StairsActorClass,
		StartLoc,
		Direction,
		Structures,
		StairTreadMesh,
		StairLandingMesh,
		StairsThickness
	);

	if (StairsActor)
	{
		// Commit the stairs actor (makes it permanent)
		StairsActor->CommitStairs();
		bCommitted = true;

		UE_LOG(LogTemp, Log, TEXT("StairsCommand: Created stairs at (%s)"), *StartLoc.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("StairsCommand: Failed to spawn stairs actor"));
	}
}

void UStairsCommand::Undo()
{
	if (!StairsActor || !LotManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsCommand: Cannot undo - stairs actor or lot invalid"));
		return;
	}

	// Remove and destroy the stairs actor
	LotManager->RemoveStairsActor(StairsActor);
	StairsActor = nullptr;

	UE_LOG(LogTemp, Log, TEXT("StairsCommand: Destroyed stairs at (%s)"), *StartLoc.ToString());
}

void UStairsCommand::Redo()
{
	if (!LotManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("StairsCommand: Cannot redo - lot invalid"));
		return;
	}

	// Respawn stairs actor
	StairsActor = LotManager->SpawnStairsActor(
		StairsActorClass,
		StartLoc,
		Direction,
		Structures,
		StairTreadMesh,
		StairLandingMesh,
		StairsThickness
	);

	if (StairsActor)
	{
		// Re-commit the stairs
		StairsActor->CommitStairs();

		UE_LOG(LogTemp, Log, TEXT("StairsCommand: Redid stairs at (%s)"), *StartLoc.ToString());
	}
}

FString UStairsCommand::GetDescription() const
{
	return FString::Printf(TEXT("Build Stairs at (%.0f, %.0f)"), StartLoc.X, StartLoc.Y);
}

bool UStairsCommand::IsValid() const
{
	return LotManager != nullptr
		&& StairsActorClass != nullptr
		&& StairTreadMesh != nullptr
		&& StairLandingMesh != nullptr
		&& Structures.Num() > 0;
}

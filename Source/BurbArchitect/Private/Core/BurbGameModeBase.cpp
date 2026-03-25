// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/BurbGameModeBase.h"
#include "Actors/BurbPawn.h"

void ABurbGameModeBase::RestartPlayer(AController* NewPlayer)
{
	// Call parent to handle spawning
	Super::RestartPlayer(NewPlayer);

	// Set the starting mode on the newly spawned pawn
	if (NewPlayer && NewPlayer->GetPawn())
	{
		if (ABurbPawn* BurbPawn = Cast<ABurbPawn>(NewPlayer->GetPawn()))
		{
			BurbPawn->SetMode(StartingBurbMode);
			UE_LOG(LogTemp, Log, TEXT("BurbGameModeBase: Set starting mode %d on newly spawned pawn"), static_cast<int32>(StartingBurbMode));
		}
	}
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "Interfaces/IDeletable.h"
#include "GameFramework/Actor.h"

bool IDeletable::RequestDeletion_Implementation()
{
	// Check if deletion is allowed
	if (!Execute_CanBeDeleted(Cast<UObject>(this)))
	{
		UE_LOG(LogTemp, Warning, TEXT("IDeletable: Deletion blocked by CanBeDeleted()"));
		return false;
	}

	// Call cleanup callback
	Execute_OnDeleted(Cast<UObject>(this));

	// Destroy the actor
	AActor* Actor = Cast<AActor>(this);
	if (Actor)
	{
		UE_LOG(LogTemp, Log, TEXT("IDeletable: Deleting actor %s"), *Actor->GetName());
		Actor->Destroy();
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("IDeletable: Failed to cast to AActor for deletion"));
	return false;
}

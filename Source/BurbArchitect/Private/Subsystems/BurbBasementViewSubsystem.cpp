// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/BurbBasementViewSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Components/LightComponentBase.h"
#include "GameplayTagContainer.h"
#include "Engine/Light.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "GameplayTagAssetInterface.h"
#include "Actors/LotManager.h"
#include "Actors/BuildTool.h"

void UBurbBasementViewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTemp, Log, TEXT("BurbBasementViewSubsystem: Initialized"));
}

void UBurbBasementViewSubsystem::Deinitialize()
{
	// If basement view is enabled, restore normal rendering before shutdown
	if (bBasementViewEnabled)
	{
		SetBasementViewEnabled(false);
	}

	UE_LOG(LogTemp, Log, TEXT("BurbBasementViewSubsystem: Deinitialized"));

	Super::Deinitialize();
}

void UBurbBasementViewSubsystem::SetBasementViewEnabled(bool bEnabled)
{
	if (bBasementViewEnabled == bEnabled)
	{
		return; // Already in desired state
	}

	bBasementViewEnabled = bEnabled;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbBasementViewSubsystem: Cannot toggle basement view - no world"));
		return;
	}

	// Iterate through all actors and toggle visibility on non-lot actors
	// When basement view is ENABLED:
	//   - SetActorHiddenInGame(true) - hide the entire actor
	//   - SetCastHiddenShadow(true) on components - still cast shadows while hidden
	// When basement view is DISABLED:
	//   - SetActorHiddenInGame(false) - show the actor
	//   - SetCastHiddenShadow(false) on components - normal shadow behavior
	const bool bShouldHide = bEnabled;
	const bool bCastShadowWhenHidden = bEnabled;

	int32 ToggledCount = 0;
	int32 SkippedCount = 0;

	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		AActor* Actor = *ActorItr;
		if (!Actor)
		{
			continue;
		}

		// Skip actors that should remain visible (lot-related, lights, tagged)
		if (ShouldSkipActor(Actor))
		{
			SkippedCount++;
			continue;
		}

		// Only toggle actors with primitive components
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

		if (PrimitiveComponents.Num() > 0)
		{
			// Hide/show the actor
			Actor->SetActorHiddenInGame(bShouldHide);

			// Set shadow casting on all primitive components
			for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
			{
				if (PrimComp)
				{
					PrimComp->SetCastHiddenShadow(bCastShadowWhenHidden);
				}
			}

			ToggledCount++;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("BurbBasementViewSubsystem: Basement view %s - toggled %d actors (skipped %d: lot/lights/tagged)"),
		bEnabled ? TEXT("ENABLED") : TEXT("DISABLED"), ToggledCount, SkippedCount);
}

bool UBurbBasementViewSubsystem::ShouldSkipActor(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	// 1. Check if this actor is lot-related - should remain visible
	// Use type checking instead of string matching to properly handle Blueprint classes
	// (Blueprint classes have "_C" suffix and use asset name, not parent C++ class name)
	AActor* Owner = Actor->GetOwner();
	AActor* AttachParent = Actor->GetAttachParentActor();

	if (Actor->IsA<ALotManager>() ||
	    Actor->IsA<ABuildTool>() ||
	    (Owner && Owner->IsA<ALotManager>()) ||
	    (AttachParent && AttachParent->IsA<ALotManager>()))
	{
		return true; // Skip - keep visible
	}

	// 2. Check if this is a light or skylight actor - should remain visible
	if (Actor->IsA<ALight>() || Actor->IsA<ASkyLight>())
	{
		return true; // Skip - keep visible (preserve lighting in basement view)
	}

	// 3. Check if actor has any light components (e.g., lamps, fixtures with attached lights)
	TArray<ULightComponentBase*> LightComponents;
	Actor->GetComponents<ULightComponentBase>(LightComponents);
	if (LightComponents.Num() > 0)
	{
		return true; // Skip - keep visible (actor has light components)
	}

	// 4. Check for gameplay tag "BasementView.AlwaysVisible"
	IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Actor);
	if (TagInterface)
	{
		FGameplayTagContainer ActorTags;
		TagInterface->GetOwnedGameplayTags(ActorTags);

		// Check for "BasementView.AlwaysVisible" tag
		FGameplayTag AlwaysVisibleTag = FGameplayTag::RequestGameplayTag(FName("BasementView.AlwaysVisible"), false);
		if (AlwaysVisibleTag.IsValid() && ActorTags.HasTag(AlwaysVisibleTag))
		{
			return true; // Skip - keep visible
		}
	}

	return false; // Don't skip - hide this actor
}

void UBurbBasementViewSubsystem::DebugPrintAllActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("BurbBasementViewSubsystem: No world available for debug"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== BurbBasementViewSubsystem Debug: All Actors ==="));

	int32 TotalActors = 0;
	int32 ActorsWithPrimitives = 0;
	int32 TotalPrimitives = 0;

	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		AActor* Actor = *ActorItr;
		if (!Actor)
		{
			continue;
		}

		TotalActors++;

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

		if (PrimitiveComponents.Num() > 0)
		{
			ActorsWithPrimitives++;
			TotalPrimitives += PrimitiveComponents.Num();

			AActor* Owner = Actor->GetOwner();
			AActor* AttachParent = Actor->GetAttachParentActor();
			FString OwnerName = Owner ? Owner->GetName() : TEXT("None");
			FString AttachName = AttachParent ? AttachParent->GetName() : TEXT("None");

			UE_LOG(LogTemp, Warning, TEXT("  Actor: %s (Class: %s) - %d primitives - Owner: %s - Parent: %s"),
				*Actor->GetName(), *Actor->GetClass()->GetName(), PrimitiveComponents.Num(), *OwnerName, *AttachName);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("=== Total: %d actors, %d with primitives (%d total primitives) ==="),
		TotalActors, ActorsWithPrimitives, TotalPrimitives);
}

bool UBurbBasementViewSubsystem::IsBasementViewEnabled() const
{
	return bBasementViewEnabled;
}

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Actors/BurbPawn.h"
#include "BurbGameModeBase.generated.h"

/**
 * Base game mode for BurbArchitect plugin
 * Handles game-wide settings like starting mode for newly spawned players
 */
UCLASS()
class BURBARCHITECT_API ABurbGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	// The mode that newly spawned players will start in
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Game Mode")
	EBurbMode StartingBurbMode = EBurbMode::Build;

	// Override to set initial mode on spawned pawns
	virtual void RestartPlayer(AController* NewPlayer) override;
};

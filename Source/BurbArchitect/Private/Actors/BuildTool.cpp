// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuildTool.h"

#include "Actors/BurbPawn.h"
#include "Net/UnrealNetwork.h"


ABuildTool::ABuildTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	// Use bReplicates property directly instead of calling SetReplicates() in constructor
	// Calling SetReplicates() in constructor causes "SetReplicates called on non-initialized actor" warning
	bReplicates = true;
}

void ABuildTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABuildTool, TargetLocation);
	DOREPLIFETIME(ABuildTool, TargetRotation);
	DOREPLIFETIME(ABuildTool, DragCreateVectors);
	DOREPLIFETIME(ABuildTool, bRequireCost);
	DOREPLIFETIME(ABuildTool, bDeletionMode);
	DOREPLIFETIME(ABuildTool, Price);
	DOREPLIFETIME(ABuildTool, PreviousLocation);
	DOREPLIFETIME(ABuildTool, bLockToForwardAxis);
	DOREPLIFETIME(ABuildTool, CurrentLot);
	DOREPLIFETIME(ABuildTool, CurrentPlayerPawn);
	DOREPLIFETIME(ABuildTool, bShiftPressed);
}

void ABuildTool::BeginPlay()
{
	Super::BeginPlay();
	SetReplicateMovement(true);
	if (CurrentPlayerPawn)
	{
		CurrentLot = CurrentPlayerPawn->CurrentLot;
	}
	else
	{
		CurrentPlayerPawn = Cast<ABurbPawn>(GetInstigator());
		if (CurrentPlayerPawn)
		{
			CurrentLot = CurrentPlayerPawn->CurrentLot;
		}
	}	
}

void ABuildTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildTool::BroadcastDelete_Implementation()
{
	Delete();
}

void ABuildTool::ServerDelete_Implementation()
{
	if (CurrentPlayerPawn)
	{
		// Switch to deletion tool (similar to ServerCancel but equips deletion tool)
		CurrentPlayerPawn->EnsureDeletionToolEquipped();
	}
	else
	{
		// Fallback: just toggle deletion mode if no pawn reference
		BroadcastDelete();
	}
}

void ABuildTool::Delete_Implementation()
{
	bDeletionMode = !bDeletionMode;
}

void ABuildTool::BroadcastMove_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	Move(MoveLocation, SelectPressed, CursorWorldHitResult, TracedLevel);
}

void ABuildTool::ServerMove_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	BroadcastMove(MoveLocation, SelectPressed, CursorWorldHitResult, TracedLevel);
}

void ABuildTool::ServerCancel_Implementation()
{
	if (CurrentPlayerPawn)
	{
		// Notify the pawn to clear the tool (triggers auto-equip of default tool)
		CurrentPlayerPawn->SetCurrentBuildTool(nullptr);
	}
	else
	{
		// Fallback: destroy directly if no pawn reference
		Destroy();
	}
}

void ABuildTool::BroadcastLocation_Implementation(FVector Location)
{
	SetActorLocation(Location);
}

void ABuildTool::UpdateLocation_Implementation(FVector Location)
{
	SetActorLocation(Location);
	BroadcastLocation(Location);
}

void ABuildTool::OnDragged_Implementation()
{
}

void ABuildTool::ServerDrag_Implementation()
{
	BroadcastDrag();
}

void ABuildTool::BroadcastDrag_Implementation()
{
	Drag();
}

void ABuildTool::OnMoved_Implementation()
{
}

void ABuildTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
}

void ABuildTool::Click_Implementation()
{
}

void ABuildTool::ServerClick_Implementation()
{
	BroadcastClick();
}

void ABuildTool::BroadcastClick_Implementation()
{
	Click();
}

void ABuildTool::Drag_Implementation()
{
}

void ABuildTool::Release_Implementation()
{
	if (HasAuthority())  // Check if this instance has authority (i.e., it's the server)
	{
		BroadcastRelease();
	}
	else
	{
		ServerRelease();
	}
}

void ABuildTool::ServerRelease_Implementation()
{
	BroadcastRelease();
}

void ABuildTool::BroadcastRelease_Implementation()
{
}

void ABuildTool::RotateLeft_Implementation()
{
	// Default implementation does nothing
	// Child classes can override this to implement rotation behavior
	UE_LOG(LogTemp, Warning, TEXT("BuildTool::RotateLeft - Base implementation called. Override in child class."));
}

void ABuildTool::RotateRight_Implementation()
{
	// Default implementation does nothing
	// Child classes can override this to implement rotation behavior
	UE_LOG(LogTemp, Warning, TEXT("BuildTool::RotateRight - Base implementation called. Override in child class."));
}
// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/PlaceableObject.h"
#include "Net/UnrealNetwork.h"


// Sets default values
APlaceableObject::APlaceableObject(): CurrentFloor(0)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
}

void APlaceableObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APlaceableObject, CurrentFloor);
	DOREPLIFETIME(APlaceableObject, PlacementMode);
	DOREPLIFETIME(APlaceableObject, PlacedRow);
	DOREPLIFETIME(APlaceableObject, PlacedColumn);
	DOREPLIFETIME(APlaceableObject, WallNormal);
}

// Called when the game starts or when spawned
void APlaceableObject::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void APlaceableObject::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}


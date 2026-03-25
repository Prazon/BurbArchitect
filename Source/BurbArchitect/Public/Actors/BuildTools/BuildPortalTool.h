// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Actors/BuildTool.h"
#include "Actors/PortalBase.h"
#include "BuildPortalTool.generated.h"


//This class represents objects that create "portals" as an abstract idea between walls and floors.
//Windows, Doors and Stairs can be considered portals as they cut out areas in floors or walls
UCLASS()
class BURBARCHITECT_API ABuildPortalTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuildPortalTool();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;

	virtual void BroadcastRelease_Implementation() override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HorizontalSnap = 50.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float VerticalSnap = 25.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bSnapsToFloor = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	bool bValidPlacementLocation = false;

	//Class of portal/window/door to spawn
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	TSoftClassPtr<APortalBase> ClassToSpawn;

	// Portal size from data asset (width x height in cm) - Replicated so server can spawn portal with correct size
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	FVector2D PortalSize;

	// Portal position offset from wall placement point (X = horizontal, Y = vertical, in cm) - Replicated for server spawn
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	FVector2D PortalOffset;

	// Static mesh for windows (set from WindowItem data asset) - Replicated for server spawn
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	TSoftObjectPtr<UStaticMesh> WindowMesh;

	// Static mesh for door panel (set from DoorItem data asset) - Replicated for server spawn
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	TSoftObjectPtr<UStaticMesh> DoorStaticMesh;

	// Static mesh for door frame (set from DoorItem data asset) - Replicated for server spawn
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	TSoftObjectPtr<UStaticMesh> DoorFrameMesh;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 HitMeshSection = -1;
	
};

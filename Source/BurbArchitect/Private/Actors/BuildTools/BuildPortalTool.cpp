// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/BuildTools/BuildPortalTool.h"

#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Components/WallComponent.h"
#include "Components/WallGraphComponent.h"
#include "Data/WallGraphData.h"
#include "Net/UnrealNetwork.h"


// Sets default values
ABuildPortalTool::ABuildPortalTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Portal tools need to trace against walls, not the grid
	TraceChannel = ECC_GameTraceChannel1; // Wall channel
}

void ABuildPortalTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate portal properties from data assets so server can spawn portal with correct values
	DOREPLIFETIME(ABuildPortalTool, bValidPlacementLocation);
	DOREPLIFETIME(ABuildPortalTool, ClassToSpawn);
	DOREPLIFETIME(ABuildPortalTool, PortalSize);
	DOREPLIFETIME(ABuildPortalTool, PortalOffset);
	DOREPLIFETIME(ABuildPortalTool, WindowMesh);
	DOREPLIFETIME(ABuildPortalTool, DoorStaticMesh);
	DOREPLIFETIME(ABuildPortalTool, DoorFrameMesh);
}

// Called when the game starts or when spawned
void ABuildPortalTool::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void ABuildPortalTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABuildPortalTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	if (!CurrentLot)
	{
		return;
	}

	if(Cast<UWallComponent>(CursorWorldHitResult.Component))
	{
		UWallComponent* HitWallComponent = Cast<UWallComponent>(CursorWorldHitResult.Component);

		// Use grid-based wall detection instead of pointer-based detection
		// This is more robust to undo/redo operations since it uses spatial coordinates
		// Get the current level from the traced parameter
		int32 Level = TracedLevel;
		HitMeshSection = HitWallComponent->GetWallArrayIndexFromHitLocation(CursorWorldHitResult.ImpactPoint, Level);

		//Make sure hit section is valid
		if (HitMeshSection >= 0)
		{
			// Check if the wall is on the current level
			int32 WallLevel = HitWallComponent->WallDataArray[HitMeshSection].Level;
			if (WallLevel != Level)
			{
				// Wall is on a different level - mark as invalid placement
				bValidPlacementLocation = false;
				return;
			}

			// Get wall data and validate WallGraph linkage
			const FWallSegmentData& HitWall = HitWallComponent->WallDataArray[HitMeshSection];
			int32 EdgeID = HitWall.WallEdgeID;

			if (EdgeID == -1 || !CurrentLot->WallGraph)
			{
				// Decorative walls (like half walls) don't support portals
				UE_LOG(LogTemp, Warning, TEXT("BuildPortalTool: Cannot place portals on decorative walls (half walls, etc.)"));
				bValidPlacementLocation = false;
				return;
			}

			// Check if this is a basement exterior wall (portals not allowed)
			if (HitWall.Level < CurrentLot->Basements)
			{
				// Get the wall edge from WallGraph
				const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(EdgeID);
				if (Edge && (Edge->Room1 == 0 || Edge->Room2 == 0))
				{
					// This is a basement wall facing outside - reject portal placement
					UE_LOG(LogTemp, Warning, TEXT("BuildPortalTool: Cannot place portals on basement exterior walls"));
					bValidPlacementLocation = false;
					return;
				}
			}

			// Check if this is a pool wall (doors not allowed, windows allowed)
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(EdgeID);
			if (Edge && Edge->bIsPoolWall)
			{
				// Pool walls cannot have doors (use bSnapsToFloor as a proxy for "is door")
				// Doors snap to floor, windows don't
				if (bSnapsToFloor)
				{
					UE_LOG(LogTemp, Warning, TEXT("BuildPortalTool: Cannot place doors on pool walls (windows are allowed)"));
					bValidPlacementLocation = false;
					return;
				}
			}

			TargetRotation = HitWallComponent->WallDataArray[HitMeshSection].WallRotation;

			// Get wall start and end positions
			FVector WallStart = HitWallComponent->WallDataArray[HitMeshSection].StartLoc;
			FVector WallEnd = HitWallComponent->WallDataArray[HitMeshSection].EndLoc;
			FVector WallDir = (WallEnd - WallStart).GetSafeNormal();
			float WallLength = FVector::Dist(WallStart, WallEnd);

			// Project hit point onto wall line to get distance along wall
			FVector HitToStart = CursorWorldHitResult.ImpactPoint - WallStart;
			float DistanceAlongWall = FVector::DotProduct(HitToStart, WallDir);

			// Snap the distance along the wall to HorizontalSnap increments
			float SnappedDistance = FMath::GridSnap(DistanceAlongWall, HorizontalSnap);
			SnappedDistance = FMath::Clamp(SnappedDistance, 0.0f, WallLength);

			// Calculate final position along the wall
			TargetLocation = WallStart + (WallDir * SnappedDistance);

			// Apply vertical snapping independently
			TargetLocation.Z = FMath::GridSnap(CursorWorldHitResult.Location.Z, VerticalSnap);

			//Snap to floor for doors
			if (bSnapsToFloor)
			{
				TargetLocation.Z = HitWall.StartLoc.Z;
			}

			// Note: Vertical bounds clamping is handled by child classes (BuildWindowTool, BuildDoorTool)
			// They have access to precise portal dimensions via PreviewWindow/PreviewDoor->Box extent

			// WallGraph-based validation: Check if position is near a junction (3+ walls)
			// Use 15cm threshold - portals shouldn't be placed directly at complex wall intersections
			if (CurrentLot->WallGraph->IsPositionNearJunction(EdgeID, TargetLocation, 15.0f))
			{
				bValidPlacementLocation = false;
				SetActorLocation(TargetLocation);
				UpdateLocation(GetActorLocation());
				return;
			}

			// WallGraph-based validation: Check if portal would extend beyond wall bounds
			// This prevents portals from sticking out past wall endpoints
			// Note: We need a preview portal to check bounds (requires box component)
			// For BuildDoorTool/BuildWindowTool, they will have PreviewDoor/PreviewWindow
			// Base class just validates position, derived classes validate bounds

			if (GetActorLocation() !=  TargetLocation)
			{

				SetActorLocation(TargetLocation);
				UpdateLocation(GetActorLocation());
				//Tell blueprint children we moved successfully
				OnMoved();
				bValidPlacementLocation = true;
				if (SelectPressed)
				{
					Drag_Implementation();
				}
				else
				{
					PreviousLocation = GetActorLocation();
				}
			}
		}
	}
	else
	{
		// No wall was hit - perform fallback trace against grid/tile to show invalid preview
		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
		if (PlayerController)
		{
			FHitResult FallbackHitResult;
			PlayerController->GetHitResultUnderCursor(ECC_GameTraceChannel3, true, FallbackHitResult);

			if (FallbackHitResult.bBlockingHit)
			{
				TargetLocation = FallbackHitResult.Location;
			}
			else
			{
				// Last resort: use the provided location
				TargetLocation = CursorWorldHitResult.Location;
			}
		}
		else
		{
			TargetLocation = CursorWorldHitResult.Location;
		}

		SetActorLocation(TargetLocation);
		UpdateLocation(GetActorLocation());
		bValidPlacementLocation = false;
	}
}

void ABuildPortalTool::BroadcastRelease_Implementation()
{
}


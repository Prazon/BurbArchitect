// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/Roofs/GableRoof.h"
#include "Actors/LotManager.h"
#include "Components/RoofComponent.h"
#include "Components/WallComponent.h"
#include "Components/RoomManagerComponent.h"
#include "Subsystems/BuildServer.h"

AGableRoof::AGableRoof()
{
	// Set default roof type
	RoofDimensions.RoofType = ERoofType::Gable;
}

void AGableRoof::GenerateRoofMesh()
{
	UE_LOG(LogTemp, Warning, TEXT("AGableRoof::GenerateRoofMesh called - bCommitted=%d"), bCommitted);

	if (!RoofMeshComponent)
		return;

	// Set default wall flags for gable roofs if not already set
	// Gable roofs need front and back triangular walls
	if (RoofDimensions.WallFlags == 0)
	{
		RoofDimensions.WallFlags = static_cast<int32>(ERoofWallFlags::Front) | static_cast<int32>(ERoofWallFlags::Back);
	}

	// Setup roof data
	FRoofSegmentData RoofData;
	RoofData.Location = GetActorLocation();
	RoofData.Direction = RoofDirection;
	RoofData.Dimensions = RoofDimensions;
	RoofData.Dimensions.RoofType = ERoofType::Gable;  // Ensure gable type
	RoofData.RoofThickness = RoofThickness;
	RoofData.GableThickness = GableThickness;
	RoofData.SectionIndex = 0;
	RoofData.Level = Level;
	RoofData.bCommitted = false;  // Never commit during mesh generation

	// IMPORTANT: Don't set LotManager during preview to prevent wall generation
	// Walls should only be generated when CommitRoof() is called
	RoofData.LotManager = nullptr;

	// Generate gable roof mesh via component (without walls)
	RoofMeshComponent->RoofData = RoofMeshComponent->GenerateGableRoofMesh(RoofData);

	// Don't store wall indices during preview
	// CreatedWallIndices will be populated when CommitRoof() calls GenerateSupportingWalls()

	// Apply material based on committed state
	if (!bCommitted && LotManager && LotManager->ValidPreviewMaterial)
	{
		// Preview mode: use valid preview material
		RoofMeshComponent->SetMaterial(0, LotManager->ValidPreviewMaterial);
	}
	else if (RoofMaterial)
	{
		// Committed or no preview material: use actual roof material
		RoofMeshComponent->SetMaterial(0, RoofMaterial);
	}
}

void AGableRoof::GenerateSupportingWalls()
{
	if (!LotManager || !LotManager->WallComponent || !RoofMeshComponent)
		return;

	// Don't generate walls in basement (Level 0)
	if (Level == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AGableRoof::GenerateSupportingWalls - Roof placement on basement (Level 0) is not allowed. Skipping wall generation."));
		return;
	}

	// Clean up any existing walls first
	CleanupWalls();

	// Calculate roof vertices
	FRoofVertices RoofVerts = URoofComponent::CalculateRoofVertices(
		GetActorLocation(), RoofDirection, RoofDimensions);

	// Generate perimeter walls (4 walls around the base to form a room)
	RoofMeshComponent->GeneratePerimeterWalls(LotManager, RoofVerts,
		GetActorLocation(), Level, RoofDimensions.RoofType, DefaultWallPattern, CreatedWallIndices);

	// Generate gable end walls (triangular walls at front and back)
	RoofMeshComponent->GenerateGableEndWalls(LotManager, RoofVerts,
		GetActorLocation(), Level, GableThickness, RoofDimensions, DefaultWallPattern, CreatedWallIndices);

	// Store wall indices in component data too
	RoofMeshComponent->RoofData.CreatedWallIndices = CreatedWallIndices;

	// Mark rooms at this roof's level as roof rooms, then generate floors/ceilings
	// BuildServer->BuildWall() has already detected rooms but deferred floor/ceiling generation
	// Now we mark rooms as bIsRoofRoom BEFORE generating, so ceilings are skipped
	if (LotManager->RoomManager)
	{
		TArray<int32> RoofRoomIDs;

		// First pass: Mark all rooms at this level as roof rooms
		for (auto& RoomPair : LotManager->RoomManager->Rooms)
		{
			FRoomData& Room = RoomPair.Value;
			// Check if room is at the same level as this roof
			if (Room.Level == Level)
			{
				Room.bIsRoofRoom = true;
				RoofRoomIDs.Add(Room.RoomID);
				UE_LOG(LogTemp, Log, TEXT("AGableRoof: Marked Room %d at Level %d as roof room"), Room.RoomID, Level);
			}
		}

		// Second pass: Now generate floors/ceilings for marked rooms
		// Since bIsRoofRoom is now true, AutoGenerateRoomFloorsAndCeilings will skip ceilings
		if (RoofRoomIDs.Num() > 0)
		{
			UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
			if (BuildServer)
			{
				for (int32 RoomID : RoofRoomIDs)
				{
					BuildServer->AutoGenerateRoomFloorsAndCeilings(RoomID);
				}
				UE_LOG(LogTemp, Log, TEXT("AGableRoof: Generated floors for %d roof rooms at Level %d (ceilings skipped)"), RoofRoomIDs.Num(), Level);
			}
		}
	}
}

bool AGableRoof::IsScaleToolValid(EScaleToolType ToolType) const
{
	// Gable roofs support all scale tool types
	switch (ToolType)
	{
	case EScaleToolType::Peak:        // Height adjustment
	case EScaleToolType::Edge:        // All edges adjustment
	case EScaleToolType::FrontRake:   // Front overhang
	case EScaleToolType::BackRake:    // Back overhang
	case EScaleToolType::RightEve:    // Right eave
	case EScaleToolType::LeftEve:     // Left eave
	case EScaleToolType::FrontWall:   // Front extent
	case EScaleToolType::BackWall:    // Back extent
	case EScaleToolType::LeftWall:    // Left extent
	case EScaleToolType::RightWall:   // Right extent
	case EScaleToolType::Submit:      // Confirm button
		return true;

	default:
		return false;
	}
}

void AGableRoof::GenerateGableRoofGeometry()
{
	// This is handled by the RoofComponent's GenerateGableRoofMesh method
	// We call it through GenerateRoofMesh() above
}

void AGableRoof::CalculateGableVertices(FRoofVertices& OutVertices)
{
	// Use the static method from URoofComponent
	OutVertices = URoofComponent::CalculateRoofVertices(
		GetActorLocation(), RoofDirection, RoofDimensions);
}
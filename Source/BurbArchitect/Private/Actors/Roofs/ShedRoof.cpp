// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/Roofs/ShedRoof.h"
#include "Actors/LotManager.h"
#include "Components/RoofComponent.h"
#include "Components/WallComponent.h"
#include "Components/GizmoComponent.h"

AShedRoof::AShedRoof()
{
	// Set default roof type
	RoofDimensions.RoofType = ERoofType::Shed;
}

void AShedRoof::GenerateRoofMesh()
{
	if (!RoofMeshComponent)
		return;

	// Set default wall flags for shed roofs if not already set
	// Shed roofs need front tall wall + left and right triangular walls
	if (RoofDimensions.WallFlags == 0)
	{
		RoofDimensions.WallFlags = static_cast<int32>(ERoofWallFlags::Front) |
		                            static_cast<int32>(ERoofWallFlags::Left) |
		                            static_cast<int32>(ERoofWallFlags::Right);
	}

	// Setup roof data
	FRoofSegmentData RoofData;
	RoofData.Location = GetActorLocation();
	RoofData.Direction = RoofDirection;
	RoofData.Dimensions = RoofDimensions;
	RoofData.Dimensions.RoofType = ERoofType::Shed;  // Ensure shed type
	RoofData.RoofThickness = RoofThickness;
	RoofData.GableThickness = GableThickness;  // Used for side walls
	RoofData.SectionIndex = 0;
	RoofData.Level = Level;
	RoofData.bCommitted = false;  // Never commit during mesh generation

	// IMPORTANT: Don't set LotManager during preview to prevent wall generation
	// Walls should only be generated when CommitRoof() is called
	RoofData.LotManager = nullptr;

	// Generate shed roof mesh via component (without walls)
	RoofMeshComponent->RoofData = RoofMeshComponent->GenerateShedRoofMesh(RoofData);

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

void AShedRoof::GenerateSupportingWalls()
{
	if (!LotManager || !LotManager->WallComponent || !RoofMeshComponent)
		return;

	// Don't generate walls in basement (Level 0)
	if (Level == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AShedRoof::GenerateSupportingWalls - Roof placement on basement (Level 0) is not allowed. Skipping wall generation."));
		return;
	}

	// Clean up any existing walls first
	CleanupWalls();

	// Calculate roof vertices
	FRoofVertices RoofVerts = URoofComponent::CalculateRoofVertices(
		GetActorLocation(), RoofDirection, RoofDimensions);

	// Generate only triangular side walls (left and right)
	// No perimeter walls - sloped roof provides coverage
	RoofMeshComponent->GenerateShedEndWalls(LotManager, RoofVerts,
		GetActorLocation(), Level, RoofDimensions, DefaultWallPattern, CreatedWallIndices);

	// Store wall indices in component data too
	RoofMeshComponent->RoofData.CreatedWallIndices = CreatedWallIndices;
}

bool AShedRoof::IsScaleToolValid(EScaleToolType ToolType) const
{
	// Shed roofs have a simpler set of controls
	switch (ToolType)
	{
	case EScaleToolType::Peak:        // Height adjustment (slope height)
	case EScaleToolType::Edge:        // All edges adjustment
	case EScaleToolType::FrontWall:   // Front extent
	case EScaleToolType::BackWall:    // Back extent (determines slope length)
	case EScaleToolType::LeftWall:    // Left extent
	case EScaleToolType::RightWall:   // Right extent
	case EScaleToolType::Submit:      // Confirm button
		return true;

	// Shed roofs don't have separate rake/eave controls
	case EScaleToolType::FrontRake:
	case EScaleToolType::BackRake:
	case EScaleToolType::RightEve:
	case EScaleToolType::LeftEve:
		return false;

	default:
		return false;
	}
}

void AShedRoof::SetupScaleTools()
{
	// Call base implementation first
	ARoofBase::SetupScaleTools();

	// For shed roofs, we might want to adjust some tool positions
	// since the geometry is different (single slope instead of two)
	if (!RoofMeshComponent)
		return;

	FRoofVertices RoofVerts = URoofComponent::CalculateRoofVertices(
		GetActorLocation(), RoofDirection, RoofDimensions);

	// Get actor location for converting world positions to relative positions
	FVector ActorLoc = GetActorLocation();

	// Position height tool at the high edge of the shed
	if (ScaleTool_Height)
	{
		// Shed roofs slope from front to back, so high edge is at front
		FVector HighEdgeCenter = (RoofVerts.GableFrontRight + RoofVerts.GableFrontLeft) * 0.5f;
		HighEdgeCenter.Z += RoofDimensions.Height;
		FVector RelativePos = HighEdgeCenter - ActorLoc;
		ScaleTool_Height->SetRelativeLocation(RelativePos);
		ScaleTool_Height->MovementAxis = FVector::UpVector;
		ScaleTool_Height->bConstrainToAxis = true;
	}
}

void AShedRoof::GenerateShedRoofGeometry()
{
	// This is handled by the RoofComponent's GenerateShedRoofMesh method
	// We call it through GenerateRoofMesh() above
}

void AShedRoof::CalculateShedVertices(FRoofVertices& OutVertices)
{
	// Use the static method from URoofComponent
	OutVertices = URoofComponent::CalculateRoofVertices(
		GetActorLocation(), RoofDirection, RoofDimensions);
}

FVector AShedRoof::GetSlopeDirection() const
{
	// Shed roofs slope from front to back along the roof direction
	return -RoofDirection;  // Negative because it slopes down towards the back
}
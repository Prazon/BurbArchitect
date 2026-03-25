// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/Roofs/HipRoof.h"
#include "Actors/LotManager.h"
#include "Components/RoofComponent.h"
#include "Components/WallComponent.h"

AHipRoof::AHipRoof()
{
	// Set default roof type
	RoofDimensions.RoofType = ERoofType::Hip;
}

void AHipRoof::GenerateRoofMesh()
{
	if (!RoofMeshComponent)
		return;

	// Setup roof data
	FRoofSegmentData RoofData;
	RoofData.Location = GetActorLocation();
	RoofData.Direction = RoofDirection;
	RoofData.Dimensions = RoofDimensions;
	RoofData.Dimensions.RoofType = ERoofType::Hip;  // Ensure hip type

	// Hip roofs don't need any supporting walls (all sides slope)
	// WallFlags should remain 0 (None)
	RoofData.RoofThickness = RoofThickness;
	RoofData.GableThickness = GableThickness;  // Not used for hip, but kept for consistency
	RoofData.SectionIndex = 0;
	RoofData.Level = Level;
	RoofData.bCommitted = false;  // Never commit during mesh generation

	// IMPORTANT: Don't set LotManager during preview to prevent wall generation
	// Walls should only be generated when CommitRoof() is called
	RoofData.LotManager = nullptr;

	// Generate hip roof mesh via component (without walls)
	RoofMeshComponent->RoofData = RoofMeshComponent->GenerateHipRoofMesh(RoofData);

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

void AHipRoof::GenerateSupportingWalls()
{
	if (!LotManager || !LotManager->WallComponent || !RoofMeshComponent)
		return;

	// Clean up any existing walls first
	CleanupWalls();

	// Hip roofs slope on all four sides - no vertical edges that need support walls
	// No wall generation needed for hip roofs
	UE_LOG(LogTemp, Log, TEXT("HipRoof: No supporting walls needed (all sides slope)"));
}

bool AHipRoof::IsScaleToolValid(EScaleToolType ToolType) const
{
	// Hip roofs don't have separate front/back rake controls
	// since all sides slope uniformly
	switch (ToolType)
	{
	case EScaleToolType::Peak:        // Height adjustment
	case EScaleToolType::Edge:        // All edges adjustment (uniform for hip)
	case EScaleToolType::FrontWall:   // Front extent
	case EScaleToolType::BackWall:    // Back extent
	case EScaleToolType::LeftWall:    // Left extent
	case EScaleToolType::RightWall:   // Right extent
	case EScaleToolType::Submit:      // Confirm button
		return true;

	// Hip roofs have uniform overhangs, so individual controls are disabled
	case EScaleToolType::FrontRake:   // Not applicable for hip
	case EScaleToolType::BackRake:    // Not applicable for hip
	case EScaleToolType::RightEve:    // Not applicable for hip
	case EScaleToolType::LeftEve:     // Not applicable for hip
		return false;

	default:
		return false;
	}
}

void AHipRoof::GenerateHipRoofGeometry()
{
	// This is handled by the RoofComponent's GenerateHipRoofMesh method
	// We call it through GenerateRoofMesh() above
}

void AHipRoof::CalculateHipVertices(FRoofVertices& OutVertices)
{
	// Use the static method from URoofComponent
	OutVertices = URoofComponent::CalculateRoofVertices(
		GetActorLocation(), RoofDirection, RoofDimensions);
}

bool AHipRoof::IsPyramidHip() const
{
	// Check if the base is roughly square (forms a pyramid instead of ridge)
	float Length = RoofDimensions.FrontDistance + RoofDimensions.BackDistance;
	float Width = RoofDimensions.LeftDistance + RoofDimensions.RightDistance;

	// Consider it a pyramid if length and width are within 10% of each other
	float Ratio = Length / FMath::Max(Width, 1.0f);
	return (Ratio >= 0.9f && Ratio <= 1.1f);
}
// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FootprintComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/RoofComponent.h"

UFootprintComponent::UFootprintComponent()
{
	// Set this component to not tick
	PrimaryComponentTick.bCanEverTick = false;

	// Initialize default appearance properties
	LineColor = FColor::Green;
	LineThickness = 6.0f;
	DepthPriority = SDPG_Foreground;  // Render on top by default
	ZOffset = 5.0f;  // Small offset to prevent z-fighting
	TranslucencySortPriority = 1000;  // High priority for on-top rendering

	// Line batch component will be created on demand
	LineBatchComponent = nullptr;
}

void UFootprintComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UFootprintComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up footprint lines
	ClearFootprint();

	Super::EndPlay(EndPlayReason);
}

void UFootprintComponent::DrawRectangle(const FVector& FrontLeft, const FVector& FrontRight,
                                        const FVector& BackRight, const FVector& BackLeft)
{
	// Ensure line batch component exists
	EnsureLineBatchComponent();
	if (!LineBatchComponent)
		return;

	// Clear existing lines
	ClearFootprint();

	// Apply Z-offset to prevent z-fighting
	FVector FL = ApplyZOffset(FrontLeft);
	FVector FR = ApplyZOffset(FrontRight);
	FVector BR = ApplyZOffset(BackRight);
	FVector BL = ApplyZOffset(BackLeft);

	// Draw four boundary lines forming the rectangle
	const float LineLifeTime = -1.0f; // Persistent lines
	LineBatchComponent->DrawLine(FL, FR, LineColor, DepthPriority, LineThickness, LineLifeTime);
	LineBatchComponent->DrawLine(FR, BR, LineColor, DepthPriority, LineThickness, LineLifeTime);
	LineBatchComponent->DrawLine(BR, BL, LineColor, DepthPriority, LineThickness, LineLifeTime);
	LineBatchComponent->DrawLine(BL, FL, LineColor, DepthPriority, LineThickness, LineLifeTime);
}

void UFootprintComponent::DrawPolygon(const TArray<FVector>& Points)
{
	// Ensure line batch component exists
	EnsureLineBatchComponent();
	if (!LineBatchComponent)
		return;

	// Need at least 3 points for a valid polygon
	if (Points.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("FootprintComponent: DrawPolygon requires at least 3 points"));
		return;
	}

	// Clear existing lines
	ClearFootprint();

	// Draw lines connecting each point to the next
	const float LineLifeTime = -1.0f; // Persistent lines
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		FVector Start = ApplyZOffset(Points[i]);
		FVector End = ApplyZOffset(Points[(i + 1) % Points.Num()]);  // Wrap around to first point
		LineBatchComponent->DrawLine(Start, End, LineColor, DepthPriority, LineThickness, LineLifeTime);
	}
}

void UFootprintComponent::DrawFromRoofVertices(const FRoofVertices& RoofVerts)
{
	// Draw footprint using the Gable corners (roof base without overhang)
	DrawRectangle(
		RoofVerts.GableFrontLeft,
		RoofVerts.GableFrontRight,
		RoofVerts.GableBackRight,
		RoofVerts.GableBackLeft
	);
}

void UFootprintComponent::DrawFromRoofDimensions(const FVector& Location, const FVector& Direction,
                                                 const FRoofDimensions& Dimensions)
{
	// Calculate roof vertices from dimensions
	FRoofVertices RoofVerts = URoofComponent::CalculateRoofVertices(Location, Direction, Dimensions);

	// Draw footprint from calculated vertices
	DrawFromRoofVertices(RoofVerts);
}

void UFootprintComponent::ClearFootprint()
{
	if (LineBatchComponent)
	{
		LineBatchComponent->Flush();
	}
}

void UFootprintComponent::ShowFootprint()
{
	if (LineBatchComponent)
	{
		LineBatchComponent->SetVisibility(true);
	}
}

void UFootprintComponent::HideFootprint()
{
	if (LineBatchComponent)
	{
		LineBatchComponent->SetVisibility(false);
	}
}

bool UFootprintComponent::IsFootprintVisible() const
{
	return LineBatchComponent && LineBatchComponent->IsVisible();
}

void UFootprintComponent::EnsureLineBatchComponent()
{
	// Create line batch component if it doesn't exist
	if (!LineBatchComponent)
	{
		LineBatchComponent = NewObject<ULineBatchComponent>(this, TEXT("FootprintLineBatch"));
		if (LineBatchComponent)
		{
			// Set high translucency sort priority to render after everything
			LineBatchComponent->SetTranslucentSortPriority(TranslucencySortPriority);

			// Register the component so it renders
			LineBatchComponent->RegisterComponent();

			// Disable custom depth - footprint lines are for visualization only
			LineBatchComponent->SetRenderCustomDepth(false);

			// Exclude from all rendering systems except direct view
			LineBatchComponent->SetVisibleInSceneCaptureOnly(false);  // Exclude from scene captures
			LineBatchComponent->bVisibleInReflectionCaptures = false;  // Exclude from reflection captures
			LineBatchComponent->bVisibleInRayTracing = false;          // Exclude from ray tracing
			LineBatchComponent->bVisibleInRealTimeSkyCaptures = false; // Exclude from real-time sky captures
		}
	}
}

FVector UFootprintComponent::ApplyZOffset(const FVector& Vertex) const
{
	return FVector(Vertex.X, Vertex.Y, Vertex.Z + ZOffset);
}

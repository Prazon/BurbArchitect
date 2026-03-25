// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/GizmoComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UGizmoComponent::UGizmoComponent()
{
	// Set this component to be ticked every frame. You can turn this off to improve performance if you don't need it.
	PrimaryComponentTick.bCanEverTick = false;

	// Default properties
	GizmoType = EGizmoType::None;
	bIsHovered = false;
	bIsDragging = false;
	bConstrainToAxis = false;
	bAutoHide = true;
	MovementAxis = FVector::ForwardVector;
	SetVisibility(false);
	SetVisibleInRayTracing(false);
	SetVisibleInSceneCaptureOnly(false);
	SetHiddenInGame(true);

	// Default colors
	DefaultColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	HoveredColor = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f);
	ActiveColor = FLinearColor(0.0f, 1.0f, 0.0f, 1.0f);

	// Setup collision
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);

	// Note: SetHiddenInGame remains true (set earlier) until ShowGizmo() is called
	// This prevents gizmos from being visible until explicitly shown
}

void UGizmoComponent::BeginPlay()
{
	Super::BeginPlay();

	// Bind click events
	OnClicked.AddDynamic(this, &UGizmoComponent::HandleClicked);
	OnReleased.AddDynamic(this, &UGizmoComponent::HandleReleased);
	OnBeginCursorOver.AddDynamic(this, &UGizmoComponent::HandleBeginCursorOver);
	OnEndCursorOver.AddDynamic(this, &UGizmoComponent::HandleEndCursorOver);

	// Store original scale for hover effects
	OriginalScale = GetRelativeScale3D();

	// Create dynamic material for color changes
	if (GetMaterial(0))
	{
		DynamicMaterial = CreateDynamicMaterialInstance(0);
		ApplyColor(DefaultColor);
	}

	// Auto-hide if enabled
	if (bAutoHide)
	{
		HideGizmo();
	}

	UE_LOG(LogTemp, Log, TEXT("GizmoComponent::BeginPlay - '%s' initialized (GizmoType=%d, HasMesh=%d, AutoHide=%d)"),
		*GetName(), (int32)GizmoType, GetStaticMesh() != nullptr, bAutoHide);
}

void UGizmoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind events
	OnClicked.RemoveDynamic(this, &UGizmoComponent::HandleClicked);
	OnReleased.RemoveDynamic(this, &UGizmoComponent::HandleReleased);
	OnBeginCursorOver.RemoveDynamic(this, &UGizmoComponent::HandleBeginCursorOver);
	OnEndCursorOver.RemoveDynamic(this, &UGizmoComponent::HandleEndCursorOver);

	Super::EndPlay(EndPlayReason);
}

void UGizmoComponent::ActivateGizmo()
{
	UpdateAppearance();
}

void UGizmoComponent::DeactivateGizmo()
{
	bIsDragging = false;
	UpdateAppearance();
}

void UGizmoComponent::ShowGizmo()
{
	SetVisibility(true);
	SetHiddenInGame(false);  // Critical: Must be false for clicks to work
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	
	UE_LOG(LogTemp, Log, TEXT("GizmoComponent::ShowGizmo - '%s' shown (Visible=%d, HiddenInGame=%d, HasMesh=%d, CollisionEnabled=%d)"),
		*GetName(), IsVisible(), bHiddenInGame, GetStaticMesh() != nullptr, GetCollisionEnabled() != ECollisionEnabled::NoCollision);
}

void UGizmoComponent::HideGizmo()
{
	SetVisibility(false);
	SetHiddenInGame(true);   // Hide from game when not needed
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UGizmoComponent::SetDragging(bool bInDragging)
{
	bIsDragging = bInDragging;
	if (!bIsDragging)
	{
		DragStartLocation = FVector::ZeroVector;
		CurrentDragLocation = FVector::ZeroVector;
	}
}

void UGizmoComponent::StartDrag(const FVector& StartLocation)
{
	bIsDragging = true;
	DragStartLocation = StartLocation;
	CurrentDragLocation = StartLocation;

	Activate();
}

void UGizmoComponent::UpdateDrag(const FVector& CurrentLocation)
{
	if (bIsDragging)
	{
		CurrentDragLocation = CurrentLocation;

		FVector Delta = GetConstrainedDragDelta();
		OnGizmoDragged.Broadcast(this, Delta);
	}
}

void UGizmoComponent::EndDrag()
{
	if (bIsDragging)
	{
		bIsDragging = false;
		OnGizmoReleased.Broadcast(this);

		// Reset drag locations
		DragStartLocation = FVector::ZeroVector;
		CurrentDragLocation = FVector::ZeroVector;

		Deactivate();
	}
}

FVector UGizmoComponent::GetDragDelta() const
{
	if (bIsDragging)
	{
		return CurrentDragLocation - DragStartLocation;
	}
	return FVector::ZeroVector;
}

FVector UGizmoComponent::GetConstrainedDragDelta() const
{
	FVector Delta = GetDragDelta();

	if (bConstrainToAxis && !MovementAxis.IsZero())
	{
		// Project delta onto movement axis
		FVector NormalizedAxis = MovementAxis.GetSafeNormal();
		float ProjectedDistance = FVector::DotProduct(Delta, NormalizedAxis);
		return NormalizedAxis * ProjectedDistance;
	}

	return Delta;
}

void UGizmoComponent::UpdateAppearance()
{
	if (!DynamicMaterial)
		return;

	if (IsActive())
	{
		ApplyColor(ActiveColor);
	}
	else if (bIsHovered)
	{
		ApplyColor(HoveredColor);
	}
	else
	{
		ApplyColor(DefaultColor);
	}
}

void UGizmoComponent::SetColors(const FLinearColor& InDefaultColor, const FLinearColor& InHoveredColor, const FLinearColor& InActiveColor)
{
	DefaultColor = InDefaultColor;
	HoveredColor = InHoveredColor;
	ActiveColor = InActiveColor;

	UpdateAppearance();
}

void UGizmoComponent::HandleClicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	if (TouchedComponent == this)
	{
		UE_LOG(LogTemp, Log, TEXT("GizmoComponent::HandleClicked - Gizmo '%s' clicked with %s"), 
			*GetName(), *ButtonPressed.ToString());
		Activate();
		OnGizmoClicked.Broadcast(this, ButtonPressed);
	}
}

void UGizmoComponent::HandleReleased(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	if (TouchedComponent == this)
	{
		// End the drag operation (sets bIsDragging = false and broadcasts OnGizmoReleased)
		EndDrag();
	}
}

void UGizmoComponent::HandleBeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	if (TouchedComponent == this)
	{
		bIsHovered = true;
		UpdateAppearance();

		// Scale up by 1% on hover
		FVector HoverScale = OriginalScale * 1.01f;
		SetRelativeScale3D(HoverScale);

		OnGizmoHovered.Broadcast(this);
	}
}

void UGizmoComponent::HandleEndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	if (TouchedComponent == this)
	{
		bIsHovered = false;
		UpdateAppearance();

		// Restore original scale on unhover
		SetRelativeScale3D(OriginalScale);

		OnGizmoUnhovered.Broadcast(this);
	}
}

void UGizmoComponent::ApplyColor(const FLinearColor& Color)
{
	if (DynamicMaterial)
	{
		// Common material parameter names for color
		DynamicMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		DynamicMaterial->SetVectorParameterValue(TEXT("BaseColor"), Color);
		DynamicMaterial->SetVectorParameterValue(TEXT("Emissive"), Color * 0.5f);
	}
}
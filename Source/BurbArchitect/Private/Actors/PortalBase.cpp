// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/PortalBase.h"
#include "Components/PortalBoxComponent.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"


// Sets default values
// IMPORTANT: Default PortalSize/PortalOffset are ZeroVector to ensure replication triggers.
// If CDO default matches the actual value, Unreal's replication won't send the property.
APortalBase::APortalBase(): CurrentLot(nullptr), CurrentWallComponent(nullptr), PortalSize(FVector2D::ZeroVector), PortalOffset(FVector2D::ZeroVector), bIsSelected(false)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Create root scene component
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	RootComponent = RootSceneComponent;

	// Create the box component for visualization and overlap detection
	Box = CreateDefaultSubobject<UPortalBoxComponent>(TEXT("PortalBox"));
	// Set initial size to a small default - real size will be set via replication or command
	// Using a small non-zero value to avoid degenerate box
	Box->SetBoxExtent(FVector(1.0f, 1.0f, 1.0f));

	// Configure for editor and runtime
	Box->SetHiddenInGame(true);
	Box->SetVisibility(true);

	// Enable collision for overlap detection when placing portals
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Overlap);

	// Attach to root component
	Box->SetupAttachment(RootComponent);

	// Enable movement replication for transform sync
	SetReplicateMovement(true);
}

void APortalBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APortalBase, PortalSize);
	DOREPLIFETIME(APortalBase, PortalOffset);
}

void APortalBase::PostNetInit()
{
	Super::PostNetInit();

	// Called on clients after all initial replicated properties have been received
	// This is the reliable place to apply replicated visual properties
	UE_LOG(LogTemp, Log, TEXT("PortalBase::PostNetInit - Applying replicated properties on client"));
	ApplyReplicatedProperties();
}

// Called when the game starts or when spawned
void APortalBase::BeginPlay()
{
	Super::BeginPlay();

	// Disable custom depth on all primitive components - portals are permanent geometry
	// This applies to any meshes added by Blueprint child classes (doors, windows, etc.)
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetRenderCustomDepth(false);
		}
	}
}

void APortalBase::ApplyReplicatedProperties()
{
	// Apply portal size if it was replicated
	if (Box && PortalSize.X > 0.0f && PortalSize.Y > 0.0f)
	{
		Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
		UE_LOG(LogTemp, Log, TEXT("PortalBase::ApplyReplicatedProperties - Applied size (%.1f, %.1f)"), PortalSize.X, PortalSize.Y);
	}

	// Apply portal offset if it was replicated
	if (Box && !PortalOffset.IsZero())
	{
		Box->SetRelativeLocation(FVector(PortalOffset.X, 0.0f, PortalOffset.Y));
		UE_LOG(LogTemp, Log, TEXT("PortalBase::ApplyReplicatedProperties - Applied offset (%.1f, %.1f)"), PortalOffset.X, PortalOffset.Y);
	}
}

void APortalBase::OnRep_PortalSize()
{
	// Update box component to match replicated portal size
	if (Box && PortalSize.X > 0.0f && PortalSize.Y > 0.0f)
	{
		Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
		UE_LOG(LogTemp, Log, TEXT("PortalBase::OnRep_PortalSize - Applied size (%.1f, %.1f)"), PortalSize.X, PortalSize.Y);
	}
}

void APortalBase::OnRep_PortalOffset()
{
	// Update box component to match replicated portal offset
	if (Box)
	{
		Box->SetRelativeLocation(FVector(PortalOffset.X, 0.0f, PortalOffset.Y));
		UE_LOG(LogTemp, Log, TEXT("PortalBase::OnRep_PortalOffset - Applied offset (%.1f, %.1f)"), PortalOffset.X, PortalOffset.Y);
	}
}

// Called every frame
void APortalBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Handle click detection for selection
	UWorld* World = GetWorld();
	if (!World)
		return;

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
		return;

	// Check if left mouse button was just pressed this frame
	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		// Perform line trace from mouse cursor
		FHitResult HitResult;
		bool bHit = PC->GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

		if (bHit)
		{
			// Check if the hit actor is this portal
			AActor* HitActor = HitResult.GetActor();
			bool bClickedOnThisPortal = (HitActor == this);

			if (bIsSelected)
			{
				// We're selected - check if clicked away to unselect
				if (!bClickedOnThisPortal)
				{
					UE_LOG(LogTemp, Log, TEXT("PortalBase: Clicked away from portal, unselecting"));
					Unselect();
				}
			}
			else
			{
				// We're NOT selected - check if clicked on portal to select
				if (bClickedOnThisPortal)
				{
					UE_LOG(LogTemp, Log, TEXT("PortalBase: Clicked on portal, selecting"));
					Select();
				}
			}
		}
	}
}

#if WITH_EDITOR
void APortalBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Update box component to match portal size
	if (Box)
	{
		// Check both Property and MemberProperty since PortalSize is a struct (FVector2D)
		// When you edit X or Y, the MemberProperty will be PortalSize
		FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		FName MemberPropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

		if (PropertyName == GET_MEMBER_NAME_CHECKED(APortalBase, PortalSize) ||
		    MemberPropertyName == GET_MEMBER_NAME_CHECKED(APortalBase, PortalSize))
		{
			// Update box extent to match portal dimensions (BoxExtent uses half-extents)
			Box->SetBoxExtent(FVector(PortalSize.X / 2.0f, 1.0f, PortalSize.Y / 2.0f));
			Box->RecreatePhysicsState();
			Box->MarkRenderStateDirty();
		}
	}
}

void APortalBase::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
}
#endif

void APortalBase::DrawPortal(UCanvas* Canvas, FVector2D PositionOnTexture)
{
	if (!Canvas || !Box)
	{
		return;
	}

	// Calculate half of the portal size for centering
	const double HalfPortalSizeX = PortalSize.X / 2.0;
	const double HalfPortalSizeY = PortalSize.Y / 2.0;

	// Calculate centered screen position
	// Subtract half the portal size to center the texture
	const double CenteredX = PositionOnTexture.X - HalfPortalSizeX;
	double CenteredY = PositionOnTexture.Y - HalfPortalSizeY;

	// Adjust Y position by subtracting the Box's Z location
	CenteredY -= Box->GetRelativeLocation().Z;

	// Create the final screen position
	const FVector2D ScreenPosition(CenteredX, CenteredY);

	// Draw the texture on the canvas using K2_DrawTexture
	// nullptr for RenderTexture will use the default white texture
	Canvas->K2_DrawTexture(
		nullptr,                              // RenderTexture (null = default white texture)
		ScreenPosition,                       // ScreenPosition
		PortalSize,                           // ScreenSize
		FVector2D(0.0, 0.0),                 // CoordinatePosition (default)
		FVector2D(1.0, 1.0),                 // CoordinateSize (default)
		FLinearColor::White,                  // RenderColor
		EBlendMode::BLEND_Opaque,            // BlendMode
		0.0f,                                 // Rotation
		FVector2D(0.5, 0.5)                  // PivotPoint
	);
}

// ==================== SELECTION ====================

void APortalBase::Select()
{
	if (bIsSelected)
		return;

	bIsSelected = true;

	// Enable custom depth for selection highlight on all primitive components
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetRenderCustomDepth(true);
			PrimComp->SetCustomDepthStencilValue(1);  // Selected stencil value
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PortalBase: Portal selected"));
}

void APortalBase::Unselect()
{
	if (!bIsSelected)
		return;

	bIsSelected = false;

	// Disable custom depth on all primitive components
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
	{
		if (PrimComp)
		{
			PrimComp->SetRenderCustomDepth(false);
			PrimComp->SetCustomDepthStencilValue(0);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PortalBase: Portal unselected"));
}

void APortalBase::ToggleSelection()
{
	if (bIsSelected)
		Unselect();
	else
		Select();
}

// ==================== IDELETABLE INTERFACE ====================

bool APortalBase::CanBeDeleted_Implementation() const
{
	// Portals can always be deleted once placed
	return true;
}

void APortalBase::OnDeleted_Implementation()
{
	// Unselect before deletion
	const_cast<APortalBase*>(this)->Unselect();

	// Note: Additional cleanup could be added here if portals need to
	// update wall components or remove holes from walls
	UE_LOG(LogTemp, Log, TEXT("PortalBase: OnDeleted - Unselected portal"));
}

bool APortalBase::IsSelected_Implementation() const
{
	// Portal is selected when bIsSelected is true
	return bIsSelected;
}


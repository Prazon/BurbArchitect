// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetEditors/SPortalItemViewport.h"
#include "AssetEditors/PortalItemEditorViewportClient.h"
#include "AdvancedPreviewScene.h"
#include "Widgets/Docking/SDockTab.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"

void SPortalItemViewport::Construct(const FArguments& InArgs)
{
	// Create the advanced preview scene
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	PreviewScene->SetFloorVisibility(false); // Hide default floor, we'll show grid instead

	// Add directional light to the preview scene
	UDirectionalLightComponent* DirectionalLight = NewObject<UDirectionalLightComponent>();
	DirectionalLight->Intensity = 3.0f;
	DirectionalLight->LightColor = FColor::White;
	PreviewScene->AddComponent(DirectionalLight, FTransform(FRotator(-45.0f, -45.0f, 0.0f)));

	// Add sky light for ambient lighting
	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>();
	SkyLight->Intensity = 1.0f;
	SkyLight->SetCubemapBlend(nullptr, nullptr, 0.0f);
	PreviewScene->AddComponent(SkyLight, FTransform::Identity);

	// Call parent Construct
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SPortalItemViewport::~SPortalItemViewport()
{
	// Cleanup viewport client
	if (ViewportClient.IsValid())
	{
		ViewportClient.Reset();
	}

	// Cleanup preview scene
	if (PreviewScene.IsValid())
	{
		PreviewScene.Reset();
	}
}

TSharedRef<FEditorViewportClient> SPortalItemViewport::MakeEditorViewportClient()
{
	// Create the viewport client
	ViewportClient = MakeShared<FPortalItemEditorViewportClient>(*PreviewScene.Get(), SharedThis(this));

	// Set the initial portal item if we have one
	if (PortalItemAsset.IsValid())
	{
		ViewportClient->SetPortalItem(PortalItemAsset.Get());
	}

	return ViewportClient.ToSharedRef();
}

void SPortalItemViewport::SetPortalItem(UObject* InPortalItem)
{
	PortalItemAsset = InPortalItem;

	if (ViewportClient.IsValid())
	{
		ViewportClient->SetPortalItem(InPortalItem);
	}
}

void SPortalItemViewport::RefreshViewport()
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->UpdatePreviewPortal();
	}
}

// ICommonEditorViewportToolbarInfoProvider interface
TSharedRef<SEditorViewport> SPortalItemViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SPortalItemViewport::GetExtenders() const
{
	return MakeShared<FExtender>();
}

void SPortalItemViewport::OnFloatingButtonClicked()
{
	// No-op for now
}

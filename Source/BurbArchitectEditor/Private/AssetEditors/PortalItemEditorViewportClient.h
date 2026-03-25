// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class APortalBase;
class UWindowItem;
class UDoorItem;

/**
 * Viewport client for the portal item editor
 * Handles rendering, camera controls, and preview scene management
 */
class FPortalItemEditorViewportClient : public FEditorViewportClient
{
public:
	FPortalItemEditorViewportClient(FAdvancedPreviewScene& InPreviewScene, const TWeakPtr<class SEditorViewport>& InEditorViewportWidget = nullptr);
	virtual ~FPortalItemEditorViewportClient();

	// FEditorViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;

	/** Set the portal item being previewed (can be UWindowItem or UDoorItem) */
	void SetPortalItem(UObject* InPortalItem);

	/** Update the preview portal actor with current data asset values */
	void UpdatePreviewPortal();

	/** Get the preview portal actor */
	APortalBase* GetPreviewPortal() const { return PreviewPortalActor; }

private:
	/** The preview scene */
	FAdvancedPreviewScene* AdvancedPreviewScene;

	/** The portal actor being previewed */
	TObjectPtr<APortalBase> PreviewPortalActor;

	/** The data asset being edited (UWindowItem or UDoorItem) */
	TWeakObjectPtr<UObject> PortalItemAsset;
};

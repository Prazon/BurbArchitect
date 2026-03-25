// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class FPortalItemEditorViewportClient;
class FAdvancedPreviewScene;

/**
 * Slate viewport widget for portal item editor
 * Displays the 3D preview of the portal with camera controls
 */
class SPortalItemViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SPortalItemViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SPortalItemViewport();

	/** Set the portal item to preview */
	void SetPortalItem(UObject* InPortalItem);

	/** Update the preview with current data asset values */
	void RefreshViewport();

	/** Get the viewport client */
	TSharedPtr<FPortalItemEditorViewportClient> GetViewportClient() const { return ViewportClient; }

	// ICommonEditorViewportToolbarInfoProvider interface
	virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;

protected:
	// SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	/** The preview scene */
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;

	/** The viewport client */
	TSharedPtr<FPortalItemEditorViewportClient> ViewportClient;

	/** The portal item being previewed */
	TWeakObjectPtr<UObject> PortalItemAsset;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class SPortalItemViewport;
class IDetailsView;
class UWindowItem;
class UDoorItem;

/**
 * Asset editor toolkit for WindowItem and DoorItem data assets
 * Provides a custom editor window with properties panel and 3D preview viewport
 */
class FPortalItemEditorToolkit : public FAssetEditorToolkit
{
public:
	/** Initialize the editor */
	void InitPortalItemEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* InPortalItem);

	// IToolkit interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	// FAssetEditorToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

private:
	/** Create the details view widget */
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);

	/** Create the viewport widget */
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);

	/** Called when a property is changed */
	void OnPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** The portal item being edited (can be UWindowItem or UDoorItem) */
	TWeakObjectPtr<UObject> PortalItemAsset;

	/** Details panel for editing properties */
	TSharedPtr<IDetailsView> DetailsView;

	/** Viewport for previewing the portal */
	TSharedPtr<SPortalItemViewport> ViewportWidget;

	/** Tab identifiers */
	static const FName DetailsTabId;
	static const FName ViewportTabId;
};

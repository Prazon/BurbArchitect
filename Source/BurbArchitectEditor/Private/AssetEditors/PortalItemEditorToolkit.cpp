// Fill out your copyright notice in the Description page of Project Settings.

#include "AssetEditors/PortalItemEditorToolkit.h"
#include "AssetEditors/SPortalItemViewport.h"
#include "Data/WindowItem.h"
#include "Data/DoorItem.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "PortalItemEditor"

const FName FPortalItemEditorToolkit::DetailsTabId(TEXT("PortalItemEditor_Details"));
const FName FPortalItemEditorToolkit::ViewportTabId(TEXT("PortalItemEditor_Viewport"));

void FPortalItemEditorToolkit::InitPortalItemEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* InPortalItem)
{
	PortalItemAsset = InPortalItem;

	// Create details view
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(InPortalItem);

	// Register property change callback
	DetailsView->OnFinishedChangingProperties().AddRaw(this, &FPortalItemEditorToolkit::OnPropertyChanged);

	// Define the layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_PortalItemEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.3f)
				->AddTab(DetailsTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.7f)
				->AddTab(ViewportTabId, ETabState::OpenedTab)
			)
		);

	// Initialize the asset editor
	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		TEXT("PortalItemEditorApp"),
		StandaloneDefaultLayout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InPortalItem
	);
}

FName FPortalItemEditorToolkit::GetToolkitFName() const
{
	return FName("PortalItemEditor");
}

FText FPortalItemEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Portal Item Editor");
}

FString FPortalItemEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Portal Item ").ToString();
}

FLinearColor FPortalItemEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

void FPortalItemEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_PortalItemEditor", "Portal Item Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FPortalItemEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FPortalItemEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FPortalItemEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(ViewportTabId);
}

TSharedRef<SDockTab> FPortalItemEditorToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == DetailsTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTitle", "Details"))
		[
			DetailsView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPortalItemEditorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == ViewportTabId);

	// Create viewport widget
	ViewportWidget = SNew(SPortalItemViewport);

	// Set the portal item to preview
	if (PortalItemAsset.IsValid())
	{
		ViewportWidget->SetPortalItem(PortalItemAsset.Get());
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("ViewportTitle", "Viewport"))
		[
			ViewportWidget.ToSharedRef()
		];
}

void FPortalItemEditorToolkit::OnPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	// Check if any relevant property was changed
	if (PropertyChangedEvent.Property && ViewportWidget.IsValid())
	{
		FName PropertyName = PropertyChangedEvent.Property->GetFName();
		FName MemberPropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

		// Check for PortalSize change (property can be the struct itself or X/Y members)
		// Or PortalOffset change (property can be the struct itself or X/Y/Z members)
		// Or mesh property changes (WindowMesh, DoorStaticMesh, DoorFrameMesh)
		if (PropertyName == TEXT("PortalSize") || MemberPropertyName == TEXT("PortalSize") ||
		    PropertyName == TEXT("PortalOffset") || MemberPropertyName == TEXT("PortalOffset") ||
		    PropertyName == TEXT("WindowMesh") ||
		    PropertyName == TEXT("DoorStaticMesh") || PropertyName == TEXT("DoorFrameMesh"))
		{
			// Update the viewport preview
			ViewportWidget->RefreshViewport();
		}
	}
}

#undef LOCTEXT_NAMESPACE

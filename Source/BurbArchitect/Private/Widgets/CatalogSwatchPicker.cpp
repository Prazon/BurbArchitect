// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/CatalogSwatchPicker.h"
#include "Widgets/CatalogItemCard.h"
#include "Widgets/SwatchEntry.h"
#include "Data/WallPattern.h"
#include "Data/FloorPattern.h"
#include "Actors/BuildTool.h"
#include "Actors/BuildTools/WallPatternTool.h"
#include "Actors/BuildTools/BuildFloorTool.h"
#include "Actors/BurbPawn.h"
#include "Controller/BurbPlayerController.h"
#include "Components/TileView.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"

void UCatalogSwatchPicker::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind to tile view selection changed events
	// Note: We use OnItemSelectionChanged instead of OnItemClicked because
	// the CatalogItemCard's Button consumes the click event before the TileView
	// can fire OnItemClicked. SetSelectedItem() is called from CatalogItemCard
	// which fires OnItemSelectionChanged.
	if (SwatchGrid)
	{
		SwatchGrid->OnItemSelectionChanged().AddUObject(this, &UCatalogSwatchPicker::OnSwatchSelectionChanged);
	}
}

void UCatalogSwatchPicker::ShowAtPosition(const FVector2D& ScreenPosition, UArchitectureItem* Pattern)
{
	UE_LOG(LogTemp, Warning, TEXT("ShowAtPosition called with pattern: %s"), Pattern ? *Pattern->GetName() : TEXT("NULL"));

	if (!Pattern)
	{
		return;
	}

	CurrentPattern = Pattern;

	// Populate swatches
	PopulateSwatches();

	// Make visible
	SetVisibility(ESlateVisibility::Visible);
	UE_LOG(LogTemp, Warning, TEXT("Swatch picker made visible"));
}

void UCatalogSwatchPicker::ClosePicker()
{
	// Hide the picker
	SetVisibility(ESlateVisibility::Collapsed);

	CurrentPattern = nullptr;

	// Clear swatch grid
	if (SwatchGrid)
	{
		SwatchGrid->ClearListItems();
	}

	// Clear button index map
	SwatchButtonIndexMap.Empty();
	SwatchCardIndexMap.Empty();
}

ABuildTool* UCatalogSwatchPicker::GetActiveBuildTool() const
{
	// Get the player controller
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ABurbPlayerController* BurbPC = Cast<ABurbPlayerController>(PC))
		{
			// Get the pawn and access its current build tool
			if (BurbPC->CurrentPawn)
			{
				return BurbPC->CurrentPawn->CurrentBuildTool;
			}
		}
	}

	return nullptr;
}

void UCatalogSwatchPicker::PopulateSwatches()
{
	UE_LOG(LogTemp, Warning, TEXT("PopulateSwatches called"));

	if (!SwatchGrid)
	{
		UE_LOG(LogTemp, Error, TEXT("SwatchGrid is NULL!"));
		return;
	}

	if (!CurrentPattern)
	{
		UE_LOG(LogTemp, Error, TEXT("CurrentPattern is NULL!"));
		return;
	}

	// Clear existing swatch entries
	SwatchGrid->ClearListItems();
	SwatchButtonIndexMap.Empty();
	SwatchCardIndexMap.Empty();

	// Get the color swatches array from the pattern
	TArray<FLinearColor> ColorSwatches;

	if (UWallPattern* WallPattern = Cast<UWallPattern>(CurrentPattern))
	{
		UE_LOG(LogTemp, Warning, TEXT("Pattern is WallPattern - bUseColourSwatches: %d, Num swatches: %d"),
			WallPattern->bUseColourSwatches, WallPattern->ColourSwatches.Num());

		if (!WallPattern->bUseColourSwatches || WallPattern->ColourSwatches.Num() == 0)
		{
			return;
		}
		ColorSwatches = WallPattern->ColourSwatches;
	}
	else if (UFloorPattern* FloorPattern = Cast<UFloorPattern>(CurrentPattern))
	{
		UE_LOG(LogTemp, Warning, TEXT("Pattern is FloorPattern - bUseColourSwatches: %d, Num swatches: %d"),
			FloorPattern->bUseColourSwatches, FloorPattern->ColourSwatches.Num());

		if (!FloorPattern->bUseColourSwatches || FloorPattern->ColourSwatches.Num() == 0)
		{
			return;
		}
		ColorSwatches = FloorPattern->ColourSwatches;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Pattern is neither WallPattern nor FloorPattern!"));
		return;
	}

	// First, add the default non-swatched pattern (white = no tint) at index 0
	USwatchEntry* DefaultEntry = NewObject<USwatchEntry>(this);
	DefaultEntry->Pattern = CurrentPattern;
	DefaultEntry->Color = FLinearColor::White;
	DefaultEntry->SwatchIndex = 0;
	SwatchGrid->AddItem(DefaultEntry);
	UE_LOG(LogTemp, Warning, TEXT("Added default swatch at index 0 (white - no tint)"));

	// Create entry objects for each color swatch (starting at index 1)
	UE_LOG(LogTemp, Warning, TEXT("Creating %d color swatch entries"), ColorSwatches.Num());
	for (int32 i = 0; i < ColorSwatches.Num(); ++i)
	{
		USwatchEntry* Entry = NewObject<USwatchEntry>(this);
		Entry->Pattern = CurrentPattern;
		Entry->Color = ColorSwatches[i];
		Entry->SwatchIndex = i + 1; // Offset by 1 because index 0 is the default swatch
		SwatchGrid->AddItem(Entry);
		UE_LOG(LogTemp, Warning, TEXT("Added swatch %d with color: %s"), i + 1, *ColorSwatches[i].ToString());
	}
}

void UCatalogSwatchPicker::OnSwatchSelectionChanged(UObject* Item)
{
	USwatchEntry* SwatchEntry = Cast<USwatchEntry>(Item);
	if (!SwatchEntry)
	{
		// This can happen when selection is cleared, just ignore
		UE_LOG(LogTemp, Log, TEXT("OnSwatchSelectionChanged: Item is not a SwatchEntry (selection cleared?)"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("OnSwatchSelectionChanged: Swatch index %d selected, Color: %s"),
		SwatchEntry->SwatchIndex, *SwatchEntry->Color.ToString());

	// Get the active build tool
	ABuildTool* ActiveTool = GetActiveBuildTool();
	if (!ActiveTool)
	{
		UE_LOG(LogTemp, Error, TEXT("OnSwatchSelectionChanged: No active build tool found!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("OnSwatchSelectionChanged: Active tool: %s"), *ActiveTool->GetName());

	// Set the selected swatch index on the appropriate tool
	if (AWallPatternTool* WallTool = Cast<AWallPatternTool>(ActiveTool))
	{
		WallTool->SelectedSwatchIndex = SwatchEntry->SwatchIndex;
		UE_LOG(LogTemp, Warning, TEXT("OnSwatchSelectionChanged: Set WallPatternTool->SelectedSwatchIndex to %d"), SwatchEntry->SwatchIndex);
	}
	else if (ABuildFloorTool* FloorTool = Cast<ABuildFloorTool>(ActiveTool))
	{
		FloorTool->SelectedSwatchIndex = SwatchEntry->SwatchIndex;
		UE_LOG(LogTemp, Warning, TEXT("OnSwatchSelectionChanged: Set BuildFloorTool->SelectedSwatchIndex to %d"), SwatchEntry->SwatchIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("OnSwatchSelectionChanged: Active tool is neither WallPatternTool nor BuildFloorTool (it's %s)"),
			*ActiveTool->GetClass()->GetName());
	}

	// Fire Blueprint event
	OnSwatchSelected(SwatchEntry->SwatchIndex);

	// Optionally close the picker after selection
	// ClosePicker();
}


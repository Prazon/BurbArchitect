// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ALotManager;

/**
 * Slate widget for visualizing room detection results in 2D top-down view.
 * Displays tiles colored by RoomID and walls as black lines for debugging.
 *
 * Visualization:
 * - Each tile is drawn as a colored square based on its RoomID
 * - RoomID 0 (outside) = dark gray
 * - Other rooms get deterministic colors using hash-based HSV generation
 * - Walls are drawn as thick black lines between tiles (queries WallGraph)
 * - Grid lines show tile boundaries
 * - Auto-scales to fit viewport while maintaining aspect ratio
 *
 * Controls:
 * - < > buttons to navigate between levels/floors
 * - Refresh button to force repaint
 * - Level indicator shows current floor
 *
 * Usage:
 * - Best accessed via URoomDebugWidget wrapper for UMG integration
 * - Can also be used directly in Slate-based debug tools
 */
class BURBARCHITECT_API SRoomDebugWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoomDebugWidget)
		: _LotManager(nullptr)
		, _CurrentLevel(0)
		{}
		SLATE_ARGUMENT(ALotManager*, LotManager)
		SLATE_ARGUMENT(int32, CurrentLevel)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** SWidget interface */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Set the lot manager to visualize */
	void SetLotManager(ALotManager* InLotManager);

	/** Set the current level/floor to display */
	void SetCurrentLevel(int32 InLevel);

	/** Refresh the visualization (call after room detection runs) */
	void Refresh();

private:
	/** Generate a unique color for a given RoomID */
	FLinearColor GetColorForRoom(int32 RoomID) const;

	/** Draw a single tile */
	void DrawTile(int32 Row, int32 Col, int32 Level, const FGeometry& AllottedGeometry,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Draw walls between tiles */
	void DrawWalls(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Convert grid coordinates to screen space */
	FVector2D GridToScreen(int32 Row, int32 Col, const FGeometry& AllottedGeometry) const;

	/** Handle level selection changed */
	void OnLevelChanged(int32 NewLevel);

	/** Handle refresh button clicked */
	FReply OnRefreshClicked();

private:
	/** The lot manager we're visualizing */
	TWeakObjectPtr<ALotManager> LotManager;

	/** Current level/floor being displayed */
	int32 CurrentLevel;

	/** Cached grid dimensions for rendering */
	int32 CachedGridSizeX;
	int32 CachedGridSizeY;

	/** Tile size in screen space */
	float TileScreenSize;

	/** Padding around the grid */
	float GridPadding;
};

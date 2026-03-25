// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ALotManager;

/**
 * Minimal Slate widget that renders only the room grid visualization.
 * No controls - just the canvas. Use URoomDebugCanvas for UMG integration.
 *
 * Features:
 * - Tiles colored by RoomID (unique color per room, dark gray for outside)
 * - Walls drawn as black lines on grid edges
 * - Auto-scales to fit available space
 */
class BURBARCHITECT_API SRoomDebugCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRoomDebugCanvas)
		: _LotManager(nullptr)
		, _CurrentLevel(0)
	{}
		SLATE_ARGUMENT(ALotManager*, LotManager)
		SLATE_ARGUMENT(int32, CurrentLevel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(200, 200); }

	/** Set the lot manager to visualize */
	void SetLotManager(ALotManager* InLotManager);

	/** Set the current level to display */
	void SetCurrentLevel(int32 InLevel);

	/** Get current level */
	int32 GetCurrentLevel() const { return CurrentLevel; }

	/** Set whether to show room numbers on tiles */
	void SetShowRoomNumbers(bool bShow);

	/** Get whether room numbers are shown */
	bool GetShowRoomNumbers() const { return bShowRoomNumbers; }

	/** Force redraw */
	void Refresh();

private:
	/** Get a deterministic color for a room ID */
	FLinearColor GetColorForRoom(int32 RoomID) const;

	/** Draw a single tile */
	void DrawTile(int32 Row, int32 Col, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Draw grid lines (tile boundaries) */
	void DrawGridLines(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Draw all walls at current level */
	void DrawWalls(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Draw half walls at current level (dashed lines) */
	void DrawHalfWalls(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Draw portals (doors and windows) at current level */
	void DrawPortals(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Draw a door symbol with swing arc */
	void DrawDoorSymbol(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2D& Position, const FVector2D& Direction, float Width) const;

	/** Draw a window symbol */
	void DrawWindowSymbol(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2D& Position, const FVector2D& Direction, float Width) const;

	/** Draw a dashed line between two points */
	void DrawDashedLine(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FVector2D& Start, const FVector2D& End, const FLinearColor& Color, float Thickness, float DashLength = 5.0f, float GapLength = 3.0f) const;

	/** Convert grid coordinates to screen position */
	FVector2D GridToScreen(int32 Row, int32 Col, const FGeometry& AllottedGeometry) const;

	/** Calculate layout metrics for current geometry */
	void CalculateLayout(const FGeometry& AllottedGeometry) const;

	TWeakObjectPtr<ALotManager> LotManager;
	int32 CurrentLevel;
	bool bShowRoomNumbers;

	// Cached layout values (mutable for const OnPaint)
	mutable float TileSize;
	mutable FVector2D GridOffset;
	mutable int32 CachedGridSizeX;
	mutable int32 CachedGridSizeY;
};

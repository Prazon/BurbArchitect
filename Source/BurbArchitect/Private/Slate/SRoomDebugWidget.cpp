// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slate/SRoomDebugWidget.h"
#include "Actors/LotManager.h"
#include "Components/WallGraphComponent.h"
#include "Data/TileData.h"
#include "Data/WallGraphData.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

void SRoomDebugWidget::Construct(const FArguments& InArgs)
{
	LotManager = InArgs._LotManager;
	CurrentLevel = InArgs._CurrentLevel;
	CachedGridSizeX = 0;
	CachedGridSizeY = 0;
	TileScreenSize = 20.0f; // Default 20 pixels per tile
	GridPadding = 40.0f;

	// Build the widget layout
	ChildSlot
	[
		SNew(SVerticalBox)

		// Controls at the top
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f)
		[
			SNew(SHorizontalBox)

			// Level label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Level:")))
			]

			// Level indicator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::AsNumber(CurrentLevel);
				})
			]

			// Previous level button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("<")))
				.OnClicked_Lambda([this]()
				{
					if (CurrentLevel > 0)
					{
						OnLevelChanged(CurrentLevel - 1);
					}
					return FReply::Handled();
				})
			]

			// Next level button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT(">")))
				.OnClicked_Lambda([this]()
				{
					if (LotManager.IsValid())
					{
						OnLevelChanged(CurrentLevel + 1);
					}
					return FReply::Handled();
				})
			]

			// Refresh button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Refresh")))
				.OnClicked(this, &SRoomDebugWidget::OnRefreshClicked)
			]
		]

		// Visualization canvas
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
		]
	];
}

int32 SRoomDebugWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Call parent implementation
	int32 MaxLayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!LotManager.IsValid() || !LotManager->WallGraph)
	{
		return MaxLayerId;
	}

	// Cache grid dimensions
	const_cast<SRoomDebugWidget*>(this)->CachedGridSizeX = LotManager->GridSizeX;
	const_cast<SRoomDebugWidget*>(this)->CachedGridSizeY = LotManager->GridSizeY;

	// Calculate appropriate tile size to fit in viewport
	const FVector2D ViewportSize = AllottedGeometry.GetLocalSize();
	const float AvailableWidth = ViewportSize.X - (GridPadding * 2.0f);
	const float AvailableHeight = ViewportSize.Y - (GridPadding * 2.0f);

	const float TileSizeByWidth = AvailableWidth / FMath::Max(1, CachedGridSizeX);
	const float TileSizeByHeight = AvailableHeight / FMath::Max(1, CachedGridSizeY);
	const_cast<SRoomDebugWidget*>(this)->TileScreenSize = FMath::Min(TileSizeByWidth, TileSizeByHeight);

	// Draw all tiles
	for (const FTileData& Tile : LotManager->GridData)
	{
		if (Tile.Level == CurrentLevel)
		{
			DrawTile((int32)Tile.TileCoord.X, (int32)Tile.TileCoord.Y, Tile.Level, AllottedGeometry, OutDrawElements, MaxLayerId);
		}
	}

	// Draw walls on top of tiles
	MaxLayerId++;
	DrawWalls(AllottedGeometry, OutDrawElements, MaxLayerId);

	return MaxLayerId;
}

void SRoomDebugWidget::SetLotManager(ALotManager* InLotManager)
{
	LotManager = InLotManager;
	Refresh();
}

void SRoomDebugWidget::SetCurrentLevel(int32 InLevel)
{
	CurrentLevel = InLevel;
	Refresh();
}

void SRoomDebugWidget::Refresh()
{
	// Force a repaint
	Invalidate(EInvalidateWidgetReason::Paint);
}

FLinearColor SRoomDebugWidget::GetColorForRoom(int32 RoomID) const
{
	if (RoomID == 0)
	{
		// Outside/no room = dark gray
		return FLinearColor(0.1f, 0.1f, 0.1f, 1.0f);
	}

	// Generate a deterministic color from RoomID using hash
	// This ensures the same room always gets the same color
	uint32 Hash = GetTypeHash(RoomID);

	// Use golden ratio for good color distribution
	const float GoldenRatio = 0.618033988749895f;
	float Hue = FMath::Fmod((Hash * GoldenRatio), 1.0f);

	// Convert HSV to RGB (high saturation and value for vibrant colors)
	float Saturation = 0.7f;
	float Value = 0.9f;

	float H = Hue * 6.0f;
	float C = Value * Saturation;
	float X = C * (1.0f - FMath::Abs(FMath::Fmod(H, 2.0f) - 1.0f));
	float M = Value - C;

	float R = 0, G = 0, B = 0;
	if (H < 1.0f) { R = C; G = X; B = 0; }
	else if (H < 2.0f) { R = X; G = C; B = 0; }
	else if (H < 3.0f) { R = 0; G = C; B = X; }
	else if (H < 4.0f) { R = 0; G = X; B = C; }
	else if (H < 5.0f) { R = X; G = 0; B = C; }
	else { R = C; G = 0; B = X; }

	return FLinearColor(R + M, G + M, B + M, 1.0f);
}

void SRoomDebugWidget::DrawTile(int32 Row, int32 Col, int32 Level, const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!LotManager.IsValid())
	{
		return;
	}

	// Find the tile data
	FIntVector Key(Row, Col, Level);
	const int32* TileIndexPtr = LotManager->TileIndexMap.Find(Key);
	if (!TileIndexPtr || !LotManager->GridData.IsValidIndex(*TileIndexPtr))
	{
		return;
	}

	const FTileData& Tile = LotManager->GridData[*TileIndexPtr];

	// Get color based on RoomID
	FLinearColor TileColor = GetColorForRoom(Tile.GetPrimaryRoomID());

	// Calculate screen position
	FVector2D TopLeft = GridToScreen(Row, Col, AllottedGeometry);
	FVector2D Size(TileScreenSize, TileScreenSize);

	// Draw filled rectangle using MakeBox
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))),
		FCoreStyle::Get().GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		TileColor
	);

	// Draw tile border (grid lines)
	FVector2D BottomRight = TopLeft + Size;
	TArray<FVector2D> BorderPoints;
	BorderPoints.Add(TopLeft);
	BorderPoints.Add(FVector2D(BottomRight.X, TopLeft.Y));
	BorderPoints.Add(BottomRight);
	BorderPoints.Add(FVector2D(TopLeft.X, BottomRight.Y));
	BorderPoints.Add(TopLeft);

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 1,
		AllottedGeometry.ToPaintGeometry(),
		BorderPoints,
		ESlateDrawEffect::None,
		FLinearColor(0.2f, 0.2f, 0.2f, 0.5f),
		false,
		1.0f
	);

	// Draw room ID number on tile
	FString RoomText = FString::Printf(TEXT("%d"), Tile.GetPrimaryRoomID());

	// Use white text with black shadow for readability
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", FMath::Max(8, FMath::FloorToInt(TileScreenSize * 0.4f)));

	// Calculate text size for centering
	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FVector2D TextSize = FontMeasure->Measure(RoomText, FontInfo);
	FVector2D TextPosition = TopLeft + (Size - TextSize) * 0.5f;

	// Draw shadow first (slight offset)
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(TextPosition + FVector2D(1, 1)))),
		RoomText,
		FontInfo,
		ESlateDrawEffect::None,
		FLinearColor::Black
	);

	// Draw text
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 3,
		AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(TextPosition))),
		RoomText,
		FontInfo,
		ESlateDrawEffect::None,
		FLinearColor::White
	);
}

void SRoomDebugWidget::DrawWalls(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!LotManager.IsValid() || !LotManager->WallGraph)
	{
		return;
	}

	// Get all edges at the current level
	const TSet<int32>* EdgesAtLevel = LotManager->WallGraph->EdgesByLevel.Find(CurrentLevel);
	if (!EdgesAtLevel)
	{
		return;
	}

	// Draw each wall edge
	for (int32 EdgeID : *EdgesAtLevel)
	{
		const FWallEdge* Edge = LotManager->WallGraph->Edges.Find(EdgeID);
		if (!Edge)
		{
			continue;
		}

		// Get the nodes
		const FWallNode* FromNode = LotManager->WallGraph->Nodes.Find(Edge->FromNodeID);
		const FWallNode* ToNode = LotManager->WallGraph->Nodes.Find(Edge->ToNodeID);

		if (!FromNode || !ToNode)
		{
			continue;
		}

		// Convert node grid coordinates to screen space
		FVector2D StartScreen = GridToScreen(FromNode->Row, FromNode->Column, AllottedGeometry);
		FVector2D EndScreen = GridToScreen(ToNode->Row, ToNode->Column, AllottedGeometry);

		// Offset to center of tile
		FVector2D TileCenter(TileScreenSize * 0.5f, TileScreenSize * 0.5f);
		StartScreen += TileCenter;
		EndScreen += TileCenter;

		// Draw wall as thick black line
		TArray<FVector2D> LinePoints;
		LinePoints.Add(StartScreen);
		LinePoints.Add(EndScreen);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			FLinearColor::Black,
			true, // anti-alias
			3.0f  // thickness
		);
	}
}

FVector2D SRoomDebugWidget::GridToScreen(int32 Row, int32 Col, const FGeometry& AllottedGeometry) const
{
	// Convert grid coordinates to screen space
	// Row = X axis (horizontal), Column = Y axis (vertical)
	float ScreenX = GridPadding + (Col * TileScreenSize);
	float ScreenY = GridPadding + (Row * TileScreenSize);

	return FVector2D(ScreenX, ScreenY);
}

void SRoomDebugWidget::OnLevelChanged(int32 NewLevel)
{
	CurrentLevel = NewLevel;
	Refresh();
}

FReply SRoomDebugWidget::OnRefreshClicked()
{
	Refresh();
	return FReply::Handled();
}

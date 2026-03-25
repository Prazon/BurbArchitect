// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slate/SRoomDebugCanvas.h"
#include "Actors/LotManager.h"
#include "Components/WallGraphComponent.h"
#include "Components/WallComponent.h"
#include "Components/FloorComponent.h"
#include "Data/TileData.h"
#include "Data/WallGraphData.h"
#include "Actors/PortalBase.h"
#include "Actors/DoorBase.h"
#include "Actors/WindowBase.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

void SRoomDebugCanvas::Construct(const FArguments& InArgs)
{
	LotManager = InArgs._LotManager;
	CurrentLevel = InArgs._CurrentLevel;
	bShowRoomNumbers = false; // Off by default
	TileSize = 20.0f;
	CachedGridSizeX = 0;
	CachedGridSizeY = 0;
}

int32 SRoomDebugCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!LotManager.IsValid() || !LotManager->WallGraph)
	{
		return LayerId;
	}

	// Calculate layout for current geometry
	CalculateLayout(AllottedGeometry);

	// Draw dark gray background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		FCoreStyle::Get().GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor(0.1f, 0.1f, 0.1f, 1.0f)
	);
	LayerId++;

	// Draw all tiles at current level
	for (const FTileData& Tile : LotManager->GridData)
	{
		if (Tile.Level == CurrentLevel)
		{
			DrawTile((int32)Tile.TileCoord.X, (int32)Tile.TileCoord.Y, AllottedGeometry, OutDrawElements, LayerId);
		}
	}
	LayerId++;

	// Draw grid lines (tile boundaries)
	DrawGridLines(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw half walls (dashed, under full walls)
	DrawHalfWalls(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw full walls on top
	DrawWalls(AllottedGeometry, OutDrawElements, LayerId);
	LayerId++;

	// Draw portals (doors/windows) on top of walls
	DrawPortals(AllottedGeometry, OutDrawElements, LayerId);

	return LayerId;
}

void SRoomDebugCanvas::SetLotManager(ALotManager* InLotManager)
{
	LotManager = InLotManager;
	Refresh();
}

void SRoomDebugCanvas::SetCurrentLevel(int32 InLevel)
{
	CurrentLevel = InLevel;
	Refresh();
}

void SRoomDebugCanvas::SetShowRoomNumbers(bool bShow)
{
	bShowRoomNumbers = bShow;
	Refresh();
}

void SRoomDebugCanvas::Refresh()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SRoomDebugCanvas::CalculateLayout(const FGeometry& AllottedGeometry) const
{
	if (!LotManager.IsValid())
	{
		return;
	}

	CachedGridSizeX = LotManager->GridSizeX;
	CachedGridSizeY = LotManager->GridSizeY;

	const FVector2D ViewportSize = AllottedGeometry.GetLocalSize();
	const float Padding = 10.0f;
	const float AvailableWidth = ViewportSize.X - (Padding * 2.0f);
	const float AvailableHeight = ViewportSize.Y - (Padding * 2.0f);

	// Fit grid to available space
	const float TileSizeByWidth = AvailableWidth / FMath::Max(1, CachedGridSizeX);
	const float TileSizeByHeight = AvailableHeight / FMath::Max(1, CachedGridSizeY);
	TileSize = FMath::Min(TileSizeByWidth, TileSizeByHeight);

	// Center the grid
	const float GridWidth = CachedGridSizeX * TileSize;
	const float GridHeight = CachedGridSizeY * TileSize;
	GridOffset.X = (ViewportSize.X - GridWidth) * 0.5f;
	GridOffset.Y = (ViewportSize.Y - GridHeight) * 0.5f;
}

FLinearColor SRoomDebugCanvas::GetColorForRoom(int32 RoomID) const
{
	if (RoomID == 0)
	{
		// Blueprint blue for outside/no room
		return FLinearColor(0.15f, 0.35f, 0.6f, 1.0f);
	}

	// Dark blue for all rooms
	return FLinearColor(0.08f, 0.15f, 0.35f, 1.0f);
}

FVector2D SRoomDebugCanvas::GridToScreen(int32 Row, int32 Col, const FGeometry& AllottedGeometry) const
{
	return FVector2D(
		GridOffset.X + (Col * TileSize),
		GridOffset.Y + (Row * TileSize)
	);
}

void SRoomDebugCanvas::DrawTile(int32 Row, int32 Col, const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!LotManager.IsValid())
	{
		return;
	}

	// Look up tile in GridData using TileIndexMap for O(1) access
	FIntVector TileKey(Row, Col, CurrentLevel);
	const int32* TileIndexPtr = LotManager->TileIndexMap.Find(TileKey);
	if (!TileIndexPtr || !LotManager->GridData.IsValidIndex(*TileIndexPtr))
	{
		return;
	}

	const FTileData& Tile = LotManager->GridData[*TileIndexPtr];

	// Screen coordinates
	FVector2D ScreenTopLeft = GridToScreen(Row, Col, AllottedGeometry);
	FVector2D ScreenTopRight = ScreenTopLeft + FVector2D(TileSize, 0);
	FVector2D ScreenBottomRight = ScreenTopLeft + FVector2D(TileSize, TileSize);
	FVector2D ScreenBottomLeft = ScreenTopLeft + FVector2D(0, TileSize);
	FVector2D ScreenCenter = ScreenTopLeft + FVector2D(TileSize * 0.5f, TileSize * 0.5f);

	// Triangle screen positions: [Top, Right, Bottom, Left]
	TArray<TArray<FVector2D>> ScreenTriangles = {
		{ ScreenTopLeft, ScreenTopRight, ScreenCenter },      // Top
		{ ScreenTopRight, ScreenBottomRight, ScreenCenter },  // Right
		{ ScreenBottomRight, ScreenBottomLeft, ScreenCenter }, // Bottom
		{ ScreenBottomLeft, ScreenTopLeft, ScreenCenter }     // Left
	};

	// Get per-triangle room IDs directly from GridData (authoritative source)
	// TriangleOwnership.TriangleRoomIDs is indexed as [Top, Right, Bottom, Left]
	TArray<int32> TriangleRoomIDs;
	TriangleRoomIDs.Reserve(4);

	// Map triangle index to ETriangleType: [0=Top, 1=Right, 2=Bottom, 3=Left]
	static const ETriangleType TriangleTypes[4] = { ETriangleType::Top, ETriangleType::Right, ETriangleType::Bottom, ETriangleType::Left };

	for (int32 i = 0; i < 4; i++)
	{
		// Get room ID for this triangle from authoritative TriangleOwnership data
		int32 RoomID = 0;
		if (Tile.TriangleOwnership.TriangleRoomIDs.IsValidIndex(i))
		{
			RoomID = Tile.TriangleOwnership.TriangleRoomIDs[i];
		}

		TriangleRoomIDs.Add(RoomID);

		// Check if this triangle is a pool tile (bright cyan override)
		// Pool tiles are drawn on both their actual level AND one level up (since pools span 2 levels vertically)
		bool bIsPoolTile = false;
		if (LotManager->FloorComponent)
		{
			// Check current level
			FFloorTileData* FloorTile = LotManager->FloorComponent->FindFloorTile(CurrentLevel, Row, Col, TriangleTypes[i]);
			if (FloorTile && FloorTile->bIsPool)
			{
				bIsPoolTile = true;
			}

			// Also check one level below (pools are built at basement level but should show on ground level too)
			if (!bIsPoolTile && CurrentLevel > 0)
			{
				FFloorTileData* FloorTileBelow = LotManager->FloorComponent->FindFloorTile(CurrentLevel - 1, Row, Col, TriangleTypes[i]);
				if (FloorTileBelow && FloorTileBelow->bIsPool)
				{
					bIsPoolTile = true;
				}
			}
		}

		// Get color: bright cyan for pool tiles, otherwise room color
		FLinearColor TriColor = bIsPoolTile ? FLinearColor(0.0f, 1.0f, 1.0f, 1.0f) : GetColorForRoom(RoomID);

		// Draw triangle
		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices = { 0, 1, 2 };

		for (const FVector2D& Pt : ScreenTriangles[i])
		{
			FSlateVertex Vert;
			Vert.Position = FVector2f(AllottedGeometry.LocalToAbsolute(Pt));
			Vert.Color = TriColor.ToFColor(false);
			Vert.TexCoords[0] = 0; Vert.TexCoords[1] = 0;
			Vert.TexCoords[2] = 0; Vert.TexCoords[3] = 0;
			Verts.Add(Vert);
		}

		FSlateDrawElement::MakeCustomVerts(
			OutDrawElements,
			LayerId,
			FSlateResourceHandle(),
			Verts,
			Indices,
			nullptr, 0, 0
		);
	}

	// Smart room ID number drawing (only if enabled)
	if (bShowRoomNumbers)
	{
		// Check if all triangles are in the same room
		bool bAllSameRoom = true;
		int32 FirstRoomID = TriangleRoomIDs[0];
		for (int32 i = 1; i < 4; i++)
		{
			if (TriangleRoomIDs[i] != FirstRoomID)
			{
				bAllSameRoom = false;
				break;
			}
		}

		if (bAllSameRoom)
		{
			// All triangles in same room - draw ONE normal-sized number at tile center
			FString RoomText = FString::Printf(TEXT("%d"), FirstRoomID);
			FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", FMath::Max(8, FMath::FloorToInt(TileSize * 0.4f)));

			TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			FVector2D TextSize = FontMeasure->Measure(RoomText, FontInfo);
			FVector2D TextPosition = ScreenCenter - (TextSize * 0.5f);

			// Draw shadow
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(TextPosition + FVector2D(1, 1)))),
				RoomText,
				FontInfo,
				ESlateDrawEffect::None,
				FLinearColor::Black
			);

			// Draw text
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 2,
				AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(TextPosition))),
				RoomText,
				FontInfo,
				ESlateDrawEffect::None,
				FLinearColor::White
			);
		}
		else
		{
			// Triangles in different rooms - draw TWO numbers (one per room)
			// Since crossing diagonals are prevented, max 2 unique room IDs possible

			// Screen-space triangle centroids
			TArray<FVector2D> TriangleCenters = {
				(ScreenTopLeft + ScreenTopRight + ScreenCenter) / 3.0f,      // Top
				(ScreenTopRight + ScreenBottomRight + ScreenCenter) / 3.0f,  // Right
				(ScreenBottomRight + ScreenBottomLeft + ScreenCenter) / 3.0f, // Bottom
				(ScreenBottomLeft + ScreenTopLeft + ScreenCenter) / 3.0f     // Left
			};

			// Group triangles by room ID
			TMap<int32, TArray<int32>> RoomToTriangles;
			for (int32 i = 0; i < 4; i++)
			{
				RoomToTriangles.FindOrAdd(TriangleRoomIDs[i]).Add(i);
			}

			// Draw one number per unique room at the centroid of its triangles
			FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Bold", FMath::Max(7, FMath::FloorToInt(TileSize * 0.3f)));
			TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

			for (auto& Pair : RoomToTriangles)
			{
				int32 RoomID = Pair.Key;
				TArray<int32>& TriangleIndices = Pair.Value;

				// Calculate centroid of all triangles belonging to this room
				FVector2D RoomCentroid = FVector2D::ZeroVector;
				for (int32 TriIdx : TriangleIndices)
				{
					RoomCentroid += TriangleCenters[TriIdx];
				}
				RoomCentroid /= TriangleIndices.Num();

				// Draw room number at this centroid
				FString RoomText = FString::Printf(TEXT("%d"), RoomID);
				FVector2D TextSize = FontMeasure->Measure(RoomText, FontInfo);
				FVector2D TextPosition = RoomCentroid - (TextSize * 0.5f);

				// Draw shadow
				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(TextPosition + FVector2D(1, 1)))),
					RoomText,
					FontInfo,
					ESlateDrawEffect::None,
					FLinearColor::Black
				);

				// Draw text
				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId + 2,
					AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(TextPosition))),
					RoomText,
					FontInfo,
					ESlateDrawEffect::None,
					FLinearColor::White
				);
			}
		}
	}
}

void SRoomDebugCanvas::DrawGridLines(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!LotManager.IsValid())
	{
		return;
	}

	// Very fine white lines with 0.5 alpha
	FLinearColor GridLineColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
	float GridLineThickness = 0.5f;

	// Draw vertical grid lines
	for (int32 Col = 0; Col <= CachedGridSizeX; ++Col)
	{
		FVector2D TopPoint = GridToScreen(0, Col, AllottedGeometry);
		FVector2D BottomPoint = GridToScreen(CachedGridSizeY, Col, AllottedGeometry);

		TArray<FVector2D> LinePoints;
		LinePoints.Add(TopPoint);
		LinePoints.Add(BottomPoint);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			GridLineColor,
			true, // anti-alias
			GridLineThickness
		);
	}

	// Draw horizontal grid lines
	for (int32 Row = 0; Row <= CachedGridSizeY; ++Row)
	{
		FVector2D LeftPoint = GridToScreen(Row, 0, AllottedGeometry);
		FVector2D RightPoint = GridToScreen(Row, CachedGridSizeX, AllottedGeometry);

		TArray<FVector2D> LinePoints;
		LinePoints.Add(LeftPoint);
		LinePoints.Add(RightPoint);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			GridLineColor,
			true, // anti-alias
			GridLineThickness
		);
	}
}

void SRoomDebugCanvas::DrawWalls(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!LotManager.IsValid() || !LotManager->WallGraph)
	{
		return;
	}

	// Track which EdgeIDs we've drawn to detect duplicates
	TSet<int32> DrawnEdges;

	// Helper lambda to draw walls from a specific level
	auto DrawWallsAtLevel = [&](int32 Level, bool bPoolWallsOnly)
	{
		const TSet<int32>* EdgesAtLevel = LotManager->WallGraph->EdgesByLevel.Find(Level);
		if (!EdgesAtLevel)
		{
			return;
		}

		for (int32 EdgeID : *EdgesAtLevel)
		{
			const FWallEdge* Edge = LotManager->WallGraph->Edges.Find(EdgeID);

			if (!Edge)
			{
				continue; // Can't draw without edge data
			}

			// If we're only drawing pool walls (from level below), skip non-pool walls
			if (bPoolWallsOnly && !Edge->bIsPoolWall)
			{
				continue;
			}

			// Skip if already drawn
			if (DrawnEdges.Contains(EdgeID))
			{
				continue;
			}
			DrawnEdges.Add(EdgeID);

			const FWallNode* FromNode = LotManager->WallGraph->Nodes.Find(Edge->FromNodeID);
			const FWallNode* ToNode = LotManager->WallGraph->Nodes.Find(Edge->ToNodeID);
			if (!FromNode || !ToNode)
			{
				continue;
			}

			// Determine wall color
			FLinearColor WallColor = FLinearColor::White; // Default: valid
			if (Edge->bIsPoolWall)
			{
				// Pool walls drawn in black
				WallColor = FLinearColor::Black;
			}

			// Walls are on grid corners, not tile centers
			// Node coordinates are corner indices, so we draw directly at those positions
			FVector2D StartScreen = GridToScreen(FromNode->Row, FromNode->Column, AllottedGeometry);
			FVector2D EndScreen = GridToScreen(ToNode->Row, ToNode->Column, AllottedGeometry);

			TArray<FVector2D> LinePoints;
			LinePoints.Add(StartScreen);
			LinePoints.Add(EndScreen);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				WallColor,
				false,
				3.0f
			);
		}
	};

	// Draw all walls at current level
	DrawWallsAtLevel(CurrentLevel, false);

	// Also draw pool walls from one level below (pools span 2 levels)
	if (CurrentLevel > 0)
	{
		DrawWallsAtLevel(CurrentLevel - 1, true);
	}

	// Second pass: Check for walls with null WallSectionData (invalid mesh data)
	// These indicate walls that have been committed but have dangling mesh pointers
	if (LotManager->WallComponent)
	{
		const TArray<FWallSegmentData>& WallDataArray = LotManager->WallComponent->WallDataArray;

		for (const FWallSegmentData& WallSegment : WallDataArray)
		{
			// Only check walls at current level
			if (WallSegment.Level != CurrentLevel)
			{
				continue;
			}

			// Check if WallSectionData is null
			if (WallSegment.WallSectionData == nullptr && WallSegment.bCommitted)
			{
				// This wall has null mesh data - draw in ORANGE
				int32 StartRow, StartCol, EndRow, EndCol;

				// Try to get grid coordinates from WallEdgeID first
				if (WallSegment.WallEdgeID != -1)
				{
					const FWallEdge* Edge = LotManager->WallGraph->Edges.Find(WallSegment.WallEdgeID);
					if (!Edge)
					{
						continue;
					}

					const FWallNode* FromNode = LotManager->WallGraph->Nodes.Find(Edge->FromNodeID);
					const FWallNode* ToNode = LotManager->WallGraph->Nodes.Find(Edge->ToNodeID);
					if (!FromNode || !ToNode)
					{
						continue;
					}

					StartRow = FromNode->Row;
					StartCol = FromNode->Column;
					EndRow = ToNode->Row;
					EndCol = ToNode->Column;
				}
				else
				{
					// Fall back to converting world positions to grid coordinates
					if (!LotManager->LocationToTile(WallSegment.StartLoc, StartRow, StartCol) ||
						!LotManager->LocationToTile(WallSegment.EndLoc, EndRow, EndCol))
					{
						continue;
					}
				}

				// Draw the wall in ORANGE
				FVector2D StartScreen = GridToScreen(StartRow, StartCol, AllottedGeometry);
				FVector2D EndScreen = GridToScreen(EndRow, EndCol, AllottedGeometry);

				TArray<FVector2D> LinePoints;
				LinePoints.Add(StartScreen);
				LinePoints.Add(EndScreen);

				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(),
					LinePoints,
					ESlateDrawEffect::None,
					FLinearColor(1.0f, 0.5f, 0.0f, 1.0f), // Orange - invalid mesh data
					false,
					4.0f // Slightly thicker to stand out
				);
			}
		}
	}
}

void SRoomDebugCanvas::DrawDashedLine(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FVector2D& Start, const FVector2D& End, const FLinearColor& Color, float Thickness, float DashLength, float GapLength) const
{
	FVector2D Direction = End - Start;
	float TotalLength = Direction.Size();
	if (TotalLength < 0.1f)
	{
		return; // Too short to draw
	}

	Direction.Normalize();
	float CurrentDistance = 0.0f;
	bool bDrawingDash = true;

	while (CurrentDistance < TotalLength)
	{
		if (bDrawingDash)
		{
			// Draw a dash
			float DashEnd = FMath::Min(CurrentDistance + DashLength, TotalLength);
			FVector2D DashStart = Start + Direction * CurrentDistance;
			FVector2D DashEndPos = Start + Direction * DashEnd;

			TArray<FVector2D> LinePoints;
			LinePoints.Add(DashStart);
			LinePoints.Add(DashEndPos);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				LinePoints,
				ESlateDrawEffect::None,
				Color,
				false,
				Thickness
			);

			CurrentDistance = DashEnd;
		}
		else
		{
			// Skip a gap
			CurrentDistance += GapLength;
		}

		bDrawingDash = !bDrawingDash;
	}
}

void SRoomDebugCanvas::DrawHalfWalls(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!LotManager.IsValid() || !LotManager->WallComponent || !LotManager->WallGraph)
	{
		return;
	}

	// Access WallDataArray from WallComponent
	const TArray<FWallSegmentData>& WallDataArray = LotManager->WallComponent->WallDataArray;

	for (const FWallSegmentData& WallSegment : WallDataArray)
	{
		// Check if this is a half wall (height around 150 units vs full wall 300 units)
		if (WallSegment.Level != CurrentLevel || WallSegment.Height > 200.0f)
		{
			continue;
		}

		// Get grid coordinates from WallGraph edge (if this wall has a graph edge)
		// Half walls created by BuildHalfWallTool have WallEdgeID = -1 (no graph edge)
		// But they still have StartLoc/EndLoc we can convert
		int32 StartRow, StartCol, EndRow, EndCol;

		if (WallSegment.WallEdgeID != -1)
		{
			// Look up the edge in WallGraph to get grid coordinates
			const FWallEdge* Edge = LotManager->WallGraph->Edges.Find(WallSegment.WallEdgeID);
			if (!Edge)
			{
				continue;
			}

			StartRow = Edge->StartRow;
			StartCol = Edge->StartColumn;
			EndRow = Edge->EndRow;
			EndCol = Edge->EndColumn;
		}
		else
		{
			// No wall edge (half wall), convert positions to grid coordinates
			if (!LotManager->LocationToTile(WallSegment.StartLoc, StartRow, StartCol) ||
				!LotManager->LocationToTile(WallSegment.EndLoc, EndRow, EndCol))
			{
				continue;
			}
		}

		// Convert to screen space
		FVector2D StartScreen = GridToScreen(StartRow, StartCol, AllottedGeometry);
		FVector2D EndScreen = GridToScreen(EndRow, EndCol, AllottedGeometry);

		// Draw as dashed white line (blueprint style for half walls)
		DrawDashedLine(AllottedGeometry, OutDrawElements, LayerId, StartScreen, EndScreen, FLinearColor::White, 2.0f);
	}
}

void SRoomDebugCanvas::DrawPortals(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!LotManager.IsValid() || !LotManager->WallComponent)
	{
		return;
	}

	// Calculate layout to ensure we have valid TileSize and GridOffset
	CalculateLayout(AllottedGeometry);

	// Track which portals we've already drawn to prevent duplicates
	TSet<APortalBase*> DrawnPortals;

	// Iterate through wall segments to find all portals
	const TArray<FWallSegmentData>& WallDataArray = LotManager->WallComponent->WallDataArray;

	for (const FWallSegmentData& WallSegment : WallDataArray)
	{
		// Only process walls on current level
		if (WallSegment.Level != CurrentLevel)
		{
			continue;
		}

		// Check all portals on this wall segment
		for (APortalBase* Portal : WallSegment.PortalArray)
		{
			if (!Portal || DrawnPortals.Contains(Portal))
			{
				continue;
			}

			// Mark as drawn
			DrawnPortals.Add(Portal);

			// Get portal world position
			FVector PortalWorldPos = Portal->GetActorLocation();
			FVector GridOrigin = LotManager->GetActorLocation();

			// Calculate portal's offset from grid origin in world units
			float WorldOffsetX = PortalWorldPos.X - GridOrigin.X;
			float WorldOffsetY = PortalWorldPos.Y - GridOrigin.Y;

			// Convert world offset to fractional tile position
			float FractionalCol = WorldOffsetX / LotManager->GridTileSize;
			float FractionalRow = WorldOffsetY / LotManager->GridTileSize;

			// Convert fractional tile position to screen coordinates
			FVector2D PortalScreenPos(
				GridOffset.X + (FractionalCol * TileSize),
				GridOffset.Y + (FractionalRow * TileSize)
			);

			// Get portal rotation and convert to 2D direction
			FRotator PortalRotation = Portal->GetActorRotation();
			FVector WorldForward = PortalRotation.RotateVector(FVector::ForwardVector);
			FVector2D PortalDirection2D = FVector2D(WorldForward.X, WorldForward.Y).GetSafeNormal();

			// Get portal width and ensure minimum visibility
			float PortalWidth = Portal->PortalSize.X;
			float ScreenWidth = (PortalWidth / LotManager->GridTileSize) * TileSize;
			ScreenWidth = FMath::Max(ScreenWidth, TileSize * 0.4f); // Minimum 40% of tile size

			// Determine if this is a door or window and draw appropriate symbol (BLACK)
			if (Portal->IsA<ADoorBase>())
			{
				DrawDoorSymbol(AllottedGeometry, OutDrawElements, LayerId, PortalScreenPos, PortalDirection2D, ScreenWidth);
			}
			else
			{
				DrawWindowSymbol(AllottedGeometry, OutDrawElements, LayerId, PortalScreenPos, PortalDirection2D, ScreenWidth);
			}
		}
	}
}

void SRoomDebugCanvas::DrawDoorSymbol(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FVector2D& Position, const FVector2D& Direction, float Width) const
{
	// Traditional architectural door symbol: door panel perpendicular to wall with 90-degree swing arc
	// Black color for visibility

	const FLinearColor DoorColor = FLinearColor::Black;

	// Calculate perpendicular direction (door opens perpendicular to wall)
	FVector2D Perpendicular(-Direction.Y, Direction.X);

	// Door panel extends perpendicular from wall
	FVector2D DoorStart = Position;
	FVector2D DoorEnd = Position + Perpendicular * Width;

	// Draw door panel as a thick line
	TArray<FVector2D> DoorLine;
	DoorLine.Add(DoorStart);
	DoorLine.Add(DoorEnd);

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		DoorLine,
		ESlateDrawEffect::None,
		DoorColor,
		true, // anti-alias
		3.0f // thicker for visibility
	);

	// Draw swing arc (90 degrees from perpendicular direction)
	const int32 ArcSegments = 12;
	TArray<FVector2D> ArcPoints;

	// Start angle is the perpendicular direction, sweep 90 degrees
	float StartAngle = FMath::Atan2(Perpendicular.Y, Perpendicular.X);
	float EndAngle = StartAngle + PI * 0.5f; // 90 degrees clockwise

	for (int32 i = 0; i <= ArcSegments; ++i)
	{
		float Alpha = (float)i / (float)ArcSegments;
		float Angle = FMath::Lerp(StartAngle, EndAngle, Alpha);
		FVector2D ArcPoint = DoorStart + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Width;
		ArcPoints.Add(ArcPoint);
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		ArcPoints,
		ESlateDrawEffect::None,
		DoorColor,
		true, // anti-alias
		2.0f // slightly thinner than door panel
	);
}

void SRoomDebugCanvas::DrawWindowSymbol(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FVector2D& Position, const FVector2D& Direction, float Width) const
{
	// Traditional architectural window symbol: three parallel lines representing the window opening
	// Black color for visibility

	const FLinearColor WindowColor = FLinearColor::Black;

	// Window extends along the Direction (use Direction directly, not perpendicular)
	FVector2D WindowStart = Position - Direction * (Width * 0.5f);
	FVector2D WindowEnd = Position + Direction * (Width * 0.5f);

	// Draw outer frame line (thickest) - main window frame
	TArray<FVector2D> OuterLine;
	OuterLine.Add(WindowStart);
	OuterLine.Add(WindowEnd);

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		OuterLine,
		ESlateDrawEffect::None,
		WindowColor,
		true, // anti-alias
		3.0f // thick for visibility
	);

	// Draw two inner parallel lines (representing glass panes)
	// These also run along Direction, just slightly shorter to show depth
	float InsetAmount = Width * 0.1f; // Shorten the inner lines slightly

	// First inner line
	FVector2D InnerStart1 = Position - Direction * (Width * 0.5f - InsetAmount);
	FVector2D InnerEnd1 = Position + Direction * (Width * 0.5f - InsetAmount);

	TArray<FVector2D> InnerLine1;
	InnerLine1.Add(InnerStart1);
	InnerLine1.Add(InnerEnd1);

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		InnerLine1,
		ESlateDrawEffect::None,
		WindowColor,
		true, // anti-alias
		1.5f
	);

	// Second inner line (even shorter for depth effect)
	FVector2D InnerStart2 = Position - Direction * (Width * 0.5f - InsetAmount * 2.0f);
	FVector2D InnerEnd2 = Position + Direction * (Width * 0.5f - InsetAmount * 2.0f);

	TArray<FVector2D> InnerLine2;
	InnerLine2.Add(InnerStart2);
	InnerLine2.Add(InnerEnd2);

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		InnerLine2,
		ESlateDrawEffect::None,
		WindowColor,
		true, // anti-alias
		1.5f
	);
}

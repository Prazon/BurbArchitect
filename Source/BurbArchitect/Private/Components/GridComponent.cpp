// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/GridComponent.h"
#include "Components/FloorComponent.h"

UGridComponent::UGridComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	// Initialize grid configuration
	GridSizeX = 0;
	GridSizeY = 0;
	GridTileSize = 100.0f;

	// Boundary line component will be created at runtime (not in constructor for non-actor components)
	BoundaryLineComponent = nullptr;

	// Disable shadow casting - grid is a visual guide and should never cast shadows
	SetCastShadow(false);

	// Disable custom depth - grid is permanent geometry
	bRenderCustomDepth = false;

	// Configure collision for grid-based cursor snapping
	// The grid should only respond to the Tile trace channel (ECC_GameTraceChannel3)
	// Set to Overlap so traces pass through to terrain below (fixes brush tool raycast blocking)
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECC_WorldStatic);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Overlap); // Tile trace channel - overlap to allow terrain traces
}

void UGridComponent::GenerateGridMesh(const TArray<FTileData>& GridData, int32 InGridSizeX, int32 InGridSizeY, float InGridTileSize, UMaterialInterface* GridMaterial, int32 ExcludeLevel)
{
	// Cache grid configuration
	GridSizeX = InGridSizeX;
	GridSizeY = InGridSizeY;
	GridTileSize = InGridTileSize;

	// Clear existing mesh sections
	ClearAllMeshSections();

	// Clear per-level cached mesh data
	LevelVertices.Empty();
	LevelTriangles.Empty();
	LevelNormals.Empty();
	LevelUVs.Empty();
	LevelVertexColors.Empty();
	LevelTangents.Empty();
	TileToLevelMap.Empty();

	// Get component's world location to convert tile positions to local space
	FVector ComponentLocation = GetComponentLocation();

	// Group tiles by level and track local indices
	TMap<int32, int32> LevelTileCounts; // Track how many tiles per level for local index assignment

	// Process each tile and add to per-level merged mesh arrays
	for (const FTileData& Tile : GridData)
	{
		// Skip out of bounds tiles (the 1-tile safety border)
		if (Tile.bOutOfBounds)
			continue;

		// Skip invalid tiles
		if (Tile.TileIndex < 0)
			continue;

		int32 Level = Tile.Level;

		// Skip tiles on the excluded level (ground floor)
		if (Level == ExcludeLevel)
			continue;

		// Initialize arrays for this level if not already present
		if (!LevelVertices.Contains(Level))
		{
			LevelVertices.Add(Level, TArray<FVector>());
			LevelTriangles.Add(Level, TArray<int32>());
			LevelNormals.Add(Level, TArray<FVector>());
			LevelUVs.Add(Level, TArray<FVector2D>());
			LevelVertexColors.Add(Level, TArray<FColor>());
			LevelTangents.Add(Level, TArray<FProcMeshTangent>());
			LevelTileCounts.Add(Level, 0);
		}

		// Get local index for this tile within its level
		int32 LocalIndex = LevelTileCounts[Level];
		LevelTileCounts[Level]++;

		// Store tile -> (level, local index) mapping
		TileToLevelMap.Add(Tile.TileIndex, FIntPoint(Level, LocalIndex));

		// Get color based on room ID
		FColor TileColor = GetColorFromRoomID(Tile.GetPrimaryRoomID());

		// Convert world-space tile center to local space
		FVector LocalTileCenter = Tile.Center - ComponentLocation;

		// Add this tile's quad geometry to the level's merged mesh arrays
		CreateTileQuad(Tile.TileIndex, LocalTileCenter, Tile.Level, TileColor,
					   LevelVertices[Level], LevelTriangles[Level], LevelNormals[Level],
					   LevelUVs[Level], LevelVertexColors[Level], LevelTangents[Level]);
	}

	// Create one mesh section per level
	for (const auto& LevelPair : LevelVertices)
	{
		int32 Level = LevelPair.Key;
		const TArray<FVector>& Vertices = LevelPair.Value;

		if (Vertices.Num() > 0)
		{
			// Create mesh section for this level (section index = level)
			// bCreateCollision = true allows traces against ECC_GameTraceChannel3 to hit the grid
			CreateMeshSection(Level, Vertices, LevelTriangles[Level], LevelNormals[Level],
							 LevelUVs[Level], LevelVertexColors[Level], LevelTangents[Level], true);

			// Apply material to this level's section
			if (GridMaterial)
			{
				SetMaterial(Level, GridMaterial);
			}
		}
	}
}

void UGridComponent::RebuildGridLevel(int32 Level, const TArray<FTileData>& GridData, UMaterialInterface* GridMaterial, int32 ExcludeLevel)
{
	// Skip excluded level (ground floor)
	if (Level == ExcludeLevel)
	{
		UE_LOG(LogTemp, Verbose, TEXT("GridComponent::RebuildGridLevel - Skipping excluded level %d"), Level);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("GridComponent::RebuildGridLevel - Rebuilding Level %d"), Level);

	// Clear this level's mesh section
	ClearMeshSection(Level);

	// Clear cached data for this level
	LevelVertices.Remove(Level);
	LevelTriangles.Remove(Level);
	LevelNormals.Remove(Level);
	LevelUVs.Remove(Level);
	LevelVertexColors.Remove(Level);
	LevelTangents.Remove(Level);

	// Remove tiles from this level in the TileToLevelMap
	TArray<int32> TilesToRemove;
	for (const auto& Pair : TileToLevelMap)
	{
		if (Pair.Value.X == Level) // X = Level
		{
			TilesToRemove.Add(Pair.Key);
		}
	}
	for (int32 TileIndex : TilesToRemove)
	{
		TileToLevelMap.Remove(TileIndex);
	}

	// Rebuild this level from GridData
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	int32 LocalIndex = 0;
	FVector ComponentLocation = GetComponentLocation();

	// Process tiles for this level only
	for (const FTileData& Tile : GridData)
	{
		if (Tile.bOutOfBounds || Tile.TileIndex < 0)
			continue;

		if (Tile.Level != Level)
			continue; // Skip tiles not on this level

		// Store mapping
		TileToLevelMap.Add(Tile.TileIndex, FIntPoint(Level, LocalIndex));

		// Generate tile geometry
		FColor TileColor = GetColorFromRoomID(Tile.GetPrimaryRoomID());
		FVector LocalTileCenter = Tile.Center - ComponentLocation;

		CreateTileQuad(Tile.TileIndex, LocalTileCenter, Tile.Level, TileColor,
					  Vertices, Triangles, Normals, UVs, VertexColors, Tangents);

		LocalIndex++;
	}

	// Create mesh section if we have vertices
	if (Vertices.Num() > 0)
	{
		// Cache for this level
		LevelVertices.Add(Level, Vertices);
		LevelTriangles.Add(Level, Triangles);
		LevelNormals.Add(Level, Normals);
		LevelUVs.Add(Level, UVs);
		LevelVertexColors.Add(Level, VertexColors);
		LevelTangents.Add(Level, Tangents);

		CreateMeshSection(Level, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

		if (GridMaterial)
		{
			SetMaterial(Level, GridMaterial);
		}

		UE_LOG(LogTemp, Log, TEXT("GridComponent::RebuildGridLevel - Rebuilt Level %d with %d tiles"), Level, LocalIndex);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("GridComponent::RebuildGridLevel - No tiles found for Level %d"), Level);
	}
}

void UGridComponent::UpdateTileColor(int32 TileIndex, FColor Color)
{
	// Find which level and local index this tile belongs to
	int32 Level, LocalIndex;
	if (!GetTileLevelAndIndex(TileIndex, Level, LocalIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("GridComponent::UpdateTileColor - TileIndex %d not found in TileToLevelMap"), TileIndex);
		return;
	}

	// Get the level's cached vertex colors
	TArray<FColor>* VertexColors = LevelVertexColors.Find(Level);
	if (!VertexColors)
		return;

	// Calculate vertex range for this tile within its level (5 vertices per tile)
	int32 VertexStart = GetLocalVertexStart(LocalIndex);

	// Bounds check
	if (VertexStart + 4 >= VertexColors->Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("GridComponent::UpdateTileColor - Invalid LocalIndex %d for Level %d"), LocalIndex, Level);
		return;
	}

	// Update the 5 vertices for this tile in cached color data
	(*VertexColors)[VertexStart + 0] = Color;
	(*VertexColors)[VertexStart + 1] = Color;
	(*VertexColors)[VertexStart + 2] = Color;
	(*VertexColors)[VertexStart + 3] = Color;
	(*VertexColors)[VertexStart + 4] = Color;

	// Upload updated mesh to GPU for this level's section
	UpdateMeshSection(Level, LevelVertices[Level], LevelNormals[Level], LevelUVs[Level],
					 *VertexColors, LevelTangents[Level]);
}

void UGridComponent::SetTileVisibility(int32 TileIndex, bool bIsVisible)
{
	// Find which level and local index this tile belongs to
	int32 Level, LocalIndex;
	if (!GetTileLevelAndIndex(TileIndex, Level, LocalIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("GridComponent::SetTileVisibility - TileIndex %d not found in TileToLevelMap"), TileIndex);
		return;
	}

	// Get the level's cached triangles
	TArray<int32>* Triangles = LevelTriangles.Find(Level);
	if (!Triangles)
		return;

	// Calculate triangle range for this tile within its level (12 indices per tile = 4 triangles)
	int32 TriangleStart = GetLocalTriangleStart(LocalIndex);

	// Bounds check
	if (TriangleStart + 11 >= Triangles->Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("GridComponent::SetTileVisibility - Invalid LocalIndex %d for Level %d"), LocalIndex, Level);
		return;
	}

	if (bIsVisible)
	{
		// Restore original triangle indices for this tile's 4 triangles
		int32 VertexStart = GetLocalVertexStart(LocalIndex);

		// Top triangle: TopLeft, TopRight, Center
		(*Triangles)[TriangleStart + 0] = VertexStart + 0;
		(*Triangles)[TriangleStart + 1] = VertexStart + 1;
		(*Triangles)[TriangleStart + 2] = VertexStart + 2;

		// Right triangle: TopRight, BottomRight, Center
		(*Triangles)[TriangleStart + 3] = VertexStart + 1;
		(*Triangles)[TriangleStart + 4] = VertexStart + 3;
		(*Triangles)[TriangleStart + 5] = VertexStart + 2;

		// Bottom triangle: BottomRight, BottomLeft, Center
		(*Triangles)[TriangleStart + 6] = VertexStart + 3;
		(*Triangles)[TriangleStart + 7] = VertexStart + 4;
		(*Triangles)[TriangleStart + 8] = VertexStart + 2;

		// Left triangle: BottomLeft, TopLeft, Center
		(*Triangles)[TriangleStart + 9] = VertexStart + 4;
		(*Triangles)[TriangleStart + 10] = VertexStart + 0;
		(*Triangles)[TriangleStart + 11] = VertexStart + 2;
	}
	else
	{
		// Create degenerate triangles (all indices point to same vertex = invisible)
		for (int32 i = 0; i < 12; i++)
		{
			(*Triangles)[TriangleStart + i] = 0;
		}
	}

	// Upload updated triangles to GPU for this level's section
	UpdateMeshSection(Level, LevelVertices[Level], LevelNormals[Level], LevelUVs[Level],
					 LevelVertexColors[Level], LevelTangents[Level]);
}

void UGridComponent::DrawBoundaryLines(float LotWidth, float LotHeight, const FVector& LotLocation)
{
	// Create boundary line component if it doesn't exist
	if (!BoundaryLineComponent)
	{
		BoundaryLineComponent = NewObject<ULineBatchComponent>(GetOwner(), TEXT("GridBoundaryLines"));
		if (BoundaryLineComponent)
		{
			BoundaryLineComponent->RegisterComponent();
			BoundaryLineComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
			// Disable custom depth - boundary lines are permanent geometry
			BoundaryLineComponent->SetRenderCustomDepth(false);
		}
	}

	if (!BoundaryLineComponent)
		return;

	// Clear existing lines
	ClearBoundaryLines();

	// Draw 1px white rectangle around lot boundary
	const FColor LineColor = FColor::White;
	const float LineThickness = 1.0f;
	const float LineLifeTime = -1.0f; // Persistent lines
	const float ZOffset = 5.0f; // Offset above ground to prevent z-fighting

	// Calculate boundary corners (with Z offset)
	FVector BottomLeft = LotLocation + FVector(0.0f, 0.0f, ZOffset);
	FVector BottomRight = LotLocation + FVector(LotWidth, 0.0f, ZOffset);
	FVector TopLeft = LotLocation + FVector(0.0f, LotHeight, ZOffset);
	FVector TopRight = LotLocation + FVector(LotWidth, LotHeight, ZOffset);

	// Draw four boundary lines
	BoundaryLineComponent->DrawLine(BottomLeft, BottomRight, LineColor, 0, LineThickness, LineLifeTime);
	BoundaryLineComponent->DrawLine(BottomRight, TopRight, LineColor, 0, LineThickness, LineLifeTime);
	BoundaryLineComponent->DrawLine(TopRight, TopLeft, LineColor, 0, LineThickness, LineLifeTime);
	BoundaryLineComponent->DrawLine(TopLeft, BottomLeft, LineColor, 0, LineThickness, LineLifeTime);
}

void UGridComponent::ClearBoundaryLines()
{
	if (BoundaryLineComponent)
	{
		BoundaryLineComponent->Flush();
	}
}

void UGridComponent::UpdateAllTileColorsFromRoomIDs(const TArray<FTileData>& GridData)
{
	// Batch operation: update all tile colors per level, then upload once per level
	TSet<int32> DirtyLevels; // Track which levels need GPU upload

	for (const FTileData& Tile : GridData)
	{
		if (Tile.bOutOfBounds || Tile.TileIndex < 0)
			continue;

		// Find which level and local index this tile belongs to
		int32 Level, LocalIndex;
		if (!GetTileLevelAndIndex(Tile.TileIndex, Level, LocalIndex))
			continue;

		// Get the level's cached vertex colors
		TArray<FColor>* VertexColors = LevelVertexColors.Find(Level);
		if (!VertexColors)
			continue;

		// Calculate vertex range for this tile within its level
		int32 VertexStart = GetLocalVertexStart(LocalIndex);

		// Bounds check
		if (VertexStart + 3 >= VertexColors->Num())
			continue;

		// Get color based on room ID
		FColor TileColor = GetColorFromRoomID(Tile.GetPrimaryRoomID());

		// Update the 4 vertices for this tile in cached color data
		(*VertexColors)[VertexStart + 0] = TileColor;
		(*VertexColors)[VertexStart + 1] = TileColor;
		(*VertexColors)[VertexStart + 2] = TileColor;
		(*VertexColors)[VertexStart + 3] = TileColor;

		DirtyLevels.Add(Level);
	}

	// Upload updated meshes to GPU (once per dirty level)
	for (int32 Level : DirtyLevels)
	{
		UpdateMeshSection(Level, LevelVertices[Level], LevelNormals[Level], LevelUVs[Level],
						 LevelVertexColors[Level], LevelTangents[Level]);
	}
}

void UGridComponent::SetGridVisibility(bool bShowGrid, bool bHideBoundaryLines)
{
	// Set visibility for all level sections
	for (const auto& LevelPair : LevelVertices)
	{
		int32 Level = LevelPair.Key;
		SetMeshSectionVisible(Level, bShowGrid);
	}

	// Hide or show boundary lines based on parameter
	if (bHideBoundaryLines)
	{
		ClearBoundaryLines();
	}
	// Note: To restore boundary lines, call DrawBoundaryLines() externally
	// We don't redraw here because we don't have lot dimensions stored
}

void UGridComponent::SetLevelVisibility(int32 Level, bool bShow)
{
	// Check if this level exists
	if (LevelVertices.Contains(Level))
	{
		// Use built-in mesh section visibility (O(1), no GPU upload needed)
		SetMeshSectionVisible(Level, bShow);
	}
}

void UGridComponent::CreateTileQuad(int32 TileIndex, const FVector& TileCenter, int32 Level, FColor VertexColor, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs, TArray<FColor>& VertexColors, TArray<FProcMeshTangent>& Tangents)
{
	const float HalfSize = GridTileSize * 0.5f;

	// Get the base vertex index for this tile in the merged mesh
	int32 BaseVertexIndex = Vertices.Num();

	// Create 5 vertices matching FloorComponent geometry
	// Order: TopLeft, TopRight, Center, BottomRight, BottomLeft
	const float Z = TileCenter.Z;

	Vertices.Add(FVector(TileCenter.X - HalfSize, TileCenter.Y + HalfSize, Z)); // 0: TopLeft
	Vertices.Add(FVector(TileCenter.X + HalfSize, TileCenter.Y + HalfSize, Z)); // 1: TopRight
	Vertices.Add(FVector(TileCenter.X, TileCenter.Y, Z));                       // 2: Center
	Vertices.Add(FVector(TileCenter.X + HalfSize, TileCenter.Y - HalfSize, Z)); // 3: BottomRight
	Vertices.Add(FVector(TileCenter.X - HalfSize, TileCenter.Y - HalfSize, Z)); // 4: BottomLeft

	// Create 4 triangles radiating from center (matching FloorComponent pattern)
	// Top triangle: TopLeft, TopRight, Center
	Triangles.Add(BaseVertexIndex + 0);
	Triangles.Add(BaseVertexIndex + 1);
	Triangles.Add(BaseVertexIndex + 2);

	// Right triangle: TopRight, BottomRight, Center
	Triangles.Add(BaseVertexIndex + 1);
	Triangles.Add(BaseVertexIndex + 3);
	Triangles.Add(BaseVertexIndex + 2);

	// Bottom triangle: BottomRight, BottomLeft, Center
	Triangles.Add(BaseVertexIndex + 3);
	Triangles.Add(BaseVertexIndex + 4);
	Triangles.Add(BaseVertexIndex + 2);

	// Left triangle: BottomLeft, TopLeft, Center
	Triangles.Add(BaseVertexIndex + 4);
	Triangles.Add(BaseVertexIndex + 0);
	Triangles.Add(BaseVertexIndex + 2);

	// Normals (all pointing up)
	Normals.Add(FVector(0.0f, 0.0f, 1.0f));
	Normals.Add(FVector(0.0f, 0.0f, 1.0f));
	Normals.Add(FVector(0.0f, 0.0f, 1.0f));
	Normals.Add(FVector(0.0f, 0.0f, 1.0f));
	Normals.Add(FVector(0.0f, 0.0f, 1.0f));

	// UVs matching FloorComponent pattern
	UVs.Add(FVector2D(0.0f, 1.0f));  // TopLeft
	UVs.Add(FVector2D(1.0f, 1.0f));  // TopRight
	UVs.Add(FVector2D(0.5f, 0.5f));  // Center
	UVs.Add(FVector2D(1.0f, 0.0f));  // BottomRight
	UVs.Add(FVector2D(0.0f, 0.0f));  // BottomLeft

	// Vertex colors (encode room ID / debug info)
	VertexColors.Add(VertexColor);
	VertexColors.Add(VertexColor);
	VertexColors.Add(VertexColor);
	VertexColors.Add(VertexColor);
	VertexColors.Add(VertexColor);

	// Tangents
	FProcMeshTangent Tangent(1.0f, 0.0f, 0.0f);
	Tangents.Add(Tangent);
	Tangents.Add(Tangent);
	Tangents.Add(Tangent);
	Tangents.Add(Tangent);
	Tangents.Add(Tangent);
}

FColor UGridComponent::GetColorFromRoomID(int32 RoomID) const
{
	// RoomID 0 = outside (transparent or grey)
	if (RoomID == 0)
	{
		return FColor(128, 128, 128, 0); // Grey, fully transparent
	}

	// Generate a color based on room ID
	// Use hue rotation for distinct colors
	const float Hue = (RoomID * 137.5f); // Golden angle for good distribution
	const float Saturation = 0.6f;
	const float Value = 0.8f;

	FLinearColor HSV(Hue, Saturation, Value);
	return HSV.HSVToLinearRGB().ToFColor(false);
}

bool UGridComponent::GetTileLevelAndIndex(int32 TileIndex, int32& OutLevel, int32& OutLocalIndex) const
{
	const FIntPoint* LevelAndIndex = TileToLevelMap.Find(TileIndex);
	if (LevelAndIndex)
	{
		OutLevel = LevelAndIndex->X;
		OutLocalIndex = LevelAndIndex->Y;
		return true;
	}
	return false;
}

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Components/FloorComponent.h"
#include "Components/WallComponent.h"
#include "Data/TileTriangleData.h"  // For FTriangleCoord
#include "BuildFloorTool.generated.h"

// Floor placement mode enum
UENUM(BlueprintType)
enum class EFloorPlacementMode : uint8
{
	FullTile UMETA(DisplayName = "Full Tile"),
	SingleTriangle UMETA(DisplayName = "Single Triangle")
};

// Triangle selection enum (matches FTileSectionState orientation)
UENUM(BlueprintType)
enum class ETriangleSelection : uint8
{
	Top UMETA(DisplayName = "Top"),
	Right UMETA(DisplayName = "Right"),
	Bottom UMETA(DisplayName = "Bottom"),
	Left UMETA(DisplayName = "Left"),
	None UMETA(DisplayName = "None")
};

// FTriangleCoord is now defined in Data/TileTriangleData.h

UCLASS()
class BURBARCHITECT_API ABuildFloorTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABuildFloorTool();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;	
	
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	void CreateFloorPreview(int32 Level, FVector TileCenter, FTileSectionState TileSectionState, UMaterialInstance* OptionalMaterial);
	
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	// ========== Network RPCs for Floor Pattern Application ==========

	// Build floors with pattern
	UFUNCTION(Server, Reliable)
	void Server_BuildFloors(int32 Level, const TArray<FVector>& TileLocations, const TArray<FTileSectionState>& SectionStates, UFloorPattern* Pattern, int32 SwatchIndex, bool bIsUpdate);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_BuildFloors(int32 Level, const TArray<FVector>& TileLocations, const TArray<FTileSectionState>& SectionStates, UFloorPattern* Pattern, int32 SwatchIndex, bool bIsUpdate);

	// Delete floors
	UFUNCTION(Server, Reliable)
	void Server_DeleteFloors(int32 Level, const TArray<int32>& Rows, const TArray<int32>& Columns, const TArray<FTileSectionState>& ExistingStates);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_DeleteFloors(int32 Level, const TArray<int32>& Rows, const TArray<int32>& Columns, const TArray<FTileSectionState>& ExistingStates);

	TArray<UFloorComponent*> DragCreateFloorArray;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Floor Tool")
	class UFloorPattern* DefaultFloorPattern;

	// Index of the currently selected color swatch from DefaultFloorPattern->ColourSwatches
	// Set by the swatch picker UI when user selects a color
	UPROPERTY(BlueprintReadWrite, Category = "Floor Tool")
	int32 SelectedSwatchIndex = 0;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Floor Tool")
	UMaterialInstance* BaseMaterial;

	UPROPERTY(BlueprintReadWrite)
	UFloorComponent* PreviewFloorMesh;

	UPROPERTY(BlueprintReadWrite)
	TArray<FVector> DragTileLocations;

	// Floor placement mode - toggleable via Blueprint (e.g., Ctrl+F)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Floor Tool")
	EFloorPlacementMode PlacementMode = EFloorPlacementMode::FullTile;

	// Current triangle that the cursor is hovering over
	UPROPERTY(BlueprintReadOnly, Category = "Floor Tool")
	ETriangleSelection CurrentHoveredTriangle = ETriangleSelection::None;

	// Tracks if room preview is currently being shown
	UPROPERTY(BlueprintReadOnly, Category = "Floor Tool")
	bool bShowingRoomPreview = false;

	// Current room ID being previewed (0 = outside, >0 = room)
	UPROPERTY(BlueprintReadOnly, Category = "Floor Tool")
	int32 CurrentRoomID = 0;

	// Array of preview floor components for room auto-fill (similar to DragCreateFloorArray)
	UPROPERTY(BlueprintReadWrite)
	TArray<UFloorComponent*> RoomPreviewFloorArray;

	// Existing tile preview tracking (similar to WallPatternTool)
	UPROPERTY(BlueprintReadOnly, Category = "Floor Tool")
	bool bShowingExistingTilePreview = false;

	UPROPERTY(BlueprintReadOnly, Category = "Floor Tool")
	int32 PreviewTileRow = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Floor Tool")
	int32 PreviewTileColumn = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Floor Tool")
	int32 PreviewTileLevel = -1;

	// Stores the original pattern of the tile being previewed
	UPROPERTY()
	UFloorPattern* OriginalTilePattern = nullptr;

	// Z-offset for preview floors to prevent Z-fighting with terrain/existing tiles (in cm)
	static constexpr float PreviewZOffset = 5.0f;

protected:
	// Determines which triangle the cursor is over based on position relative to tile center
	UFUNCTION(BlueprintCallable, Category = "Floor Tool")
	ETriangleSelection DetermineTriangleFromCursorPosition(const FVector& TileCenter, const FVector& CursorLocation);

	// Creates a TileSectionState for the specified triangle only
	UFUNCTION(BlueprintCallable, Category = "Floor Tool")
	FTileSectionState CreateSingleTriangleState(ETriangleSelection Triangle);

	// Gets all tiles belonging to a specific room
	UFUNCTION(BlueprintCallable, Category = "Floor Tool")
	TArray<FTileData> GetTilesInRoom(int32 RoomID, int32 Level);

	// Clears the room preview floors
	UFUNCTION(BlueprintCallable, Category = "Floor Tool")
	void ClearRoomPreview();

	// Shows preview floors for all tiles in the specified room
	void ShowRoomPreview(int32 RoomID, int32 Level);

	// Shows preview pattern on an existing floor tile
	void ShowExistingTilePreview(int32 Level, int32 Row, int32 Column);

	// Clears the existing tile preview and restores original pattern
	void ClearExistingTilePreview();

	// ========== Pattern-Region Detection (Paint Bucket Style) ==========

	// Get the dominant pattern at a tile location (checks all 4 triangles, returns most common)
	// Returns nullptr if tile has no floor or is mostly empty
	UFUNCTION(BlueprintCallable, Category = "Floor Tool")
	UFloorPattern* GetDominantPatternAtTile(int32 Level, int32 Row, int32 Column);

	// Flood-fill to find all contiguous tiles matching the target pattern
	// TargetPattern can be nullptr to find connected empty tiles
	// Returns array of tile coordinates (Level, Row, Column) in the connected region
	UFUNCTION(BlueprintCallable, Category = "Floor Tool")
	TArray<FIntVector> FloodFillPatternRegion(int32 Level, int32 StartRow, int32 StartColumn, UFloorPattern* TargetPattern, int32 RoomID);

	// Show preview for a specific pattern region (used for intelligent fill)
	void ShowPatternRegionPreview(const TArray<FIntVector>& TileCoords, int32 Level);

	// ========== Triangle-Level Pattern Detection (Diagonal Wall Support) ==========

	// Get all adjacent triangles for a given triangle coordinate
	// Returns up to 3 neighbors: 2 same-tile + 1 cross-tile
	TArray<FTriangleCoord> GetAdjacentTriangles(const FTriangleCoord& Coord);

	// Get the pattern applied to a specific triangle (not tile-level dominant)
	// Returns nullptr if the triangle has no floor
	UFloorPattern* GetPatternAtTriangle(int32 Level, int32 Row, int32 Column, ETriangleType Triangle);

	// Get room ID for a specific triangle using RoomManager's InteriorTriangles
	// Returns 0 if triangle is outside all rooms
	int32 GetRoomIDAtTriangle(int32 Level, int32 Row, int32 Column, ETriangleType Triangle);

	// Convert ETriangleSelection to ETriangleType
	ETriangleType SelectionToTriangleType(ETriangleSelection Selection);

	// Triangle-level flood-fill for pattern regions (handles diagonal walls correctly)
	// Returns array of triangle coordinates in the connected region
	TArray<FTriangleCoord> FloodFillPatternRegionTriangles(
		int32 Level,
		int32 StartRow,
		int32 StartColumn,
		ETriangleType StartTriangle,
		UFloorPattern* TargetPattern,
		int32 RoomID);

	// Show preview for triangle-level pattern region
	void ShowPatternRegionPreviewTriangles(const TArray<FTriangleCoord>& Triangles, int32 Level);

	// Check if a wall blocks movement between two triangles
	// Handles both same-tile diagonal walls and cross-tile edge walls
	bool IsWallBlockingTriangles(const FTriangleCoord& From, const FTriangleCoord& To) const;

	// Count distinct floor patterns used in a room
	// Returns 0 if room has no floors, 1 if single pattern, 2+ if multiple patterns
	int32 CountPatternsInRoom(int32 RoomID, int32 Level) const;

	// Creates a dynamic material with the floor pattern applied
	UFUNCTION(BlueprintCallable, Category = "Floor Tool")
	UMaterialInstanceDynamic* CreatePatternedMaterial(UMaterialInstance* BaseMaterialTemplate, UFloorPattern* Pattern);

protected:
	// Override OnMoved from parent to handle floor preview creation
	virtual void OnMoved_Implementation() override;

	// Current traced level - updated during Move() and used by OnMoved()
	UPROPERTY()
	int32 CurrentTracedLevel = 0;

	// Room ID where drag operation started - used to constrain drag to same room
	UPROPERTY()
	int32 DragStartRoomID = 0;

	// Flag to indicate we're committing a room fill (shift+click) operation
	// When true, Drag_Implementation is skipped and BroadcastRelease uses per-triangle processing
	UPROPERTY()
	bool bIsRoomFillOperation = false;

	// Helper to get room ID at a specific tile location using RoomManager polygon check
	// Returns 0 if tile is outside all rooms
	int32 GetRoomIDAtTile(int32 Level, int32 Row, int32 Column);

private:
	// Cache for reusing preview floor components during drag
	TMap<FVector, UFloorComponent*> PreviewFloorCache;

	// Helper function to clean up preview cache
	void ClearPreviewCache();
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Components/WallComponent.h"
#include "WallPatternTool.generated.h"

// Identifies which face of a wall was hit by a trace
UENUM(BlueprintType)
enum class EWallFace : uint8
{
	PosY,      // Main face adjacent to Room1 (uses WallCoveringA)
	NegY,      // Main face adjacent to Room2 (uses WallCoveringB)
	StartCap,  // End cap at wall start
	EndCap,    // End cap at wall end
	Unknown    // Could not determine face
};

UCLASS()
class BURBARCHITECT_API AWallPatternTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AWallPatternTool();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	// ========== Network RPCs for Pattern Application ==========

	// Single wall pattern application
	UFUNCTION(Server, Reliable)
	void Server_ApplySingleWallPattern(int32 WallArrayIndex, EWallFace FaceType, UWallPattern* Pattern, int32 SwatchIndex);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplySingleWallPattern(int32 WallArrayIndex, EWallFace FaceType, UWallPattern* Pattern, int32 SwatchIndex);

	// Room walls pattern application
	UFUNCTION(Server, Reliable)
	void Server_ApplyRoomWallPatterns(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID, UWallPattern* Pattern, int32 SwatchIndex);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyRoomWallPatterns(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID, UWallPattern* Pattern, int32 SwatchIndex);

	// Drag painted walls pattern application
	UFUNCTION(Server, Reliable)
	void Server_ApplyDragPaintedWalls(const TArray<int32>& WallIndices, const TArray<EWallFace>& FaceTypes, UWallPattern* Pattern, int32 SwatchIndex);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyDragPaintedWalls(const TArray<int32>& WallIndices, const TArray<EWallFace>& FaceTypes, UWallPattern* Pattern, int32 SwatchIndex);

	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	int32 HitMeshSection = 0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	UWallComponent* HitWallComponent;
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector HitWallNormal;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float HorizontalSnap = 50.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float VerticalSnap = 25.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bSnapsToFloor = false;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bValidPlacementLocation = false;
	
	//Class of portal/window/door to spawn
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TSoftClassPtr<APortalBase> ClassToSpawn;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Wall Pattern Tool")
	class UWallPattern* SelectedWallPattern;

	// Index of the currently selected color swatch from SelectedWallPattern->ColourSwatches
	// Set by the swatch picker UI when user selects a color
	UPROPERTY(BlueprintReadWrite, Category = "Wall Pattern Tool")
	int32 SelectedSwatchIndex = 0;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Wall Pattern Tool")
	UMaterialInstance* BaseMaterial;

	FWallTextures DefaultWallTextures;

	// Tracks if room wall preview is currently being shown
	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	bool bShowingRoomWallPreview = false;

	// Current room ID being previewed (0 = outside/exterior, >0 = interior room)
	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	int32 CurrentPreviewRoomID = -1;

	// Whether we're previewing interior or exterior walls
	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	bool bPreviewingInterior = false;

	// Starting edge ID for exterior preview (used to get only connected walls)
	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	int32 PreviewStartEdgeID = -1;

	// Stores the original textures and face type of walls being previewed (for restoration)
	struct FRoomPreviewData
	{
		FWallTextures OriginalTextures;
		EWallFace ModifiedFace = EWallFace::Unknown;

		// Additional texture maps not stored in FWallTextures
		UTexture* OriginalNormalA = nullptr;
		UTexture* OriginalNormalB = nullptr;
		UTexture* OriginalRoughnessA = nullptr;
		UTexture* OriginalRoughnessB = nullptr;

		// Detail normal textures
		UTexture* OriginalDetailNormalA = nullptr;
		UTexture* OriginalDetailNormalB = nullptr;

		// Color vectors
		FLinearColor OriginalColourA = FLinearColor::White;
		FLinearColor OriginalColourB = FLinearColor::White;

		// Swatch scalar parameters
		float OriginalUseColourSwatchesA = 0.0f;
		float OriginalUseColourMaskA = 0.0f;
		float OriginalUseColourSwatchesB = 0.0f;
		float OriginalUseColourMaskB = 0.0f;

		// Detail normal scalar parameters
		float OriginalUseDetailNormalA = 0.0f;
		float OriginalUseDetailNormalB = 0.0f;
		float OriginalDetailNormalIntensityA = 0.0f;
		float OriginalDetailNormalIntensityB = 0.0f;
	};
	TMap<int32, FRoomPreviewData> OriginalWallTextures;

	// Single wall preview tracking
	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	bool bShowingSingleWallPreview = false;

	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	int32 PreviewWallArrayIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	EWallFace PreviewFaceType = EWallFace::Unknown;

	// Stores the original texture of the single wall being previewed
	FWallTextures OriginalPreviewTextures;

	// Stores original normal and roughness maps for single wall preview
	UTexture* OriginalPreviewNormalA = nullptr;
	UTexture* OriginalPreviewNormalB = nullptr;
	UTexture* OriginalPreviewRoughnessA = nullptr;
	UTexture* OriginalPreviewRoughnessB = nullptr;

	// Stores original color vectors for single wall preview
	FLinearColor OriginalPreviewColourA = FLinearColor::White;
	FLinearColor OriginalPreviewColourB = FLinearColor::White;

	// Stores original swatch scalar parameters for single wall preview
	float OriginalPreviewbUseColourSwatchesA = 0.0f;
	float OriginalPreviewbUseColourMaskA = 0.0f;
	float OriginalPreviewbUseColourSwatchesB = 0.0f;
	float OriginalPreviewbUseColourMaskB = 0.0f;

	// Stores original detail normal parameters for single wall preview
	UTexture* OriginalPreviewDetailNormalA = nullptr;
	UTexture* OriginalPreviewDetailNormalB = nullptr;
	float OriginalPreviewbUseDetailNormalA = 0.0f;
	float OriginalPreviewbUseDetailNormalB = 0.0f;
	float OriginalPreviewDetailNormalIntensityA = 0.0f;
	float OriginalPreviewDetailNormalIntensityB = 0.0f;

	// Drag painting tracking
	UPROPERTY(BlueprintReadOnly, Category = "Wall Pattern Tool")
	bool bIsDragPainting = false;

	// Map of walls painted during drag: WallArrayIndex -> FaceType
	// Stores which face of each wall should be painted
	TMap<int32, EWallFace> DragPaintedWalls;

	// Stores original textures for all walls in drag operation (for preview restoration)
	TMap<int32, FRoomPreviewData> DragPreviewData;

protected:
	// Gets all wall segments for a specific room (interior or exterior)
	UFUNCTION(BlueprintCallable, Category = "Wall Pattern Tool")
	TArray<FWallSegmentData> GetWallsInRoom(int32 RoomID, int32 Level, bool bInterior);

	// Shows preview texture on all walls in the specified room
	// StartEdgeID is used for exterior mode to get only connected walls
	void ShowRoomWallPreview(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID = -1);

	// Clears the room wall preview and restores original textures
	UFUNCTION(BlueprintCallable, Category = "Wall Pattern Tool")
	void ClearRoomWallPreview();

	// Applies wallpaper to all walls in the room
	// StartEdgeID is used for exterior mode to get only connected walls
	void ApplyTextureToRoomWalls(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID = -1);

	// Shows preview texture on a single wall face
	void ShowSingleWallPreview(int32 WallArrayIndex, EWallFace FaceType);

	// Clears the single wall preview and restores original texture
	void ClearSingleWallPreview();

	// Determines which face of a wall was hit using grid-based room detection
	// Works for walls at any orientation and regardless of wall build direction
	UFUNCTION(BlueprintCallable, Category = "Wall Pattern Tool")
	EWallFace DetermineHitFace(const FWallSegmentData& Wall, const FVector& HitLocation, const FVector& HitNormal) const;

	// Determines which face of a wall faces into the specified room
	// Uses wall orientation logic to correctly map Room1/Room2 to PosY/NegY
	EWallFace GetFaceFacingRoom(const FWallSegmentData& Wall, int32 RoomID) const;

	// Returns the RoomID that is on the specified face of the wall
	// Inverse of GetFaceFacingRoom - accounts for wall orientation
	int32 GetRoomOnFace(const FWallSegmentData& Wall, EWallFace Face) const;

	// Gets all exterior walls connected to the starting wall via shared nodes
	// Uses BFS to find only walls that are part of the same continuous structure
	TArray<FWallSegmentData> GetConnectedExteriorWalls(int32 StartEdgeID, int32 Level);

	// Shows preview texture on all walls in the drag selection
	void ShowDragPaintPreview();

	// Clears the drag paint preview and restores original textures
	void ClearDragPaintPreview();

	// Helper function to apply pattern to a single wall face (used by all multicast implementations)
	// This is the actual pattern application logic - called on all machines
	void ApplyPatternToWallFace(int32 WallArrayIndex, EWallFace FaceType, UWallPattern* Pattern, int32 SwatchIndex);

	// UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Material Texture Parameters")
	// FName RightFaceParam; // 0.1.0 A
	//
	// UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Material Texture Parameters")
	// FName ForwardFaceParam; // 1.0.0 B
	//
	// UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Material Texture Parameters")
	// FName LeftFaceParam; // 0.-1.0 C
	//
	// UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Material Texture Parameters")
	// FName BackwardFaceParam; // -1.0.0 D
};

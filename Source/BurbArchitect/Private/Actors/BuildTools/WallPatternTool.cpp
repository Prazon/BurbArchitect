// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/BuildTools/WallPatternTool.h"

#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Data/WallPattern.h"
#include "Net/UnrealNetwork.h"

AWallPatternTool::AWallPatternTool()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Use wall trace channel to detect wall surfaces
	TraceChannel = ECC_GameTraceChannel1;
}

void AWallPatternTool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void AWallPatternTool::BeginPlay()
{
	Super::BeginPlay();
}

void AWallPatternTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up previews when tool is destroyed
	ClearSingleWallPreview();
	ClearRoomWallPreview();
	ClearDragPaintPreview();
	DragPaintedWalls.Empty();
	bIsDragPainting = false;

	Super::EndPlay(EndPlayReason);
}

EWallFace AWallPatternTool::DetermineHitFace(const FWallSegmentData& Wall, const FVector& HitLocation, const FVector& HitNormal) const
{
	// The mesh is generated in local space with PosY face having normal (0,1,0)
	// Then rotated by WallDirection.Rotation()
	// After rotation, PosY face points perpendicular to wall direction (left side when looking along wall)

	// Calculate wall direction from world positions (this matches mesh generation)
	FVector WallDir = (Wall.EndLoc - Wall.StartLoc).GetSafeNormal2D();

	// The perpendicular (where PosY face points after rotation) is 90 degrees CCW from wall direction
	// For wall going +X: perp = +Y
	// For wall going +Y: perp = -X
	// This is (-WallDir.Y, WallDir.X)
	FVector PosYWorldNormal = FVector(-WallDir.Y, WallDir.X, 0.0f);

	// PRIMARY METHOD: Use hit normal (works for most walls)
	// Flatten hit normal to 2D
	FVector HitNormal2D = FVector(HitNormal.X, HitNormal.Y, 0.0f);
	float HitNormal2DLength = HitNormal2D.Size();

	// Only use normal method if the 2D component is significant (not a vertical/top face hit)
	if (HitNormal2DLength > 0.3f)
	{
		HitNormal2D /= HitNormal2DLength; // Normalize
		float NormalDot = FVector::DotProduct(HitNormal2D, PosYWorldNormal);

		if (NormalDot > 0.1f) return EWallFace::PosY;
		if (NormalDot < -0.1f) return EWallFace::NegY;
	}

	// FALLBACK METHOD: Use hit location relative to wall center line
	// This is more reliable for trimmed/diagonal walls where normals may be unreliable
	// The wall faces are offset from the center line by half the wall thickness
	FVector WallCenter = (Wall.StartLoc + Wall.EndLoc) * 0.5f;
	WallCenter.Z = HitLocation.Z; // Match Z for 2D comparison

	FVector ToHit = HitLocation - WallCenter;
	ToHit.Z = 0.0f;

	// Project hit offset onto perpendicular direction
	float LocationDot = FVector::DotProduct(ToHit, PosYWorldNormal);

	// Wall thickness is typically 20 units (10 per side from center)
	// Use a small threshold to determine which face was hit
	if (LocationDot > 0.5f) return EWallFace::PosY;
	if (LocationDot < -0.5f) return EWallFace::NegY;

	return EWallFace::Unknown;
}

EWallFace AWallPatternTool::GetFaceFacingRoom(const FWallSegmentData& Wall, int32 RoomID) const
{
	// Handle decorative walls (half walls, etc.) that don't have WallGraph edges
	if (Wall.WallEdgeID == -1)
	{
		// For decorative walls without room data, treat both faces as exterior (room 0)
		// If looking for room 0 (exterior), return PosY as default face
		// Otherwise, no face matches since decorative walls don't define rooms
		if (RoomID == 0)
		{
			// Both faces are considered exterior, return PosY as default
			return EWallFace::PosY;
		}
		return EWallFace::Unknown;
	}

	if (!CurrentLot || !CurrentLot->WallGraph)
	{
		return EWallFace::Unknown;
	}

	// Get Room1/Room2 from WallGraph
	int32 Room1 = 0;
	int32 Room2 = 0;
	const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(Wall.WallEdgeID);
	if (Edge)
	{
		Room1 = Edge->Room1;
		Room2 = Edge->Room2;
	}

	// WallGraph convention: Room1 is on +perpendicular side where perp = (-WallDir.Y, WallDir.X)
	// Mesh convention: PosY face points in +perpendicular direction after rotation
	// Therefore: PosY face faces Room1, NegY face faces Room2
	// This is consistent for ALL wall orientations (horizontal, vertical, diagonal)

	if (RoomID == Room1)
	{
		return EWallFace::PosY;
	}
	else if (RoomID == Room2)
	{
		return EWallFace::NegY;
	}

	return EWallFace::Unknown;
}

int32 AWallPatternTool::GetRoomOnFace(const FWallSegmentData& Wall, EWallFace Face) const
{
	if (Face == EWallFace::Unknown)
	{
		return -1;
	}

	// Handle decorative walls (half walls, etc.) that don't have WallGraph edges
	if (Wall.WallEdgeID == -1)
	{
		// Decorative walls are treated as exterior on both sides
		return 0;
	}

	if (!CurrentLot || !CurrentLot->WallGraph)
	{
		return -1;
	}

	// Get Room1/Room2 from WallGraph
	int32 Room1 = 0;
	int32 Room2 = 0;
	const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(Wall.WallEdgeID);
	if (Edge)
	{
		Room1 = Edge->Room1;
		Room2 = Edge->Room2;
	}

	// Simple mapping (inverse of GetFaceFacingRoom):
	// PosY face faces Room1, NegY face faces Room2
	// This is consistent for ALL wall orientations
	if (Face == EWallFace::PosY)
	{
		return Room1;
	}
	else if (Face == EWallFace::NegY)
	{
		return Room2;
	}

	return -1;
}

TArray<FWallSegmentData> AWallPatternTool::GetWallsInRoom(int32 RoomID, int32 Level, bool bInterior)
{
	TArray<FWallSegmentData> RoomWalls;

	if (!CurrentLot || !CurrentLot->WallComponent)
	{
		return RoomWalls;
	}

	// Iterate through all wall segments
	for (const FWallSegmentData& Wall : CurrentLot->WallComponent->WallDataArray)
	{
		// Only consider walls on the same level
		if (Wall.Level != Level)
		{
			continue;
		}

		// Query RoomIDs from WallGraph using WallEdgeID
		int32 Room1 = 0;
		int32 Room2 = 0;
		if (Wall.WallEdgeID != -1 && CurrentLot->WallGraph)
		{
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(Wall.WallEdgeID);
			if (Edge)
			{
				Room1 = Edge->Room1;
				Room2 = Edge->Room2;
			}
		}

		// Check if this wall belongs to the room/category
		if (bInterior)
		{
			// For interior walls, get ALL walls that touch this room
			// We'll paint the face facing INTO the room
			if (Room1 == RoomID || Room2 == RoomID)
			{
				RoomWalls.Add(Wall);
			}
		}
		else
		{
			// For exterior walls, get walls with exactly ONE exterior face
			// Excludes freestanding walls (both Room1=0 and Room2=0) - those are painted individually
			if ((Room1 == 0) != (Room2 == 0))
			{
				RoomWalls.Add(Wall);
			}
		}
	}

	return RoomWalls;
}

TArray<FWallSegmentData> AWallPatternTool::GetConnectedExteriorWalls(int32 StartEdgeID, int32 Level)
{
	TArray<FWallSegmentData> ConnectedWalls;

	if (!CurrentLot || !CurrentLot->WallGraph || !CurrentLot->WallComponent)
	{
		return ConnectedWalls;
	}

	// BFS to find all connected exterior walls
	TSet<int32> VisitedEdges;
	TQueue<int32> EdgeQueue;

	// Start with the initial edge
	EdgeQueue.Enqueue(StartEdgeID);
	VisitedEdges.Add(StartEdgeID);

	while (!EdgeQueue.IsEmpty())
	{
		int32 CurrentEdgeID;
		EdgeQueue.Dequeue(CurrentEdgeID);

		const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(CurrentEdgeID);
		if (!Edge || Edge->Level != Level)
		{
			continue;
		}

		// Check if this is an exterior wall (exactly one side is room 0)
		bool bIsExterior = (Edge->Room1 == 0) != (Edge->Room2 == 0);
		if (!bIsExterior)
		{
			continue;
		}

		// Find the corresponding WallSegmentData
		for (const FWallSegmentData& Wall : CurrentLot->WallComponent->WallDataArray)
		{
			if (Wall.WallEdgeID == CurrentEdgeID && Wall.Level == Level)
			{
				ConnectedWalls.Add(Wall);
				break;
			}
		}

		// Get connected edges via FromNode
		const FWallNode* FromNode = CurrentLot->WallGraph->Nodes.Find(Edge->FromNodeID);
		if (FromNode)
		{
			for (int32 ConnectedEdgeID : FromNode->ConnectedEdgeIDs)
			{
				if (!VisitedEdges.Contains(ConnectedEdgeID))
				{
					VisitedEdges.Add(ConnectedEdgeID);
					EdgeQueue.Enqueue(ConnectedEdgeID);
				}
			}
		}

		// Get connected edges via ToNode
		const FWallNode* ToNode = CurrentLot->WallGraph->Nodes.Find(Edge->ToNodeID);
		if (ToNode)
		{
			for (int32 ConnectedEdgeID : ToNode->ConnectedEdgeIDs)
			{
				if (!VisitedEdges.Contains(ConnectedEdgeID))
				{
					VisitedEdges.Add(ConnectedEdgeID);
					EdgeQueue.Enqueue(ConnectedEdgeID);
				}
			}
		}
	}

	return ConnectedWalls;
}

void AWallPatternTool::ShowRoomWallPreview(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID)
{
	// Clear any existing preview
	ClearRoomWallPreview();

	// Get walls - for exterior mode, only get connected walls; for interior, get all room walls
	TArray<FWallSegmentData> RoomWalls;
	if (bInterior)
	{
		RoomWalls = GetWallsInRoom(RoomID, Level, bInterior);
	}
	else
	{
		// Exterior mode - get only walls connected to the starting wall
		if (StartEdgeID >= 0)
		{
			RoomWalls = GetConnectedExteriorWalls(StartEdgeID, Level);
		}
		else
		{
			// Fallback if no StartEdgeID provided
			RoomWalls = GetWallsInRoom(RoomID, Level, bInterior);
		}
	}

	if (RoomWalls.Num() == 0)
	{
		return;
	}

	// Store original textures and apply preview texture directly to materials
	for (const FWallSegmentData& Wall : RoomWalls)
	{
		if (Wall.WallArrayIndex < 0 || Wall.WallArrayIndex >= CurrentLot->WallComponent->WallDataArray.Num())
		{
			continue;
		}

		FWallSegmentData& WallRef = CurrentLot->WallComponent->WallDataArray[Wall.WallArrayIndex];

		if (!WallRef.WallMaterial || !SelectedWallPattern)
		{
			continue;
		}

		// Get Room1/Room2 for this wall
		int32 Room1 = 0, Room2 = 0;
		if (WallRef.WallEdgeID != -1 && CurrentLot->WallGraph)
		{
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(WallRef.WallEdgeID);
			if (Edge)
			{
				Room1 = Edge->Room1;
				Room2 = Edge->Room2;
			}
		}

		// Capture original textures from material
		FRoomPreviewData PreviewData;
		PreviewData.OriginalTextures = WallRef.WallTextures;
		UTexture* CurrentTextureA = nullptr;
		UTexture* CurrentTextureB = nullptr;
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallCoveringA"), CurrentTextureA);
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallCoveringB"), CurrentTextureB);
		if (CurrentTextureA)
			PreviewData.OriginalTextures.RightFaceTexture = Cast<UTexture2D>(CurrentTextureA);
		if (CurrentTextureB)
			PreviewData.OriginalTextures.LeftFaceTexture = Cast<UTexture2D>(CurrentTextureB);

		// Capture normal and roughness maps
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallNormalA"), PreviewData.OriginalNormalA);
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallNormalB"), PreviewData.OriginalNormalB);
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallRoughnessA"), PreviewData.OriginalRoughnessA);
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallRoughnessB"), PreviewData.OriginalRoughnessB);

		// Capture detail normal textures
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("DetailNormalA"), PreviewData.OriginalDetailNormalA);
		WallRef.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("DetailNormalB"), PreviewData.OriginalDetailNormalB);

		// Capture color vectors
		WallRef.WallMaterial->GetVectorParameterValue(FMaterialParameterInfo("WallColourA"), PreviewData.OriginalColourA);
		WallRef.WallMaterial->GetVectorParameterValue(FMaterialParameterInfo("WallColourB"), PreviewData.OriginalColourB);

		// Capture swatch scalar parameters
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourSwatchesA"), PreviewData.OriginalUseColourSwatchesA);
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourMaskA"), PreviewData.OriginalUseColourMaskA);
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourSwatchesB"), PreviewData.OriginalUseColourSwatchesB);
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourMaskB"), PreviewData.OriginalUseColourMaskB);

		// Capture detail normal scalar parameters
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseDetailNormalA"), PreviewData.OriginalUseDetailNormalA);
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseDetailNormalB"), PreviewData.OriginalUseDetailNormalB);
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("DetailNormalIntensityA"), PreviewData.OriginalDetailNormalIntensityA);
		WallRef.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("DetailNormalIntensityB"), PreviewData.OriginalDetailNormalIntensityB);

		// Determine which face(s) to modify
		bool bPaintPosY = false;
		bool bPaintNegY = false;

		if (bInterior)
		{
			// Interior: paint the face facing INTO the room
			EWallFace FaceToModify = GetFaceFacingRoom(WallRef, RoomID);
			if (FaceToModify == EWallFace::PosY) bPaintPosY = true;
			else if (FaceToModify == EWallFace::NegY) bPaintNegY = true;
			PreviewData.ModifiedFace = FaceToModify;

		}
		else
		{
			// Exterior: paint all faces that face exterior (room 0)
			// For walls with both sides = 0, paint both faces
			if (Room1 == 0)
			{
				EWallFace Face1 = GetFaceFacingRoom(WallRef, Room1);
				if (Face1 == EWallFace::PosY) bPaintPosY = true;
				else if (Face1 == EWallFace::NegY) bPaintNegY = true;
			}
			if (Room2 == 0)
			{
				EWallFace Face2 = GetFaceFacingRoom(WallRef, Room2);
				if (Face2 == EWallFace::PosY) bPaintPosY = true;
				else if (Face2 == EWallFace::NegY) bPaintNegY = true;
			}
			// Track which faces were modified (use a special value for "both")
			if (bPaintPosY && bPaintNegY)
				PreviewData.ModifiedFace = EWallFace::StartCap; // Repurpose as "both" marker
			else if (bPaintPosY)
				PreviewData.ModifiedFace = EWallFace::PosY;
			else if (bPaintNegY)
				PreviewData.ModifiedFace = EWallFace::NegY;
			else
				PreviewData.ModifiedFace = EWallFace::Unknown;
		}

		if (!bPaintPosY && !bPaintNegY)
		{
			continue;
		}

		OriginalWallTextures.Add(Wall.WallArrayIndex, PreviewData);

		// Apply preview texture to the selected face(s)
		// PosY uses WallCoveringB, NegY uses WallCoveringA
		if (bPaintPosY)
		{
			if (SelectedWallPattern->BaseTexture)
				WallRef.WallMaterial->SetTextureParameterValue("WallCoveringB", SelectedWallPattern->BaseTexture);
			if (SelectedWallPattern->NormalMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallNormalB", SelectedWallPattern->NormalMap);
			if (SelectedWallPattern->RoughnessMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallRoughnessB", SelectedWallPattern->RoughnessMap);

			// Apply detail normal
			WallRef.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				WallRef.WallMaterial->SetTextureParameterValue("DetailNormalB", SelectedWallPattern->DetailNormal);
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", 0.0f);
			}

			// Apply color swatch to this face only using per-face switches
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			bool bApplySwatchB = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", bApplySwatchB ? 1.0f : 0.0f);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourMaskB", (bApplySwatchB && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchB)
			{
				WallRef.WallMaterial->SetVectorParameterValue("WallColourB", SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1]);
			}
		}
		if (bPaintNegY)
		{
			if (SelectedWallPattern->BaseTexture)
				WallRef.WallMaterial->SetTextureParameterValue("WallCoveringA", SelectedWallPattern->BaseTexture);
			if (SelectedWallPattern->NormalMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallNormalA", SelectedWallPattern->NormalMap);
			if (SelectedWallPattern->RoughnessMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallRoughnessA", SelectedWallPattern->RoughnessMap);

			// Apply detail normal
			WallRef.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				WallRef.WallMaterial->SetTextureParameterValue("DetailNormalA", SelectedWallPattern->DetailNormal);
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", 0.0f);
			}

			// Apply color swatch to this face only using per-face switches
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			bool bApplySwatchA = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", bApplySwatchA ? 1.0f : 0.0f);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourMaskA", (bApplySwatchA && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchA)
			{
				WallRef.WallMaterial->SetVectorParameterValue("WallColourA", SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1]);
			}
		}
	}

	bShowingRoomWallPreview = true;
	CurrentPreviewRoomID = RoomID;
	bPreviewingInterior = bInterior;
}

void AWallPatternTool::ClearRoomWallPreview()
{
	if (!bShowingRoomWallPreview || !CurrentLot || !CurrentLot->WallComponent)
	{
		return;
	}

	// Restore original textures using stored face info
	for (const TPair<int32, FRoomPreviewData>& Entry : OriginalWallTextures)
	{
		int32 WallIndex = Entry.Key;
		const FRoomPreviewData& PreviewData = Entry.Value;

		if (WallIndex >= 0 && WallIndex < CurrentLot->WallComponent->WallDataArray.Num())
		{
			FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallIndex];

			if (Wall.WallMaterial)
			{
				// Restore based on which face was modified
				// PosY uses WallCoveringB (LeftFaceTexture), NegY uses WallCoveringA (RightFaceTexture)
				// StartCap is used as marker for "both faces"
				if (PreviewData.ModifiedFace == EWallFace::PosY || PreviewData.ModifiedFace == EWallFace::StartCap)
				{
					Wall.WallMaterial->SetTextureParameterValue("WallCoveringB", Cast<UTexture>(PreviewData.OriginalTextures.LeftFaceTexture));
					Wall.WallMaterial->SetTextureParameterValue("WallNormalB", PreviewData.OriginalNormalB);
					Wall.WallMaterial->SetTextureParameterValue("WallRoughnessB", PreviewData.OriginalRoughnessB);
					Wall.WallMaterial->SetTextureParameterValue("DetailNormalB", PreviewData.OriginalDetailNormalB);

					// Restore color vector
					Wall.WallMaterial->SetVectorParameterValue("WallColourB", PreviewData.OriginalColourB);

					// Restore swatch scalar parameters
					Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", PreviewData.OriginalUseColourSwatchesB);
					Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskB", PreviewData.OriginalUseColourMaskB);

					// Restore detail normal scalar parameters
					Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", PreviewData.OriginalUseDetailNormalB);
					Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", PreviewData.OriginalDetailNormalIntensityB);
				}
				if (PreviewData.ModifiedFace == EWallFace::NegY || PreviewData.ModifiedFace == EWallFace::StartCap)
				{
					Wall.WallMaterial->SetTextureParameterValue("WallCoveringA", Cast<UTexture>(PreviewData.OriginalTextures.RightFaceTexture));
					Wall.WallMaterial->SetTextureParameterValue("WallNormalA", PreviewData.OriginalNormalA);
					Wall.WallMaterial->SetTextureParameterValue("WallRoughnessA", PreviewData.OriginalRoughnessA);
					Wall.WallMaterial->SetTextureParameterValue("DetailNormalA", PreviewData.OriginalDetailNormalA);

					// Restore color vector
					Wall.WallMaterial->SetVectorParameterValue("WallColourA", PreviewData.OriginalColourA);

					// Restore swatch scalar parameters
					Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", PreviewData.OriginalUseColourSwatchesA);
					Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskA", PreviewData.OriginalUseColourMaskA);

					// Restore detail normal scalar parameters
					Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", PreviewData.OriginalUseDetailNormalA);
					Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", PreviewData.OriginalDetailNormalIntensityA);
				}
			}
		}
	}

	OriginalWallTextures.Empty();
	bShowingRoomWallPreview = false;
	CurrentPreviewRoomID = -1;
	bPreviewingInterior = false;
	PreviewStartEdgeID = -1;
}

void AWallPatternTool::ApplyTextureToRoomWalls(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID)
{
	if (!CurrentLot || !CurrentLot->WallComponent)
	{
		return;
	}

	// Get walls - for exterior mode, only get connected walls; for interior, get all room walls
	TArray<FWallSegmentData> RoomWalls;
	if (bInterior)
	{
		RoomWalls = GetWallsInRoom(RoomID, Level, bInterior);
	}
	else
	{
		if (StartEdgeID >= 0)
		{
			RoomWalls = GetConnectedExteriorWalls(StartEdgeID, Level);
		}
		else
		{
			RoomWalls = GetWallsInRoom(RoomID, Level, bInterior);
		}
	}

	// Apply wallpaper to all walls
	for (const FWallSegmentData& Wall : RoomWalls)
	{
		if (Wall.WallArrayIndex < 0 || Wall.WallArrayIndex >= CurrentLot->WallComponent->WallDataArray.Num())
		{
			continue;
		}

		FWallSegmentData& WallRef = CurrentLot->WallComponent->WallDataArray[Wall.WallArrayIndex];

		if (!WallRef.WallMaterial || !SelectedWallPattern)
		{
			continue;
		}

		// Get Room1/Room2 for this wall
		int32 Room1 = 0, Room2 = 0;
		if (WallRef.WallEdgeID != -1 && CurrentLot->WallGraph)
		{
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(WallRef.WallEdgeID);
			if (Edge)
			{
				Room1 = Edge->Room1;
				Room2 = Edge->Room2;
			}
		}

		// Determine which face(s) to modify
		bool bPaintPosY = false;
		bool bPaintNegY = false;

		if (bInterior)
		{
			// Interior: paint the face facing INTO the room
			EWallFace FaceToModify = GetFaceFacingRoom(WallRef, RoomID);
			if (FaceToModify == EWallFace::PosY) bPaintPosY = true;
			else if (FaceToModify == EWallFace::NegY) bPaintNegY = true;
		}
		else
		{
			// Exterior: paint all faces that face exterior (room 0)
			if (Room1 == 0)
			{
				EWallFace Face1 = GetFaceFacingRoom(WallRef, Room1);
				if (Face1 == EWallFace::PosY) bPaintPosY = true;
				else if (Face1 == EWallFace::NegY) bPaintNegY = true;
			}
			if (Room2 == 0)
			{
				EWallFace Face2 = GetFaceFacingRoom(WallRef, Room2);
				if (Face2 == EWallFace::PosY) bPaintPosY = true;
				else if (Face2 == EWallFace::NegY) bPaintNegY = true;
			}
		}

		if (!bPaintPosY && !bPaintNegY)
		{
			continue;
		}

		// Store the applied pattern
		WallRef.AppliedPattern = SelectedWallPattern;

		// Apply texture to the selected face(s)
		// PosY uses WallCoveringB (LeftFaceTexture), NegY uses WallCoveringA (RightFaceTexture)
		if (bPaintPosY)
		{
			if (SelectedWallPattern->BaseTexture)
			{
				WallRef.WallMaterial->SetTextureParameterValue("WallCoveringB", SelectedWallPattern->BaseTexture);
				WallRef.WallTextures.LeftFaceTexture = SelectedWallPattern->BaseTexture;
			}
			if (SelectedWallPattern->NormalMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallNormalB", SelectedWallPattern->NormalMap);

			// Apply detail normal
			WallRef.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				WallRef.WallMaterial->SetTextureParameterValue("DetailNormalB", SelectedWallPattern->DetailNormal);
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", 0.0f);
			}
			if (SelectedWallPattern->RoughnessMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallRoughnessB", SelectedWallPattern->RoughnessMap);

			// Apply color swatch to this face only using per-face switches
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			UE_LOG(LogTemp, Warning, TEXT("ApplyTextureToRoomWalls: PosY face - bUseColourSwatches=%d, bUseColourMask=%d, SelectedSwatchIndex=%d, NumSwatches=%d"),
				SelectedWallPattern->bUseColourSwatches, SelectedWallPattern->bUseColourMask, SelectedSwatchIndex, SelectedWallPattern->ColourSwatches.Num());

			bool bApplySwatchB = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", bApplySwatchB ? 1.0f : 0.0f);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourMaskB", (bApplySwatchB && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchB)
			{
				FLinearColor SwatchColor = SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1];
				WallRef.WallMaterial->SetVectorParameterValue("WallColourB", SwatchColor);
				UE_LOG(LogTemp, Warning, TEXT("ApplyTextureToRoomWalls: Applied swatch color to WallColourB: %s"), *SwatchColor.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ApplyTextureToRoomWalls: NOT applying swatch color (bUseColourSwatches=%d, SelectedSwatchIndex=%d)"),
					SelectedWallPattern->bUseColourSwatches, SelectedSwatchIndex);
			}
		}
		if (bPaintNegY)
		{
			if (SelectedWallPattern->BaseTexture)
			{
				WallRef.WallMaterial->SetTextureParameterValue("WallCoveringA", SelectedWallPattern->BaseTexture);
				WallRef.WallTextures.RightFaceTexture = SelectedWallPattern->BaseTexture;
			}
			if (SelectedWallPattern->NormalMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallNormalA", SelectedWallPattern->NormalMap);

			// Apply detail normal
			WallRef.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				WallRef.WallMaterial->SetTextureParameterValue("DetailNormalA", SelectedWallPattern->DetailNormal);
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				WallRef.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", 0.0f);
			}
			if (SelectedWallPattern->RoughnessMap)
				WallRef.WallMaterial->SetTextureParameterValue("WallRoughnessA", SelectedWallPattern->RoughnessMap);

			// Apply color swatch to this face only using per-face switches
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			UE_LOG(LogTemp, Warning, TEXT("ApplyTextureToRoomWalls: NegY face - bUseColourSwatches=%d, bUseColourMask=%d, SelectedSwatchIndex=%d, NumSwatches=%d"),
				SelectedWallPattern->bUseColourSwatches, SelectedWallPattern->bUseColourMask, SelectedSwatchIndex, SelectedWallPattern->ColourSwatches.Num());

			bool bApplySwatchA = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", bApplySwatchA ? 1.0f : 0.0f);
			WallRef.WallMaterial->SetScalarParameterValue("bUseColourMaskA", (bApplySwatchA && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchA)
			{
				FLinearColor SwatchColor = SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1];
				WallRef.WallMaterial->SetVectorParameterValue("WallColourA", SwatchColor);
				UE_LOG(LogTemp, Warning, TEXT("ApplyTextureToRoomWalls: Applied swatch color to WallColourA: %s"), *SwatchColor.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ApplyTextureToRoomWalls: NOT applying swatch color (bUseColourSwatches=%d, SelectedSwatchIndex=%d)"),
					SelectedWallPattern->bUseColourSwatches, SelectedSwatchIndex);
			}
		}
	}
}

void AWallPatternTool::ShowSingleWallPreview(int32 WallArrayIndex, EWallFace FaceType)
{
	if (!CurrentLot || !CurrentLot->WallComponent || WallArrayIndex < 0 || WallArrayIndex >= CurrentLot->WallComponent->WallDataArray.Num())
	{
		return;
	}

	// Check if we're already previewing this exact wall face
	if (bShowingSingleWallPreview && PreviewWallArrayIndex == WallArrayIndex && PreviewFaceType == FaceType)
	{
		// Already showing preview for this face, no need to update
		return;
	}

	// If we're already showing a different preview, clear it first
	if (bShowingSingleWallPreview)
	{
		ClearSingleWallPreview();
	}

	FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallArrayIndex];

	// Store original textures - read directly from material to capture actual current textures
	OriginalPreviewTextures = Wall.WallTextures;

	// Also capture the actual texture from the material in case WallTextures struct is out of sync
	if (Wall.WallMaterial)
	{
		UTexture* CurrentTextureA = nullptr;
		UTexture* CurrentTextureB = nullptr;
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallCoveringA"), CurrentTextureA);
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallCoveringB"), CurrentTextureB);

		// Store in OriginalPreviewTextures if we got valid textures from the material
		if (CurrentTextureA)
			OriginalPreviewTextures.RightFaceTexture = Cast<UTexture2D>(CurrentTextureA);
		if (CurrentTextureB)
			OriginalPreviewTextures.LeftFaceTexture = Cast<UTexture2D>(CurrentTextureB);

		// Capture normal and roughness maps
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallNormalA"), OriginalPreviewNormalA);
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallNormalB"), OriginalPreviewNormalB);
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallRoughnessA"), OriginalPreviewRoughnessA);
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallRoughnessB"), OriginalPreviewRoughnessB);

		// Capture original color vectors
		Wall.WallMaterial->GetVectorParameterValue(FMaterialParameterInfo("WallColourA"), OriginalPreviewColourA);
		Wall.WallMaterial->GetVectorParameterValue(FMaterialParameterInfo("WallColourB"), OriginalPreviewColourB);

		// Capture original swatch scalar parameters
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourSwatchesA"), OriginalPreviewbUseColourSwatchesA);
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourMaskA"), OriginalPreviewbUseColourMaskA);
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourSwatchesB"), OriginalPreviewbUseColourSwatchesB);
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourMaskB"), OriginalPreviewbUseColourMaskB);

		// Capture original detail normal parameters
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("DetailNormalA"), OriginalPreviewDetailNormalA);
		Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("DetailNormalB"), OriginalPreviewDetailNormalB);
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseDetailNormalA"), OriginalPreviewbUseDetailNormalA);
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseDetailNormalB"), OriginalPreviewbUseDetailNormalB);
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("DetailNormalIntensityA"), OriginalPreviewDetailNormalIntensityA);
		Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("DetailNormalIntensityB"), OriginalPreviewDetailNormalIntensityB);
	}

	// Apply preview texture directly to the material (lightweight - no mesh regeneration)
	if (Wall.WallMaterial && SelectedWallPattern)
	{
		// PosY and StartCap use WallCoveringB (the face normal points away from this side)
		// NegY and EndCap use WallCoveringA
		if (FaceType == EWallFace::PosY || FaceType == EWallFace::StartCap)
		{
			if (SelectedWallPattern->BaseTexture)
				Wall.WallMaterial->SetTextureParameterValue("WallCoveringB", SelectedWallPattern->BaseTexture);
			if (SelectedWallPattern->NormalMap)
				Wall.WallMaterial->SetTextureParameterValue("WallNormalB", SelectedWallPattern->NormalMap);
			if (SelectedWallPattern->RoughnessMap)
				Wall.WallMaterial->SetTextureParameterValue("WallRoughnessB", SelectedWallPattern->RoughnessMap);

			// Apply detail normal
			Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				Wall.WallMaterial->SetTextureParameterValue("DetailNormalB", SelectedWallPattern->DetailNormal);
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", 0.0f);
			}

			// Apply color swatch to this face only using per-face switches
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			bool bApplySwatchB = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", bApplySwatchB ? 1.0f : 0.0f);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskB", (bApplySwatchB && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchB)
			{
				Wall.WallMaterial->SetVectorParameterValue("WallColourB", SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1]);
			}
		}
		else if (FaceType == EWallFace::NegY || FaceType == EWallFace::EndCap)
		{
			if (SelectedWallPattern->BaseTexture)
				Wall.WallMaterial->SetTextureParameterValue("WallCoveringA", SelectedWallPattern->BaseTexture);
			if (SelectedWallPattern->NormalMap)
				Wall.WallMaterial->SetTextureParameterValue("WallNormalA", SelectedWallPattern->NormalMap);
			if (SelectedWallPattern->RoughnessMap)
				Wall.WallMaterial->SetTextureParameterValue("WallRoughnessA", SelectedWallPattern->RoughnessMap);

			// Apply detail normal
			Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				Wall.WallMaterial->SetTextureParameterValue("DetailNormalA", SelectedWallPattern->DetailNormal);
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", 0.0f);
			}

			// Apply color swatch to this face only using per-face switches
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			bool bApplySwatchA = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", bApplySwatchA ? 1.0f : 0.0f);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskA", (bApplySwatchA && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchA)
			{
				Wall.WallMaterial->SetVectorParameterValue("WallColourA", SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1]);
			}
		}
	}

	// Update tracking
	bShowingSingleWallPreview = true;
	PreviewWallArrayIndex = WallArrayIndex;
	PreviewFaceType = FaceType;
}

void AWallPatternTool::ClearSingleWallPreview()
{
	if (!bShowingSingleWallPreview || !CurrentLot || !CurrentLot->WallComponent)
	{
		return;
	}

	// Restore original texture directly on material (lightweight - no mesh regeneration)
	if (PreviewWallArrayIndex >= 0 && PreviewWallArrayIndex < CurrentLot->WallComponent->WallDataArray.Num())
	{
		FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[PreviewWallArrayIndex];

		if (Wall.WallMaterial)
		{
			// Restore from OriginalPreviewTextures (which was captured from the material directly)
			// PosY and StartCap use WallCoveringB (stored in LeftFaceTexture)
			// NegY and EndCap use WallCoveringA (stored in RightFaceTexture)
			if (PreviewFaceType == EWallFace::PosY || PreviewFaceType == EWallFace::StartCap)
			{
				// Restore WallCoveringB - OriginalPreviewTextures.LeftFaceTexture was captured from material
				Wall.WallMaterial->SetTextureParameterValue("WallCoveringB", Cast<UTexture>(OriginalPreviewTextures.LeftFaceTexture));
				Wall.WallMaterial->SetTextureParameterValue("WallNormalB", OriginalPreviewNormalB);
				Wall.WallMaterial->SetTextureParameterValue("WallRoughnessB", OriginalPreviewRoughnessB);

				// Restore original color vector
				Wall.WallMaterial->SetVectorParameterValue("WallColourB", OriginalPreviewColourB);
			}
			else if (PreviewFaceType == EWallFace::NegY || PreviewFaceType == EWallFace::EndCap)
			{
				// Restore WallCoveringA - OriginalPreviewTextures.RightFaceTexture was captured from material
				Wall.WallMaterial->SetTextureParameterValue("WallCoveringA", Cast<UTexture>(OriginalPreviewTextures.RightFaceTexture));
				Wall.WallMaterial->SetTextureParameterValue("WallNormalA", OriginalPreviewNormalA);
				Wall.WallMaterial->SetTextureParameterValue("WallRoughnessA", OriginalPreviewRoughnessA);

				// Restore original color vector
				Wall.WallMaterial->SetVectorParameterValue("WallColourA", OriginalPreviewColourA);
			}

			// Restore original swatch scalar parameters (affects both faces)
			Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", OriginalPreviewbUseColourSwatchesA);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskA", OriginalPreviewbUseColourMaskA);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", OriginalPreviewbUseColourSwatchesB);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskB", OriginalPreviewbUseColourMaskB);

			// Restore original detail normal parameters (affects both faces)
			Wall.WallMaterial->SetTextureParameterValue("DetailNormalA", OriginalPreviewDetailNormalA);
			Wall.WallMaterial->SetTextureParameterValue("DetailNormalB", OriginalPreviewDetailNormalB);
			Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", OriginalPreviewbUseDetailNormalA);
			Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", OriginalPreviewbUseDetailNormalB);
			Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", OriginalPreviewDetailNormalIntensityA);
			Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", OriginalPreviewDetailNormalIntensityB);
		}
	}

	// Reset tracking
	bShowingSingleWallPreview = false;
	PreviewWallArrayIndex = -1;
	PreviewFaceType = EWallFace::Unknown;
	OriginalPreviewNormalA = nullptr;
	OriginalPreviewNormalB = nullptr;
	OriginalPreviewRoughnessA = nullptr;
	OriginalPreviewRoughnessB = nullptr;
}

void AWallPatternTool::ShowDragPaintPreview()
{
	if (!CurrentLot || !CurrentLot->WallComponent || !SelectedWallPattern)
	{
		return;
	}

	// Iterate through all walls in the drag selection
	for (const TPair<int32, EWallFace>& Entry : DragPaintedWalls)
	{
		int32 WallIndex = Entry.Key;
		EWallFace FaceType = Entry.Value;

		if (WallIndex < 0 || WallIndex >= CurrentLot->WallComponent->WallDataArray.Num())
		{
			continue;
		}

		FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallIndex];

		if (!Wall.WallMaterial)
		{
			continue;
		}

		// If we haven't captured original data for this wall yet, do it now
		if (!DragPreviewData.Contains(WallIndex))
		{
			FRoomPreviewData PreviewData;
			PreviewData.OriginalTextures = Wall.WallTextures;
			PreviewData.ModifiedFace = FaceType;

			// Capture textures from material
			UTexture* CurrentTextureA = nullptr;
			UTexture* CurrentTextureB = nullptr;
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallCoveringA"), CurrentTextureA);
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallCoveringB"), CurrentTextureB);
			if (CurrentTextureA)
				PreviewData.OriginalTextures.RightFaceTexture = Cast<UTexture2D>(CurrentTextureA);
			if (CurrentTextureB)
				PreviewData.OriginalTextures.LeftFaceTexture = Cast<UTexture2D>(CurrentTextureB);

			// Capture normal and roughness maps
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallNormalA"), PreviewData.OriginalNormalA);
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallNormalB"), PreviewData.OriginalNormalB);
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallRoughnessA"), PreviewData.OriginalRoughnessA);
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("WallRoughnessB"), PreviewData.OriginalRoughnessB);

			// Capture detail normal textures
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("DetailNormalA"), PreviewData.OriginalDetailNormalA);
			Wall.WallMaterial->GetTextureParameterValue(FMaterialParameterInfo("DetailNormalB"), PreviewData.OriginalDetailNormalB);

			// Capture color vectors
			Wall.WallMaterial->GetVectorParameterValue(FMaterialParameterInfo("WallColourA"), PreviewData.OriginalColourA);
			Wall.WallMaterial->GetVectorParameterValue(FMaterialParameterInfo("WallColourB"), PreviewData.OriginalColourB);

			// Capture swatch scalar parameters
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourSwatchesA"), PreviewData.OriginalUseColourSwatchesA);
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourMaskA"), PreviewData.OriginalUseColourMaskA);
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourSwatchesB"), PreviewData.OriginalUseColourSwatchesB);
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseColourMaskB"), PreviewData.OriginalUseColourMaskB);

			// Capture detail normal scalar parameters
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseDetailNormalA"), PreviewData.OriginalUseDetailNormalA);
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("bUseDetailNormalB"), PreviewData.OriginalUseDetailNormalB);
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("DetailNormalIntensityA"), PreviewData.OriginalDetailNormalIntensityA);
			Wall.WallMaterial->GetScalarParameterValue(FMaterialParameterInfo("DetailNormalIntensityB"), PreviewData.OriginalDetailNormalIntensityB);

			DragPreviewData.Add(WallIndex, PreviewData);
		}

		// Apply preview texture to the appropriate face
		// PosY and StartCap use WallCoveringB, NegY and EndCap use WallCoveringA
		if (FaceType == EWallFace::PosY || FaceType == EWallFace::StartCap)
		{
			if (SelectedWallPattern->BaseTexture)
				Wall.WallMaterial->SetTextureParameterValue("WallCoveringB", SelectedWallPattern->BaseTexture);
			if (SelectedWallPattern->NormalMap)
				Wall.WallMaterial->SetTextureParameterValue("WallNormalB", SelectedWallPattern->NormalMap);
			if (SelectedWallPattern->RoughnessMap)
				Wall.WallMaterial->SetTextureParameterValue("WallRoughnessB", SelectedWallPattern->RoughnessMap);

			// Apply detail normal
			Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				Wall.WallMaterial->SetTextureParameterValue("DetailNormalB", SelectedWallPattern->DetailNormal);
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", 0.0f);
			}

			// Apply color swatch
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			bool bApplySwatchB = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", bApplySwatchB ? 1.0f : 0.0f);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskB", (bApplySwatchB && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchB)
			{
				Wall.WallMaterial->SetVectorParameterValue("WallColourB", SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1]);
			}
		}
		else if (FaceType == EWallFace::NegY || FaceType == EWallFace::EndCap)
		{
			if (SelectedWallPattern->BaseTexture)
				Wall.WallMaterial->SetTextureParameterValue("WallCoveringA", SelectedWallPattern->BaseTexture);
			if (SelectedWallPattern->NormalMap)
				Wall.WallMaterial->SetTextureParameterValue("WallNormalA", SelectedWallPattern->NormalMap);
			if (SelectedWallPattern->RoughnessMap)
				Wall.WallMaterial->SetTextureParameterValue("WallRoughnessA", SelectedWallPattern->RoughnessMap);

			// Apply detail normal
			Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", SelectedWallPattern->bUseDetailNormal ? 1.0f : 0.0f);
			if (SelectedWallPattern->bUseDetailNormal && SelectedWallPattern->DetailNormal)
			{
				Wall.WallMaterial->SetTextureParameterValue("DetailNormalA", SelectedWallPattern->DetailNormal);
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", SelectedWallPattern->DetailNormalIntensity);
			}
			else
			{
				Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", 0.0f);
			}

			// Apply color swatch
			// Note: SelectedSwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
			bool bApplySwatchA = SelectedWallPattern->bUseColourSwatches && SelectedSwatchIndex > 0 && SelectedWallPattern->ColourSwatches.IsValidIndex(SelectedSwatchIndex - 1);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", bApplySwatchA ? 1.0f : 0.0f);
			Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskA", (bApplySwatchA && SelectedWallPattern->bUseColourMask) ? 1.0f : 0.0f);
			if (bApplySwatchA)
			{
				Wall.WallMaterial->SetVectorParameterValue("WallColourA", SelectedWallPattern->ColourSwatches[SelectedSwatchIndex - 1]);
			}
		}
	}
}

void AWallPatternTool::ClearDragPaintPreview()
{
	if (!CurrentLot || !CurrentLot->WallComponent)
	{
		return;
	}

	// Restore original textures for all walls in the drag preview
	for (const TPair<int32, FRoomPreviewData>& Entry : DragPreviewData)
	{
		int32 WallIndex = Entry.Key;
		const FRoomPreviewData& PreviewData = Entry.Value;

		if (WallIndex >= 0 && WallIndex < CurrentLot->WallComponent->WallDataArray.Num())
		{
			FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallIndex];

			if (Wall.WallMaterial)
			{
				// Restore based on which face was modified
				if (PreviewData.ModifiedFace == EWallFace::PosY || PreviewData.ModifiedFace == EWallFace::StartCap)
				{
					Wall.WallMaterial->SetTextureParameterValue("WallCoveringB", Cast<UTexture>(PreviewData.OriginalTextures.LeftFaceTexture));
					Wall.WallMaterial->SetTextureParameterValue("WallNormalB", PreviewData.OriginalNormalB);
					Wall.WallMaterial->SetTextureParameterValue("WallRoughnessB", PreviewData.OriginalRoughnessB);
					Wall.WallMaterial->SetTextureParameterValue("DetailNormalB", PreviewData.OriginalDetailNormalB);
					Wall.WallMaterial->SetVectorParameterValue("WallColourB", PreviewData.OriginalColourB);
					Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", PreviewData.OriginalUseColourSwatchesB);
					Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskB", PreviewData.OriginalUseColourMaskB);
					Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", PreviewData.OriginalUseDetailNormalB);
					Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", PreviewData.OriginalDetailNormalIntensityB);
				}
				else if (PreviewData.ModifiedFace == EWallFace::NegY || PreviewData.ModifiedFace == EWallFace::EndCap)
				{
					Wall.WallMaterial->SetTextureParameterValue("WallCoveringA", Cast<UTexture>(PreviewData.OriginalTextures.RightFaceTexture));
					Wall.WallMaterial->SetTextureParameterValue("WallNormalA", PreviewData.OriginalNormalA);
					Wall.WallMaterial->SetTextureParameterValue("WallRoughnessA", PreviewData.OriginalRoughnessA);
					Wall.WallMaterial->SetTextureParameterValue("DetailNormalA", PreviewData.OriginalDetailNormalA);
					Wall.WallMaterial->SetVectorParameterValue("WallColourA", PreviewData.OriginalColourA);
					Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", PreviewData.OriginalUseColourSwatchesA);
					Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskA", PreviewData.OriginalUseColourMaskA);
					Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", PreviewData.OriginalUseDetailNormalA);
					Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", PreviewData.OriginalDetailNormalIntensityA);
				}
			}
		}
	}

	DragPreviewData.Empty();
}

void AWallPatternTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AWallPatternTool::Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel)
{
	if(Cast<UWallComponent>(CursorWorldHitResult.Component))
	{
		HitWallComponent = Cast<UWallComponent>(CursorWorldHitResult.Component);
		HitMeshSection = HitWallComponent->GetSectionIDFromHitResult(CursorWorldHitResult);

		//Make sure hit section is valid
		if (HitMeshSection >= 0)
		{
			HitWallComponent->GetProcMeshSection(HitMeshSection);

			TargetLocation = CursorWorldHitResult.Location;
			HitWallNormal = CursorWorldHitResult.ImpactNormal;

			SetActorLocation(TargetLocation);
			UpdateLocation(GetActorLocation());

			// Handle shift key for room wall preview vs single wall preview
			if (bShiftPressed && HitWallComponent && HitMeshSection >= 0 && HitMeshSection < HitWallComponent->WallDataArray.Num())
			{
				FWallSegmentData& HitWall = HitWallComponent->WallDataArray[HitMeshSection];

				// Check if this is a decorative wall (half wall) or freestanding wall
				// Decorative walls have WallEdgeID == -1
				// Freestanding walls have both Room1=0 AND Room2=0
				bool bIsDecorativeWall = (HitWall.WallEdgeID == -1);
				bool bIsFreestandingWall = false;

				int32 WallRoom1 = 0, WallRoom2 = 0;
				if (!bIsDecorativeWall && CurrentLot && CurrentLot->WallGraph)
				{
					const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(HitWall.WallEdgeID);
					if (Edge)
					{
						WallRoom1 = Edge->Room1;
						WallRoom2 = Edge->Room2;
						bIsFreestandingWall = (WallRoom1 == 0 && WallRoom2 == 0);
					}
				}

				// Decorative walls and freestanding walls don't participate in room preview
				// Fall back to single wall preview for them
				if (bIsDecorativeWall || bIsFreestandingWall)
				{
					if (bShowingRoomWallPreview)
					{
						ClearRoomWallPreview();
					}
					EWallFace HitFace = DetermineHitFace(HitWall, TargetLocation, HitWallNormal);
					if (HitFace != EWallFace::Unknown)
					{
						ShowSingleWallPreview(HitMeshSection, HitFace);
					}
				}
				else
				{
					// Clear single wall preview when doing room preview
					if (bShowingSingleWallPreview)
					{
						ClearSingleWallPreview();
					}

					// Determine which face was hit
					EWallFace HitFace = DetermineHitFace(HitWall, TargetLocation, HitWallNormal);

					// Get the room that the hit face is looking into (accounts for wall orientation)
					int32 FaceRoomID = GetRoomOnFace(HitWall, HitFace);

					// Determine target room and whether we're painting interior or exterior
					int32 TargetRoomID = -1;
					bool bIsInterior = false;

					if (FaceRoomID > 0)
					{
						// Looking at an interior face - paint that room's interior walls
						TargetRoomID = FaceRoomID;
						bIsInterior = true;
					}
					else if (FaceRoomID == 0)
					{
						// Looking at an exterior face - paint ALL exterior walls
						TargetRoomID = 0; // RoomID is ignored for exterior in GetWallsInRoom
						bIsInterior = false;
					}

					// Show room preview if valid face was detected
					if (HitFace != EWallFace::Unknown && TargetRoomID >= 0)
					{
						// For exterior mode, also check if we're on a different starting wall
						bool bNeedsUpdate = !bShowingRoomWallPreview ||
							CurrentPreviewRoomID != TargetRoomID ||
							bPreviewingInterior != bIsInterior ||
							(!bIsInterior && PreviewStartEdgeID != HitWall.WallEdgeID);

						if (bNeedsUpdate)
						{
							// Pass EdgeID for exterior mode to get only connected walls
							int32 StartEdgeID = bIsInterior ? -1 : HitWall.WallEdgeID;
							PreviewStartEdgeID = StartEdgeID;
							ShowRoomWallPreview(TargetRoomID, HitWall.Level, bIsInterior, StartEdgeID);
						}
					}
				}
			}
			else
			{
				// Shift not pressed - show single wall preview OR drag painting
				if (bShowingRoomWallPreview)
				{
					ClearRoomWallPreview();
				}

				// Show single wall face preview
				if (HitWallComponent && HitMeshSection >= 0 && HitMeshSection < HitWallComponent->WallDataArray.Num())
				{
					FWallSegmentData& HitWall = HitWallComponent->WallDataArray[HitMeshSection];
					EWallFace HitFace = DetermineHitFace(HitWall, TargetLocation, HitWallNormal);
					if (HitFace != EWallFace::Unknown)
					{
						// If select is pressed (dragging), add this wall to drag selection
						if (SelectPressed)
						{
							// Add or update this wall in the drag selection
							DragPaintedWalls.Add(HitMeshSection, HitFace);
						}
						else
						{
							// Just hovering - show single wall preview
							ShowSingleWallPreview(HitMeshSection, HitFace);
						}
					}
				}
			}

			//Tell blueprint children we moved successfully
			OnMoved();
			bValidPlacementLocation = true;
			if (SelectPressed)
			{
				Drag_Implementation();
			}
			else
			{
				PreviousLocation = GetActorLocation();
			}

		}
		else
		{
			// Hit a wall component but section is invalid - clear previews
			if (bShowingRoomWallPreview)
			{
				ClearRoomWallPreview();
			}
			if (bShowingSingleWallPreview)
			{
				ClearSingleWallPreview();
			}

			TargetLocation = CurrentLot->GetMouseWorldPosition(FVector::Dist(CurrentPlayerPawn->GetPawnViewLocation(), DragCreateVectors.StartOperation));
			SetActorLocation(TargetLocation);
			UpdateLocation(GetActorLocation());
			bValidPlacementLocation = false;
		}
	}
	else
	{
		// Not hovering over a wall - clear any previews
		if (bShowingRoomWallPreview)
		{
			ClearRoomWallPreview();
		}
		if (bShowingSingleWallPreview)
		{
			ClearSingleWallPreview();
		}

		TargetLocation = CurrentLot->GetMouseWorldPosition(FVector::Dist(CurrentPlayerPawn->GetPawnViewLocation(), DragCreateVectors.StartOperation));
		SetActorLocation(TargetLocation);
		UpdateLocation(GetActorLocation());
		bValidPlacementLocation = false;
	}
}

void AWallPatternTool::Click_Implementation()
{
	// Clear any previous drag operation
	DragPaintedWalls.Empty();
	DragPreviewData.Empty();
	bIsDragPainting = false;

	if (HitMeshSection >= 0 && HitWallComponent && HitMeshSection < HitWallComponent->WallDataArray.Num())
	{
		// If shift is pressed and showing room preview, apply to entire room via RPC
		if (bShiftPressed && bShowingRoomWallPreview && SelectedWallPattern)
		{
			int32 Level = HitWallComponent->WallDataArray[HitMeshSection].Level;

			// Send RPC to apply pattern to all walls in room (server will multicast to all clients)
			Server_ApplyRoomWallPatterns(CurrentPreviewRoomID, Level, bPreviewingInterior, PreviewStartEdgeID, SelectedWallPattern, SelectedSwatchIndex);

			// Reset preview tracking WITHOUT restoring textures (we just applied them permanently)
			OriginalWallTextures.Empty();
			bShowingRoomWallPreview = false;
			CurrentPreviewRoomID = -1;
			bPreviewingInterior = false;
			PreviewStartEdgeID = -1;
		}
		// If showing single wall preview, commit it via RPC
		else if (bShowingSingleWallPreview && SelectedWallPattern)
		{
			// Send RPC to apply pattern (server will multicast to all clients)
			Server_ApplySingleWallPattern(PreviewWallArrayIndex, PreviewFaceType, SelectedWallPattern, SelectedSwatchIndex);

			// Clear the preview tracking
			bShowingSingleWallPreview = false;
			PreviewWallArrayIndex = -1;
			PreviewFaceType = EWallFace::Unknown;
		}
		// Fallback: if somehow we clicked without a preview showing, apply via RPC
		else if (SelectedWallPattern)
		{
			FWallSegmentData& Wall = HitWallComponent->WallDataArray[HitMeshSection];

			// Determine which face was hit using grid-based room detection
			EWallFace HitFace = DetermineHitFace(Wall, TargetLocation, HitWallNormal);

			if (HitFace != EWallFace::Unknown)
			{
				// Send RPC to apply pattern (server will multicast to all clients)
				Server_ApplySingleWallPattern(HitMeshSection, HitFace, SelectedWallPattern, SelectedSwatchIndex);
			}
		}
	}
}

void AWallPatternTool::Drag_Implementation()
{
	bIsDragPainting = true;

	// Clear any room preview (incompatible with drag painting)
	if (bShowingRoomWallPreview)
	{
		ClearRoomWallPreview();
	}

	// Clear single wall preview (incompatible with drag painting)
	if (bShowingSingleWallPreview)
	{
		ClearSingleWallPreview();
	}

	// Show preview for all walls in drag selection
	ShowDragPaintPreview();
}

void AWallPatternTool::BroadcastRelease_Implementation()
{
	if (!bIsDragPainting || DragPaintedWalls.Num() == 0)
	{
		return;
	}

	if (!SelectedWallPattern)
	{
		// Clean up and return
		ClearDragPaintPreview();
		DragPaintedWalls.Empty();
		bIsDragPainting = false;
		return;
	}

	// Collect wall indices and face types into arrays for RPC
	TArray<int32> WallIndices;
	TArray<EWallFace> FaceTypes;
	WallIndices.Reserve(DragPaintedWalls.Num());
	FaceTypes.Reserve(DragPaintedWalls.Num());

	for (const TPair<int32, EWallFace>& Entry : DragPaintedWalls)
	{
		WallIndices.Add(Entry.Key);
		FaceTypes.Add(Entry.Value);
	}

	// Send RPC to apply patterns to all drag-painted walls (server will multicast to all clients)
	Server_ApplyDragPaintedWalls(WallIndices, FaceTypes, SelectedWallPattern, SelectedSwatchIndex);

	// Clean up - do NOT restore textures since we just applied them permanently
	DragPreviewData.Empty();
	DragPaintedWalls.Empty();
	bIsDragPainting = false;
}

// ========== Network RPC Implementations ==========

void AWallPatternTool::ApplyPatternToWallFace(int32 WallArrayIndex, EWallFace FaceType, UWallPattern* Pattern, int32 SwatchIndex)
{
	if (!CurrentLot || !CurrentLot->WallComponent || !Pattern)
	{
		return;
	}

	if (WallArrayIndex < 0 || WallArrayIndex >= CurrentLot->WallComponent->WallDataArray.Num())
	{
		return;
	}

	FWallSegmentData& Wall = CurrentLot->WallComponent->WallDataArray[WallArrayIndex];

	if (!Wall.WallMaterial)
	{
		return;
	}

	// Store the applied pattern
	Wall.AppliedPattern = Pattern;

	// Apply texture based on face type
	// PosY and StartCap use WallCoveringB (LeftFaceTexture)
	// NegY and EndCap use WallCoveringA (RightFaceTexture)
	if (FaceType == EWallFace::PosY || FaceType == EWallFace::StartCap)
	{
		if (Pattern->BaseTexture)
		{
			Wall.WallMaterial->SetTextureParameterValue("WallCoveringB", Pattern->BaseTexture);
			Wall.WallTextures.LeftFaceTexture = Pattern->BaseTexture;
		}
		if (Pattern->NormalMap)
			Wall.WallMaterial->SetTextureParameterValue("WallNormalB", Pattern->NormalMap);
		if (Pattern->RoughnessMap)
			Wall.WallMaterial->SetTextureParameterValue("WallRoughnessB", Pattern->RoughnessMap);

		// Apply detail normal
		Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalB", Pattern->bUseDetailNormal ? 1.0f : 0.0f);
		if (Pattern->bUseDetailNormal && Pattern->DetailNormal)
		{
			Wall.WallMaterial->SetTextureParameterValue("DetailNormalB", Pattern->DetailNormal);
			Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", Pattern->DetailNormalIntensity);
		}
		else
		{
			Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityB", 0.0f);
		}

		// Apply color swatch
		// Note: SwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
		bool bApplySwatch = Pattern->bUseColourSwatches && SwatchIndex > 0 && Pattern->ColourSwatches.IsValidIndex(SwatchIndex - 1);
		Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesB", bApplySwatch ? 1.0f : 0.0f);
		Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskB", (bApplySwatch && Pattern->bUseColourMask) ? 1.0f : 0.0f);
		if (bApplySwatch)
		{
			Wall.WallMaterial->SetVectorParameterValue("WallColourB", Pattern->ColourSwatches[SwatchIndex - 1]);
		}
	}
	else if (FaceType == EWallFace::NegY || FaceType == EWallFace::EndCap)
	{
		if (Pattern->BaseTexture)
		{
			Wall.WallMaterial->SetTextureParameterValue("WallCoveringA", Pattern->BaseTexture);
			Wall.WallTextures.RightFaceTexture = Pattern->BaseTexture;
		}
		if (Pattern->NormalMap)
			Wall.WallMaterial->SetTextureParameterValue("WallNormalA", Pattern->NormalMap);
		if (Pattern->RoughnessMap)
			Wall.WallMaterial->SetTextureParameterValue("WallRoughnessA", Pattern->RoughnessMap);

		// Apply detail normal
		Wall.WallMaterial->SetScalarParameterValue("bUseDetailNormalA", Pattern->bUseDetailNormal ? 1.0f : 0.0f);
		if (Pattern->bUseDetailNormal && Pattern->DetailNormal)
		{
			Wall.WallMaterial->SetTextureParameterValue("DetailNormalA", Pattern->DetailNormal);
			Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", Pattern->DetailNormalIntensity);
		}
		else
		{
			Wall.WallMaterial->SetScalarParameterValue("DetailNormalIntensityA", 0.0f);
		}

		// Apply color swatch
		// Note: SwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
		bool bApplySwatch = Pattern->bUseColourSwatches && SwatchIndex > 0 && Pattern->ColourSwatches.IsValidIndex(SwatchIndex - 1);
		Wall.WallMaterial->SetScalarParameterValue("bUseColourSwatchesA", bApplySwatch ? 1.0f : 0.0f);
		Wall.WallMaterial->SetScalarParameterValue("bUseColourMaskA", (bApplySwatch && Pattern->bUseColourMask) ? 1.0f : 0.0f);
		if (bApplySwatch)
		{
			Wall.WallMaterial->SetVectorParameterValue("WallColourA", Pattern->ColourSwatches[SwatchIndex - 1]);
		}
	}
}

void AWallPatternTool::Server_ApplySingleWallPattern_Implementation(int32 WallArrayIndex, EWallFace FaceType, UWallPattern* Pattern, int32 SwatchIndex)
{
	// Server received the request, multicast to all clients
	Multicast_ApplySingleWallPattern(WallArrayIndex, FaceType, Pattern, SwatchIndex);
}

void AWallPatternTool::Multicast_ApplySingleWallPattern_Implementation(int32 WallArrayIndex, EWallFace FaceType, UWallPattern* Pattern, int32 SwatchIndex)
{
	// Apply pattern on all machines (server and clients)
	ApplyPatternToWallFace(WallArrayIndex, FaceType, Pattern, SwatchIndex);
}

void AWallPatternTool::Server_ApplyRoomWallPatterns_Implementation(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID, UWallPattern* Pattern, int32 SwatchIndex)
{
	// Server received the request, multicast to all clients
	Multicast_ApplyRoomWallPatterns(RoomID, Level, bInterior, StartEdgeID, Pattern, SwatchIndex);
}

void AWallPatternTool::Multicast_ApplyRoomWallPatterns_Implementation(int32 RoomID, int32 Level, bool bInterior, int32 StartEdgeID, UWallPattern* Pattern, int32 SwatchIndex)
{
	if (!CurrentLot || !CurrentLot->WallComponent || !Pattern)
	{
		return;
	}

	// Get walls - for exterior mode, only get connected walls; for interior, get all room walls
	TArray<FWallSegmentData> RoomWalls;
	if (bInterior)
	{
		RoomWalls = GetWallsInRoom(RoomID, Level, bInterior);
	}
	else
	{
		if (StartEdgeID >= 0)
		{
			RoomWalls = GetConnectedExteriorWalls(StartEdgeID, Level);
		}
		else
		{
			RoomWalls = GetWallsInRoom(RoomID, Level, bInterior);
		}
	}

	// Apply pattern to all walls in the room
	for (const FWallSegmentData& Wall : RoomWalls)
	{
		if (Wall.WallArrayIndex < 0 || Wall.WallArrayIndex >= CurrentLot->WallComponent->WallDataArray.Num())
		{
			continue;
		}

		FWallSegmentData& WallRef = CurrentLot->WallComponent->WallDataArray[Wall.WallArrayIndex];

		// Get Room1/Room2 for this wall
		int32 Room1 = 0, Room2 = 0;
		if (WallRef.WallEdgeID != -1 && CurrentLot->WallGraph)
		{
			const FWallEdge* Edge = CurrentLot->WallGraph->Edges.Find(WallRef.WallEdgeID);
			if (Edge)
			{
				Room1 = Edge->Room1;
				Room2 = Edge->Room2;
			}
		}

		// Determine which face(s) to modify
		if (bInterior)
		{
			// Interior: paint the face facing INTO the room
			EWallFace FaceToModify = GetFaceFacingRoom(WallRef, RoomID);
			if (FaceToModify != EWallFace::Unknown)
			{
				ApplyPatternToWallFace(Wall.WallArrayIndex, FaceToModify, Pattern, SwatchIndex);
			}
		}
		else
		{
			// Exterior: paint all faces that face exterior (room 0)
			if (Room1 == 0)
			{
				EWallFace Face1 = GetFaceFacingRoom(WallRef, Room1);
				if (Face1 != EWallFace::Unknown)
				{
					ApplyPatternToWallFace(Wall.WallArrayIndex, Face1, Pattern, SwatchIndex);
				}
			}
			if (Room2 == 0)
			{
				EWallFace Face2 = GetFaceFacingRoom(WallRef, Room2);
				if (Face2 != EWallFace::Unknown)
				{
					ApplyPatternToWallFace(Wall.WallArrayIndex, Face2, Pattern, SwatchIndex);
				}
			}
		}
	}
}

void AWallPatternTool::Server_ApplyDragPaintedWalls_Implementation(const TArray<int32>& WallIndices, const TArray<EWallFace>& FaceTypes, UWallPattern* Pattern, int32 SwatchIndex)
{
	// Server received the request, multicast to all clients
	Multicast_ApplyDragPaintedWalls(WallIndices, FaceTypes, Pattern, SwatchIndex);
}

void AWallPatternTool::Multicast_ApplyDragPaintedWalls_Implementation(const TArray<int32>& WallIndices, const TArray<EWallFace>& FaceTypes, UWallPattern* Pattern, int32 SwatchIndex)
{
	if (WallIndices.Num() != FaceTypes.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Multicast_ApplyDragPaintedWalls: Mismatched array sizes"));
		return;
	}

	// Apply pattern to all walls
	for (int32 i = 0; i < WallIndices.Num(); i++)
	{
		ApplyPatternToWallFace(WallIndices[i], FaceTypes[i], Pattern, SwatchIndex);
	}
}

// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/FloorComponent.h"

#include "Actors/LotManager.h"
#include "Components/RoomManagerComponent.h"
#include "Data/FloorPattern.h"


class ALotManager;

UFloorComponent::UFloorComponent(const FObjectInitializer& ObjectInitializer) : UProceduralMeshComponent(ObjectInitializer)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	FloorData.StartLoc = FVector(0, 0, 0);

	bCommitted = false;

	// Disable custom depth - floors are permanent geometry
	bRenderCustomDepth = false;
}

// Called when the game starts
void UFloorComponent::BeginPlay()
{
	Super::BeginPlay();
	SetGenerateOverlapEvents(true);

	// Configure collision for AI navigation and gameplay
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionObjectType(ECC_WorldStatic);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Block); // Pawn channel - block so AI can walk on floors
	SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore); // Primitives/Wall trace channel - ignore so portal tools can trace through floors to walls
}


// Called every frame
void UFloorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

int32 UFloorComponent::GetSectionIDFromHitResult(const FHitResult& HitResult) const
{
	if (!HitResult.Component.IsValid() || HitResult.Component.Get() != this)
	{
		// Hit result is not on this component
		return -2;
	}

	// Convert world-space impact point to component-local space for comparison with vertex bounds
	FVector LocalImpactPoint = GetComponentTransform().InverseTransformPosition(HitResult.ImpactPoint);
	FVector BoxExtents(4.0f, 4.0f, 4.0f); // Adjust the extents as needed
	FBox ImpactBox(LocalImpactPoint - BoxExtents, LocalImpactPoint + BoxExtents);
	// DrawDebugBox(GetWorld(), LocalImpactPoint, BoxExtents, FColor::Green, false, 1, 0, 1);
	
	// Iterate over each section to find the one containing the hit point
	for (const FFloorSegmentData& Data : FloorDataArray)
	{
		// for (int32 SectionIndex = 0; SectionIndex < GetNumSections(); ++SectionIndex) // fized bug
		// {
		// Get bounds of this section
		FBox SectionBounds(Data.Vertices);
		// Check if the impact point is within the bounds of this section
		if (ImpactBox.Intersect(SectionBounds))
		{
			return Data.ArrayIndex;
		}
	}
	
	// Hit point is not within any section bounds
	return -1;
}

void UFloorComponent::ConstructFloor()
{
	//Check other stuff here maybe?
	GenerateFloorMesh();
}

void UFloorComponent::DestroyFloor()
{
	UE_LOG(LogTemp, Log, TEXT("FloorComponent::DestroyFloor - Destroying component '%s' (this=%p, owner=%s)"),
		*GetName(), this, *GetOwner()->GetName());

	// Clear all mesh sections
	for (int i = 0; i < 5; i++)
	{
		ClearMeshSection(i);
	}

	// Unregister before destroying to ensure proper cleanup
	if (IsRegistered())
	{
		UnregisterComponent();
	}

	// Remove from owner's instance components
	AActor* Owner = GetOwner();
	if (Owner)
	{
		Owner->RemoveInstanceComponent(this);
	}

	DestroyComponent();
}

void UFloorComponent::DestroyFloorSection(FFloorSegmentData& FloorSection)
{
	ClearMeshSection(FloorSection.SectionIndex);
	FloorDataArray.Remove(FloorSection);
}

void UFloorComponent::RemoveFloorSection(int FloorArrayIndex)
{
	if(FloorDataArray.Num() > FloorArrayIndex)
	{
		// Clear Mesh Section
		ClearMeshSection(FloorDataArray[FloorArrayIndex].SectionIndex);

		// Disable The Floor Data
		FloorDataArray[FloorArrayIndex].bCommitted = false;
		FloorFreeIndices.Add(FloorArrayIndex);
	}
}

void UFloorComponent::CommitFloor(UMaterialInstance* DefaultFloorMaterial, bool Top, bool Bottom, bool Right, bool Left)
{
	bTop = Top;
	bBottom = Bottom;
	bRight = Right;
	bLeft = Left;
	bCommitted = true;
	//Set material on floor to default
	FloorMaterial = DefaultFloorMaterial;
	//regen floor with new material
	GenerateFloorMesh();
}

void UFloorComponent::CommitFloorSection(FFloorSegmentData InFloorData, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial)
{
	FloorDataArray[InFloorData.ArrayIndex].bCommitted = true;

	// Store the applied pattern
	FloorDataArray[InFloorData.ArrayIndex].AppliedPattern = Pattern;

	// Determine which base material to use
	// Priority: Pattern's BaseMaterial > Tool's BaseMaterial
	UMaterialInstance* MaterialToUse = BaseMaterial;
	if (Pattern && Pattern->BaseMaterial)
	{
		MaterialToUse = Pattern->BaseMaterial;
	}

	// Always create unique material instance for each floor to support per-level visibility control
	// This is necessary because SetCurrentLevel needs to set VisibilityLevel parameter independently per floor
	FloorDataArray[InFloorData.ArrayIndex].FloorMaterial = CreateDynamicMaterialInstance(
		FloorDataArray[InFloorData.ArrayIndex].SectionIndex,
		Cast<UMaterialInterface>(MaterialToUse));

	// Apply pattern textures if pattern is provided
	if (Pattern && FloorDataArray[InFloorData.ArrayIndex].FloorMaterial)
	{
		UMaterialInstanceDynamic* MID = FloorDataArray[InFloorData.ArrayIndex].FloorMaterial;

		if (Pattern->BaseTexture)
		{
			MID->SetTextureParameterValue("FloorMaterial", Pattern->BaseTexture);
		}
		if (Pattern->NormalMap)
		{
			MID->SetTextureParameterValue("FloorNormal", Pattern->NormalMap);
		}
		// Apply detail normal if enabled
		MID->SetScalarParameterValue("bUseDetailNormal", Pattern->bUseDetailNormal ? 1.0f : 0.0f);
		if (Pattern->bUseDetailNormal && Pattern->DetailNormal)
		{
			MID->SetTextureParameterValue("DetailNormal", Pattern->DetailNormal);
			MID->SetScalarParameterValue("DetailNormalIntensity", Pattern->DetailNormalIntensity);
		}
		else
		{
			MID->SetScalarParameterValue("DetailNormalIntensity", 0.0f);
		}
		if (Pattern->RoughnessMap)
		{
			MID->SetTextureParameterValue("FloorRoughness", Pattern->RoughnessMap);
		}

		// Set color swatch parameters to off by default (BuildFloorTool will enable them)
		MID->SetScalarParameterValue("bUseColourSwatches", 0.0f);
		MID->SetScalarParameterValue("bUseColourMask", 0.0f);
	}

	SetMaterial(FloorDataArray[InFloorData.ArrayIndex].SectionIndex, FloorDataArray[InFloorData.ArrayIndex].FloorMaterial);
	//regen floor with new material
	// GenerateFloorMeshSection(FloorDataArray[InFloorData.ArrayIndex], BaseMaterial);
	bRenderCustomDepth = true;
}

void UFloorComponent::UpdateFloorSection(FFloorSegmentData& InFloorData, const FTileSectionState& NewTileSectionState, UFloorPattern* Pattern, UMaterialInstance* BaseMaterial)
{
	// Check if the tile section state changed
	bool bStateChanged = !(FloorDataArray[InFloorData.ArrayIndex].TileSectionState.Top == NewTileSectionState.Top &&
						   FloorDataArray[InFloorData.ArrayIndex].TileSectionState.Bottom == NewTileSectionState.Bottom &&
						   FloorDataArray[InFloorData.ArrayIndex].TileSectionState.Left == NewTileSectionState.Left &&
						   FloorDataArray[InFloorData.ArrayIndex].TileSectionState.Right == NewTileSectionState.Right);

	if (bStateChanged)
	{
		// TileSectionState changed - update and regenerate mesh
		FloorDataArray[InFloorData.ArrayIndex].TileSectionState = NewTileSectionState;
		GenerateFloorMeshSection(FloorDataArray[InFloorData.ArrayIndex], nullptr);
	}

	// Store the applied pattern
	FloorDataArray[InFloorData.ArrayIndex].AppliedPattern = Pattern;

	// Determine which base material to use
	// Priority: Pattern's BaseMaterial > Tool's BaseMaterial
	UMaterialInstance* MaterialToUse = BaseMaterial;
	if (Pattern && Pattern->BaseMaterial)
	{
		MaterialToUse = Pattern->BaseMaterial;
	}

	// Always create unique material instance for each floor to support per-level visibility control
	FloorDataArray[InFloorData.ArrayIndex].FloorMaterial = CreateDynamicMaterialInstance(
		FloorDataArray[InFloorData.ArrayIndex].SectionIndex,
		Cast<UMaterialInterface>(MaterialToUse));

	// Apply pattern textures if pattern is provided
	if (Pattern && FloorDataArray[InFloorData.ArrayIndex].FloorMaterial)
	{
		UMaterialInstanceDynamic* MID = FloorDataArray[InFloorData.ArrayIndex].FloorMaterial;

		if (Pattern->BaseTexture)
		{
			MID->SetTextureParameterValue("FloorMaterial", Pattern->BaseTexture);
		}
		if (Pattern->NormalMap)
		{
			MID->SetTextureParameterValue("FloorNormal", Pattern->NormalMap);
		}
		// Apply detail normal if enabled
		MID->SetScalarParameterValue("bUseDetailNormal", Pattern->bUseDetailNormal ? 1.0f : 0.0f);
		if (Pattern->bUseDetailNormal && Pattern->DetailNormal)
		{
			MID->SetTextureParameterValue("DetailNormal", Pattern->DetailNormal);
			MID->SetScalarParameterValue("DetailNormalIntensity", Pattern->DetailNormalIntensity);
		}
		else
		{
			MID->SetScalarParameterValue("DetailNormalIntensity", 0.0f);
		}
		if (Pattern->RoughnessMap)
		{
			MID->SetTextureParameterValue("FloorRoughness", Pattern->RoughnessMap);
		}

		// Set color swatch parameters to off by default (BuildFloorTool will enable them)
		MID->SetScalarParameterValue("bUseColourSwatches", 0.0f);
		MID->SetScalarParameterValue("bUseColourMask", 0.0f);
	}

	SetMaterial(FloorDataArray[InFloorData.ArrayIndex].SectionIndex, FloorDataArray[InFloorData.ArrayIndex].FloorMaterial);
}

UMaterialInstanceDynamic* UFloorComponent::GetOrCreateSharedMaterialInstance(UMaterialInstance* BaseMaterial)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}

	// Check if we already have a shared instance for this base material
	UMaterialInstanceDynamic** ExistingInstance = SharedMaterialInstances.Find(BaseMaterial);
	if (ExistingInstance && *ExistingInstance)
	{
		return *ExistingInstance;
	}

	// Create new shared instance
	UMaterialInstanceDynamic* NewInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (NewInstance)
	{
		SharedMaterialInstances.Add(BaseMaterial, NewInstance);
	}

	return NewInstance;
}

void UFloorComponent::GenerateFloorMesh()
{
	// All required Vertices 
	FVector Center = FVector(0,0,0);
	FVector BottomLeft = GetComponentTransform().InverseTransformPosition(FloorData.CornerLocs[0]);
	FVector BottomRight = GetComponentTransform().InverseTransformPosition(FloorData.CornerLocs[1]);
	FVector TopLeft = GetComponentTransform().InverseTransformPosition(FloorData.CornerLocs[2]);
	FVector TopRight = GetComponentTransform().InverseTransformPosition(FloorData.CornerLocs[3]);
	//FloorTile
	TArray<FVector> FloorVerts;
	TArray<int32> FloorTriangles;
	TArray<FVector> FloorNormals;
	TArray<FVector2D> FloorUVs;
	TArray<FColor> FloorVertexColors;
	TArray<FProcMeshTangent> FloorTangents;
	
	FloorVerts.Add(TopLeft);
	FloorVerts.Add(TopRight);
	FloorVerts.Add(Center);
	FloorVerts.Add(BottomRight);
	FloorVerts.Add(BottomLeft);
	
	FloorUVs.Add(FVector2D(0, 1));
	FloorUVs.Add(FVector2D(1, 1));
	FloorUVs.Add(FVector2D(0.5, 0.5));
	FloorUVs.Add(FVector2D(1, 0));
	FloorUVs.Add(FVector2D(0, 0));

	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	
	//top surface triangles
	if(FloorData.TileSectionState.Top)
	{
		FloorTriangles.Add(0); // Top Left
		FloorTriangles.Add(1); // Top Right
		FloorTriangles.Add(2); // Center
	}
	if(FloorData.TileSectionState.Right)
	{
		FloorTriangles.Add(1); // Top Right
		FloorTriangles.Add(3); // Bottom Right
		FloorTriangles.Add(2); // Center
	}
	if(FloorData.TileSectionState.Bottom)
	{
		FloorTriangles.Add(3); // Bottom Right
		FloorTriangles.Add(4); // Bottom Left
		FloorTriangles.Add(2); // Center
	}
	if(FloorData.TileSectionState.Left)
	{
		FloorTriangles.Add(4); // Bottom Left
		FloorTriangles.Add(0); // Top Left
		FloorTriangles.Add(2); // Center
	}
	
	// Check if section exists and has valid vertex count
	FProcMeshSection* Section = GetProcMeshSection(0);
	if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != FloorVerts.Num())
	{
		// Check if this is the main FloorComponent (not a preview)
		bool bIsMainComponent = (GetName() == TEXT("FloorComponent"));
		if (bIsMainComponent)
		{
			UE_LOG(LogTemp, Error, TEXT("FloorComponent::GenerateFloorMesh - MAIN COMPONENT creating section 0! This should not happen with merged mesh system. Component '%s' (this=%p)"),
				*GetName(), this);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FloorComponent::GenerateFloorMesh - Creating OLD SYSTEM mesh section 0 on preview component '%s' (this=%p, owner=%s)"),
				*GetName(), this, *GetOwner()->GetName());
		}
		CreateMeshSection(0, FloorVerts, FloorTriangles, FloorNormals, FloorUVs, FloorVertexColors, FloorTangents, bCreateCollision);
	}
	else
	{
		UpdateMeshSection(0, FloorVerts, FloorNormals, FloorUVs, FloorVertexColors, FloorTangents);
	}
	SetMaterial(0, FloorMaterial);
}

FFloorSegmentData UFloorComponent::GenerateFloorSection(int32 Level, const FVector& TileCenter, UMaterialInstance* OptionalMaterial, const FTileSectionState& TileSectionState)
{
	FFloorSegmentData NewFloorData;
	NewFloorData.Level = Level;
	NewFloorData.StartLoc = TileCenter;
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());

	// Store grid coordinates for exact tile matching
	int32 Row, Column;
	if (OurLot->LocationToTile(TileCenter, Row, Column))
	{
		NewFloorData.Row = Row;
		NewFloorData.Column = Column;
	}

	NewFloorData.CornerLocs = OurLot->LocationToAllTileCorners(TileCenter, Level);
	NewFloorData.Width = OurLot->GridTileSize;
	NewFloorData.TileSectionState = TileSectionState;
	return GenerateFloorMeshSection(NewFloorData, OptionalMaterial);
}

FFloorSegmentData UFloorComponent::GenerateFloorMeshSection(FFloorSegmentData InFloorData, UMaterialInstance* OptionalMaterial)
{
	int32 NewSectionIndex = -1;
	if(InFloorData.SectionIndex != -1)
	{
		NewSectionIndex = InFloorData.SectionIndex;
	}
	else
	{
		NewSectionIndex = GetNumSections();
	}
	InFloorData.SectionIndex = NewSectionIndex;

	// All required Vertices
	FVector Center = InFloorData.StartLoc;
	FVector BottomLeft = InFloorData.CornerLocs[0];
	FVector BottomRight = InFloorData.CornerLocs[1];
	FVector TopLeft = InFloorData.CornerLocs[2];
	FVector TopRight = InFloorData.CornerLocs[3];

	// Determine if this is ground floor or ceiling floor
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	bool bIsGroundFloor = (OurLot && InFloorData.Level == OurLot->Basements);

	// Use appropriate thickness based on floor type
	const float Thickness = FloorThickness;
	FVector ThicknessOffset = FVector(0, 0, Thickness);

	//FloorTile
	TArray<FVector> FloorVerts;
	TArray<int32> FloorTriangles;
	TArray<FVector> FloorNormals;
	TArray<FVector2D> FloorUVs;
	TArray<FColor> FloorVertexColors;
	TArray<FProcMeshTangent> FloorTangents;

	// Add vertices based on floor type
	if (bIsGroundFloor)
	{
		// Ground floor: top surface at H + thickness
		FloorVerts.Add(TopLeft + ThicknessOffset);
		FloorVerts.Add(TopRight + ThicknessOffset);
		FloorVerts.Add(Center + ThicknessOffset);
		FloorVerts.Add(BottomRight + ThicknessOffset);
		FloorVerts.Add(BottomLeft + ThicknessOffset);

		// Bottom surface vertices (at grid level H)
		FloorVerts.Add(TopLeft);
		FloorVerts.Add(TopRight);
		FloorVerts.Add(Center);
		FloorVerts.Add(BottomRight);
		FloorVerts.Add(BottomLeft);
	}
	else
	{
		// Ceiling floor: top surface at H (grid level)
		FloorVerts.Add(TopLeft);
		FloorVerts.Add(TopRight);
		FloorVerts.Add(Center);
		FloorVerts.Add(BottomRight);
		FloorVerts.Add(BottomLeft);

		// Bottom surface at H - thickness
		FloorVerts.Add(TopLeft - ThicknessOffset);
		FloorVerts.Add(TopRight - ThicknessOffset);
		FloorVerts.Add(Center - ThicknessOffset);
		FloorVerts.Add(BottomRight - ThicknessOffset);
		FloorVerts.Add(BottomLeft - ThicknessOffset);
	}
	
	FloorUVs.Add(FVector2D(0, 1));
	FloorUVs.Add(FVector2D(1, 1));
	FloorUVs.Add(FVector2D(0.5, 0.5));
	FloorUVs.Add(FVector2D(1, 0));
	FloorUVs.Add(FVector2D(0, 0));
	//top normals
	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	FloorNormals.Add(FVector(0, 0, 1));
	//bottom normals
	FloorNormals.Add(FVector(0, 0, -1));
	FloorNormals.Add(FVector(0, 0, -1));
	FloorNormals.Add(FVector(0, 0, -1));
	FloorNormals.Add(FVector(0, 0, -1));
	FloorNormals.Add(FVector(0, 0, -1));

	//top surface triangles
	if(InFloorData.TileSectionState.Top)
	{
		FloorTriangles.Add(0); // Top Left
		FloorTriangles.Add(1); // Top Right
		FloorTriangles.Add(2); // Center
	}
	if(InFloorData.TileSectionState.Right)
	{
		FloorTriangles.Add(1); // Top Right
		FloorTriangles.Add(3); // Bottom Right
		FloorTriangles.Add(2); // Center
	}
	if(InFloorData.TileSectionState.Bottom)
	{
		FloorTriangles.Add(3); // Bottom Right
		FloorTriangles.Add(4); // Bottom Left
		FloorTriangles.Add(2); // Center
	}
	if(InFloorData.TileSectionState.Left)
	{
		FloorTriangles.Add(4); // Bottom Left
		FloorTriangles.Add(0); // Top Left
		FloorTriangles.Add(2); // Center
	}
	// Bottom surface triangles (reverse winding order)
	if (InFloorData.TileSectionState.Top)
	{
		FloorTriangles.Add(5); 
		FloorTriangles.Add(7); // Bottom Center
		FloorTriangles.Add(6); 
	}
	if (InFloorData.TileSectionState.Right)
	{
		FloorTriangles.Add(6); 
		FloorTriangles.Add(7); // Bottom Center
		FloorTriangles.Add(8); 
	}
	if (InFloorData.TileSectionState.Bottom)
	{
		FloorTriangles.Add(8); 
		FloorTriangles.Add(7); 
		FloorTriangles.Add(9); 
	}
	if (InFloorData.TileSectionState.Left)
	{
		FloorTriangles.Add(9);
		FloorTriangles.Add(7);
		FloorTriangles.Add(5);
	}

	// Check if section exists and has valid vertices with matching count
	FProcMeshSection* Section = GetProcMeshSection(NewSectionIndex);
	if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0 || Section->ProcVertexBuffer.Num() != FloorVerts.Num())
	{
		CreateMeshSection(NewSectionIndex, FloorVerts, FloorTriangles, FloorNormals, FloorUVs, FloorVertexColors, FloorTangents, bCreateCollision);
	}
	else
	{
		UpdateMeshSection(NewSectionIndex, FloorVerts, FloorNormals, FloorUVs, FloorVertexColors, FloorTangents);
	}
	InFloorData.FloorMaterial = CreateDynamicMaterialInstance(NewSectionIndex, Cast<UMaterialInterface>(OptionalMaterial));
	SetMaterial(NewSectionIndex, InFloorData.FloorMaterial);

	InFloorData.Vertices = FloorVerts;

	// OLD SYSTEM - Disabled for merged mesh system
	// Only populate ArrayIndex for backward compatibility, don't actually use FloorDataArray
	if (InFloorData.ArrayIndex == -1)
	{
		InFloorData.ArrayIndex = 0; // Dummy value for compatibility
	}

	return InFloorData;
}

bool UFloorComponent::FindExistingFloorSection(int32 Level, const FVector& TileCenter, FFloorSegmentData& OutFloor)
{
	// Convert location to grid coordinates for exact integer-based matching
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	int32 TargetRow, TargetColumn;

	if (!OurLot->LocationToTile(TileCenter, TargetRow, TargetColumn))
	{
		return false; // Invalid location
	}

	// Search using grid coordinates (exact integer comparison - no floating point errors!)
	for (const FFloorSegmentData& FoundFloorComponent : FloorDataArray)
	{
		if (FoundFloorComponent.bCommitted == true &&
			FoundFloorComponent.Level == Level &&
			FoundFloorComponent.Row == TargetRow &&
			FoundFloorComponent.Column == TargetColumn)
		{
			// Found an existing floor segment at this grid tile
			OutFloor = FoundFloorComponent;
			return true;
		}
	}

	// No existing floor segment found at this grid position
	return false;
}

// ============================================================================
// Merged Mesh System Implementation (OpenTS2-style)
// ============================================================================

FFloorTileData* UFloorComponent::FindFloorTile(int32 Level, int32 Row, int32 Column, ETriangleType Triangle) const
{
	int32 GridKey = MakeGridKey(Level, Row, Column, Triangle);
	if (const int32* IndexPtr = FloorSpatialMap.Find(GridKey))
	{
		if (FloorTileDataArray.IsValidIndex(*IndexPtr))
		{
			return const_cast<FFloorTileData*>(&FloorTileDataArray[*IndexPtr]);
		}
	}
	return nullptr;
}

FFloorTileData* UFloorComponent::FindFloorTile(int32 Level, int32 Row, int32 Column) const
{
	// Legacy version - defaults to Top triangle
	return FindFloorTile(Level, Row, Column, ETriangleType::Top);
}

void UFloorComponent::AddFloorTile(const FFloorTileData& TileData, UMaterialInstance* Material)
{
	// Validate tile data
	if (TileData.Row == -1 || TileData.Column == -1)
	{
		UE_LOG(LogTemp, Error, TEXT("FloorComponent::AddFloorTile - Invalid grid coordinates (Row: %d, Column: %d)"),
			TileData.Row, TileData.Column);
		return;
	}

	// Create unique key for this specific triangle
	int32 GridKey = MakeGridKey(TileData.Level, TileData.Row, TileData.Column, TileData.Triangle);

	// Check if this specific triangle already exists
	if (int32* ExistingIndex = FloorSpatialMap.Find(GridKey))
	{
		// Validate existing index
		if (!FloorTileDataArray.IsValidIndex(*ExistingIndex))
		{
			UE_LOG(LogTemp, Error, TEXT("FloorComponent::AddFloorTile - Invalid index %d in spatial map"), *ExistingIndex);
			FloorSpatialMap.Remove(GridKey);

			// Add as new triangle
			int32 NewIndex = FloorTileDataArray.Add(TileData);
			FloorSpatialMap.Add(GridKey, NewIndex);
		}
		else
		{
			// Update existing triangle - just update the pattern and swatch (no merging needed!)
			FFloorTileData& ExistingTriangle = FloorTileDataArray[*ExistingIndex];

			// Update pattern
			if (TileData.Pattern)
			{
				ExistingTriangle.Pattern = TileData.Pattern;
			}

			// Update swatch index
			ExistingTriangle.SwatchIndex = TileData.SwatchIndex;

			ExistingTriangle.bCommitted = TileData.bCommitted;
		}
	}
	else
	{
		// Add new triangle
		int32 NewIndex = FloorTileDataArray.Add(TileData);
		FloorSpatialMap.Add(GridKey, NewIndex);

		UE_LOG(LogTemp, Verbose, TEXT("FloorComponent::AddFloorTile - Added triangle %d at Level %d, Row %d, Column %d (Index: %d)"),
			static_cast<int32>(TileData.Triangle), TileData.Level, TileData.Row, TileData.Column, NewIndex);
	}

	// Store/update material for this level if provided
	if (Material)
	{
		UMaterialInstance** ExistingMaterial = LevelMaterials.Find(TileData.Level);
		if (!ExistingMaterial)
		{
			// First tile on this level - set material
			LevelMaterials.Add(TileData.Level, Material);
			UE_LOG(LogTemp, Log, TEXT("FloorComponent::AddFloorTile - Set material for Level %d"), TileData.Level);
		}
		else if (*ExistingMaterial != Material)
		{
			// Material changed - update it and log warning
			LevelMaterials[TileData.Level] = Material;
			UE_LOG(LogTemp, Warning, TEXT("FloorComponent::AddFloorTile - Material changed for Level %d (merged mesh uses single material per level)"), TileData.Level);
		}
	}

	// Mark level dirty for rebuild
	MarkLevelDirty(TileData.Level);
}

void UFloorComponent::RemoveFloorTile(int32 Level, int32 Row, int32 Column, ETriangleType Triangle)
{
	// Validate coordinates
	if (Row == -1 || Column == -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("FloorComponent::RemoveFloorTile - Invalid grid coordinates (Row: %d, Column: %d)"),
			Row, Column);
		return;
	}

	int32 GridKey = MakeGridKey(Level, Row, Column, Triangle);

	if (int32* IndexPtr = FloorSpatialMap.Find(GridKey))
	{
		if (FloorTileDataArray.IsValidIndex(*IndexPtr))
		{
			// Mark as not committed (lazy deletion)
			FloorTileDataArray[*IndexPtr].bCommitted = false;

			// Remove from spatial map
			FloorSpatialMap.Remove(GridKey);

			UE_LOG(LogTemp, Verbose, TEXT("FloorComponent::RemoveFloorTile - Removed triangle %d at Level %d, Row %d, Column %d"),
				static_cast<int32>(Triangle), Level, Row, Column);

			// Mark level dirty for rebuild
			MarkLevelDirty(Level);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FloorComponent::RemoveFloorTile - Invalid index %d in spatial map"), *IndexPtr);
			FloorSpatialMap.Remove(GridKey);
		}
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("FloorComponent::RemoveFloorTile - No triangle %d found at Level %d, Row %d, Column %d"),
			static_cast<int32>(Triangle), Level, Row, Column);
	}
}

void UFloorComponent::RemoveFloorTile(int32 Level, int32 Row, int32 Column)
{
	// Legacy version - defaults to Top triangle
	RemoveFloorTile(Level, Row, Column, ETriangleType::Top);
}

bool UFloorComponent::HasAnyFloorTile(int32 Level, int32 Row, int32 Column) const
{
	// Check if ANY of the 4 triangles exist at this tile location
	const int32 KeyTop = MakeGridKey(Level, Row, Column, ETriangleType::Top);
	const int32 KeyRight = MakeGridKey(Level, Row, Column, ETriangleType::Right);
	const int32 KeyBottom = MakeGridKey(Level, Row, Column, ETriangleType::Bottom);
	const int32 KeyLeft = MakeGridKey(Level, Row, Column, ETriangleType::Left);

	return FloorSpatialMap.Contains(KeyTop) ||
	       FloorSpatialMap.Contains(KeyRight) ||
	       FloorSpatialMap.Contains(KeyBottom) ||
	       FloorSpatialMap.Contains(KeyLeft);
}

FTileSectionState UFloorComponent::GetExistingTriangles(int32 Level, int32 Row, int32 Column) const
{
	FTileSectionState State;

	// Check each triangle and set the corresponding flag
	State.Top = (FindFloorTile(Level, Row, Column, ETriangleType::Top) != nullptr);
	State.Right = (FindFloorTile(Level, Row, Column, ETriangleType::Right) != nullptr);
	State.Bottom = (FindFloorTile(Level, Row, Column, ETriangleType::Bottom) != nullptr);
	State.Left = (FindFloorTile(Level, Row, Column, ETriangleType::Left) != nullptr);

	return State;
}

TArray<FFloorTileData*> UFloorComponent::GetAllTrianglesAtTile(int32 Level, int32 Row, int32 Column)
{
	TArray<FFloorTileData*> Triangles;
	Triangles.Reserve(4);

	// Check each triangle type and add if exists
	if (FFloorTileData* Top = FindFloorTile(Level, Row, Column, ETriangleType::Top))
	{
		Triangles.Add(Top);
	}
	if (FFloorTileData* Right = FindFloorTile(Level, Row, Column, ETriangleType::Right))
	{
		Triangles.Add(Right);
	}
	if (FFloorTileData* Bottom = FindFloorTile(Level, Row, Column, ETriangleType::Bottom))
	{
		Triangles.Add(Bottom);
	}
	if (FFloorTileData* Left = FindFloorTile(Level, Row, Column, ETriangleType::Left))
	{
		Triangles.Add(Left);
	}

	return Triangles;
}

void UFloorComponent::MarkLevelDirty(int32 Level)
{
	DirtyLevels.Add(Level);

	// If not in batch operation, rebuild immediately
	if (!bInBatchOperation)
	{
		RebuildLevel(Level);
		// Clear from dirty set after immediate rebuild to prevent duplicate rebuilds
		DirtyLevels.Remove(Level);
	}
}

void UFloorComponent::BeginBatchOperation()
{
	bInBatchOperation = true;
}

void UFloorComponent::EndBatchOperation()
{
	bInBatchOperation = false;

	// Copy dirty levels to avoid iteration issues
	TSet<int32> LevelsToRebuild = DirtyLevels;
	DirtyLevels.Empty();

	// Rebuild all dirty levels
	for (int32 Level : LevelsToRebuild)
	{
		RebuildLevel(Level);
	}
}

void UFloorComponent::RebuildLevel(int32 Level)
{
	UE_LOG(LogTemp, Log, TEXT("FloorComponent::RebuildLevel - Level %d (%d tiles)"), Level, FloorTileDataArray.Num());

	// Clear existing mesh sections for this level
	// Use LevelToSectionIndices to clear only the sections that belong to this level
	// IMPORTANT: Save ALL cleared section indices so we can reuse ONLY those exact indices
	TArray<int32> ClearedSectionIndices;

	if (TArray<int32>* SectionIndices = LevelToSectionIndices.Find(Level))
	{
		for (int32 SectionIndex : *SectionIndices)
		{
			if (GetProcMeshSection(SectionIndex) != nullptr)
			{
				ClearMeshSection(SectionIndex);
				// Save this cleared index for reuse
				ClearedSectionIndices.Add(SectionIndex);
			}
		}
		// Clear the tracking array for this level (will be repopulated below)
		SectionIndices->Empty();
	}

	UE_LOG(LogTemp, Log, TEXT("FloorComponent::RebuildLevel - Cleared %d existing sections for level %d (indices: %s)"),
		ClearedSectionIndices.Num(), Level,
		*FString::JoinBy(ClearedSectionIndices, TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));

	// Get LotManager for corner calculations
	ALotManager* OurLot = Cast<ALotManager>(GetOwner());
	if (!OurLot)
	{
		UE_LOG(LogTemp, Error, TEXT("FloorComponent::RebuildLevel - No LotManager found"));
		return;
	}

	// Safety check for empty array
	if (FloorTileDataArray.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("FloorComponent::RebuildLevel - No tiles to rebuild for level %d"), Level);
		return;
	}

	// ==================== STEP 1: Group triangles by pattern AND swatch ====================
	// Build map of (Pattern, SwatchIndex) -> Array of triangles with that combination
	// Using pointer comparison for pattern (nullptr is valid - means no pattern/default)
	// SwatchIndex is important because different swatches need different material instances
	// NOTE: Each FFloorTileData now represents ONE triangle, not a full tile
	// Use TPair as key since it already has hash support
	TMap<TPair<UFloorPattern*, int32>, TArray<const FFloorTileData*>> PatternGroups;

	int32 TotalTriangleCount = 0;
	for (int32 i = 0; i < FloorTileDataArray.Num(); i++)
	{
		const FFloorTileData& Triangle = FloorTileDataArray[i];
		if (!Triangle.bCommitted || Triangle.Level != Level)
		{
			continue;
		}

		// Group by pattern+swatch combination
		TPair<UFloorPattern*, int32> Key(Triangle.Pattern, Triangle.SwatchIndex);
		TArray<const FFloorTileData*>& Group = PatternGroups.FindOrAdd(Key);
		Group.Add(&Triangle);
		TotalTriangleCount++;
	}

	UE_LOG(LogTemp, Log, TEXT("FloorComponent::RebuildLevel - Found %d committed triangles across %d unique pattern+swatch combinations at level %d"),
		TotalTriangleCount, PatternGroups.Num(), Level);

	// ==================== STEP 2: Create mesh section per pattern ====================
	// Get or create section tracking array for this level
	TArray<int32>& LevelSections = LevelToSectionIndices.FindOrAdd(Level);

	// Track which cleared index we're reusing (pop from back of array)
	int32 ClearedIndexPosition = ClearedSectionIndices.Num() - 1;
	int32 PatternIndex = 0;
	for (auto& PatternGroup : PatternGroups)
	{
		const TPair<UFloorPattern*, int32>& Key = PatternGroup.Key;
		UFloorPattern* Pattern = Key.Key;
		int32 SwatchIndex = Key.Value;
		const TArray<const FFloorTileData*>& TrianglesWithPattern = PatternGroup.Value;

		UE_LOG(LogTemp, Log, TEXT("  Pattern %d: %d triangles (Pattern=%s, SwatchIndex=%d)"),
			PatternIndex, TrianglesWithPattern.Num(),
			Pattern ? *Pattern->GetName() : TEXT("nullptr (default)"), SwatchIndex);

		// Arrays for this pattern's merged mesh
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;

		// Reserve space for efficiency (each triangle = 5 top + 5 bottom + up to 8 edge vertices = ~18 vertices max)
		Vertices.Reserve(TrianglesWithPattern.Num() * 18);
		Triangles.Reserve(TrianglesWithPattern.Num() * 12); // 6 indices for top/bottom + up to 12 for edges
		Normals.Reserve(TrianglesWithPattern.Num() * 18);
		UVs.Reserve(TrianglesWithPattern.Num() * 18);

		// Iterate triangles in this pattern group
		for (const FFloorTileData* TrianglePtr : TrianglesWithPattern)
		{
			const FFloorTileData& Triangle = *TrianglePtr;

			// Each FFloorTileData now represents ONE triangle (not a full tile with 4 triangles)
			// Generate geometry for this specific triangle only

			// Get tile center location from grid coordinates
			FVector TileCenter;
			OurLot->TileToGridLocation(Level, Triangle.Row, Triangle.Column, true, TileCenter);

			// Get tile corners from LotManager
			TArray<FVector> CornerLocs = OurLot->LocationToAllTileCorners(TileCenter, Level);
			if (CornerLocs.Num() != 4)
			{
				continue; // Skip invalid tiles
			}

			// Calculate center point
			FVector Center = (CornerLocs[0] + CornerLocs[1] + CornerLocs[2] + CornerLocs[3]) / 4.0f;

			// Use appropriate thickness
			const float Thickness = FloorThickness;
			FVector ThicknessOffset = FVector(0, 0, Thickness);
			FVector TopOffset(0, 0, FloorTopOffset); // Slight offset prevents z-fighting with wall tops

			// Base vertex index for this triangle
			int32 BaseVertexIndex = Vertices.Num();

			// Add top surface vertices (5 vertices: 4 corners + center)
			// Order: TopLeft, TopRight, Center, BottomRight, BottomLeft
			Vertices.Add(CornerLocs[2] + TopOffset);  // TopLeft top
			Vertices.Add(CornerLocs[3] + TopOffset);  // TopRight top
			Vertices.Add(Center + TopOffset);          // Center top
			Vertices.Add(CornerLocs[1] + TopOffset);  // BottomRight top
			Vertices.Add(CornerLocs[0] + TopOffset);  // BottomLeft top

			// Add bottom surface vertices
			Vertices.Add(CornerLocs[2] - ThicknessOffset);  // TopLeft bottom
			Vertices.Add(CornerLocs[3] - ThicknessOffset);  // TopRight bottom
			Vertices.Add(Center - ThicknessOffset);         // Center bottom
			Vertices.Add(CornerLocs[1] - ThicknessOffset);  // BottomRight bottom
			Vertices.Add(CornerLocs[0] - ThicknessOffset);  // BottomLeft bottom

			// Add UVs for all vertices
			UVs.Add(FVector2D(0, 1));    // TopLeft
			UVs.Add(FVector2D(1, 1));    // TopRight
			UVs.Add(FVector2D(0.5, 0.5)); // Center
			UVs.Add(FVector2D(1, 0));    // BottomRight
			UVs.Add(FVector2D(0, 0));    // BottomLeft
			// Bottom UVs (same as top)
			UVs.Add(FVector2D(0, 1));
			UVs.Add(FVector2D(1, 1));
			UVs.Add(FVector2D(0.5, 0.5));
			UVs.Add(FVector2D(1, 0));
			UVs.Add(FVector2D(0, 0));

			// Add normals (up for top surface, down for bottom surface)
			for (int32 NormalIdx = 0; NormalIdx < 5; NormalIdx++)
			{
				Normals.Add(FVector(0, 0, 1));  // Top surface normals
			}
			for (int32 NormalIdx = 0; NormalIdx < 5; NormalIdx++)
			{
				Normals.Add(FVector(0, 0, -1)); // Bottom surface normals
			}

			// Add top surface triangle indices based on Triangle.Triangle type
			// Triangle types match visual position (Top = visual top edge of tile)
			switch (Triangle.Triangle)
			{
			case ETriangleType::Top:
				// Top triangle renders at top edge (TopLeft, TopRight, Center)
				Triangles.Add(BaseVertexIndex + 0); // TopLeft
				Triangles.Add(BaseVertexIndex + 1); // TopRight
				Triangles.Add(BaseVertexIndex + 2); // Center
				break;
			case ETriangleType::Right:
				Triangles.Add(BaseVertexIndex + 1); // TopRight
				Triangles.Add(BaseVertexIndex + 3); // BottomRight
				Triangles.Add(BaseVertexIndex + 2); // Center
				break;
			case ETriangleType::Bottom:
				// Bottom triangle renders at bottom edge (BottomRight, BottomLeft, Center)
				Triangles.Add(BaseVertexIndex + 3); // BottomRight
				Triangles.Add(BaseVertexIndex + 4); // BottomLeft
				Triangles.Add(BaseVertexIndex + 2); // Center
				break;
			case ETriangleType::Left:
				Triangles.Add(BaseVertexIndex + 4); // BottomLeft
				Triangles.Add(BaseVertexIndex + 0); // TopLeft
				Triangles.Add(BaseVertexIndex + 2); // Center
				break;
			default:
				UE_LOG(LogTemp, Warning, TEXT("Triangle (%d,%d) Level %d has invalid triangle type %d"),
					Triangle.Row, Triangle.Column, Level, static_cast<uint8>(Triangle.Triangle));
				continue;
			}

			// Add bottom surface triangle (reverse winding order)
			switch (Triangle.Triangle)
			{
			case ETriangleType::Top:
				// Top triangle bottom surface at top edge (TopLeft, TopRight, Center)
				Triangles.Add(BaseVertexIndex + 5); // TopLeft bottom
				Triangles.Add(BaseVertexIndex + 7); // Center
				Triangles.Add(BaseVertexIndex + 6); // TopRight bottom
				break;
			case ETriangleType::Right:
				Triangles.Add(BaseVertexIndex + 6);
				Triangles.Add(BaseVertexIndex + 7); // Center
				Triangles.Add(BaseVertexIndex + 8);
				break;
			case ETriangleType::Bottom:
				// Bottom triangle bottom surface at bottom edge (BottomRight, BottomLeft, Center)
				Triangles.Add(BaseVertexIndex + 8); // BottomRight bottom
				Triangles.Add(BaseVertexIndex + 7); // Center
				Triangles.Add(BaseVertexIndex + 9); // BottomLeft bottom
				break;
			case ETriangleType::Left:
				Triangles.Add(BaseVertexIndex + 9);
				Triangles.Add(BaseVertexIndex + 7);
				Triangles.Add(BaseVertexIndex + 5);
				break;
			}

			// Generate edge faces for this triangle
			// Each triangle has TWO exposed edges:
			// 1. Boundary edge (shared with neighbor tile)
			// 2. One diagonal edge (shared with adjacent triangle in same tile)

			// Top triangle: Top boundary (Row+1 neighbor) + TopRight->Center diagonal (our Right triangle)
			// Right triangle: Right boundary (Column+1 neighbor) + BottomRight->Center diagonal (our Bottom triangle)
			// Bottom triangle: Bottom boundary (Row-1 neighbor) + BottomLeft->Center diagonal (our Left triangle)
			// Left triangle: Left boundary (Column-1 neighbor) + TopLeft->Center diagonal (our Top triangle)

			switch (Triangle.Triangle)
			{
			case ETriangleType::Top:
			{
				// Top triangle renders at TOP edge (north)
				// Boundary edge: Top (TopLeft to TopRight) - render if neighbor's Bottom triangle missing
				FFloorTileData* NorthNeighbor = FindFloorTile(Level, Triangle.Row + 1, Triangle.Column, ETriangleType::Bottom);
				if (!NorthNeighbor)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[2] + TopOffset); // TopLeft top
					Vertices.Add(CornerLocs[3] + TopOffset); // TopRight top
					Vertices.Add(CornerLocs[3] - ThicknessOffset); // TopRight bottom
					Vertices.Add(CornerLocs[2] - ThicknessOffset); // TopLeft bottom

					FVector EdgeNormal = FVector(0, 1, 0); // Facing north
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}

				// Diagonal edge: TopRight to Center - render if our Right triangle missing
				FFloorTileData* OurRightTriangle = FindFloorTile(Level, Triangle.Row, Triangle.Column, ETriangleType::Right);
				if (!OurRightTriangle)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[3] + TopOffset); // TopRight top
					Vertices.Add(Center + TopOffset); // Center top
					Vertices.Add(Center - ThicknessOffset); // Center bottom
					Vertices.Add(CornerLocs[3] - ThicknessOffset); // TopRight bottom

					FVector EdgeNormal = (CornerLocs[3] - Center).GetSafeNormal2D().RotateAngleAxis(90.0f, FVector::UpVector);
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}
				break;
			}

			case ETriangleType::Right:
			{
				// Boundary edge: Right (TopRight to BottomRight) - render if neighbor's Left triangle missing
				FFloorTileData* RightNeighbor = FindFloorTile(Level, Triangle.Row, Triangle.Column + 1, ETriangleType::Left);
				if (!RightNeighbor)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[3] + TopOffset); // TopRight top
					Vertices.Add(CornerLocs[1] + TopOffset); // BottomRight top
					Vertices.Add(CornerLocs[1] - ThicknessOffset); // BottomRight bottom
					Vertices.Add(CornerLocs[3] - ThicknessOffset); // TopRight bottom

					FVector EdgeNormal = FVector(1, 0, 0);
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}

				// Diagonal edge: BottomRight to Center - render if our Bottom triangle missing
				FFloorTileData* OurBottomTriangle = FindFloorTile(Level, Triangle.Row, Triangle.Column, ETriangleType::Bottom);
				if (!OurBottomTriangle)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[1] + TopOffset); // BottomRight top
					Vertices.Add(Center + TopOffset); // Center top
					Vertices.Add(Center - ThicknessOffset); // Center bottom
					Vertices.Add(CornerLocs[1] - ThicknessOffset); // BottomRight bottom

					FVector EdgeNormal = (CornerLocs[1] - Center).GetSafeNormal2D().RotateAngleAxis(90.0f, FVector::UpVector);
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}
				break;
			}

			case ETriangleType::Bottom:
			{
				// Bottom triangle renders at BOTTOM edge (south)
				// Boundary edge: Bottom (BottomRight to BottomLeft) - render if neighbor's Top triangle missing
				FFloorTileData* SouthNeighbor = FindFloorTile(Level, Triangle.Row - 1, Triangle.Column, ETriangleType::Top);
				if (!SouthNeighbor)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[1] + TopOffset); // BottomRight top
					Vertices.Add(CornerLocs[0] + TopOffset); // BottomLeft top
					Vertices.Add(CornerLocs[0] - ThicknessOffset); // BottomLeft bottom
					Vertices.Add(CornerLocs[1] - ThicknessOffset); // BottomRight bottom

					FVector EdgeNormal = FVector(0, -1, 0); // Facing south
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}

				// Diagonal edge: BottomLeft to Center - render if our Left triangle missing
				FFloorTileData* OurLeftTriangle = FindFloorTile(Level, Triangle.Row, Triangle.Column, ETriangleType::Left);
				if (!OurLeftTriangle)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[0] + TopOffset); // BottomLeft top
					Vertices.Add(Center + TopOffset); // Center top
					Vertices.Add(Center - ThicknessOffset); // Center bottom
					Vertices.Add(CornerLocs[0] - ThicknessOffset); // BottomLeft bottom

					FVector EdgeNormal = (CornerLocs[0] - Center).GetSafeNormal2D().RotateAngleAxis(90.0f, FVector::UpVector);
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}
				break;
			}

			case ETriangleType::Left:
			{
				// Boundary edge: Left (BottomLeft to TopLeft) - render if neighbor's Right triangle missing
				FFloorTileData* LeftNeighbor = FindFloorTile(Level, Triangle.Row, Triangle.Column - 1, ETriangleType::Right);
				if (!LeftNeighbor)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[0] + TopOffset); // BottomLeft top
					Vertices.Add(CornerLocs[2] + TopOffset); // TopLeft top
					Vertices.Add(CornerLocs[2] - ThicknessOffset); // TopLeft bottom
					Vertices.Add(CornerLocs[0] - ThicknessOffset); // BottomLeft bottom

					FVector EdgeNormal = FVector(-1, 0, 0);
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}

				// Diagonal edge: TopLeft to Center - render if our Top triangle missing
				FFloorTileData* OurTopTriangle = FindFloorTile(Level, Triangle.Row, Triangle.Column, ETriangleType::Top);
				if (!OurTopTriangle)
				{
					int32 EdgeBaseIndex = Vertices.Num();
					Vertices.Add(CornerLocs[2] + TopOffset); // TopLeft top
					Vertices.Add(Center + TopOffset); // Center top
					Vertices.Add(Center - ThicknessOffset); // Center bottom
					Vertices.Add(CornerLocs[2] - ThicknessOffset); // TopLeft bottom

					FVector EdgeNormal = (CornerLocs[2] - Center).GetSafeNormal2D().RotateAngleAxis(90.0f, FVector::UpVector);
					for (int32 i = 0; i < 4; i++) Normals.Add(EdgeNormal);

					UVs.Add(FVector2D(0, 1));
					UVs.Add(FVector2D(1, 1));
					UVs.Add(FVector2D(1, 0));
					UVs.Add(FVector2D(0, 0));

					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 2);
					Triangles.Add(EdgeBaseIndex + 1);
					Triangles.Add(EdgeBaseIndex + 0);
					Triangles.Add(EdgeBaseIndex + 3);
					Triangles.Add(EdgeBaseIndex + 2);
				}
				break;
			}
			}
		}

		// Only create mesh if we have vertices for this pattern
		if (Vertices.Num() > 0)
		{
			// Allocate section index:
			// 1. Try to reuse a cleared section index from THIS level (pop from array)
			// 2. If no cleared indices left, allocate a fresh one from global pool
			int32 SectionIndex;
			if (ClearedIndexPosition >= 0)
			{
				// Reuse a cleared section index from THIS level only
				SectionIndex = ClearedSectionIndices[ClearedIndexPosition];
				ClearedIndexPosition--;
				UE_LOG(LogTemp, Log, TEXT("    Reusing cleared section %d"), SectionIndex);
			}
			else
			{
				// Need more sections than we cleared - allocate from global pool
				SectionIndex = NextSectionIndex++;
				UE_LOG(LogTemp, Log, TEXT("    Allocating new section %d from global pool"), SectionIndex);
			}

			// Track this section as belonging to this level
			LevelSections.Add(SectionIndex);

			UE_LOG(LogTemp, Log, TEXT("    Creating mesh section %d with %d vertices, %d triangles"),
				SectionIndex, Vertices.Num(), Triangles.Num());

			// Create mesh section for this pattern group
			CreateMeshSection(SectionIndex, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

			// ==================== STEP 3: Apply material with pattern textures ====================

			// Determine base material to use
			UMaterialInstance* BaseMaterialToUse = nullptr;

			// Priority:
			// 1. Pattern's BaseMaterial (if pattern exists and has material)
			// 2. LevelMaterials map (legacy support)
			// 3. Component's FloorMaterial (fallback)
			if (Pattern && Pattern->BaseMaterial)
			{
				BaseMaterialToUse = Pattern->BaseMaterial;
			}
			else if (UMaterialInstance* LevelMat = LevelMaterials.FindRef(Level))
			{
				BaseMaterialToUse = LevelMat;
			}
			else
			{
				BaseMaterialToUse = FloorMaterial;
			}

			if (BaseMaterialToUse)
			{
				// Check if we already have a cached material for this section
				UMaterialInstanceDynamic* SectionDynamicMat = SectionMaterialCache.FindRef(SectionIndex);

				// Create new material if: no cached material OR base material changed
				if (!SectionDynamicMat || SectionDynamicMat->Parent != BaseMaterialToUse)
				{
					SectionDynamicMat = UMaterialInstanceDynamic::Create(BaseMaterialToUse, this);
					if (SectionDynamicMat)
					{
						// Store in cache for future rebuilds
						SectionMaterialCache.Add(SectionIndex, SectionDynamicMat);
						UE_LOG(LogTemp, Log, TEXT("    Created NEW material for section %d"), SectionIndex);
					}
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("    Reusing CACHED material for section %d (preserves grid/visibility state)"), SectionIndex);
				}

				if (SectionDynamicMat)
				{
					// Apply pattern textures if pattern exists
					if (Pattern)
					{
						if (Pattern->BaseTexture)
						{
							SectionDynamicMat->SetTextureParameterValue(FName("FloorMaterial"), Pattern->BaseTexture);
							UE_LOG(LogTemp, Log, TEXT("    Applied BaseTexture: %s"), *Pattern->BaseTexture->GetName());
						}
						if (Pattern->NormalMap)
						{
							SectionDynamicMat->SetTextureParameterValue(FName("FloorNormal"), Pattern->NormalMap);
						}
						if (Pattern->RoughnessMap)
						{
							SectionDynamicMat->SetTextureParameterValue(FName("FloorRoughness"), Pattern->RoughnessMap);
						}

						// Apply color swatch if pattern uses swatches
						// Note: SwatchIndex 0 = default (no color tint), 1+ = ColourSwatches[index-1]
						bool bApplySwatch = Pattern->bUseColourSwatches && SwatchIndex > 0 && Pattern->ColourSwatches.IsValidIndex(SwatchIndex - 1);
						SectionDynamicMat->SetScalarParameterValue(FName("bUseColourSwatches"), bApplySwatch ? 1.0f : 0.0f);
						SectionDynamicMat->SetScalarParameterValue(FName("bUseColourMask"), (bApplySwatch && Pattern->bUseColourMask) ? 1.0f : 0.0f);
						if (bApplySwatch)
						{
							SectionDynamicMat->SetVectorParameterValue(FName("FloorColour"), Pattern->ColourSwatches[SwatchIndex - 1]);
							UE_LOG(LogTemp, Log, TEXT("    Applied swatch color (index %d, array index %d): %s"), SwatchIndex, SwatchIndex - 1, *Pattern->ColourSwatches[SwatchIndex - 1].ToString());
						}
					}

					// Set visibility based on current level
					float VisibilityValue = (Level > OurLot->CurrentLevel) ? 0.0f : 1.0f;
					SectionDynamicMat->SetScalarParameterValue(FName("VisibilityLevel"), VisibilityValue);

					// Set grid visibility
					bool bShouldShowGrid = OurLot->bShowGrid && (Level == OurLot->CurrentLevel);
					SectionDynamicMat->SetScalarParameterValue(FName("ShowGrid"), bShouldShowGrid ? 1.0f : 0.0f);

					// Apply material to this section
					SetMaterial(SectionIndex, SectionDynamicMat);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("    No base material available for pattern %s"), Pattern ? *Pattern->GetName() : TEXT("nullptr"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("  Pattern %d: No vertices generated"), PatternIndex);
		}

		PatternIndex++;
	}

	UE_LOG(LogTemp, Log, TEXT("FloorComponent::RebuildLevel - COMPLETE Level %d (%d sections created)"), Level, PatternGroups.Num());
}

TArray<int32> UFloorComponent::TriangulatePolygon(const TArray<FVector>& Vertices)
{
	TArray<int32> Triangles;

	if (Vertices.Num() < 3)
	{
		return Triangles; // Can't triangulate
	}

	if (Vertices.Num() == 3)
	{
		// Already a triangle
		Triangles.Add(0);
		Triangles.Add(1);
		Triangles.Add(2);
		return Triangles;
	}

	// Ear clipping algorithm for simple polygons (handles both convex and concave)
	// Always use ear clipping - the convexity check was broken for L-shapes
	{
		TArray<int32> RemainingIndices;
		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			RemainingIndices.Add(i);
		}

		while (RemainingIndices.Num() > 3)
		{
			bool bFoundEar = false;

			for (int32 i = 0; i < RemainingIndices.Num(); i++)
			{
				int32 PrevIdx = (i == 0) ? RemainingIndices.Num() - 1 : i - 1;
				int32 NextIdx = (i + 1) % RemainingIndices.Num();

				int32 V0 = RemainingIndices[PrevIdx];
				int32 V1 = RemainingIndices[i];
				int32 V2 = RemainingIndices[NextIdx];

				FVector Edge1 = Vertices[V1] - Vertices[V0];
				FVector Edge2 = Vertices[V2] - Vertices[V1];
				float CrossZ = FVector::CrossProduct(Edge1, Edge2).Z;

				// Check if this is a convex vertex (ear candidate)
				if (CrossZ > 0.0f)
				{
					// Create triangle
					Triangles.Add(V0);
					Triangles.Add(V1);
					Triangles.Add(V2);

					// Remove the ear vertex
					RemainingIndices.RemoveAt(i);
					bFoundEar = true;
					break;
				}
			}

			if (!bFoundEar)
			{
				// Couldn't find an ear - polygon might be self-intersecting
				// Just do a fan triangulation as fallback
				UE_LOG(LogTemp, Warning, TEXT("TriangulatePolygon: Couldn't find ear, using fan triangulation"));
				Triangles.Empty();
				for (int32 i = 1; i < Vertices.Num() - 1; i++)
				{
					Triangles.Add(0);
					Triangles.Add(i);
					Triangles.Add(i + 1);
				}
				break;
			}
		}

		// Add final triangle
		if (RemainingIndices.Num() == 3)
		{
			Triangles.Add(RemainingIndices[0]);
			Triangles.Add(RemainingIndices[1]);
			Triangles.Add(RemainingIndices[2]);
		}
	}

	return Triangles;
}

uint8 UFloorComponent::CalculateFilledMask(int32 Row, int32 Column, int32 Level) const
{
	uint8 Mask = 0;

	// Check each of the 4 neighbors and set corresponding bit if neighbor exists AND has connecting section
	// Bit 0: Top neighbor (Row+1, Column) with Bottom section
	// Bit 1: Right neighbor (Row, Column+1) with Left section
	// Bit 2: Bottom neighbor (Row-1, Column) with Top section
	// Bit 3: Left neighbor (Row, Column-1) with Right section

	// Top neighbor check (Row+1)
	{
		int32 GridKey = MakeGridKey(Level, Row + 1, Column);
		if (const int32* IndexPtr = FloorSpatialMap.Find(GridKey))
		{
			if (FloorTileDataArray.IsValidIndex(*IndexPtr) && FloorTileDataArray[*IndexPtr].bCommitted)
			{
				// Only set bit if neighbor has Bottom section enabled (connects to our Top edge)
				if (FloorTileDataArray[*IndexPtr].TileSectionState.Bottom)
				{
					Mask |= 0x01; // Bit 0
				}
			}
		}
	}

	// Right neighbor check (Column+1)
	{
		int32 GridKey = MakeGridKey(Level, Row, Column + 1);
		if (const int32* IndexPtr = FloorSpatialMap.Find(GridKey))
		{
			if (FloorTileDataArray.IsValidIndex(*IndexPtr) && FloorTileDataArray[*IndexPtr].bCommitted)
			{
				// Only set bit if neighbor has Left section enabled (connects to our Right edge)
				if (FloorTileDataArray[*IndexPtr].TileSectionState.Left)
				{
					Mask |= 0x02; // Bit 1
				}
			}
		}
	}

	// Bottom neighbor check (Row-1)
	{
		int32 GridKey = MakeGridKey(Level, Row - 1, Column);
		if (const int32* IndexPtr = FloorSpatialMap.Find(GridKey))
		{
			if (FloorTileDataArray.IsValidIndex(*IndexPtr) && FloorTileDataArray[*IndexPtr].bCommitted)
			{
				// Only set bit if neighbor has Top section enabled (connects to our Bottom edge)
				if (FloorTileDataArray[*IndexPtr].TileSectionState.Top)
				{
					Mask |= 0x04; // Bit 2
				}
			}
		}
	}

	// Left neighbor check (Column-1)
	{
		int32 GridKey = MakeGridKey(Level, Row, Column - 1);
		if (const int32* IndexPtr = FloorSpatialMap.Find(GridKey))
		{
			if (FloorTileDataArray.IsValidIndex(*IndexPtr) && FloorTileDataArray[*IndexPtr].bCommitted)
			{
				// Only set bit if neighbor has Right section enabled (connects to our Left edge)
				if (FloorTileDataArray[*IndexPtr].TileSectionState.Right)
				{
					Mask |= 0x08; // Bit 3
				}
			}
		}
	}

	return Mask;
}
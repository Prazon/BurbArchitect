// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "EditorUtilityObject.h"
#include "WallMeshExporter.generated.h"

/**
 * Editor utility for exporting a reference wall section mesh as a static mesh asset
 */
UCLASS(Blueprintable)
class BURBARCHITECTEDITOR_API UWallMeshExporter : public UEditorUtilityObject
{
	GENERATED_BODY()

public:
	/**
	 * Exports a standard wall section mesh as a static mesh asset
	 *
	 * @param WallHeight Height of the wall in units (default: 300)
	 * @param WallThickness Thickness of the wall in units (default: 20)
	 * @param WallLength Length of the wall in units (default: 200 - one tile width)
	 * @param AssetPath Path where the static mesh will be saved (default: /Game/ReferenceGeometry/)
	 * @param AssetName Name of the static mesh asset (default: SM_WallSection_Reference)
	 * @return True if export was successful, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "BurbArchitect|Export")
	bool ExportWallSectionMesh(
		float WallHeight = 300.0f,
		float WallThickness = 20.0f,
		float WallLength = 200.0f,
		const FString& AssetPath = TEXT("/Game/ReferenceGeometry/"),
		const FString& AssetName = TEXT("SM_WallSection_Reference")
	);

private:
	/**
	 * Generates wall mesh geometry
	 *
	 * @param WallLength Length of the wall
	 * @param WallHeight Height of the wall
	 * @param WallThickness Thickness of the wall
	 * @param OutVertices Generated vertices
	 * @param OutTriangles Generated triangle indices
	 * @param OutNormals Generated normals
	 * @param OutUVs Generated UV coordinates
	 * @param OutTangents Generated tangents
	 */
	void GenerateWallGeometry(
		float WallLength,
		float WallHeight,
		float WallThickness,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		TArray<FVector>& OutTangents
	);

	/**
	 * Converts procedural mesh data to a static mesh asset
	 *
	 * @param Vertices Mesh vertices
	 * @param Triangles Triangle indices
	 * @param Normals Vertex normals
	 * @param UVs UV coordinates
	 * @param Tangents Vertex tangents
	 * @param PackageName Full package path for the asset
	 * @param AssetName Name of the asset
	 * @return The created static mesh asset, or nullptr if failed
	 */
	UStaticMesh* CreateStaticMeshAsset(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UVs,
		const TArray<FVector>& Tangents,
		const FString& PackageName,
		const FString& AssetName
	);
};

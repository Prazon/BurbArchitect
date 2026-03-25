// Fill out your copyright notice in the Description page of Project Settings.

#include "WallMeshExporter.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshDescription.h"
#include "MeshDescription.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "PackageTools.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"

bool UWallMeshExporter::ExportWallSectionMesh(
	float WallHeight,
	float WallThickness,
	float WallLength,
	const FString& AssetPath,
	const FString& AssetName)
{
	// Validate parameters
	if (WallHeight <= 0.0f || WallThickness <= 0.0f || WallLength <= 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("WallMeshExporter: Invalid dimensions. All values must be greater than 0."));
		return false;
	}

	// Generate wall geometry
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FVector> Tangents;

	GenerateWallGeometry(WallLength, WallHeight, WallThickness, Vertices, Triangles, Normals, UVs, Tangents);

	if (Vertices.Num() == 0 || Triangles.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("WallMeshExporter: Failed to generate wall geometry."));
		return false;
	}

	// Ensure asset path ends with /
	FString ValidatedAssetPath = AssetPath;
	if (!ValidatedAssetPath.EndsWith(TEXT("/")))
	{
		ValidatedAssetPath += TEXT("/");
	}

	// Create full package name
	FString PackageName = ValidatedAssetPath + AssetName;

	// Create the static mesh asset
	UStaticMesh* StaticMesh = CreateStaticMeshAsset(Vertices, Triangles, Normals, UVs, Tangents, PackageName, AssetName);

	if (StaticMesh)
	{
		UE_LOG(LogTemp, Log, TEXT("WallMeshExporter: Successfully exported wall section to %s"), *PackageName);
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("WallMeshExporter: Failed to create static mesh asset."));
	return false;
}

void UWallMeshExporter::GenerateWallGeometry(
	float WallLength,
	float WallHeight,
	float WallThickness,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs,
	TArray<FVector>& OutTangents)
{
	// Clear output arrays
	OutVertices.Empty();
	OutTriangles.Empty();
	OutNormals.Empty();
	OutUVs.Empty();
	OutTangents.Empty();

	const float HalfWidth = WallThickness / 2.0f;
	const float HalfLength = WallLength / 2.0f;

	// Define wall corner points - Wall extends along X axis, centered at origin
	FVector StartLoc(-HalfLength, 0.0f, 0.0f);
	FVector EndLoc(HalfLength, 0.0f, 0.0f);

	// Calculate the 8 corner vertices of the wall
	// NOTE: At the end of the wall, Left/Right are swapped (relative to wall direction)
	FVector RightPointStart = StartLoc + FVector(0.0f, -HalfWidth, 0.0f);
	FVector LeftPointStart = StartLoc + FVector(0.0f, HalfWidth, 0.0f);
	FVector RightPointEnd = EndLoc + FVector(0.0f, HalfWidth, 0.0f);   // Swapped: +Y at end
	FVector LeftPointEnd = EndLoc + FVector(0.0f, -HalfWidth, 0.0f);   // Swapped: -Y at end

	FVector RightPointStartUp = RightPointStart + FVector(0.0f, 0.0f, WallHeight);
	FVector LeftPointStartUp = LeftPointStart + FVector(0.0f, 0.0f, WallHeight);
	FVector RightPointEndUp = RightPointEnd + FVector(0.0f, 0.0f, WallHeight);
	FVector LeftPointEndUp = LeftPointEnd + FVector(0.0f, 0.0f, WallHeight);

	// Cutaway geometry vertices
	const float BaseboardHeight = 30.0f;
	FVector BaseboardTopLeft = LeftPointStart + FVector(0.0f, 0.0f, BaseboardHeight);
	FVector BaseboardTopRight = RightPointStart + FVector(0.0f, 0.0f, BaseboardHeight);
	FVector BaseboardTopLeftEnd = LeftPointEnd + FVector(0.0f, 0.0f, BaseboardHeight);
	FVector BaseboardTopRightEnd = RightPointEnd + FVector(0.0f, 0.0f, BaseboardHeight);

	// Baseboard center points (convergence points)
	FVector BaseboardTopCenterPosY = ((LeftPointStart + RightPointEnd) * 0.5f) + FVector(0.0f, 0.0f, BaseboardHeight);
	FVector BaseboardTopCenterNegY = ((RightPointStart + LeftPointEnd) * 0.5f) + FVector(0.0f, 0.0f, BaseboardHeight);
	FVector BaseboardTopCenterStart = ((LeftPointStart + RightPointStart) * 0.5f) + FVector(0.0f, 0.0f, BaseboardHeight);
	FVector BaseboardTopCenterEnd = ((LeftPointEnd + RightPointEnd) * 0.5f) + FVector(0.0f, 0.0f, BaseboardHeight);

	// Lambda to add a face to the mesh with proper UVs and tangents
	auto AddFace = [&](const TArray<FVector>& FaceVerts, const TArray<int32>& FaceTris, const TArray<FVector2D>& FaceUVs, const FVector& Normal)
	{
		int32 VertexBase = OutVertices.Num();

		// Add vertices
		OutVertices.Append(FaceVerts);

		// Add triangles with offset
		for (int32 Index : FaceTris)
		{
			OutTriangles.Add(VertexBase + Index);
		}

		// Add normals, UVs, and tangents for each vertex
		for (int i = 0; i < FaceVerts.Num(); i++)
		{
			OutNormals.Add(Normal);
			OutUVs.Add(FaceUVs.IsValidIndex(i) ? FaceUVs[i] : FVector2D::ZeroVector);

			// Calculate tangent (perpendicular to normal, pointing right in UV space)
			FVector Tangent = FVector::CrossProduct(Normal, FVector::UpVector);
			if (Tangent.IsNearlyZero())
			{
				Tangent = FVector::CrossProduct(Normal, FVector::ForwardVector);
			}
			OutTangents.Add(Tangent.GetSafeNormal());
		}
	};

	// Calculate UV offsets for extended render target (matches runtime)
	// The render target is wider than the wall (20 unit margin on each side)
	// so we need to map the wall surface to the center portion of the texture
	const float RenderTargetMargin = 20.0f;
	float WallWidth = WallLength;
	float ExtendedWidth = WallWidth + (RenderTargetMargin * 2.0f);
	float UVMin = RenderTargetMargin / ExtendedWidth;
	float UVMax = (WallWidth + RenderTargetMargin) / ExtendedWidth;

	// === MAIN FACES (PosY and NegY) with cutaway geometry ===

	// PosY Surface (Left side) - 7 vertices, 6 triangles
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(LeftPointStart);          // 0 - Bottom left
		FaceVerts.Add(RightPointEnd);           // 1 - Bottom right
		FaceVerts.Add(BaseboardTopLeft);        // 2 - Baseboard top left
		FaceVerts.Add(BaseboardTopRightEnd);    // 3 - Baseboard top right
		FaceVerts.Add(BaseboardTopCenterPosY);  // 4 - Baseboard center (convergence point)
		FaceVerts.Add(LeftPointStartUp);        // 5 - Top left
		FaceVerts.Add(RightPointEndUp);         // 6 - Top right

		TArray<int32> FaceTris;
		// Baseboard triangles (3 triangles)
		FaceTris.Add(0); FaceTris.Add(4); FaceTris.Add(2); // Triangle 1
		FaceTris.Add(0); FaceTris.Add(1); FaceTris.Add(4); // Triangle 2
		FaceTris.Add(4); FaceTris.Add(1); FaceTris.Add(3); // Triangle 3
		// Main wall triangles (3 triangles with diagonal cuts)
		FaceTris.Add(5); FaceTris.Add(2); FaceTris.Add(4); // Triangle 4 - Left side
		FaceTris.Add(6); FaceTris.Add(4); FaceTris.Add(3); // Triangle 5 - Right side
		FaceTris.Add(4); FaceTris.Add(6); FaceTris.Add(5); // Triangle 6 - Top diagonal

		TArray<FVector2D> FaceUVs;
		float baseboardUVHeight = BaseboardHeight / WallHeight;
		FaceUVs.Add(FVector2D(UVMin, 1.0f));                                      // 0 - Bottom left
		FaceUVs.Add(FVector2D(UVMax, 1.0f));                                      // 1 - Bottom right
		FaceUVs.Add(FVector2D(UVMin, 1.0f - baseboardUVHeight));                  // 2 - Baseboard top left
		FaceUVs.Add(FVector2D(UVMax, 1.0f - baseboardUVHeight));                  // 3 - Baseboard top right
		FaceUVs.Add(FVector2D((UVMin + UVMax) * 0.5f, 1.0f - baseboardUVHeight)); // 4 - Center
		FaceUVs.Add(FVector2D(UVMin, 0.0f));                                      // 5 - Top left
		FaceUVs.Add(FVector2D(UVMax, 0.0f));                                      // 6 - Top right

		// Calculate normal from first triangle (0,4,2) using winding order - reversed to point outward
		FVector PosYNormal = FVector::CrossProduct((FaceVerts[2] - FaceVerts[0]), (FaceVerts[4] - FaceVerts[0])).GetSafeNormal();
		AddFace(FaceVerts, FaceTris, FaceUVs, PosYNormal);
	}

	// NegY Surface (Right side) - 7 vertices, 6 triangles
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(RightPointStart);         // 0 - Bottom left (right side)
		FaceVerts.Add(LeftPointEnd);            // 1 - Bottom right (left side)
		FaceVerts.Add(BaseboardTopRight);       // 2 - Baseboard top left
		FaceVerts.Add(BaseboardTopLeftEnd);     // 3 - Baseboard top right
		FaceVerts.Add(BaseboardTopCenterNegY);  // 4 - Baseboard center
		FaceVerts.Add(RightPointStartUp);       // 5 - Top left
		FaceVerts.Add(LeftPointEndUp);          // 6 - Top right

		TArray<int32> FaceTris;
		// Baseboard triangles (3 triangles, reversed winding)
		FaceTris.Add(0); FaceTris.Add(2); FaceTris.Add(4);
		FaceTris.Add(0); FaceTris.Add(4); FaceTris.Add(1);
		FaceTris.Add(4); FaceTris.Add(3); FaceTris.Add(1);
		// Main wall triangles (3 triangles, reversed winding)
		FaceTris.Add(5); FaceTris.Add(4); FaceTris.Add(2);
		FaceTris.Add(6); FaceTris.Add(3); FaceTris.Add(4);
		FaceTris.Add(6); FaceTris.Add(4); FaceTris.Add(5); // Top diagonal

		TArray<FVector2D> FaceUVs;
		float baseboardUVHeight = BaseboardHeight / WallHeight;
		FaceUVs.Add(FVector2D(UVMin, 1.0f));
		FaceUVs.Add(FVector2D(UVMax, 1.0f));
		FaceUVs.Add(FVector2D(UVMin, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(UVMax, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D((UVMin + UVMax) * 0.5f, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(UVMin, 0.0f));
		FaceUVs.Add(FVector2D(UVMax, 0.0f));

		// Calculate normal from first triangle (0,2,4) using winding order - reversed to point outward
		FVector NegYNormal = FVector::CrossProduct((FaceVerts[4] - FaceVerts[0]), (FaceVerts[2] - FaceVerts[0])).GetSafeNormal();
		AddFace(FaceVerts, FaceTris, FaceUVs, NegYNormal);
	}

	// === EDGE FACES (NegX and PosX) with cutaway geometry ===

	// Start Edge face (NegX) - 7 vertices, 6 triangles
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(LeftPointStart);          // 0 - Bottom outer
		FaceVerts.Add(RightPointStart);         // 1 - Bottom inner
		FaceVerts.Add(BaseboardTopLeft);        // 2 - Baseboard top outer
		FaceVerts.Add(BaseboardTopRight);       // 3 - Baseboard top inner
		FaceVerts.Add(BaseboardTopCenterStart); // 4 - Baseboard center (convergence)
		FaceVerts.Add(LeftPointStartUp);        // 5 - Top outer
		FaceVerts.Add(RightPointStartUp);       // 6 - Top inner

		TArray<int32> FaceTris;
		// Baseboard triangles (3 triangles, reversed winding)
		FaceTris.Add(0); FaceTris.Add(2); FaceTris.Add(4);
		FaceTris.Add(0); FaceTris.Add(4); FaceTris.Add(1);
		FaceTris.Add(4); FaceTris.Add(3); FaceTris.Add(1);
		// Main wall triangles (3 triangles, reversed winding)
		FaceTris.Add(5); FaceTris.Add(4); FaceTris.Add(2);
		FaceTris.Add(6); FaceTris.Add(3); FaceTris.Add(4);
		FaceTris.Add(6); FaceTris.Add(4); FaceTris.Add(5); // Top diagonal

		TArray<FVector2D> FaceUVs;
		float baseboardUVHeight = BaseboardHeight / WallHeight;
		FaceUVs.Add(FVector2D(0.0f, 1.0f));
		FaceUVs.Add(FVector2D(1.0f, 1.0f));
		FaceUVs.Add(FVector2D(0.0f, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(1.0f, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(0.5f, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(0.0f, 0.0f));
		FaceUVs.Add(FVector2D(1.0f, 0.0f));

		// Calculate normal from first triangle (0,2,4) using winding order - reversed to point outward
		FVector NegXNormal = FVector::CrossProduct((FaceVerts[4] - FaceVerts[0]), (FaceVerts[2] - FaceVerts[0])).GetSafeNormal();
		AddFace(FaceVerts, FaceTris, FaceUVs, NegXNormal);
	}

	// End Edge face (PosX) - 7 vertices, 6 triangles
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(RightPointEnd);         // 0 - Bottom inner
		FaceVerts.Add(LeftPointEnd);          // 1 - Bottom outer
		FaceVerts.Add(BaseboardTopRightEnd);  // 2 - Baseboard top inner
		FaceVerts.Add(BaseboardTopLeftEnd);   // 3 - Baseboard top outer
		FaceVerts.Add(BaseboardTopCenterEnd); // 4 - Baseboard center (convergence)
		FaceVerts.Add(RightPointEndUp);       // 5 - Top inner
		FaceVerts.Add(LeftPointEndUp);        // 6 - Top outer

		TArray<int32> FaceTris;
		// Baseboard triangles (3 triangles, reversed winding from NegX)
		FaceTris.Add(4); FaceTris.Add(2); FaceTris.Add(0);
		FaceTris.Add(1); FaceTris.Add(4); FaceTris.Add(0);
		FaceTris.Add(1); FaceTris.Add(3); FaceTris.Add(4);
		// Main wall triangles (3 triangles, reversed winding from NegX)
		FaceTris.Add(2); FaceTris.Add(4); FaceTris.Add(5);
		FaceTris.Add(4); FaceTris.Add(3); FaceTris.Add(6);
		FaceTris.Add(5); FaceTris.Add(4); FaceTris.Add(6); // Top diagonal

		TArray<FVector2D> FaceUVs;
		float baseboardUVHeight = BaseboardHeight / WallHeight;
		FaceUVs.Add(FVector2D(0.0f, 1.0f));
		FaceUVs.Add(FVector2D(1.0f, 1.0f));
		FaceUVs.Add(FVector2D(0.0f, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(1.0f, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(0.5f, 1.0f - baseboardUVHeight));
		FaceUVs.Add(FVector2D(0.0f, 0.0f));
		FaceUVs.Add(FVector2D(1.0f, 0.0f));

		// Calculate normal from first triangle (4,2,0) using winding order - reversed to point outward
		FVector PosXNormal = FVector::CrossProduct((FaceVerts[0] - FaceVerts[4]), (FaceVerts[2] - FaceVerts[4])).GetSafeNormal();
		AddFace(FaceVerts, FaceTris, FaceUVs, PosXNormal);
	}

	// === TOP AND BOTTOM FACES ===

	// Top face (positive Z) - 6 vertices with center points
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(RightPointEndUp);    // 0
		FaceVerts.Add(LeftPointEndUp);     // 1
		FaceVerts.Add(LeftPointStartUp);   // 2
		FaceVerts.Add(RightPointStartUp);  // 3
		FaceVerts.Add(EndLoc + FVector(0.0f, 0.0f, WallHeight));   // 4 - MainPointEndUp (center at end)
		FaceVerts.Add(StartLoc + FVector(0.0f, 0.0f, WallHeight)); // 5 - MainPointStartUp (center at start)

		TArray<int32> FaceTris;
		// First set of triangles (outer rectangle)
		FaceTris.Add(0); FaceTris.Add(1); FaceTris.Add(2);
		FaceTris.Add(1); FaceTris.Add(3); FaceTris.Add(2);
		// Additional triangles with center points
		FaceTris.Add(0); FaceTris.Add(4); FaceTris.Add(1);
		FaceTris.Add(3); FaceTris.Add(5); FaceTris.Add(2);

		TArray<FVector2D> FaceUVs;
		FaceUVs.Add(FVector2D(0, 1));
		FaceUVs.Add(FVector2D(0, 0));
		FaceUVs.Add(FVector2D(1, 1));
		FaceUVs.Add(FVector2D(1, 0));
		FaceUVs.Add(FVector2D(0, 1)); // Center end
		FaceUVs.Add(FVector2D(0, 0)); // Center start

		AddFace(FaceVerts, FaceTris, FaceUVs, FVector(0, 0, 1));
	}

	// Bottom face (negative Z) - 6 vertices with center points
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(RightPointEnd);    // 0
		FaceVerts.Add(LeftPointEnd);     // 1
		FaceVerts.Add(LeftPointStart);   // 2
		FaceVerts.Add(RightPointStart);  // 3
		FaceVerts.Add(EndLoc);           // 4 - MainPointEnd (center at end)
		FaceVerts.Add(StartLoc);         // 5 - MainPointStart (center at start)

		TArray<int32> FaceTris;
		// First set of triangles (outer rectangle)
		FaceTris.Add(1); FaceTris.Add(0); FaceTris.Add(2);
		FaceTris.Add(3); FaceTris.Add(1); FaceTris.Add(2);
		// Additional triangles with center points
		FaceTris.Add(1); FaceTris.Add(4); FaceTris.Add(0);
		FaceTris.Add(3); FaceTris.Add(2); FaceTris.Add(5);

		TArray<FVector2D> FaceUVs;
		FaceUVs.Add(FVector2D(0, 1));
		FaceUVs.Add(FVector2D(0, 0));
		FaceUVs.Add(FVector2D(1, 1));
		FaceUVs.Add(FVector2D(1, 0));
		FaceUVs.Add(FVector2D(0, 1)); // Center end
		FaceUVs.Add(FVector2D(0, 0)); // Center start

		AddFace(FaceVerts, FaceTris, FaceUVs, FVector(0, 0, -1));
	}

	// === CAP FACES ===

	// Baseboard Top Cap - 8 vertices, 6 triangles
	{
		TArray<FVector> FaceVerts;
		// Outer rectangle vertices (4 corners)
		FaceVerts.Add(BaseboardTopLeft);        // 0
		FaceVerts.Add(BaseboardTopRight);       // 1
		FaceVerts.Add(BaseboardTopLeftEnd);     // 2
		FaceVerts.Add(BaseboardTopRightEnd);    // 3
		// Inner diamond vertices (4 convergence points)
		FaceVerts.Add(BaseboardTopCenterStart); // 4
		FaceVerts.Add(BaseboardTopCenterPosY);  // 5
		FaceVerts.Add(BaseboardTopCenterEnd);   // 6
		FaceVerts.Add(BaseboardTopCenterNegY);  // 7

		TArray<int32> FaceTris;
		// 6 triangles - all reversed for upward facing
		FaceTris.Add(4); FaceTris.Add(7); FaceTris.Add(1);
		FaceTris.Add(5); FaceTris.Add(7); FaceTris.Add(4);
		FaceTris.Add(0); FaceTris.Add(5); FaceTris.Add(4);
		FaceTris.Add(6); FaceTris.Add(2); FaceTris.Add(7);
		FaceTris.Add(5); FaceTris.Add(6); FaceTris.Add(7);
		FaceTris.Add(3); FaceTris.Add(6); FaceTris.Add(5);

		// Calculate UVs by projecting vertices onto the wall plane (matches WallComponent)
		TArray<FVector2D> FaceUVs;
		FVector WallDirection = (EndLoc - StartLoc).GetSafeNormal();
		for (int32 i = 0; i < FaceVerts.Num(); i++)
		{
			// Project vertex position onto the wall to get U coordinate
			FVector ToVertex = FaceVerts[i] - StartLoc;
			float U = FVector::DotProduct(ToVertex, WallDirection) / WallLength;
			float UScaled = UVMin + (U * (UVMax - UVMin));

			// V coordinate is based on height (Z component)
			float V = (FaceVerts[i].Z - StartLoc.Z) / WallHeight;

			FaceUVs.Add(FVector2D(UScaled, 1.0f - V)); // Flip V to match texture orientation
		}

		AddFace(FaceVerts, FaceTris, FaceUVs, FVector(0, 0, 1));
	}

	// Start Diagonal Cap - 4 vertices, 2 triangles
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(RightPointStartUp);      // 0 - RPSU
		FaceVerts.Add(BaseboardTopCenterNegY); // 1 - BTCNY
		FaceVerts.Add(LeftPointStartUp);       // 2 - LPSU
		FaceVerts.Add(BaseboardTopCenterPosY); // 3 - BTCPY

		TArray<int32> FaceTris;
		FaceTris.Add(2); FaceTris.Add(1); FaceTris.Add(0); // Reversed winding
		FaceTris.Add(2); FaceTris.Add(3); FaceTris.Add(1);

		// Calculate UVs by projecting vertices onto the wall plane (matches WallComponent)
		TArray<FVector2D> FaceUVs;
		FVector WallDirection = (EndLoc - StartLoc).GetSafeNormal();
		for (int32 i = 0; i < FaceVerts.Num(); i++)
		{
			// Project vertex position onto the wall to get U coordinate
			FVector ToVertex = FaceVerts[i] - StartLoc;
			float U = FVector::DotProduct(ToVertex, WallDirection) / WallLength;
			float UScaled = UVMin + (U * (UVMax - UVMin));

			// V coordinate is based on height (Z component)
			float V = (FaceVerts[i].Z - StartLoc.Z) / WallHeight;

			FaceUVs.Add(FVector2D(UScaled, 1.0f - V)); // Flip V to match texture orientation
		}

		// Calculate normal from first triangle (2,1,0) using winding order - matches runtime
		FVector StartDiagNormal = FVector::CrossProduct((FaceVerts[1] - FaceVerts[2]), (FaceVerts[0] - FaceVerts[2])).GetSafeNormal();
		AddFace(FaceVerts, FaceTris, FaceUVs, StartDiagNormal);
	}

	// End Diagonal Cap - 4 vertices, 2 triangles
	{
		TArray<FVector> FaceVerts;
		FaceVerts.Add(RightPointEndUp);        // 0 - RPEU
		FaceVerts.Add(BaseboardTopCenterNegY); // 1 - BTCNY
		FaceVerts.Add(LeftPointEndUp);         // 2 - LPEU
		FaceVerts.Add(BaseboardTopCenterPosY); // 3 - BTCPY

		TArray<int32> FaceTris;
		FaceTris.Add(2); FaceTris.Add(1); FaceTris.Add(0); // Reversed winding
		FaceTris.Add(0); FaceTris.Add(1); FaceTris.Add(3);

		// Calculate UVs by projecting vertices onto the wall plane (matches WallComponent)
		TArray<FVector2D> FaceUVs;
		FVector WallDirection = (EndLoc - StartLoc).GetSafeNormal();
		for (int32 i = 0; i < FaceVerts.Num(); i++)
		{
			// Project vertex position onto the wall to get U coordinate
			FVector ToVertex = FaceVerts[i] - StartLoc;
			float U = FVector::DotProduct(ToVertex, WallDirection) / WallLength;
			float UScaled = UVMin + (U * (UVMax - UVMin));

			// V coordinate is based on height (Z component)
			float V = (FaceVerts[i].Z - StartLoc.Z) / WallHeight;

			FaceUVs.Add(FVector2D(UScaled, 1.0f - V)); // Flip V to match texture orientation
		}

		// Calculate normal from first triangle (2,1,0) using winding order - matches runtime
		FVector EndDiagNormal = FVector::CrossProduct((FaceVerts[1] - FaceVerts[2]), (FaceVerts[0] - FaceVerts[2])).GetSafeNormal();
		AddFace(FaceVerts, FaceTris, FaceUVs, EndDiagNormal);
	}
}

UStaticMesh* UWallMeshExporter::CreateStaticMeshAsset(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const TArray<FVector2D>& UVs,
	const TArray<FVector>& Tangents,
	const FString& PackageName,
	const FString& AssetName)
{
	// Create package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("WallMeshExporter: Failed to create package %s"), *PackageName);
		return nullptr;
	}

	// Create static mesh object
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!StaticMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("WallMeshExporter: Failed to create static mesh object"));
		return nullptr;
	}

	// Initialize mesh description
	FMeshDescription MeshDescription;
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();

	// Get attribute accessors
	TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	// Set number of UV channels
	VertexInstanceUVs.SetNumChannels(1);

	// Create polygon group
	FPolygonGroupID PolygonGroupID = MeshDescription.CreatePolygonGroup();
	PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = FName(TEXT("WallMaterial"));

	// Reserve space
	MeshDescription.ReserveNewVertices(Vertices.Num());
	MeshDescription.ReserveNewVertexInstances(Vertices.Num());
	MeshDescription.ReserveNewPolygons(Triangles.Num() / 3);
	MeshDescription.ReserveNewEdges(Triangles.Num());

	// Create vertices
	TArray<FVertexID> VertexIDs;
	VertexIDs.Reserve(Vertices.Num());
	for (const FVector& Vertex : Vertices)
	{
		FVertexID VertexID = MeshDescription.CreateVertex();
		VertexPositions[VertexID] = FVector3f(Vertex);
		VertexIDs.Add(VertexID);
	}

	// Create triangles
	for (int32 i = 0; i < Triangles.Num(); i += 3)
	{
		// Get vertex indices for this triangle
		int32 Index0 = Triangles[i];
		int32 Index1 = Triangles[i + 1];
		int32 Index2 = Triangles[i + 2];

		// Validate indices
		if (!VertexIDs.IsValidIndex(Index0) || !VertexIDs.IsValidIndex(Index1) || !VertexIDs.IsValidIndex(Index2))
		{
			UE_LOG(LogTemp, Warning, TEXT("WallMeshExporter: Invalid triangle indices at position %d"), i);
			continue;
		}

		// Create vertex instances for this triangle
		TArray<FVertexInstanceID> VertexInstanceIDs;
		VertexInstanceIDs.SetNum(3);

		for (int32 Corner = 0; Corner < 3; Corner++)
		{
			int32 VertexIndex = Triangles[i + Corner];
			FVertexID VertexID = VertexIDs[VertexIndex];

			FVertexInstanceID VertexInstanceID = MeshDescription.CreateVertexInstance(VertexID);
			VertexInstanceIDs[Corner] = VertexInstanceID;

			// Set vertex instance attributes
			if (Normals.IsValidIndex(VertexIndex))
			{
				VertexInstanceNormals[VertexInstanceID] = FVector3f(Normals[VertexIndex]);
			}

			if (Tangents.IsValidIndex(VertexIndex))
			{
				VertexInstanceTangents[VertexInstanceID] = FVector3f(Tangents[VertexIndex]);
			}

			VertexInstanceBinormalSigns[VertexInstanceID] = 1.0f;
			VertexInstanceColors[VertexInstanceID] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);

			if (UVs.IsValidIndex(VertexIndex))
			{
				VertexInstanceUVs.Set(VertexInstanceID, 0, FVector2f(UVs[VertexIndex]));
			}
		}

		// Create the triangle
		MeshDescription.CreateTriangle(PolygonGroupID, VertexInstanceIDs);
	}

	// Build static mesh from mesh description
	TArray<const FMeshDescription*> MeshDescriptions;
	MeshDescriptions.Add(&MeshDescription);

	StaticMesh->BuildFromMeshDescriptions(MeshDescriptions);

	// Add a default material slot
	StaticMesh->GetStaticMaterials().Add(FStaticMaterial());

	// Build the static mesh
	StaticMesh->Build();
	StaticMesh->PostEditChange();

	// Mark package as dirty
	Package->MarkPackageDirty();

	// Save the package
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;

	bool bSaved = UPackage::SavePackage(Package, StaticMesh, *PackageFileName, SaveArgs);

	if (!bSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("WallMeshExporter: Failed to save package %s"), *PackageFileName);
		return nullptr;
	}

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(StaticMesh);

	return StaticMesh;
}

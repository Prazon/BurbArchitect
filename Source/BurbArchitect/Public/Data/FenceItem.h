// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/ArchitectureItem.h"
#include "FenceItem.generated.h"

// Forward declarations
class UStaticMesh;

/**
 * Catalog item representing a fence with configurable panels and posts
 * Extends ArchitectureItem to include fence-specific properties
 * Fences are decorative only (no room detection) - similar to half walls
 */
UCLASS()
class BURBARCHITECT_API UFenceItem : public UArchitectureItem
{
	GENERATED_BODY()

public:
	UFenceItem();

	// ==================== MESH ASSETS ====================

	/** Static mesh for continuous fence panels (repeating sections) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Meshes")
	TSoftObjectPtr<UStaticMesh> FencePanelMesh;

	/** Static mesh for fence posts (corners, ends, junctions) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Meshes")
	TSoftObjectPtr<UStaticMesh> FencePostMesh;

	// ==================== POST PLACEMENT ====================

	/** Spacing between intermediate fence posts in grid tiles
	 *  0 = no intermediate posts (only corners/ends)
	 *  1 = post every tile
	 *  2 = post every 2 tiles
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Post Placement",
			  meta = (ClampMin = "0", ClampMax = "10"))
	int32 PostSpacing;

	/** Always place posts at fence corners */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Post Placement")
	bool bPostsAtCorners;

	/** Always place posts at fence ends */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Post Placement")
	bool bPostsAtEnds;

	/** Place posts at fence junctions (T-intersections, crosses) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Post Placement")
	bool bPostsAtJunctions;

	// ==================== DIMENSIONS ====================

	/** Height of fence in units (default: 200) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Dimensions",
			  meta = (ClampMin = "50.0", ClampMax = "500.0"))
	float FenceHeight;

	/** Width of one fence panel mesh in units (for proper spacing) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fence|Dimensions",
			  meta = (ClampMin = "10.0"))
	float PanelWidth;
};

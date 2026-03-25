// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Centralized debug visualization system for BurbArchitect plugin
 *
 * Provides console commands to toggle debug drawing for various systems.
 * This prevents performance issues from always-on debug visualization.
 *
 * Console Commands:
 * - burb.debug.walls       - Toggle wall graph node/edge visualization
 * - burb.debug.rooms       - Toggle room boundary/fill visualization
 * - burb.debug.tiles       - Toggle tile section polygon testing visualization
 * - burb.debug.labels      - Toggle room ID text labels
 * - burb.debug.all         - Toggle all debug visualization
 *
 * Usage in code:
 *     if (BurbArchitectDebug::IsWallDebugEnabled())
 *     {
 *         DrawDebugSphere(...);
 *     }
 */
namespace BurbArchitectDebug
{
	/** Check if wall graph debug visualization is enabled */
	BURBARCHITECT_API bool IsWallDebugEnabled();

	/** Check if room debug visualization is enabled */
	BURBARCHITECT_API bool IsRoomDebugEnabled();

	/** Check if tile debug visualization is enabled */
	BURBARCHITECT_API bool IsTileDebugEnabled();

	/** Check if label debug visualization is enabled */
	BURBARCHITECT_API bool IsLabelDebugEnabled();

	/** Check if ANY debug visualization is enabled */
	BURBARCHITECT_API bool IsAnyDebugEnabled();

	/** Set all debug visualization flags */
	BURBARCHITECT_API void SetAllDebugEnabled(bool bEnabled);
}

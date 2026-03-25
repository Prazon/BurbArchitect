// Copyright Epic Games, Inc. All Rights Reserved.

#include "BurbArchitectDebug.h"
#include "HAL/IConsoleManager.h"

// ========================================
// Console Variables
// ========================================

// Wall graph visualization (nodes, edges, phantom edge detection)
static TAutoConsoleVariable<bool> CVarDebugWalls(
	TEXT("burb.debug.walls"),
	false,
	TEXT("Toggle wall graph debug visualization (nodes, edges, connection counts)\n")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Enabled - draws spheres at nodes colored by connection count"),
	ECVF_Default
);

// Room boundary and interior visualization
static TAutoConsoleVariable<bool> CVarDebugRooms(
	TEXT("burb.debug.rooms"),
	false,
	TEXT("Toggle room boundary and interior debug visualization\n")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Enabled - draws boundary edges, centroids, and interior tiles"),
	ECVF_Default
);

// Tile section polygon testing visualization
static TAutoConsoleVariable<bool> CVarDebugTiles(
	TEXT("burb.debug.tiles"),
	false,
	TEXT("Toggle tile section polygon testing visualization\n")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Enabled - draws spheres for tile quadrant testing (VERY EXPENSIVE)"),
	ECVF_Default
);

// Room ID text labels
static TAutoConsoleVariable<bool> CVarDebugLabels(
	TEXT("burb.debug.labels"),
	false,
	TEXT("Toggle room ID text label visualization\n")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Enabled - draws room IDs above tiles"),
	ECVF_Default
);

// ========================================
// Console Command: Toggle All
// ========================================

static FAutoConsoleCommand CCmdDebugAll(
	TEXT("burb.debug.all"),
	TEXT("Toggle all debug visualization on/off\n")
	TEXT("Usage: burb.debug.all [0|1]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() > 0)
		{
			bool bEnable = FCString::Atoi(*Args[0]) != 0;
			BurbArchitectDebug::SetAllDebugEnabled(bEnable);
			UE_LOG(LogTemp, Log, TEXT("BurbArchitect Debug: All visualization %s"),
				bEnable ? TEXT("ENABLED") : TEXT("DISABLED"));
		}
		else
		{
			// Toggle current state
			bool bCurrentState = BurbArchitectDebug::IsAnyDebugEnabled();
			BurbArchitectDebug::SetAllDebugEnabled(!bCurrentState);
			UE_LOG(LogTemp, Log, TEXT("BurbArchitect Debug: All visualization %s"),
				!bCurrentState ? TEXT("ENABLED") : TEXT("DISABLED"));
		}
	})
);

// ========================================
// Namespace Implementation
// ========================================

namespace BurbArchitectDebug
{
	bool IsWallDebugEnabled()
	{
		return CVarDebugWalls.GetValueOnAnyThread();
	}

	bool IsRoomDebugEnabled()
	{
		return CVarDebugRooms.GetValueOnAnyThread();
	}

	bool IsTileDebugEnabled()
	{
		return CVarDebugTiles.GetValueOnAnyThread();
	}

	bool IsLabelDebugEnabled()
	{
		return CVarDebugLabels.GetValueOnAnyThread();
	}

	bool IsAnyDebugEnabled()
	{
		return IsWallDebugEnabled() || IsRoomDebugEnabled() || IsTileDebugEnabled() || IsLabelDebugEnabled();
	}

	void SetAllDebugEnabled(bool bEnabled)
	{
		CVarDebugWalls->Set(bEnabled, ECVF_SetByCode);
		CVarDebugRooms->Set(bEnabled, ECVF_SetByCode);
		CVarDebugTiles->Set(bEnabled, ECVF_SetByCode);
		CVarDebugLabels->Set(bEnabled, ECVF_SetByCode);
	}
}

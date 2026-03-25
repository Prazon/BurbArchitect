// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/RoomDebugCanvas.h"
#include "Slate/SRoomDebugCanvas.h"
#include "Actors/LotManager.h"

TSharedRef<SWidget> URoomDebugCanvas::RebuildWidget()
{
	SlateCanvas = SNew(SRoomDebugCanvas)
		.LotManager(CachedLotManager)
		.CurrentLevel(CachedCurrentLevel);

	// Apply cached show room numbers setting
	if (SlateCanvas.IsValid())
	{
		SlateCanvas->SetShowRoomNumbers(bCachedShowRoomNumbers);
	}

	return SlateCanvas.ToSharedRef();
}

void URoomDebugCanvas::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SlateCanvas.Reset();
}

void URoomDebugCanvas::SetLotManager(ALotManager* InLotManager)
{
	CachedLotManager = InLotManager;

	if (SlateCanvas.IsValid())
	{
		SlateCanvas->SetLotManager(InLotManager);
	}
}

void URoomDebugCanvas::SetCurrentLevel(int32 InLevel)
{
	CachedCurrentLevel = InLevel;

	if (SlateCanvas.IsValid())
	{
		SlateCanvas->SetCurrentLevel(InLevel);
	}
}

int32 URoomDebugCanvas::GetCurrentLevel() const
{
	if (SlateCanvas.IsValid())
	{
		return SlateCanvas->GetCurrentLevel();
	}
	return CachedCurrentLevel;
}

void URoomDebugCanvas::SetShowRoomNumbers(bool bShow)
{
	bCachedShowRoomNumbers = bShow;

	if (SlateCanvas.IsValid())
	{
		SlateCanvas->SetShowRoomNumbers(bShow);
	}
}

bool URoomDebugCanvas::GetShowRoomNumbers() const
{
	if (SlateCanvas.IsValid())
	{
		return SlateCanvas->GetShowRoomNumbers();
	}
	return bCachedShowRoomNumbers;
}

void URoomDebugCanvas::Refresh()
{
	if (SlateCanvas.IsValid())
	{
		SlateCanvas->Refresh();
	}
}

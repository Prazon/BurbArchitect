// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/RoomDebugWidget.h"
#include "Slate/SRoomDebugWidget.h"
#include "Actors/LotManager.h"

TSharedRef<SWidget> URoomDebugWidget::RebuildWidget()
{
	SlateWidget = SNew(SRoomDebugWidget)
		.LotManager(CachedLotManager)
		.CurrentLevel(CachedCurrentLevel);

	return SlateWidget.ToSharedRef();
}

void URoomDebugWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SlateWidget.Reset();
}

void URoomDebugWidget::SetLotManager(ALotManager* InLotManager)
{
	CachedLotManager = InLotManager;

	if (SlateWidget.IsValid())
	{
		SlateWidget->SetLotManager(InLotManager);
	}
}

void URoomDebugWidget::SetCurrentLevel(int32 InLevel)
{
	CachedCurrentLevel = InLevel;

	if (SlateWidget.IsValid())
	{
		SlateWidget->SetCurrentLevel(InLevel);
	}
}

void URoomDebugWidget::Refresh()
{
	if (SlateWidget.IsValid())
	{
		SlateWidget->Refresh();
	}
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/GateItem.h"

UGateItem::UGateItem()
{
	// Gates typically smaller/narrower than doors, adjusted for fence height
	PortalSize = FVector2D(100.0, 150.0);  // 100cm wide x 150cm tall (matches typical fence height)
	PortalOffset = FVector2D(0.0, 0.0);     // No offset by default
	HorizontalSnap = 50.0f;                 // Snap to 50cm increments along fence
	VerticalSnap = 25.0f;                   // Snap to 25cm increments vertically
	bSnapsToFloor = true;                   // Gates snap to ground level
}

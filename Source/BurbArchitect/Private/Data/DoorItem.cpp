// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/DoorItem.h"

UDoorItem::UDoorItem()
{
	// Set sensible defaults for standard door
	PortalSize = FVector2D(100.0, 200.0);  // 100cm wide x 200cm tall (standard door)
	PortalOffset = FVector2D(0.0, 0.0);     // No offset by default
	HorizontalSnap = 50.0f;                 // Snap to 50cm increments along wall
	VerticalSnap = 25.0f;                   // Snap to 25cm increments vertically
	bSnapsToFloor = true;                   // Doors typically snap to floor level
}

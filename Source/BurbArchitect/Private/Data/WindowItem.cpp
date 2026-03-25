// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/WindowItem.h"

UWindowItem::UWindowItem()
{
	// Set sensible defaults for standard window
	PortalSize = FVector2D(120.0, 150.0);  // 120cm wide x 150cm tall (standard window)
	PortalOffset = FVector2D(0.0, 0.0);     // No offset by default
	HorizontalSnap = 50.0f;                 // Snap to 50cm increments along wall
	VerticalSnap = 25.0f;                   // Snap to 25cm increments vertically
	bSnapsToFloor = false;                  // Windows don't snap to floor (usually higher up)
}

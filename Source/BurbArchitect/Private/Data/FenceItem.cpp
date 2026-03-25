// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FenceItem.h"

UFenceItem::UFenceItem()
{
	// Default fence configuration
	PostSpacing = 2;           // Post every 2 tiles
	bPostsAtCorners = true;    // Posts at corners
	bPostsAtEnds = true;       // Posts at ends
	bPostsAtJunctions = true;  // Posts at junctions
	FenceHeight = 200.0f;      // 200 units tall (taller than half wall, shorter than full wall)
	PanelWidth = 100.0f;       // 100 units wide per panel
}

// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/HipRoofComponent.h"

UHipRoofComponent::UHipRoofComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FRoofSegmentData UHipRoofComponent::GenerateRoofMeshSection(FRoofSegmentData InRoofData)
{
	// Ensure roof type is set correctly
	InRoofData.Dimensions.RoofType = ERoofType::Hip;

	// Call the base class hip roof generation
	return GenerateHipRoofMesh(InRoofData);
}

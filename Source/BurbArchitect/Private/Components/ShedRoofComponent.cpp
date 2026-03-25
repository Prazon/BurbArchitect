// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/ShedRoofComponent.h"

UShedRoofComponent::UShedRoofComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FRoofSegmentData UShedRoofComponent::GenerateRoofMeshSection(FRoofSegmentData InRoofData)
{
	// Ensure roof type is set correctly
	InRoofData.Dimensions.RoofType = ERoofType::Shed;

	// Call the base class shed roof generation
	return GenerateShedRoofMesh(InRoofData);
}

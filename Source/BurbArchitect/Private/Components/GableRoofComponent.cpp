// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/GableRoofComponent.h"

UGableRoofComponent::UGableRoofComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FRoofSegmentData UGableRoofComponent::GenerateRoofMeshSection(FRoofSegmentData InRoofData)
{
	// Ensure roof type is set correctly
	InRoofData.Dimensions.RoofType = ERoofType::Gable;

	// Call the base class gable roof generation
	return GenerateGableRoofMesh(InRoofData);
}

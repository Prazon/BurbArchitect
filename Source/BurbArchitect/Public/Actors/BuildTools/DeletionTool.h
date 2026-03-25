// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Components/WallComponent.h"
#include "Components/FloorComponent.h"
#include "Components/RoofComponent.h"
#include "Actors/StairsBase.h"
#include "DeletionTool.generated.h"

// Component type enumeration for universal element detection
UENUM(BlueprintType)
enum class EDeletionComponentType : uint8
{
	UnknownComponent = 0 UMETA(DisplayName = "Unknown Component"),
	WallComponent = 1 UMETA(DisplayName = "Wall Component"),
	FloorComponent = 2 UMETA(DisplayName = "Floor Component"),
	StairsComponent = 3 UMETA(DisplayName = "Stairs Component"),
	RoofComponent = 4 UMETA(DisplayName = "Roof Component"),
	PortalComponent = 5 UMETA(DisplayName = "Portal Component")
};

// Data structure for storing detected component information
USTRUCT(BlueprintType)
struct FDeletionComponentData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EDeletionComponentType ComponentType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ArrayIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location;

	// Transient reference to the specific roof component (for multiple roof components support)
	UPROPERTY(Transient)
	URoofComponent* HitRoofComponent = nullptr;

	// Transient reference to the specific stairs actor
	UPROPERTY(Transient)
	AStairsBase* HitStairsActor = nullptr;

	// Transient reference to the specific portal actor (door/window)
	UPROPERTY(Transient)
	class APortalBase* HitPortalActor = nullptr;

	FDeletionComponentData()
		: ComponentType(EDeletionComponentType::UnknownComponent)
		, ArrayIndex(-1)
		, Location(FVector::ZeroVector)
		, HitRoofComponent(nullptr)
		, HitStairsActor(nullptr)
		, HitPortalActor(nullptr)
	{}

	// Equality operator for comparison
	bool operator==(const FDeletionComponentData& Other) const
	{
		return ComponentType == Other.ComponentType
			&& ArrayIndex == Other.ArrayIndex
			&& HitRoofComponent == Other.HitRoofComponent
			&& HitStairsActor == Other.HitStairsActor
			&& HitPortalActor == Other.HitPortalActor;
	}
};

/**
 * ADeletionTool: Universal deletion tool that can delete walls, floors, roofs, and stairs
 * Uses BuildServer for proper undo/redo support and batch deletion capabilities
 */
UCLASS()
class BURBARCHITECT_API ADeletionTool : public ABuildTool
{
	GENERATED_BODY()

public:
	ADeletionTool();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// Base tool overrides
	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void Release_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;

	// Server RPC variants
	UFUNCTION(Server, Reliable)
	void ServerClick_Deletion(const FDeletionComponentData& ComponentData);

	UFUNCTION(Server, Reliable)
	void ServerDrag_Deletion(FVector DragStart, int32 Level);

	UFUNCTION(Server, Reliable)
	void ServerRelease_Deletion(const TArray<FDeletionComponentData>& InComponentsToDelete);

	// Broadcast RPC variants
	UFUNCTION(NetMulticast, Reliable)
	void BroadcastClick_Deletion(const FDeletionComponentData& ComponentData);

	UFUNCTION(NetMulticast, Reliable)
	void BroadcastDrag_Deletion(FVector DragStart, int32 Level);

	UFUNCTION(NetMulticast, Reliable)
	void BroadcastRelease_Deletion(const TArray<FDeletionComponentData>& InComponentsToDelete);

	// Helper methods

	/** Detect component at the given hit result location */
	UFUNCTION(BlueprintCallable, Category = "Deletion Tool")
	void DetectComponentAtLocation(const FHitResult& HitResult, FDeletionComponentData& OutComponentData);

	/** Highlight or unhighlight a component for visual feedback */
	UFUNCTION(BlueprintCallable, Category = "Deletion Tool")
	void HighlightComponent(const FDeletionComponentData& ComponentData, bool bHighlight);

	/** Delete a single component via BuildServer */
	UFUNCTION(BlueprintCallable, Category = "Deletion Tool")
	void DeleteComponent(const FDeletionComponentData& ComponentData);

	/** Gather all components within the drag selection area */
	UFUNCTION(BlueprintCallable, Category = "Deletion Tool")
	void GatherComponentsInArea(FVector StartLocation, FVector EndLocation, int32 Level);

	/** Clear all highlighted components */
	UFUNCTION(BlueprintCallable, Category = "Deletion Tool")
	void ClearHighlights();

	// Blueprint events

	UFUNCTION(BlueprintNativeEvent, Category = "Deletion Tool")
	void OnComponentHovered(const FDeletionComponentData& ComponentData);

	UFUNCTION(BlueprintNativeEvent, Category = "Deletion Tool")
	void OnComponentDeleted(const FDeletionComponentData& ComponentData);

	UFUNCTION(BlueprintNativeEvent, Category = "Deletion Tool")
	void OnBatchDeletionStarted(int32 ComponentCount);

	UFUNCTION(BlueprintNativeEvent, Category = "Deletion Tool")
	void OnBatchDeletionCompleted(int32 ComponentCount);

protected:
	// Currently hovered component
	UPROPERTY(BlueprintReadWrite, Category = "Deletion Tool")
	FDeletionComponentData HoveredComponent;

	// Store original material for unhighlighting
	UPROPERTY(Transient)
	UMaterialInterface* HoveredOriginalMaterial = nullptr;

	// Drag-based batch deletion state
	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Deletion Tool")
	bool bIsDragging = false;

	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Deletion Tool")
	FVector DragStartLocation;

	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Deletion Tool")
	int32 DragLevel = 0;

	// Components gathered during drag for batch deletion
	// Note: Transient pointers (HitRoofComponent, HitStairsActor, HitPortalActor) in FDeletionComponentData
	// will not replicate - only ComponentType, ArrayIndex, and Location are synced
	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Deletion Tool")
	TArray<FDeletionComponentData> ComponentsToDelete;

	// Map to store original materials for all highlighted components
	UPROPERTY(Transient)
	TMap<int32, UMaterialInterface*> HighlightedMaterials;

	// Counter for generating unique highlight keys
	int32 HighlightKeyCounter = 0;

	/** Helper to generate unique key for highlight map */
	int32 GenerateHighlightKey(const FDeletionComponentData& ComponentData) const;

	/** Helper to restore material for a component */
	void RestoreMaterial(const FDeletionComponentData& ComponentData, UMaterialInterface* OriginalMaterial);
};

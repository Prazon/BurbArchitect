// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/PlayerController.h"
#include "BurbPlayerController.generated.h"

class ABurbPawn;
class ABuildTool;
class UCatalogItem;
class UFloorPattern;
class UWallPattern;
class ABuildFloorTool;
class AWallPatternTool;
class UArchitectureItem;

UENUM(BlueprintType)
enum ETileType : uint8
{
	Wall,
	Floor,
	Object,
};
/**
 * 
 */
UCLASS(Blueprintable)
class ABurbPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:
	// Called when the game starts
	virtual void BeginPlay() override;	

public:
	ABurbPlayerController();

	UPROPERTY(BlueprintReadOnly)
	FVector CursorWorldLocation;
	
	UPROPERTY(BlueprintReadOnly)
	FHitResult CursorWorldHitResult;

	UPROPERTY(BlueprintReadOnly)
	int32 LastTracedLevel = 0;

	UPROPERTY(BlueprintReadWrite)
	bool bSelectPressed;
	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	FHitResult CustomGetHitResultUnderCursor(UObject* WorldContextObject, const FVector2D& ScreenPosition, float TraceDistance, bool bTraceComplex, ECollisionChannel TraceChannel);

	UPROPERTY(BlueprintReadWrite)
	ABurbPawn* CurrentPawn;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Cursor Optimization")
	float MouseMovementThreshold = 1.0f;

	// Calculate cursor position on a horizontal plane at given Z height
	// Uses camera ray intersection with plane for accurate positioning at all angles
	UFUNCTION(BlueprintCallable)
	bool CalculateCursorPositionAtHeight(float ZHeight, FVector& OutPosition);

	/**
	 * Broadcast delete request to all selected actors implementing IDeletable interface
	 * This checks all actors in the world that implement IDeletable, asks if they're selected,
	 * and requests deletion on the selected ones
	 * @return Number of actors deleted
	 */
	UFUNCTION(BlueprintCallable, Category = "Deletion")
	int32 BroadcastDeleteToSelected();

	// ========================================
	// Catalog Item Activation
	// ========================================

	/**
	 * Handle catalog item activation from UI
	 * Detects item type (Floor Pattern, Wall Pattern, Architecture Item, etc.)
	 * and spawns appropriate tool with pattern assignment
	 * @param Item The catalog item to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void HandleCatalogItemActivation(UCatalogItem* Item);

	/**
	 * Server RPC to create a build tool without pattern assignment
	 * Used for generic architecture items (walls, rooms, stairs, etc.)
	 * @param BuildToolClass The soft class pointer to the build tool to spawn
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Build Tools")
	void ServerTryCreateBuildTool(const TSoftClassPtr<ABuildTool>& BuildToolClass);
	virtual void ServerTryCreateBuildTool_Implementation(const TSoftClassPtr<ABuildTool>& BuildToolClass);

	/**
	 * Server RPC to create a build tool with pattern assignment
	 * Used for floor patterns and wall patterns
	 * @param BuildToolClass The soft class pointer to the build tool to spawn
	 * @param PatternItem The pattern item (UFloorPattern or UWallPattern) to assign to the tool
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Build Tools")
	void ServerTryCreateBuildToolWithPattern(const TSoftClassPtr<ABuildTool>& BuildToolClass, UCatalogItem* PatternItem);
	virtual void ServerTryCreateBuildToolWithPattern_Implementation(const TSoftClassPtr<ABuildTool>& BuildToolClass, UCatalogItem* PatternItem);

	// ========================================
	// Save/Load Operations
	// ========================================

	/**
	 * Quick save the current lot to the QuickSave slot
	 * Uses BuildServer for undo/redo support
	 * @return True if save was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Save/Load")
	bool QuickSave();

	/**
	 * Quick load the lot from the QuickSave slot
	 * Uses BuildServer for undo/redo support
	 * @return True if load was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Save/Load")
	bool QuickLoad();

	/**
	 * Save the current lot to a named slot
	 * @param SlotName The name of the save slot (e.g., "Slot1", "Autosave")
	 * @return True if save was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Save/Load")
	bool SaveLotToSlot(const FString& SlotName);

	/**
	 * Load a lot from a named slot
	 * @param SlotName The name of the save slot to load from
	 * @return True if load was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Save/Load")
	bool LoadLotFromSlot(const FString& SlotName);

	/**
	 * Export the current lot to a JSON file
	 * @param FilePath Full path to the JSON file (e.g., "C:/MyLots/MyLot.json")
	 * @return True if export was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Save/Load")
	bool ExportLotToJSON(const FString& FilePath);

	/**
	 * Import a lot from a JSON file
	 * @param FilePath Full path to the JSON file to import
	 * @return True if import was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Save/Load")
	bool ImportLotFromJSON(const FString& FilePath);

private:
	// Cache for optimizing cursor traces
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	bool bFirstTick = true;

};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/LotSerialization.h"
#include "LotSerializationSubsystem.generated.h"

class ALotManager;
class UWallGraphComponent;
class UFloorComponent;
class URoofComponent;
class UStairsComponent;
class UTerrainComponent;
class UWallPattern;
class UFloorPattern;

/**
 * Subsystem for serializing and deserializing lot data
 * Provides centralized save/load logic with version management and validation
 *
 * Usage:
 * - Get subsystem: ULotSerializationSubsystem* Subsystem = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();
 * - Serialize: FSerializedLotData Data = Subsystem->SerializeLot(LotManager);
 * - Deserialize: Subsystem->DeserializeLot(LotManager, Data);
 */
UCLASS()
class BURBARCHITECT_API ULotSerializationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	// ========================================
	// High-Level Serialization
	// ========================================

	/**
	 * Serializes a complete lot into FSerializedLotData
	 * @param LotManager The lot to serialize
	 * @return Serialized lot data
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Serialization")
	FSerializedLotData SerializeLot(ALotManager* LotManager);

	/**
	 * Deserializes lot data and applies it to a LotManager
	 * @param LotManager The lot to populate
	 * @param Data The serialized data to load
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Serialization")
	bool DeserializeLot(ALotManager* LotManager, const FSerializedLotData& Data);

	// ========================================
	// Component-Level Serialization
	// ========================================

	/**
	 * Serializes wall graph data
	 */
	void SerializeWallGraph(UWallGraphComponent* WallGraph, FSerializedLotData& OutData);

	/**
	 * Deserializes wall graph data
	 */
	bool DeserializeWallGraph(UWallGraphComponent* WallGraph, ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes floor component data
	 */
	void SerializeFloorComponent(UFloorComponent* FloorComp, FSerializedLotData& OutData);

	/**
	 * Deserializes floor component data
	 */
	bool DeserializeFloorComponent(UFloorComponent* FloorComp, ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes roof component data
	 */
	void SerializeRoofComponent(URoofComponent* RoofComp, FSerializedLotData& OutData);

	/**
	 * Deserializes roof component data
	 */
	bool DeserializeRoofComponent(URoofComponent* RoofComp, ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes stairs component data
	 */
	void SerializeStairsComponent(UStairsComponent* StairsComp, FSerializedLotData& OutData);

	/**
	 * Deserializes stairs component data
	 */
	bool DeserializeStairsComponent(UStairsComponent* StairsComp, ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes terrain component data
	 */
	void SerializeTerrainComponent(UTerrainComponent* TerrainComp, FSerializedLotData& OutData);

	/**
	 * Deserializes terrain component data
	 */
	bool DeserializeTerrainComponent(UTerrainComponent* TerrainComp, ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes water component data (pools)
	 */
	void SerializeWaterComponent(class UWaterComponent* WaterComp, FSerializedLotData& OutData);

	/**
	 * Deserializes water component data (pools)
	 */
	bool DeserializeWaterComponent(class UWaterComponent* WaterComp, ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes fence component data
	 */
	void SerializeFenceComponent(class UFenceComponent* FenceComp, FSerializedLotData& OutData);

	/**
	 * Deserializes fence component data
	 */
	bool DeserializeFenceComponent(class UFenceComponent* FenceComp, ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes grid configuration
	 */
	void SerializeGridConfig(ALotManager* LotManager, FSerializedLotData& OutData);

	/**
	 * Serializes portals (doors/windows)
	 */
	void SerializePortals(ALotManager* LotManager, FSerializedLotData& OutData);

	/**
	 * Deserializes portals (doors/windows)
	 */
	bool DeserializePortals(ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes placed objects (furniture, decorations)
	 */
	void SerializePlacedObjects(ALotManager* LotManager, FSerializedLotData& OutData);

	/**
	 * Deserializes placed objects (furniture, decorations)
	 */
	bool DeserializePlacedObjects(ALotManager* LotManager, const FSerializedLotData& Data);

	/**
	 * Serializes room IDs
	 */
	void SerializeRoomIDs(ALotManager* LotManager, FSerializedLotData& OutData);

	/**
	 * Deserializes room IDs
	 */
	bool DeserializeRoomIDs(ALotManager* LotManager, const FSerializedLotData& Data);

	// ========================================
	// Validation and Versioning
	// ========================================

	/**
	 * Validates that lot data is complete and correct
	 * @param Data The data to validate
	 * @return True if valid
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Serialization")
	bool ValidateLotData(const FSerializedLotData& Data) const;

	// ========================================
	// File I/O Helpers
	// ========================================

	/**
	 * Exports lot data to a JSON file
	 * @param Data The data to export
	 * @param FilePath The file path to write to
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Serialization")
	bool ExportToJSON(const FSerializedLotData& Data, const FString& FilePath);

	/**
	 * Imports lot data from a JSON file
	 * @param FilePath The file path to read from
	 * @param OutData The loaded data
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Lot Serialization")
	bool ImportFromJSON(const FString& FilePath, FSerializedLotData& OutData);

	// ========================================
	// Helper Functions
	// ========================================

	/**
	 * Resolves a material path to a material instance
	 * @param MaterialPath The path string (e.g., "/Game/Materials/M_Wall.M_Wall")
	 * @return The loaded material, or nullptr if not found
	 */
	UMaterialInterface* ResolveMaterialPath(const FString& MaterialPath);

	/**
	 * Gets the asset path for a material
	 * @param Material The material to get the path for
	 * @return The path string, or empty string if invalid
	 */
	FString GetMaterialPath(UMaterialInterface* Material);

	/**
	 * Resolves a class path to a UClass
	 * @param ClassPath The path string (e.g., "/Game/Blueprints/BP_Door.BP_Door_C")
	 * @return The loaded class, or nullptr if not found
	 */
	UClass* ResolveClassPath(const FString& ClassPath);

	/**
	 * Gets the asset path for a class
	 * @param Class The class to get the path for
	 * @return The path string, or empty string if invalid
	 */
	FString GetClassPath(UClass* Class);
};

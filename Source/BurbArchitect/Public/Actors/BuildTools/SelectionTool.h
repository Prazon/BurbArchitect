// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/BuildTool.h"
#include "Components/WallComponent.h"
#include "Components/RoofComponent.h"
#include "Components/WidgetComponent.h"
#include "SelectionTool.generated.h"

UENUM(BlueprintType)
enum class ESectionComponentType : uint8
{
	UnknownComponent = 0 UMETA(DisplayName = "Unknown Component"),
	WallComponent = 1 UMETA(DisplayName = "Wall Component"),
	FloorComponent = 2 UMETA(DisplayName = "Floor Component"),
	StairsComponent = 3 UMETA(DisplayName = "Stairs Component"),
	RoofComponent = 4 UMETA(DisplayName = "Roof Component"),
	RoomComponent = 5 UMETA(DisplayName = "Room Component")
};

USTRUCT(BlueprintType)
struct FComponentData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESectionComponentType ComponentType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int SectionIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int ArrayIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location;

	// Transient reference to the specific roof component (for multiple roof components support)
	UPROPERTY(Transient)
	URoofComponent* HitRoofComponent = nullptr;

	// Transient reference to the specific roof actor
	UPROPERTY(Transient)
	class ARoofBase* HitRoofActor = nullptr;

	// Transient reference to the specific stairs actor
	UPROPERTY(Transient)
	class AStairsBase* HitStairsActor = nullptr;

	FComponentData() : ComponentType(ESectionComponentType::UnknownComponent), SectionIndex(-1), ArrayIndex(-1), Location(FVector(0,0,0)), HitRoofComponent(nullptr), HitRoofActor(nullptr), HitStairsActor(nullptr) {}
	FComponentData(UPrimitiveComponent* Component, ESectionComponentType ComponentType, int SectionIndex, int ArrayIndex, FVector HeadLocation)
	: ComponentType(ComponentType), SectionIndex(SectionIndex), ArrayIndex(ArrayIndex), Location(HeadLocation), HitRoofComponent(nullptr), HitRoofActor(nullptr), HitStairsActor(nullptr) {}
};

UCLASS()
class BURBARCHITECT_API ASelectionTool : public ABuildTool
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASelectionTool();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called when the game ends or the actor is destroyed
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void Move_Implementation(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel) override;
	virtual void Click_Implementation() override;
	virtual void Drag_Implementation() override;
	virtual void BroadcastRelease_Implementation() override;
	virtual void RotateLeft_Implementation() override;
	virtual void RotateRight_Implementation() override;
	virtual void ServerDelete_Implementation() override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FComponentData SelectedComponent;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bSnapsToFloor = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bValidPlacementLocation = false;

	// Currently selected room ID (0 = none)
	UPROPERTY(BlueprintReadWrite, Category = "Room Selection")
	int32 SelectedRoomID = 0;

	// Footprint line component for visualizing selected room boundary
	UPROPERTY()
	class ULineBatchComponent* RoomFootprintLineComponent = nullptr;

	// Widget component for room control UI (spawns at room centroid)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room Selection")
	UWidgetComponent* RoomControlWidgetComponent = nullptr;

	// Widget class to use for the room control UI (assign in Blueprint)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Selection")
	TSubclassOf<UUserWidget> RoomControlWidgetClass;

	// Vertical offset above floor for the room control widget
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Selection")
	float RoomControlWidgetZOffset = 100.0f;

	// Cached hit result from last Move call (used for Click detection)
	FHitResult LastCursorHitResult;

	// Current traced level (floor) - stored from Move_Implementation for level filtering
	int32 CurrentTracedLevel = 0;

	// Room selection functions
	UFUNCTION(BlueprintCallable, Category = "Room Selection")
	void SelectRoom(int32 RoomID);

	UFUNCTION(BlueprintCallable, Category = "Room Selection")
	void DeselectRoom();

	UFUNCTION(BlueprintCallable, Category = "Room Selection")
	void DrawRoomFootprintLines(int32 RoomID);

	UFUNCTION(BlueprintCallable, Category = "Room Selection")
	void ClearRoomFootprintLines();

	// Show the room control widget at the specified location
	UFUNCTION(BlueprintCallable, Category = "Room Selection")
	void ShowRoomControlWidget(const FVector& Location);

	// Hide the room control widget
	UFUNCTION(BlueprintCallable, Category = "Room Selection")
	void HideRoomControlWidget();

	// Refresh room selection visuals (call when cutaway mode changes)
	UFUNCTION(BlueprintCallable, Category = "Room Selection")
	void RefreshRoomSelection();

	// Helper to get room from wall click
	int32 GetRoomFromWallClick(const FHitResult& HitResult);

	void SelectObject(FComponentData ComponentData, const FVector& MoveLocation);
	void ReleaseObject(FComponentData ComponentData);

	UFUNCTION(BlueprintNativeEvent, Category = "Using")
	void OnSelectedSection(const FComponentData& ComponentData);

	UFUNCTION(BlueprintNativeEvent, Category = "Using")
	void OnReleasedSection();

	UFUNCTION(BlueprintCallable, Category = "Using")
	void ModifySection(const FComponentData& ComponentData);

	UFUNCTION(BlueprintCallable, Category = "Using")
	void AppearanceSection(const FComponentData& ComponentData);

	UFUNCTION(BlueprintCallable, Category = "Using")
	void DeleteSection(const FComponentData& ComponentData);
};

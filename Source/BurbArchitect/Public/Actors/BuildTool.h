// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Controller/BurbPlayerController.h"
#include "BuildTool.generated.h"

class ABurbPawn;
class ALotManager;
//data used for build tool drag operations from the start click until end release
USTRUCT(BlueprintType)
struct FToolDragOperation
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector StartOperation;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector EndOperation;
};

USTRUCT(BlueprintType)
struct FBuildToolData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText ToolName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UTexture2D> ToolImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* IndicatorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UUserWidget* IndicatorWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftClassPtr<ABuildTool> ToolClass;	
};

// Enum for Tool State
enum class EToolState : uint8
{
	Ts_Placing = 1 UMETA(DisplayName = "Placing"),
	Ts_Dragging = 2 UMETA(DisplayName = "Dragging"),
	Ts_Adjusting = 3 UMETA(DisplayName = "Adjusting"),
	Ts_Completed  = 4 UMETA(DisplayName = "Completed")
};

// Tool State Machine Class
class BURBARCHITECT_API FToolStateMachine
{
public:
	FToolStateMachine() : CurrentState(EToolState::Ts_Placing) {}

	void SetState(EToolState NewState)
	{
		CurrentState = NewState;
	}

	EToolState GetState() const { return CurrentState; }

	// Delegate to notify when the state changes
	DECLARE_EVENT_OneParam(FRoofToolStateMachine, FOnStateChanged, EToolState)

private:
	EToolState CurrentState;
	FOnStateChanged StateChangedEvent;
};

/**
 *  ABuildTool: This class is the base class for all tools, this includes Wall Tool, Door Tool, Floor Tool, etc.
 */
UCLASS(Blueprintable)
class ABuildTool : public AActor
{
	GENERATED_BODY()
public:	
	// Sets default values for this actor's properties
	ABuildTool();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** Control functions **/
	
	//Creates or gets a fresh UBuildCommand-subclass object.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Click();
	
	UFUNCTION(BlueprintCallable, Server, Reliable)
	void ServerClick();
	
	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	void BroadcastClick();
	
	//Updates the location of the tool
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Move(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel);

	UFUNCTION(BlueprintCallable, Server, Reliable)
	void ServerMove(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel);

	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	void BroadcastMove(FVector MoveLocation, bool SelectPressed, FHitResult CursorWorldHitResult, int32 TracedLevel);
	
	//Blueprint hook for successful Move
	UFUNCTION(BlueprintNativeEvent)
	void OnMoved();
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Drag();

	UFUNCTION(BlueprintCallable, Server, Reliable)
	void ServerDrag();
	
	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	void BroadcastDrag();
	
	//Blueprint hook for successful Drag
	UFUNCTION(BlueprintNativeEvent)
	void OnDragged();
	
	//Commits UBuildCommand object, forcing it into an "Undoable" state
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Release();
	
	UFUNCTION(BlueprintCallable, Server, Reliable)
	void ServerRelease();
	
	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	void BroadcastRelease();
	
	UFUNCTION(BlueprintCallable, Server, Reliable)
	void ServerCancel();
	
	//Blueprint hook for successful Release
	UFUNCTION(BlueprintImplementableEvent)
	void OnReleased();

	//Update tool location on the server
	UFUNCTION(Server, Reliable)
	void UpdateLocation(FVector Location);
	
	//Update tool location on the clients
	UFUNCTION(NetMulticast, Reliable)
	void BroadcastLocation(FVector Location);
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void Delete();

	UFUNCTION(BlueprintCallable, Server, Reliable)
	void ServerDelete();

	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	void BroadcastDelete();

	// Rotation functions - virtual so tools can override for custom rotation behavior
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Build Tool")
	void RotateLeft();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Build Tool")
	void RotateRight();

	/** Variables **/
	
	UPROPERTY(BlueprintReadWrite, Replicated)
	FToolDragOperation DragCreateVectors;
	
	UPROPERTY(BlueprintReadWrite, Replicated)
	FToolDragOperation DragScalingVectors;
	
	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bLockToForwardAxis;

	UPROPERTY(BlueprintReadWrite, Replicated, meta=(ExposeOnSpawn))
	ALotManager* CurrentLot;

	UPROPERTY(BlueprintReadWrite, Replicated, meta=(ExposeOnSpawn))
	ABurbPawn* CurrentPlayerPawn;

	UPROPERTY(BlueprintReadWrite, Replicated)
	FVector TargetLocation;

	UPROPERTY(BlueprintReadWrite, Replicated)
	FRotator TargetRotation;	

	UPROPERTY(BlueprintReadWrite, Replicated)
	FVector PreviousLocation;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	bool bDeletionMode = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	bool bRequireCost = false;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Replicated)
	float Price = 10;

	// Shift modifier - set from Blueprint based on input (can be used by any build tool)
	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Build Tool Input")
	bool bShiftPressed = false;

	// Trace channel used by the player controller when this tool is active
	// Default: ECC_GameTraceChannel3 (Grid/Tile) for most tools
	// Portal tools should use ECC_GameTraceChannel1 (Wall)
	// Terrain tools should use ECC_GameTraceChannel5 (Terrain)
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Build Tool")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_GameTraceChannel3;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	USoundBase* MoveSound;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	USoundBase* CreateSound;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	USoundBase* DeleteSound;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	USoundBase* FailSound;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UMaterialInstance* ValidMaterial;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UMaterialInstance* InvalidMaterial;
	
	FToolStateMachine ToolState;
};
// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/BurbPlayerController.h"

#include "Actors/BuildTool.h"
#include "Actors/BurbPawn.h"
#include "Actors/LotManager.h"
#include "Actors/BuildTools/BuildFloorTool.h"
#include "Actors/BuildTools/WallPatternTool.h"
#include "Data/CatalogItem.h"
#include "Data/FloorPattern.h"
#include "Data/WallPattern.h"
#include "Data/ArchitectureItem.h"
#include "Data/DoorItem.h"
#include "Data/WindowItem.h"
#include "Interfaces/IDeletable.h"
#include "Subsystems/BuildServer.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

ABurbPlayerController::ABurbPlayerController(): bSelectPressed(false), CurrentPawn(nullptr)
{
	// Set this player controller to tick every frame
	PrimaryActorTick.bCanEverTick = true;
}

void ABurbPlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void ABurbPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Get current mouse position
	float mouseX;
	float mouseY;
	GetMousePosition(mouseX, mouseY);
	FVector2D CurrentMousePos = FVector2D(mouseX, mouseY);

	// Only perform trace if mouse has moved beyond threshold or this is first tick
	// This significantly reduces unnecessary raycasts
	const float MouseDelta = FVector2D::Distance(CurrentMousePos, LastMousePosition);

	if (bFirstTick || MouseDelta >= MouseMovementThreshold)
	{
		// Calculate cursor position using plane intersection at current level's Z height
		if (CurrentPawn && CurrentPawn->CurrentLot)
		{
			ABurbPawn* BurbPawn = Cast<ABurbPawn>(CurrentPawn);
			if (BurbPawn)
			{
				// Determine which trace channel to use based on active build tool
				ECollisionChannel PrimaryChannel = ECC_GameTraceChannel3; // Default: Grid/Tile
				if (BurbPawn->CurrentBuildTool)
				{
					PrimaryChannel = BurbPawn->CurrentBuildTool->TraceChannel;
				}

				// Try to trace at current level first, then fall back to lower levels if no hit
				bool bFoundValidHit = false;
				FHitResult HitResult;
				int32 LevelToTry = BurbPawn->CurrentLevel;

				// For tile traces, use plane intersection and check if tiles exist
				// For other traces, do actual raycasts
				if (PrimaryChannel == ECC_GameTraceChannel3)
				{
					// Tile trace: Use plane intersection to get cursor position
					while (LevelToTry >= 0 && !bFoundValidHit)
					{
						// Calculate Z height for this level
						float LevelZ = CurrentPawn->CurrentLot->GetActorLocation().Z +
							(CurrentPawn->CurrentLot->DefaultWallHeight * (LevelToTry - CurrentPawn->CurrentLot->Basements));

						FVector PlanePosition;
						if (CalculateCursorPositionAtHeight(LevelZ, PlanePosition))
						{
							// Convert plane position to tile coordinates
							int32 Row, Col;
							if (CurrentPawn->CurrentLot->LocationToTile(PlanePosition, Row, Col))
							{
								// Check if a tile exists at this coordinate and level
								FTileData Tile = CurrentPawn->CurrentLot->FindTileByGridCoords(Row, Col, LevelToTry);
								if (Tile.TileIndex >= 0 && !Tile.bOutOfBounds)
								{
									// Valid tile exists at this level
									bFoundValidHit = true;
									CursorWorldLocation = PlanePosition;
									LastTracedLevel = LevelToTry;

									// Set up hit result for tools that need it
									HitResult.bBlockingHit = true;
									HitResult.Location = PlanePosition;
									CursorWorldHitResult = HitResult;
									break;
								}
							}
						}

						// No tile at this level, try one level down
						LevelToTry--;
					}
				}
				else
				{
					// Non-tile trace: Do actual raycast and fall back through levels
					while (LevelToTry >= 0 && !bFoundValidHit)
					{
						// Calculate Z height for this level
						float LevelZ = CurrentPawn->CurrentLot->GetActorLocation().Z +
							(CurrentPawn->CurrentLot->DefaultWallHeight * (LevelToTry - CurrentPawn->CurrentLot->Basements));

						FVector PlanePosition;
						if (CalculateCursorPositionAtHeight(LevelZ, PlanePosition))
						{
							// Try trace at this level with primary channel
							GetHitResultUnderCursor(PrimaryChannel, true, HitResult);

							// Check if we hit something at approximately this level's Z
							if (HitResult.bBlockingHit)
							{
								float ZTolerance = CurrentPawn->CurrentLot->DefaultWallHeight * 0.5f;
								if (FMath::Abs(HitResult.Location.Z - LevelZ) < ZTolerance)
								{
									// Hit is at this level
									bFoundValidHit = true;
									LastTracedLevel = LevelToTry;
									CursorWorldHitResult = HitResult;
									CursorWorldLocation = HitResult.Location;
									break;
								}
							}
						}

						// No hit at this level, try one level down
						LevelToTry--;
					}
				}

				// If we still haven't found anything and we weren't already using tile channel, fall back to tile trace
				if (!bFoundValidHit && PrimaryChannel != ECC_GameTraceChannel3)
				{
					LevelToTry = BurbPawn->CurrentLevel;

					// Tile fallback: Use plane intersection to get cursor position
					while (LevelToTry >= 0 && !bFoundValidHit)
					{
						// Calculate Z height for this level
						float LevelZ = CurrentPawn->CurrentLot->GetActorLocation().Z +
							(CurrentPawn->CurrentLot->DefaultWallHeight * (LevelToTry - CurrentPawn->CurrentLot->Basements));

						FVector PlanePosition;
						if (CalculateCursorPositionAtHeight(LevelZ, PlanePosition))
						{
							// Convert plane position to tile coordinates
							int32 Row, Col;
							if (CurrentPawn->CurrentLot->LocationToTile(PlanePosition, Row, Col))
							{
								// Check if a tile exists at this coordinate and level
								FTileData Tile = CurrentPawn->CurrentLot->FindTileByGridCoords(Row, Col, LevelToTry);
								if (Tile.TileIndex >= 0 && !Tile.bOutOfBounds)
								{
									// Valid tile exists at this level
									bFoundValidHit = true;
									CursorWorldLocation = PlanePosition;
									LastTracedLevel = LevelToTry;

									// Set up hit result for tools that need it
									HitResult.bBlockingHit = true;
									HitResult.Location = PlanePosition;
									CursorWorldHitResult = HitResult;
									break;
								}
							}
						}

						// No tile at this level, try one level down
						LevelToTry--;
					}
				}
			}
		}
		else
		{
			// Determine which trace channel to use based on active build tool
			ECollisionChannel ChannelToUse = ECC_GameTraceChannel3; // Default: Grid/Tile
			ABurbPawn* BurbPawn = Cast<ABurbPawn>(CurrentPawn);
			if (BurbPawn && BurbPawn->CurrentBuildTool)
			{
				ChannelToUse = BurbPawn->CurrentBuildTool->TraceChannel;
			}

			// Fallback to trace if no pawn/lot available
			FHitResult HitResult;
			GetHitResultUnderCursor(ChannelToUse, true, HitResult);
			CursorWorldLocation = HitResult.Location;
			CursorWorldHitResult = HitResult;
		}

		LastMousePosition = CurrentMousePos;
		bFirstTick = false;
	}
	// If mouse hasn't moved, reuse cached hit result (no trace needed)
}

bool ABurbPlayerController::CalculateCursorPositionAtHeight(float ZHeight, FVector& OutPosition)
{
	// Get mouse position
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}

	// Deproject screen position to get camera ray
	FVector CameraWorldPosition, CameraWorldDirection;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, CameraWorldPosition, CameraWorldDirection))
	{
		return false;
	}

	// Define horizontal plane at given Z height
	FPlane HorizontalPlane(FVector(0, 0, ZHeight), FVector::UpVector);

	// Calculate intersection of camera ray with horizontal plane
	// Ray equation: P = CameraWorldPosition + t * CameraWorldDirection
	// Plane equation: (P - PlaneOrigin) · PlaneNormal = 0
	// Solve for t: t = ((PlaneOrigin - CameraWorldPosition) · PlaneNormal) / (CameraWorldDirection · PlaneNormal)

	float Denominator = FVector::DotProduct(CameraWorldDirection, HorizontalPlane.GetNormal());

	// Check if ray is parallel to plane (denominator near zero)
	if (FMath::Abs(Denominator) < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FVector PlaneOrigin(0, 0, ZHeight);
	float Numerator = FVector::DotProduct(PlaneOrigin - CameraWorldPosition, HorizontalPlane.GetNormal());
	float t = Numerator / Denominator;

	// Check if intersection is behind camera
	if (t < 0)
	{
		return false;
	}

	// Calculate intersection point
	OutPosition = CameraWorldPosition + (CameraWorldDirection * t);
	return true;
}

FHitResult ABurbPlayerController::CustomGetHitResultUnderCursor(UObject* WorldContextObject, const FVector2D& ScreenPosition, float TraceDistance, bool bTraceComplex, ECollisionChannel TraceChannel)
{
	FHitResult HitResult;

	if (UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject))
	{
		APlayerController* PlayerController = UGameplayStatics::GetPlayerController(WorldContextObject, 0);
		if (PlayerController)
		{
			FVector WorldPosition, WorldDirection;
			if (PlayerController->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, WorldPosition, WorldDirection))
			{
				FCollisionQueryParams Params;
				Params.bReturnFaceIndex = true; // Set to true to get face index information
				Params.bTraceComplex = true;
				Params.AddIgnoredActor(PlayerController->GetPawn()); // Ignore the player pawn

				// Perform line trace
				World->LineTraceSingleByChannel(HitResult, WorldPosition, WorldPosition + (WorldDirection * TraceDistance), TraceChannel, Params);
			}
		}
	}

	return HitResult;
}

int32 ABurbPlayerController::BroadcastDeleteToSelected()
{
	int32 DeletedCount = 0;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::BroadcastDeleteToSelected - No valid world"));
		return 0;
	}

	// Collect all actors that need to be deleted
	// We collect them first instead of deleting immediately to avoid iterator invalidation
	TArray<AActor*> ActorsToDelete;

	// Iterate through all actors that implement IDeletable
	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		AActor* Actor = *ActorItr;
		if (!Actor || !Actor->GetClass()->ImplementsInterface(UDeletable::StaticClass()))
		{
			continue;
		}

		// Check if this actor is selected
		if (IDeletable::Execute_IsSelected(Actor))
		{
			// Check if this actor can be deleted
			if (IDeletable::Execute_CanBeDeleted(Actor))
			{
				ActorsToDelete.Add(Actor);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Actor %s is selected but cannot be deleted"), *Actor->GetName());
			}
		}
	}

	// Now delete all collected actors
	for (AActor* Actor : ActorsToDelete)
	{
		if (IsValid(Actor))
		{
			UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Requesting deletion of selected actor %s"), *Actor->GetName());
			if (IDeletable::Execute_RequestDeletion(Actor))
			{
				DeletedCount++;
			}
		}
	}

	if (DeletedCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Deleted %d selected actor(s)"), DeletedCount);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: No selected actors to delete"));
	}

	return DeletedCount;
}

// ========================================
// Save/Load Operations
// ========================================

bool ABurbPlayerController::QuickSave()
{
	// Only the host can save in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::QuickSave - Only the host can save"));
		return false;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::QuickSave - BuildServer not found"));
		return false;
	}

	ALotManager* Lot = BuildServer->GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::QuickSave - No current lot"));
		return false;
	}

	bool bSuccess = Lot->SaveLotToSlot("QuickSave");
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Quick Save successful"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Quick Save failed"));
	}

	return bSuccess;
}

bool ABurbPlayerController::QuickLoad()
{
	// Only the host can load in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::QuickLoad - Only the host can load"));
		return false;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::QuickLoad - BuildServer not found"));
		return false;
	}
	ALotManager* Lot = BuildServer->GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::QuickSave - No current lot"));
		return false;
	}
	bool bSuccess = Lot->LoadLotFromSlot("QuickSave");
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Quick Load successful"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Quick Load failed"));
	}

	return bSuccess;
}

bool ABurbPlayerController::SaveLotToSlot(const FString& SlotName)
{
	// Only the host can save in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::SaveLotToSlot - Only the host can save"));
		return false;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::SaveLotToSlot - BuildServer not found"));
		return false;
	}

	ALotManager* Lot = BuildServer->GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::SaveLotToSlot - No current lot"));
		return false;
	}

	bool bSuccess = Lot->SaveLotToSlot(SlotName);
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Saved lot to slot '%s'"), *SlotName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Failed to save lot to slot '%s'"), *SlotName);
	}

	return bSuccess;
}

bool ABurbPlayerController::LoadLotFromSlot(const FString& SlotName)
{
	// Only the host can load in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::LoadLotFromSlot - Only the host can load"));
		return false;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::LoadLotFromSlot - BuildServer not found"));
		return false;
	}

	bool bSuccess = BuildServer->LoadLotFromSlot(SlotName);
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Loaded lot from slot '%s'"), *SlotName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Failed to load lot from slot '%s'"), *SlotName);
	}

	return bSuccess;
}

bool ABurbPlayerController::ExportLotToJSON(const FString& FilePath)
{
	// Only the host can export in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ExportLotToJSON - Only the host can export"));
		return false;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ExportLotToJSON - BuildServer not found"));
		return false;
	}

	ALotManager* Lot = BuildServer->GetCurrentLot();
	if (!Lot)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ExportLotToJSON - No current lot"));
		return false;
	}

	bool bSuccess = Lot->ExportLotToFile(FilePath);
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Exported lot to '%s'"), *FilePath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Failed to export lot to '%s'"), *FilePath);
	}

	return bSuccess;
}

bool ABurbPlayerController::ImportLotFromJSON(const FString& FilePath)
{
	// Only the host can import in a listen server environment
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ImportLotFromJSON - Only the host can import"));
		return false;
	}

	UBuildServer* BuildServer = GetWorld()->GetSubsystem<UBuildServer>();
	if (!BuildServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ImportLotFromJSON - BuildServer not found"));
		return false;
	}

	bool bSuccess = BuildServer->ImportLotFromFile(FilePath);
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Imported lot from '%s'"), *FilePath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Failed to import lot from '%s'"), *FilePath);
	}

	return bSuccess;
}

// ========================================
// Catalog Item Activation
// ========================================

void ABurbPlayerController::HandleCatalogItemActivation(UCatalogItem* Item)
{
	if (!Item)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::HandleCatalogItemActivation - Item is null"));
		return;
	}

	// Check if this is a Floor Pattern
	if (UFloorPattern* FloorPattern = Cast<UFloorPattern>(Item))
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Activating Floor Pattern '%s'"), *FloorPattern->GetName());

		// Get the build tool class from the pattern (should be ABuildFloorTool or its Blueprint child)
		if (UArchitectureItem* ArchItem = Cast<UArchitectureItem>(FloorPattern))
		{
			if (!ArchItem->BuildToolClass.IsNull())
			{
				ServerTryCreateBuildToolWithPattern(ArchItem->BuildToolClass, FloorPattern);
				return;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Floor Pattern '%s' has no BuildToolClass assigned"), *FloorPattern->GetName());
			}
		}
	}
	// Check if this is a Wall Pattern
	else if (UWallPattern* WallPattern = Cast<UWallPattern>(Item))
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Activating Wall Pattern '%s'"), *WallPattern->GetName());

		// Get the build tool class from the pattern (should be AWallPatternTool or its Blueprint child)
		if (UArchitectureItem* ArchItem = Cast<UArchitectureItem>(WallPattern))
		{
			if (!ArchItem->BuildToolClass.IsNull())
			{
				ServerTryCreateBuildToolWithPattern(ArchItem->BuildToolClass, WallPattern);
				return;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Wall Pattern '%s' has no BuildToolClass assigned"), *WallPattern->GetName());
			}
		}
	}
	// Check if this is a Door Item - forward to BurbPawn for proper property setup
	else if (UDoorItem* DoorItem = Cast<UDoorItem>(Item))
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Door Item detected '%s', forwarding to BurbPawn"), *DoorItem->GetName());

		ABurbPawn* BurbPawn = Cast<ABurbPawn>(GetPawn());
		if (BurbPawn)
		{
			BurbPawn->HandleCatalogItemActivation(Item);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::HandleCatalogItemActivation - No valid BurbPawn for door item"));
		}
	}
	// Check if this is a Window Item - forward to BurbPawn for proper property setup
	else if (UWindowItem* WindowItem = Cast<UWindowItem>(Item))
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Window Item detected '%s', forwarding to BurbPawn"), *WindowItem->GetName());

		ABurbPawn* BurbPawn = Cast<ABurbPawn>(GetPawn());
		if (BurbPawn)
		{
			BurbPawn->HandleCatalogItemActivation(Item);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::HandleCatalogItemActivation - No valid BurbPawn for window item"));
		}
	}
	// Check if this is a generic Architecture Item (walls, rooms, stairs, etc.)
	else if (UArchitectureItem* ArchItem = Cast<UArchitectureItem>(Item))
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Activating Architecture Item '%s'"), *ArchItem->GetName());

		if (!ArchItem->BuildToolClass.IsNull())
		{
			ServerTryCreateBuildTool(ArchItem->BuildToolClass);
			return;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController: Architecture Item '%s' has no BuildToolClass assigned"), *ArchItem->GetName());
		}
	}
	// For other item types (Furniture, etc.), forward to BurbPawn's existing logic
	else
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Forwarding item '%s' to BurbPawn::HandleCatalogItemActivation"), *Item->GetName());

		ABurbPawn* BurbPawn = Cast<ABurbPawn>(GetPawn());
		if (BurbPawn)
		{
			BurbPawn->HandleCatalogItemActivation(Item);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::HandleCatalogItemActivation - No valid BurbPawn"));
		}
	}
}

void ABurbPlayerController::ServerTryCreateBuildTool_Implementation(const TSoftClassPtr<ABuildTool>& BuildToolClass)
{
	if (BuildToolClass.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildTool - BuildToolClass is null"));
		return;
	}

	// Load the class (blocking)
	UClass* LoadedClass = BuildToolClass.LoadSynchronous();
	if (!LoadedClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildTool - Failed to load BuildToolClass"));
		return;
	}

	// Cast to TSubclassOf<ABuildTool>
	TSubclassOf<ABuildTool> ToolClass = LoadedClass;
	if (!ToolClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildTool - Loaded class is not a subclass of ABuildTool"));
		return;
	}

	// Get the controlled pawn
	ABurbPawn* BurbPawn = Cast<ABurbPawn>(GetPawn());
	if (!BurbPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildTool - No valid BurbPawn"));
		return;
	}

	// Get the world
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildTool - No valid world"));
		return;
	}

	// Spawn the tool at the pawn's location
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = BurbPawn;
	SpawnParams.Instigator = BurbPawn;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn tool at pawn location but with zero rotation (don't inherit isometric camera pitch)
	FTransform SpawnTransform = BurbPawn->GetActorTransform();
	SpawnTransform.SetRotation(FQuat::Identity); // Reset rotation to zero
	ABuildTool* SpawnedTool = World->SpawnActor<ABuildTool>(ToolClass, SpawnTransform, SpawnParams);
	if (!SpawnedTool)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildTool - Failed to spawn tool"));
		return;
	}

	// Initialize tool properties
	SpawnedTool->CurrentLot = BurbPawn->CurrentLot;
	SpawnedTool->CurrentPlayerPawn = BurbPawn;

	// Equip the tool
	BurbPawn->SetCurrentBuildTool(SpawnedTool);

	UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Successfully created and equipped build tool '%s'"), *ToolClass->GetName());
}

void ABurbPlayerController::ServerTryCreateBuildToolWithPattern_Implementation(const TSoftClassPtr<ABuildTool>& BuildToolClass, UCatalogItem* PatternItem)
{
	if (BuildToolClass.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - BuildToolClass is null"));
		return;
	}

	if (!PatternItem)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - PatternItem is null"));
		return;
	}

	// Load the class (blocking)
	UClass* LoadedClass = BuildToolClass.LoadSynchronous();
	if (!LoadedClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - Failed to load BuildToolClass"));
		return;
	}

	// Cast to TSubclassOf<ABuildTool>
	TSubclassOf<ABuildTool> ToolClass = LoadedClass;
	if (!ToolClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - Loaded class is not a subclass of ABuildTool"));
		return;
	}

	// Get the controlled pawn
	ABurbPawn* BurbPawn = Cast<ABurbPawn>(GetPawn());
	if (!BurbPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - No valid BurbPawn"));
		return;
	}

	// Get the world
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - No valid world"));
		return;
	}

	// Spawn the tool at the pawn's location
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = BurbPawn;
	SpawnParams.Instigator = BurbPawn;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn tool at pawn location but with zero rotation (don't inherit isometric camera pitch)
	FTransform SpawnTransform = BurbPawn->GetActorTransform();
	SpawnTransform.SetRotation(FQuat::Identity); // Reset rotation to zero
	ABuildTool* SpawnedTool = World->SpawnActor<ABuildTool>(ToolClass, SpawnTransform, SpawnParams);
	if (!SpawnedTool)
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - Failed to spawn tool"));
		return;
	}

	// Initialize base tool properties
	SpawnedTool->CurrentLot = BurbPawn->CurrentLot;
	SpawnedTool->CurrentPlayerPawn = BurbPawn;

	// Pattern assignment logic
	bool bPatternAssigned = false;

	// Check if this is a Floor Pattern
	if (UFloorPattern* FloorPattern = Cast<UFloorPattern>(PatternItem))
	{
		// Cast tool to ABuildFloorTool
		if (ABuildFloorTool* FloorTool = Cast<ABuildFloorTool>(SpawnedTool))
		{
			FloorTool->DefaultFloorPattern = FloorPattern;
			bPatternAssigned = true;
			UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Assigned Floor Pattern '%s' to BuildFloorTool"), *FloorPattern->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - PatternItem is a FloorPattern but tool is not a BuildFloorTool"));
		}
	}
	// Check if this is a Wall Pattern
	else if (UWallPattern* WallPattern = Cast<UWallPattern>(PatternItem))
	{
		// Cast tool to AWallPatternTool
		if (AWallPatternTool* WallTool = Cast<AWallPatternTool>(SpawnedTool))
		{
			WallTool->SelectedWallPattern = WallPattern;
			bPatternAssigned = true;
			UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Assigned Wall Pattern '%s' to WallPatternTool"), *WallPattern->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - PatternItem is a WallPattern but tool is not a WallPatternTool"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BurbPlayerController::ServerTryCreateBuildToolWithPattern - PatternItem is not a FloorPattern or WallPattern"));
	}

	// Equip the tool (regardless of whether pattern assignment succeeded)
	BurbPawn->SetCurrentBuildTool(SpawnedTool);

	if (bPatternAssigned)
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Successfully created and equipped build tool '%s' with pattern"), *ToolClass->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("BurbPlayerController: Created and equipped build tool '%s' but pattern assignment failed"), *ToolClass->GetName());
	}
}
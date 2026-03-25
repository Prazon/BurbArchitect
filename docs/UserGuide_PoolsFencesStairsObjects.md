# BurbArchitect User Guide: Pools, Fences, Stairs, Objects & Save System

> **Audience:** Mid-level Unreal Engine 5 developers integrating BurbArchitect into their project.
> **Version:** Reflects current source as of February 2026.

---

## Table of Contents

1. [Pools](#1-pools)
2. [Fences & Gates](#2-fences--gates)
3. [Stairs](#3-stairs)
4. [Furniture & Objects (Buy Mode)](#4-furniture--objects-buy-mode)
5. [Save / Load System](#5-save--load-system)
6. [Creating Custom Data Assets](#6-creating-custom-data-assets)
7. [Blueprint API Reference](#7-blueprint-api-reference)

---

## 1. Pools

Pools in BurbArchitect are rooms built one level below the current floor. When viewing the ground floor (Level 1), the pool tool creates geometry at Level 0 — essentially a specialized basement room filled with a water volume.

### 1.1 How Pool Creation Works

`ABuildPoolTool` extends the room drawing tool (`ABuildRoomTool`). You draw a pool the same way you draw a room — click and drag a rectangular region on the grid. The tool automatically targets one level below the current traced level.

**Workflow (runtime):**

1. Activate the pool tool (set your active `ABuildTool` to an instance of `ABuildPoolTool`).
2. The player clicks and drags on the lot grid. The tool snaps to grid corners and shows wall previews at the target level.
3. On release (`BroadcastRelease`), pool walls are committed at the sub-level and a water volume is generated.

### 1.2 Pool Merging

When a new pool is drawn adjacent to an existing pool, shared wall segments are automatically detected and removed. The `PoolWallsToRemove` array tracks edge IDs of walls that should be deleted, and `PoolRoomsToMerge` tracks room IDs whose water volumes need to be regenerated as a single merged volume.

The result is seamless, Sims-style pool merging — draw two adjacent rectangles and they become one L-shaped pool.

### 1.3 Water Volumes (`UWaterComponent`)

Each pool room gets a 3D water volume managed by `UWaterComponent`, a procedural mesh component. The water mesh consists of:

- **Top surface** — a triangulated polygon matching the room boundary vertices
- **Side walls** — quads connecting the top surface to the pool floor

Water volumes are stored in the `PoolWaterArray` with O(1) lookup via `PoolWaterMap` (keyed by `RoomID`).

**Key properties on `ABuildPoolTool`:**

| Property | Type | Description |
|---|---|---|
| `DefaultWaterMaterial` | `UMaterialInstance*` | Material applied to the water mesh. Assign your custom water shader here. |
| `PoolFloorPattern` | `UFloorPattern*` | Pattern used for pool bottom tiles. |
| `PoolFloorMaterial` | `UMaterialInstance*` | Material for the pool floor surface. |
| `WaterSurfaceZOffset` | `float` | Vertical offset of the water surface relative to ground level (default: `-5.0`). Negative values lower the surface below the surrounding ground. |

**Key `UWaterComponent` functions (all BlueprintCallable):**

| Function | Description |
|---|---|
| `GeneratePoolWater(RoomID, BoundaryVertices, WaterSurfaceZ, PoolFloorZ, BaseMaterial)` | Creates a new water volume. Returns `FPoolWaterData`. |
| `UpdatePoolWater(RoomID, BoundaryVertices)` | Updates an existing pool when its room polygon changes. |
| `RemovePoolWater(RoomID)` | Destroys water mesh for the given room. |
| `HasPoolWater(RoomID)` | Returns `true` if a water volume exists for this room. |
| `DestroyAllWater()` | Removes every water volume on the lot. |

### 1.4 Pool Materials

To customize the water appearance:

1. Create a Material Instance based on your water material (translucent, depth fade, etc.).
2. Assign it to `DefaultWaterMaterial` on your `ABuildPoolTool` instance.
3. At runtime, the component creates a `UMaterialInstanceDynamic` per pool from your base material, stored in `FPoolWaterData::WaterMaterial`. You can modify per-pool parameters dynamically.

---

## 2. Fences & Gates

Fences are **decorative only** — they do not participate in room detection. They use static meshes (panels + posts) instead of procedural wall geometry.

### 2.1 Fence Placement

`ABuildFenceTool` extends `ABuildHalfWallTool` to inherit the drag/snap behavior along grid edges. During the drag phase, the tool spawns preview panel and post meshes. On release, actual `FFenceSegmentData` entries are committed to the `UFenceComponent`.

**Runtime workflow:**

1. Set `CurrentFenceItem` on the tool to a `UFenceItem` data asset.
2. Player clicks a grid corner and drags to define the fence line.
3. Preview meshes appear along the path showing panels and posts.
4. On release, the tool calls `UFenceComponent::GenerateFenceSegment()` to commit.

### 2.2 Fence Component (`UFenceComponent`)

The `UFenceComponent` is a scene component that manages all fence segments on a lot, parallel in architecture to the `WallComponent` for walls.

Each fence segment (`FFenceSegmentData`) stores:

- Start/end world locations
- Floor level
- Reference to the `UFenceItem` data asset
- Arrays of spawned panel and post mesh components
- Array of gates placed on this segment
- Commit state

**Key functions (BlueprintCallable):**

| Function | Description |
|---|---|
| `GenerateFenceSegment(Level, StartLoc, EndLoc, FenceItem)` | Creates a fence segment. Returns its index. |
| `RemoveFenceSegment(FenceIndex)` | Destroys a segment and all its meshes. |
| `FindFenceSegmentAtLocation(Location, Tolerance)` | Finds the nearest segment index. Returns `-1` if none found. |
| `AddGateToFence(FenceIndex, GateLocation, GateWidth, Gate)` | Inserts a gate, regenerating panels/posts around it. |
| `RemoveGateFromFence(FenceIndex, Gate)` | Removes a gate and regenerates the fence. |

### 2.3 Gate Placement

`ABuildGateTool` extends `ABuildDoorTool` but detects **fences** instead of walls. It uses `FenceComponent->FindFenceSegmentAtLocation()` to find the target fence, then spawns an `AGateBase` actor.

**How gates integrate with fences:**

1. Gate tool hovers — `DetectFenceSegment()` checks if the cursor is over a fence.
2. Position snaps along the fence segment via `SnapToFence()`.
3. Rotation auto-aligns to the fence direction via `CalculateFenceRotation()`.
4. On placement, `AGateBase::OnGatePlaced()` tells the `FenceComponent` to:
   - Force posts on each side of the gate opening.
   - Regenerate panels with a cutout for the gate.
5. On deletion, `AGateBase::OnDeleted_Implementation()` regenerates the fence without the cutout.

**`AGateBase`** extends `ADoorBase`, so gates inherit the full door animation system (open/close skeletal mesh animations).

### 2.4 UFenceItem Data Asset

`UFenceItem` extends `UArchitectureItem` (which extends `UCatalogItem`). It defines:

| Property | Type | Description |
|---|---|---|
| `FencePanelMesh` | `TSoftObjectPtr<UStaticMesh>` | The repeating panel mesh. |
| `FencePostMesh` | `TSoftObjectPtr<UStaticMesh>` | The post mesh (corners, ends, junctions). |
| `PostSpacing` | `int32` | Tiles between intermediate posts. `0` = no intermediate posts. |
| `bPostsAtCorners` | `bool` | Force posts at fence corners. |
| `bPostsAtEnds` | `bool` | Force posts at fence endpoints. |
| `bPostsAtJunctions` | `bool` | Force posts at T-intersections / crosses. |
| `FenceHeight` | `float` | Height in units (default `200`). |
| `PanelWidth` | `float` | Width of one panel mesh for spacing calculations. |

**Inherited from `UCatalogItem`:**

| Property | Type | Description |
|---|---|---|
| `DisplayName` | `FText` | Name shown in catalog UI. |
| `Icon` | `FSlateBrush` | Thumbnail icon. |
| `Cost` | `float` | In-game price. |
| `Category` | `UCatalogCategory*` | Root catalog category. |
| `Subcategory` | `UCatalogSubcategory*` | Optional subcategory. |

### 2.5 UGateItem Data Asset

`UGateItem` extends `UDoorItem` (which extends `UArchitectureItem`). In addition to inherited portal properties (size, offset, snapping), it adds:

| Property | Type | Description |
|---|---|---|
| `GateSkeletalMesh` | `TSoftObjectPtr<USkeletalMesh>` | Skeletal mesh for animation. Can reuse a door skeleton. |
| `GateStaticMesh` | `TSoftObjectPtr<UStaticMesh>` | Gate panel static mesh. |
| `GateFrameMesh` | `TSoftObjectPtr<UStaticMesh>` | Gate frame static mesh. |

Inherited portal properties from `UDoorItem`: `PortalSize`, `PortalOffset`, `HorizontalSnap`, `VerticalSnap`, `bSnapsToFloor`, `ClassToSpawn`.

---

## 3. Stairs

Stairs connect two adjacent floor levels. BurbArchitect's stair system supports straight runs, 90° turns (left/right), and landings, all adjustable after placement via on-screen gizmo tools.

### 3.1 Stair Placement

`ABuildStairsTool` is the build tool. It creates a preview staircase that the player positions and rotates before committing.

**Runtime workflow:**

1. Activate the stairs tool.
2. The tool spawns a preview `AStairsBase` actor (`DragCreateStairs`).
3. Player moves the cursor — the preview snaps to valid grid positions.
4. `RotateLeft` / `RotateRight` rotate the preview by 90°.
5. Clicking commits the stairs via `CreateStairs()`.

**Key `ABuildStairsTool` properties:**

| Property | Type | Description |
|---|---|---|
| `StairsActorClass` | `TSubclassOf<AStairsBase>` | Blueprint class to spawn. Override this for custom stair actors. |
| `StairTreadMesh` | `UStaticMesh*` | Mesh for individual stair treads. |
| `StairLandingMesh` | `UStaticMesh*` | Mesh for landing platforms. |
| `DefaultStairsMaterial` | `UMaterialInstance*` | Default material applied to stair meshes. |
| `TreadsCount` | `int` | Number of treads to span one level (default: `12`). At 25 units rise per tread, 12 treads = 300 units (one standard wall height). |
| `Height` | `float` | Total vertical travel (default: `300`). |
| `StairsThickness` | `float` | Thickness of the stair underside (default: `15`). |
| `TreadSize` | `int` | Depth of each tread (default: `25`). |
| `LandingSize` | `int` | Depth of landing platforms (default: `95`). |

> **Mesh Socket Convention:** Stair tread meshes must have `Front_Socket` and `Back_Socket`. The offset between them should be approximately `(25, 0, 25)` for standard rise. The tool chains treads via these sockets.

### 3.2 AStairsBase — The Stair Actor

`AStairsBase` is an abstract actor that encapsulates a complete staircase. It manages:

- **Stair mesh generation** via an embedded `UStairsComponent` (procedural mesh)
- **Adjustment gizmos** for post-placement editing
- **Floor cutaway** at the upper level
- **Validation** (both levels must exist, landing tiles must be present)
- **Deletion** via the `IDeletable` interface (DEL key support)

#### 3.2.1 Stair Modules

Each staircase is defined as an array of `FStairModuleStructure`:

```cpp
USTRUCT(BlueprintType)
struct FStairModuleStructure
{
    EStairModuleType StairType;    // Tread or Landing
    ETurningSocket TurningSocket;  // Idle, Right, or Left
};
```

A straight 12-tread staircase is 12 entries of `{Tread, Idle}`. To add a 90° turn, insert a `{Landing, Right}` or `{Landing, Left}` at the desired step.

The `StepsPerSection` property (default: `4`) sets the minimum number of treads before a landing/turn can occur. Landings snap to multiples of this value.

#### 3.2.2 Adjustment Gizmos

After placing stairs, clicking them enters **Edit Mode** with three gizmo handles:

| Gizmo | Type | What It Does |
|---|---|---|
| `AdjustmentTool_StartingStep` | Starting step | Drag to extend or shorten the bottom of the staircase. |
| `AdjustmentTool_EndingStep` | Ending step | Drag to extend or shorten the top of the staircase. |
| `AdjustmentTool_LandingTool` | Landing/turn | Drag left or right to insert a 90° turn at that position. |

**Blueprint API for edit mode:**

| Function | Description |
|---|---|
| `EnterEditMode()` | Shows gizmo handles. |
| `ExitEditMode()` | Hides gizmo handles. |
| `ToggleEditMode()` | Toggles. |
| `RotateLeft()` / `RotateRight()` | Rotates the entire staircase 90°. |
| `UpdateStairStructure(TurnSocket, LandingIndex)` | Programmatically insert a turn at a specific step. |

#### 3.2.3 Floor Cutaway

When stairs are committed (`CommitStairs()`), the system automatically cuts a hole in the upper floor where the staircase emerges. This uses `CutAwayStairOpening()`, which:

1. Calculates the stair footprint at the top level via `GetTopLevelFootprint()`.
2. Removes floor/terrain tiles at those grid positions.
3. Stores removed tiles in `RemovedFloorTiles` / `RemovedTerrainTiles` for undo support.

If stairs are deleted, `RestoreStairOpening()` puts those tiles back.

#### 3.2.4 Validation

Before committing, `ValidatePlacement()` checks:

- Both the start level and end level (`Level` and `Level + 1`) exist within the lot's floor/basement range.
- Floor or terrain exists at the bottom landing tile.
- Floor or terrain exists at the top landing tile.

The `bValidPlacement` property and `ValidationError` string are updated for UI feedback.

### 3.3 StairsComponent

`UStairsComponent` is the procedural mesh component that generates the actual stair geometry. It manages an array of `FStairsSegmentData` entries. Most interaction goes through `AStairsBase`, but you can also use the component directly:

| Function | Description |
|---|---|
| `GenerateStairsMeshSection(InStairsData)` | Generates mesh for a single stair segment. |
| `CommitStairsSection(InStairsData, TreadsMaterial, LandingsMaterial)` | Finalizes a segment with materials. |
| `DestroyStairsSection(InStairsData)` | Removes a specific section. |
| `FindExistingStairsSection(TileCornerStart, Structures, OutStairs)` | Checks if stairs already exist at a location. |

---

## 4. Furniture & Objects (Buy Mode)

The object placement system handles furniture, decorations, and fixtures. It supports four placement modes: Floor, Wall Hanging, Ceiling Fixture, and Surface.

### 4.1 Placement Modes

```cpp
UENUM(BlueprintType)
enum class EObjectPlacementMode : uint8
{
    Floor,           // Chairs, tables — snaps to floor grid
    WallHanging,     // Posters, shelves — snaps to wall surfaces
    CeilingFixture,  // Chandeliers — snaps to ceiling
    Surface          // (Future) — on countertops, desks
};
```

### 4.2 ABuyObjectTool

`ABuyObjectTool` is the build tool for placing objects. It reads placement rules from the `CurrentCatalogItem` (`UFurnitureItem*`) and delegates to the appropriate handler.

**Key properties:**

| Property | Type | Description |
|---|---|---|
| `CurrentCatalogItem` | `UFurnitureItem*` | The furniture item being placed. Set this before activating the tool. |
| `bValidPlacement` | `bool` (read-only) | Whether the current position is a valid placement. Use for visual feedback (green vs red ghost). |
| `CurrentPlacementMode` | `EObjectPlacementMode` (read-only) | Derived from the catalog item's `PlacementRules`. |

**Placement helpers (all BlueprintCallable):**

| Function | Description |
|---|---|
| `TraceForWall(FromLocation, OutHit)` | Raycasts for a wall mesh. Returns `true` if hit. |
| `CalculateWallMountPosition(WallHit, HeightAboveFloor, WallOffset, Level)` | Calculates world position on a wall surface. |
| `CalculateWallMountRotation(WallNormal)` | Calculates rotation facing away from wall. |
| `CheckAdjacentWalls(Row, Col, Level, OutWallNormal)` | O(1) grid check for adjacent walls. |
| `GetAdjacentWallNormals(Row, Col, Level)` | Returns all wall normals adjacent to a tile. |
| `CalculateCeilingHeight(Level)` | Returns world Z of the ceiling at a level. |
| `CheckCeilingExists(Row, Col, Level)` | Returns `true` if a ceiling (roof or upper floor) exists. |

### 4.3 APlaceableObject

`APlaceableObject` is the base class for all placed furniture/decor actors. It's replicated for multiplayer and stores placement metadata for serialization.

**Properties:**

| Property | Type | Description |
|---|---|---|
| `CurrentFloor` | `int32` | Floor level this object is on. Replicated + SaveGame. |
| `PlacementMode` | `EObjectPlacementMode` | How this object was placed. Replicated + SaveGame. |
| `PlacedRow` / `PlacedColumn` | `int32` | Grid coordinates for floor/ceiling objects. Replicated + SaveGame. |
| `WallNormal` | `FVector` | For wall-mounted objects, the wall's outward normal. Replicated + SaveGame. |

All your custom furniture Blueprints should extend `APlaceableObject` (or a Blueprint subclass of it).

### 4.4 UFurnitureItem Data Asset

`UFurnitureItem` extends `UCatalogItem` and adds:

| Property | Type | Description |
|---|---|---|
| `ClassToSpawn` | `TSoftClassPtr<APlaceableObject>` | The Blueprint class spawned when this item is placed. |
| `PlacementRules` | `FPlacementConstraints` | Defines how and where this item can be placed. |

#### FPlacementConstraints

The constraints struct is the core of the placement validation system:

**General:**

| Property | Default | Description |
|---|---|---|
| `PlacementMode` | `Floor` | Determines snap behavior — Floor, WallHanging, CeilingFixture, or Surface. |
| `bCanPlaceOutdoors` | `true` | Whether the object can be placed outside of enclosed rooms. |
| `GridSize` | `(1, 1, 1)` | Footprint in tiles (X × Y × Z). |

**Floor placement:**

| Property | Default | Description |
|---|---|---|
| `bMustBeAgainstWall` | `false` | Requires an adjacent wall (toilets, beds against walls). |
| `bAutoRotateFromWall` | `true` | Auto-faces away from the wall when placed against one. |
| `bRequiresFloor` | `true` | Must have a floor tile underneath. |

**Wall hanging:**

| Property | Default | Description |
|---|---|---|
| `WallMountHeight` | `150.0` | Height above floor in cm. |
| `bCanRotateOnWall` | `true` | Allow 360° rotation on the wall surface. |
| `WallOffset` | `5.0` | Distance from wall surface in cm. |

**Ceiling fixture:**

| Property | Default | Description |
|---|---|---|
| `CeilingOffset` | `20.0` | Distance from ceiling surface in cm. |

> **Tip:** The Editor uses `EditCondition` and `EditConditionHides` metadata so only relevant properties show in the Details panel based on the selected `PlacementMode`.

### 4.5 Catalog System

All catalog items (furniture, fences, gates, architecture) are auto-discovered by the `UCatalogSubsystem` at game startup. It scans the Asset Registry for all `UCatalogItem` subclass assets — **regardless of directory** — making it mod-friendly.

**Getting items in Blueprint:**

```cpp
// Get the subsystem
UCatalogSubsystem* Catalog = GetGameInstance()->GetSubsystem<UCatalogSubsystem>();

// Browse categories
TArray<UCatalogCategory*> Categories = Catalog->GetRootCategories();

// Get items in a category (loads on demand)
TArray<UCatalogItem*> Items = Catalog->GetItemsInCategory(MyCategory);

// Search by name
TArray<UCatalogItem*> Results = Catalog->SearchItemsByName("Chair");
```

The catalog uses **lazy loading** — items are stored as `FSoftObjectPath` and only loaded when requested. For UI lists that just need counts/names, use `GetItemPathsInCategory()` / `SearchItemPathsByName()`.

---

## 5. Save / Load System

BurbArchitect provides a complete serialization pipeline with multiple output formats.

### 5.1 Architecture Overview

```
LotManager                          (high-level API - BlueprintCallable)
  └─ ULotSerializationSubsystem    (GameInstance subsystem - does the work)
       ├─ SerializeLot()            → FSerializedLotData
       ├─ DeserializeLot()          ← FSerializedLotData
       ├─ ExportToJSON()            → .json file
       └─ ImportFromJSON()          ← .json file

ULotSaveGame (USaveGame)            (binary .sav files in Saved/SaveGames/)
ULotDataAsset (UDataAsset)          (packaged assets for default/shipped lots)
```

### 5.2 LotManager Save/Load Functions

All of these are `BlueprintCallable` and available on any `ALotManager` reference:

| Function | Description |
|---|---|
| `SaveLotToSlot(SlotName)` | Saves to Unreal's binary SaveGame system. Files land in `Saved/SaveGames/<SlotName>.sav`. Returns `true` on success. |
| `LoadLotFromSlot(SlotName)` | Loads from a save slot. Clears the current lot first, then rebuilds everything. Returns `true` on success. |
| `ExportLotToFile(FilePath)` | Exports to a human-readable JSON file at the given absolute path. Great for sharing lots or debugging. |
| `ImportLotFromFile(FilePath)` | Imports a lot from a JSON file. |
| `LoadDefaultLot(LotAsset)` | Loads a pre-built lot from a `ULotDataAsset`. Use for starter homes, pre-furnished lots, etc. |
| `SaveAsDataAsset(AssetName, PackagePath)` | **Editor only.** Creates a `ULotDataAsset` in your Content Browser that can be packaged with the game. |

### 5.3 FSerializedLotData

This is the master struct containing everything needed to reconstruct a lot:

| Field | Type | What It Stores |
|---|---|---|
| `GridConfig` | `FLotGridConfig` | Grid dimensions, tile size, floor/basement count, current level. |
| `WallNodes` | `TArray<FSerializedWallNode>` | Wall graph vertices (node ID, row, col, level). |
| `WallEdges` | `TArray<FSerializedWallEdge>` | Wall segments connecting nodes (edge ID, rooms, height, pattern/material paths). |
| `FloorTiles` | `TArray<FSerializedFloorTile>` | Individual floor tiles (row, col, level, pattern/material, active sections bitmask). |
| `Roofs` | `TArray<FSerializedRoofData>` | Roof structures (class, location, dimensions, pitch, material). |
| `Stairs` | `TArray<FSerializedStairsData>` | Staircases (class, location, direction, modules, meshes). |
| `Terrain` | `FSerializedTerrainData` | Terrain tiles with corner heights. |
| `Portals` | `TArray<FSerializedPortalData>` | Doors and windows (class, location, rotation, attached wall edge). |
| `PlacedObjects` | `TArray<FSerializedPlacedObject>` | Furniture/decor (class path, transform, level). |
| `PoolWater` | `TArray<FSerializedPoolWater>` | Pool water volumes (room ID, boundary vertices, surface/floor Z, material). |
| `Fences` | `TArray<FSerializedFenceSegment>` | Fence segments (start/end locations, level, fence item asset path). |
| `TileRoomIDs` | `TArray<int32>` | Flattened room ID grid: `[Level * GridSizeX * GridSizeY + Row * GridSizeX + Col]`. |
| `UpperFloorTiles` | `TArray<int32>` | Packed tile references: `(Level << 24) \| (Row << 12) \| Column`. |
| `LotName` | `FString` | User-friendly lot name. |
| `Description` | `FString` | Optional description. |
| `SaveTimestamp` | `FDateTime` | When the lot was saved. |

### 5.4 ULotSerializationSubsystem

This `UGameInstanceSubsystem` handles all the actual serialization work. Access it from any Blueprint or C++:

```cpp
ULotSerializationSubsystem* Serializer = GetGameInstance()->GetSubsystem<ULotSerializationSubsystem>();

// Serialize the entire lot
FSerializedLotData Data = Serializer->SerializeLot(LotManager);

// Validate data
bool bValid = Serializer->ValidateLotData(Data);

// Export to JSON
Serializer->ExportToJSON(Data, TEXT("C:/MyLots/TestHouse.json"));

// Import from JSON
FSerializedLotData ImportedData;
Serializer->ImportFromJSON(TEXT("C:/MyLots/TestHouse.json"), ImportedData);

// Apply to a lot
Serializer->DeserializeLot(LotManager, ImportedData);
```

The subsystem serializes each component independently:

| Serialize Function | Deserialize Function | What |
|---|---|---|
| `SerializeWallGraph()` | `DeserializeWallGraph()` | Wall nodes + edges |
| `SerializeFloorComponent()` | `DeserializeFloorComponent()` | Floor tiles |
| `SerializeRoofComponent()` | `DeserializeRoofComponent()` | Roof actors |
| `SerializeStairsComponent()` | `DeserializeStairsComponent()` | Staircase actors |
| `SerializeTerrainComponent()` | `DeserializeTerrainComponent()` | Terrain heightmap |
| `SerializeWaterComponent()` | `DeserializeWaterComponent()` | Pool water volumes |
| `SerializeFenceComponent()` | `DeserializeFenceComponent()` | Fence segments |
| `SerializePortals()` | `DeserializePortals()` | Doors + windows |
| `SerializePlacedObjects()` | `DeserializePlacedObjects()` | Furniture/decor |
| `SerializeRoomIDs()` | `DeserializeRoomIDs()` | Room assignment grid |

### 5.5 ULotDataAsset — Pre-Built Lots

`ULotDataAsset` is a `UDataAsset` containing an `FSerializedLotData`. Use it for default/starter lots shipped with your game.

**Properties:**

| Property | Type | Description |
|---|---|---|
| `LotData` | `FSerializedLotData` | The complete serialized lot. |
| `Thumbnail` | `UTexture2D*` | Preview image for menus. |
| `Category` | `FString` | Organizational category (e.g., "Starter", "Large"). |
| `Tags` | `TArray<FString>` | Filter tags (e.g., "Modern", "Colonial"). |
| `Price` | `int32` | Cost to place this lot (`0` = free). |

**Functions:**

| Function | Description |
|---|---|
| `ValidateLotData()` | Returns `true` if the stored data is valid. |
| `GetLotName()` | Returns `LotData.LotName` if set, otherwise the asset name. |
| `GetDescription()` | Returns `LotData.Description`. |

### 5.6 JSON Format

The JSON export uses Unreal's `FJsonObjectConverter` to serialize `FSerializedLotData`. The resulting file is human-readable and diff-friendly, making it ideal for:

- **Lot sharing** between players or developers
- **Version control** (JSON diffs are meaningful)
- **Debugging** serialization issues
- **External tools** that generate or modify lots

---

## 6. Creating Custom Data Assets

### 6.1 Creating a Custom Fence

1. **Prepare meshes:**
   - Create a **panel mesh** — one repeating fence section. The mesh width should match your intended `PanelWidth` value.
   - Create a **post mesh** — the vertical element placed at corners, ends, and intermediate points.

2. **Create the data asset:**
   - In the Content Browser, right-click → **Miscellaneous → Data Asset**.
   - Select **FenceItem** as the class.
   - Name it descriptively (e.g., `FI_WoodPicket`).

3. **Configure properties:**
   - Set `FencePanelMesh` and `FencePostMesh` to your static meshes.
   - Set `PanelWidth` to match your mesh (e.g., `100.0` for a 1-tile-wide panel).
   - Set `FenceHeight` (default `200`).
   - Configure `PostSpacing` — `1` means a post every tile, `2` every other tile, etc.
   - Enable `bPostsAtCorners`, `bPostsAtEnds`, `bPostsAtJunctions` as needed.

4. **Set catalog properties:**
   - Set `DisplayName`, `Icon`, and `Cost`.
   - Assign a `Category` (create a `UCatalogCategory` data asset if you don't have one).
   - Optionally assign a `Subcategory`.

5. **Done!** The `CatalogSubsystem` will auto-discover your asset on next game launch. Assign it to `ABuildFenceTool::CurrentFenceItem` to use it.

### 6.2 Creating a Custom Gate

1. **Prepare meshes:**
   - Create gate panel, frame, and optionally skeletal meshes.
   - Gates reuse the door animation skeleton — your skeletal mesh should have the same bone structure as doors if you want open/close animations.

2. **Create the data asset:**
   - Right-click → **Miscellaneous → Data Asset → GateItem** (e.g., `GI_IronGate`).

3. **Configure:**
   - Set `GateSkeletalMesh`, `GateStaticMesh`, `GateFrameMesh`.
   - Set `PortalSize` (width × height of the gate opening).
   - Set `HorizontalSnap` for placement snapping along the fence.
   - Set `ClassToSpawn` to your gate Blueprint class (must extend `AGateBase` or its Blueprint equivalent).
   - Set catalog properties (DisplayName, Icon, Cost, Category).

### 6.3 Creating a Custom Furniture Item

1. **Create the actor Blueprint:**
   - Create a new Blueprint extending `APlaceableObject` (or a child class).
   - Add your static/skeletal mesh components, collision, interaction logic, etc.

2. **Create the data asset:**
   - Right-click → **Miscellaneous → Data Asset → FurnitureItem** (e.g., `FI_DiningChair`).

3. **Configure:**
   - Set `ClassToSpawn` to your Blueprint class.
   - Configure `PlacementRules`:
     - **Floor item** (chair, table): `PlacementMode = Floor`, set `GridSize`, `bRequiresFloor = true`.
     - **Wall item** (painting, shelf): `PlacementMode = WallHanging`, set `WallMountHeight`, `WallOffset`.
     - **Against-wall item** (toilet, bed): `PlacementMode = Floor`, `bMustBeAgainstWall = true`, `bAutoRotateFromWall = true`.
     - **Ceiling item** (chandelier): `PlacementMode = CeilingFixture`, set `CeilingOffset`.
   - Set catalog properties (DisplayName, Icon, Cost, Category).

4. **Test:**
   - Launch PIE, open buy mode, find your item in the catalog.
   - Verify placement rules (snapping, validation, rotation).

### 6.4 Creating a Pre-Built Lot

1. **Build the lot in-game** using the build tools (walls, floors, roofs, furniture, etc.).

2. **Export as data asset (Editor only):**
   - In the LotManager Details panel, use `SaveAsDataAsset("DA_StarterHome", "/Game/DefaultLots/")`.
   - This creates a `ULotDataAsset` in your Content Browser.

3. **Or export as JSON:**
   - Call `LotManager->ExportLotToFile("C:/MyLots/StarterHome.json")`.
   - Import later with `ImportLotFromFile()`.

4. **Customize the asset:**
   - Set `Thumbnail`, `Category`, `Tags`, and `Price` on the data asset.

5. **Load at runtime:**
   ```cpp
   // In Blueprint or C++
   ULotDataAsset* StarterHome = LoadObject<ULotDataAsset>(nullptr, TEXT("/Game/DefaultLots/DA_StarterHome"));
   LotManager->LoadDefaultLot(StarterHome);
   ```

### 6.5 Catalog Categories & Subcategories

To organize items in your catalog UI:

1. **Create a category:** Right-click → Data Asset → **CatalogCategory** (e.g., `CC_Seating`).
   - Set `DisplayName`, `Icon`, `SortOrder` (lower numbers appear first).

2. **Create subcategories** (optional): Right-click → Data Asset → **CatalogSubcategory**.
   - Set `DisplayName`, `SortOrder`.

3. **Assign to items:** On each `UCatalogItem`, set `Category` and optionally `Subcategory`.

4. The `CatalogSubsystem` auto-discovers and organizes everything.

---

## 7. Blueprint API Reference

### 7.1 Pool System

**ABuildPoolTool** (extends ABuildRoomTool):

| Node | Description |
|---|---|
| `DefaultWaterMaterial` | Set the water material for new pools. |
| `PoolFloorPattern` / `PoolFloorMaterial` | Set the pool bottom appearance. |
| `WaterSurfaceZOffset` | Adjust how far below ground the water surface sits. |

**UWaterComponent** (on LotManager):

| Node | Description |
|---|---|
| `Generate Pool Water` | Create a water volume for a room. |
| `Update Pool Water` | Update when room shape changes. |
| `Remove Pool Water` | Delete a pool's water. |
| `Has Pool Water` | Check if a room has water. |
| `Destroy All Water` | Nuclear option — removes all water. |

### 7.2 Fence & Gate System

**ABuildFenceTool**:

| Node | Description |
|---|---|
| `Current Fence Item` | Set before placement to define which fence to build. |

**UFenceComponent** (on LotManager):

| Node | Description |
|---|---|
| `Generate Fence Segment` | Create a fence between two points. Returns index. |
| `Remove Fence Segment` | Delete a fence by index. |
| `Find Fence Segment At Location` | Look up a fence near a world position. |
| `Add Gate To Fence` | Place a gate on a fence (regenerates panels). |
| `Remove Gate From Fence` | Remove a gate (regenerates panels). |

**AGateBase**:

| Node | Description |
|---|---|
| `On Gate Placed` | Call after spawning to integrate with fence. |

### 7.3 Stairs System

**ABuildStairsTool**:

| Node | Description |
|---|---|
| `Create Stairs Preview` | Spawn the ghost staircase for placement. |
| `Create Stairs` | Commit the preview stairs to the lot. |
| `Stairs Actor Class` | Set to your custom stairs Blueprint class. |

**AStairsBase**:

| Node | Description |
|---|---|
| `Initialize Stairs` | Set up with location, direction, modules, meshes. |
| `Generate Stairs Mesh` | Build the procedural mesh. |
| `Update Stairs Mesh` | Rebuild after configuration changes. |
| `Commit Stairs` | Finalize placement and cut floor opening. |
| `Destroy Stairs` | Remove the staircase entirely. |
| `Enter Edit Mode` / `Exit Edit Mode` | Show/hide adjustment gizmos. |
| `Rotate Left` / `Rotate Right` | 90° rotation. |
| `Validate Placement` | Check if current position is valid. |
| `Cut Away Stair Opening` | Manually cut the upper floor. |
| `Restore Stair Opening` | Undo the floor cut (for deletion). |
| `Get Bottom Landing Tile` / `Get Top Landing Tile` | Grid positions for validation. |
| `Get Bottom Level Footprint` / `Get Top Level Footprint` | All tiles occupied by the staircase. |

### 7.4 Object Placement System

**ABuyObjectTool**:

| Node | Description |
|---|---|
| `Current Catalog Item` | Set the `UFurnitureItem` to place. |
| `bValid Placement` | Read to check if current position is valid. |
| `Trace For Wall` | Raycast for wall surfaces. |
| `Calculate Wall Mount Position` | Get world position on a wall. |
| `Calculate Wall Mount Rotation` | Get rotation facing away from wall. |
| `Check Adjacent Walls` | Check if tile has a wall next to it. |
| `Get Adjacent Wall Normals` | Get all wall normals near a tile. |
| `Calculate Ceiling Height` | Get ceiling Z for a level. |
| `Check Ceiling Exists` | Verify ceiling at a tile. |

### 7.5 Save/Load System

**ALotManager**:

| Node | Description |
|---|---|
| `Save Lot To Slot` | Save to binary .sav file. |
| `Load Lot From Slot` | Load from binary .sav file. |
| `Export Lot To File` | Export to JSON. |
| `Import Lot From File` | Import from JSON. |
| `Load Default Lot` | Load a `ULotDataAsset`. |

**ULotSerializationSubsystem** (GameInstance subsystem):

| Node | Description |
|---|---|
| `Serialize Lot` | Convert a lot to `FSerializedLotData`. |
| `Deserialize Lot` | Apply `FSerializedLotData` to a lot. |
| `Validate Lot Data` | Check data integrity. |
| `Export To JSON` | Write serialized data to a JSON file. |
| `Import From JSON` | Read serialized data from a JSON file. |

### 7.6 Catalog System

**UCatalogSubsystem** (GameInstance subsystem):

| Node | Description |
|---|---|
| `Get Root Categories` | All top-level categories (sorted). |
| `Get Subcategories` | Subcategories under a category. |
| `Get Items In Category` | All items in a category (loads on demand). |
| `Get Items In Subcategory` | Items in a specific subcategory. |
| `Get Item Paths In Category` | Fast — returns paths without loading. |
| `Search Items By Name` | Case-insensitive partial match. |
| `Load Item` / `Load Items` | Load catalog items on demand from paths. |
| `Refresh Catalog` | Rescan Asset Registry (useful in editor). |

---

## Quick Start Checklist

- [ ] **Pools:** Assign `DefaultWaterMaterial` and `PoolFloorMaterial` on your `ABuildPoolTool` instance
- [ ] **Fences:** Create at least one `UFenceItem` data asset with panel + post meshes
- [ ] **Gates:** Create `UGateItem` data assets for any gate styles; ensure skeleton matches door rig
- [ ] **Stairs:** Set `StairsActorClass`, `StairTreadMesh`, and `StairLandingMesh` on `ABuildStairsTool`; verify mesh sockets
- [ ] **Furniture:** Extend `APlaceableObject` for each item; create `UFurnitureItem` with `PlacementRules`
- [ ] **Catalog:** Create `UCatalogCategory` assets and assign them to all your catalog items
- [ ] **Save/Load:** Test `SaveLotToSlot` / `LoadLotFromSlot` round-trip early in development
- [ ] **Default Lots:** Use `SaveAsDataAsset` in Editor to create `ULotDataAsset` for shipped lots

---

*This document covers BurbArchitect's pool, fence, stair, object, and save systems. For walls, floors, roofs, and terrain, see the companion user guides.*

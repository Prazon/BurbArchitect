# BurbArchitect — Getting Started & Overview

> **Version:** 1.0 · **Engine:** Unreal Engine 5.7 · **Replication:** Yes (Multiplayer-ready)

---

## What Is BurbArchitect?

BurbArchitect is a runtime house-building plugin for Unreal Engine 5 inspired by *The Sims*. It gives your players the ability to **draw walls, paint floors, place doors and windows, sculpt terrain, add roofs and stairs, and furnish rooms** — all at runtime, with full multiplayer replication out of the box.

Drop the plugin into your project, wire up a few base classes, populate a catalog with your art, and you have a complete architectural build mode ready for your game.

**Key features at a glance:**

- Grid-based lot system with configurable tile size and lot dimensions
- Wall drawing with automatic room detection (supports diagonal walls)
- Multi-story building with floors, basements, and stairs
- Procedural roof system (gable, hip, shed)
- Terrain sculpting tools (raise, lower, flatten, smooth)
- Sims-style wall cutaway camera (Walls Up → Cutaway → Floor Plan)
- Perspective and Isometric camera modes
- Data-asset-driven item catalog (furniture, doors, windows, fences, patterns)
- Save/Load system with slot-based saves and JSON import/export
- Pre-built lot data assets for shipping default houses
- Neighbourhood system for world-level lot placement
- Full network replication — build together in multiplayer

---

## Prerequisites

| Requirement | Details |
|---|---|
| **Engine** | Unreal Engine 5.7 |
| **Modules** | The plugin depends on: `ProceduralMeshComponent`, `UMG`, `GameplayTags`, `Json`, `JsonUtilities`, `Landscape`, `Foliage` — all ship with the engine. No third-party dependencies. |
| **Project type** | C++ or Blueprint (plugin is compiled C++ with full Blueprint exposure) |

---

## Installation & Project Setup

### 1. Install the Plugin

Copy (or install via Fab) the `BurbArchitect` folder into your project's `Plugins/` directory:

```
YourProject/
  Plugins/
    BurbArchitect/
      BurbArchitect.uplugin
      Source/
      Content/
      ...
```

Regenerate project files and compile (or simply restart the editor if using a Blueprint-only project with a pre-compiled binary).

### 2. Enable the Plugin

Open **Edit → Plugins**, search for **BurbArchitect**, and ensure it is enabled. Restart the editor if prompted.

### 3. Add the Module Dependency (C++ Projects)

If you need to reference BurbArchitect classes from your own C++ code, add it to your `.Build.cs`:

```csharp
PublicDependencyModuleNames.Add("BurbArchitect");
```

For Blueprint-only projects this step is not required — all key classes are already exposed to Blueprint.

### 4. Configure Your Game Mode

BurbArchitect ships a set of base classes designed to work together. The fastest way to get running is to **create Blueprint children** of each and assign them in your Game Mode.

| Base Class | What It Does |
|---|---|
| `ABurbGameModeBase` | Sets the starting gameplay mode (Build or Live) for new players |
| `ABurbPawn` | Camera pawn with spring arm, zoom, rotation, floor-level navigation, cutaway system |
| `ABurbPlayerController` | Cursor tracing, tool spawning, catalog activation, save/load helpers |
| `ABurbGameStateBase` | Game state stub — extend as needed for your game |

**Quick start (Blueprint):**

1. **Create child Blueprints:**
   - `BP_BurbGameMode` (parent: `BurbGameModeBase`)
   - `BP_BurbPawn` (parent: `BurbPawn`)
   - `BP_BurbPlayerController` (parent: `BurbPlayerController`)

2. **In BP_BurbGameMode**, set:
   - **Default Pawn Class** → `BP_BurbPawn`
   - **Player Controller Class** → `BP_BurbPlayerController`
   - **Starting Burb Mode** → `Build` (or `Live`, depending on your game flow)

3. **In World Settings** (or your Project Settings → Maps & Modes), set your default Game Mode to `BP_BurbGameMode`.

4. Press **Play**. You should spawn with the BurbPawn camera. No lot exists yet — read on to create one.

### 5. Place a Lot

Drag an `ALotManager` actor into your level (search for "LotManager" in the Place Actors panel). This is the central actor that represents a buildable lot.

Configure it in the Details panel:

| Property | Default | Description |
|---|---|---|
| **Lot Name** | *(empty)* | Display name for the lot |
| **Grid Size X** | `20` | Number of tiles along X (range: 8–100) |
| **Grid Size Y** | `20` | Number of tiles along Y (range: 8–100) |
| **Grid Tile Size** | `100` | Size of each tile in Unreal units (cm). 100 = 1 meter |
| **Floors** | `1` | Number of above-ground floors |
| **Basements** | `0` | Number of basement levels |
| **Default Wall Height** | `300` | Wall height in cm per floor |

The grid generates automatically on construction. You can also call `GenerateGrid()` at runtime from Blueprint.

> **Tip:** A 20×20 lot at tile size 100 gives you a 20m × 20m buildable area — a comfortable starter home footprint.

---

## Core Classes Overview

### ALotManager — The Lot

The LotManager is the **heart of BurbArchitect**. Each lot in your world is one LotManager actor. It owns:

- **GridComponent** — procedural mesh that renders the tile grid
- **WallComponent / WallGraphComponent** — wall geometry and the graph structure connecting walls
- **FloorComponent** — floor tile geometry
- **TerrainComponent** — sculptable terrain mesh
- **RoomManagerComponent** — automatic room detection from enclosed walls
- **RoofActors** — spawned roof actors (gable, hip, shed)
- **StairsActors** — stairs connecting floors
- **WaterComponent** — pools / water features
- **FenceComponent** — decorative fencing

**Key Blueprint-callable functions:**

| Function | Purpose |
|---|---|
| `GenerateGrid()` | Rebuild the entire grid (data + visuals) |
| `GenerateGridData()` | Rebuild tile data only (no visuals — call at BeginPlay for runtime-only grids) |
| `GenerateGridMesh()` | Rebuild just the visual grid mesh |
| `LocationToTile(Location, Row, Column)` | Convert a world position to grid coordinates |
| `TileToGridLocation(Level, Row, Column, bCenter, Location)` | Convert grid coordinates back to world position |
| `ToggleGrid(bVisible)` | Show/hide the grid for the current level |
| `SetCurrentLevel(NewLevel)` | Change the active floor level |
| `SaveLotToSlot(SlotName)` / `LoadLotFromSlot(SlotName)` | Slot-based save/load |
| `ExportLotToFile(FilePath)` / `ImportLotFromFile(FilePath)` | JSON file export/import |
| `LoadDefaultLot(LotDataAsset)` | Load a pre-built lot from a data asset |
| `ClearLot()` | Reset the lot to an empty state |
| `BuildRoomCache()` | Force a room detection recalculation |
| `GetRoomAtTile(Level, Row, Column)` | Query which room a tile belongs to |

### ABurbPawn — The Player Camera

The BurbPawn is the player's eyes. It provides:

- **Spring arm + camera** with configurable zoom range, rotation speed, and pitch limits
- **Perspective and Isometric camera modes** — toggle with `ToggleCameraMode()`
- **Floor level navigation** — `MoveUpFloor()` / `MoveDownFloor()` smoothly transitions the camera between stories
- **Wall cutaway system** — cycles through four modes (Walls Up → Partial Interiors → Walls Cutaway → Full Cutaway)
- **Current mode tracking** (`CurrentMode`) and the `OnModeChanged` delegate
- **Build tool management** — holds a reference to the active `ABuildTool`

**Important Blueprint properties on BurbPawn:**

| Property | Category | Description |
|---|---|---|
| `CurrentMode` | Mode | Current gameplay mode (`Build` or `Live`) |
| `CurrentLot` | — | Reference to the lot this pawn is building on |
| `CurrentLevel` | — | Active floor level |
| `CutawayMode` | Cutaway | Active wall cutaway mode |
| `CameraMode` | Camera > Mode | `Perspective` or `Isometric` |
| `DefaultToolClass` | Build Tools | Default tool spawned when no tool is active (e.g., Selection Tool) |
| `DeletionToolClass` | Build Tools | Tool spawned for deletion mode |
| `CameraZoomDefault` / `CameraMinZoom` / `CameraMaxZoom` | Camera > Zoom | Zoom range |
| `CameraMinPitch` / `CameraMaxPitch` | Camera > Rotation | Pitch clamps |
| `IsometricCameraPitch` | Camera > Isometric | Fixed pitch angle in isometric mode |

**Delegates:**

- `OnModeChanged(OldMode, NewMode)` — fires when the player switches between Build and Live mode. Bind to this in your UI to show/hide build panels.

### ABurbPlayerController — Input & Tool Spawning

The player controller handles:

- **Cursor tracing** — performs hit tests each tick and exposes `CursorWorldLocation` and `CursorWorldHitResult` to Blueprint
- **Catalog item activation** — `HandleCatalogItemActivation(CatalogItem)` detects the item type and spawns the correct build tool
- **Build tool creation RPCs** — `ServerTryCreateBuildTool()` and `ServerTryCreateBuildToolWithPattern()` handle multiplayer tool spawning
- **Save/Load shortcuts** — `QuickSave()`, `QuickLoad()`, `SaveLotToSlot()`, `LoadLotFromSlot()`, `ExportLotToJSON()`, `ImportLotFromJSON()`
- **Deletion** — `BroadcastDeleteToSelected()` deletes all currently selected objects

### ABurbGameModeBase — Game Mode

Minimal but important. Its main job is setting `StartingBurbMode` on newly spawned pawns via `RestartPlayer()`. Set this to `Build` if players should enter build mode immediately, or `Live` if they start in gameplay mode and opt into building.

---

## The Lot & Grid System

### How the Grid Works

Every lot is built on a **2D tile grid** that extends vertically across multiple floor levels. Each tile is a square of `GridTileSize` units (default 100 cm = 1 meter).

```
Grid Coordinates:
  Row    → X axis (0 to GridSizeX-1)
  Column → Y axis (0 to GridSizeY-1)
  Level  → Z axis (negative for basements, 0 for ground, positive for upper floors)
```

Tiles are stored in a flat `TArray<FTileData>` on the LotManager, with a spatial hash map (`TileIndexMap`) for fast O(1) lookups by `(Row, Column, Level)`.

Each `FTileData` contains:
- Grid coordinates and world-space corner/center positions
- **Triangle ownership** — which room each sub-triangle of the tile belongs to (supports tiles split by diagonal walls)
- Corner height indices for terrain elevation

### Multi-Story Building

- **Floors** — set via the `Floors` property on LotManager (number of above-ground stories)
- **Basements** — set via the `Basements` property (number of below-ground levels)
- **Level numbering** — basements are negative levels, ground floor is 0, upper floors are positive
- When a room's ceiling is completed, tiles automatically generate on the floor above to support continued building upward

### Room Detection

Rooms are detected automatically when walls form an enclosed area. The system uses flood-fill from the wall graph and supports **diagonal walls** splitting tiles between rooms. You don't need to manually define rooms — just draw walls and the plugin figures it out.

Call `BuildRoomCache()` after bulk wall operations, or `InvalidateRoom(RoomID)` to refresh a specific room. Query room membership with `GetRoomAtTile(Level, Row, Column)`.

---

## Build Mode vs Live Mode

BurbArchitect defines two gameplay modes via the `EBurbMode` enum:

| Mode | Description |
|---|---|
| **Build** | Full construction mode. The grid is visible, build tools are active, and the player can draw walls, place floors, sculpt terrain, place furniture — the full building experience. |
| **Live** | Simulation/gameplay mode. The grid and lot boundaries are hidden. Building tools are deactivated. This is where your game's actual gameplay happens (Sim-style living, strategy, etc.). |

**Switching modes from Blueprint:**

```
// On BurbPawn:
SetMode(EBurbMode::Build)    // Enter build mode
SetMode(EBurbMode::Live)     // Enter live mode
ToggleMode()                 // Flip between the two
```

The `OnModeChanged` delegate fires whenever the mode changes — bind your UI to this to show/hide the build toolbar, catalog panel, etc.

When switching to Live mode, the LotManager automatically hides the grid and boundary lines. When switching back to Build, they reappear.

---

## Build Tools

Build tools are the player's instruments for construction. Every tool extends `ABuildTool` and follows a consistent interaction pattern:

1. **Click** — begin a placement or start a drag operation
2. **Move** — update the tool's preview position each frame
3. **Drag** — extend a drag operation (walls, rooms, floors)
4. **Release** — commit the build operation
5. **Delete** — remove placed elements
6. **RotateLeft / RotateRight** — rotate the current placement

All of these functions are `BlueprintNativeEvent` — you can override them in Blueprint child classes for custom behavior. They are also fully replicated (server-authoritative with multicast broadcasts).

### Included Build Tools

| Tool | File | What It Does |
|---|---|---|
| **BuildWallTool** | `BuildTools/BuildWallTool.h` | Draw walls between tile corners |
| **BuildRoomTool** | `BuildTools/BuildRoomTool.h` | Drag-draw a rectangular room (walls + floor in one operation) |
| **BuildDiagonalRoomTool** | `BuildTools/BuildDiagonalRoomTool.h` | Room tool supporting diagonal walls |
| **BuildFloorTool** | `BuildTools/BuildFloorTool.h` | Paint floor tiles with patterns |
| **BuildDoorTool** | `BuildTools/BuildDoorTool.h` | Place doors in walls |
| **BuildWindowTool** | `BuildTools/BuildWindowTool.h` | Place windows in walls |
| **BuildRoofTool** | `BuildTools/BuildRoofTool.h` | Place and configure roofs |
| **BuildStairsTool** | `BuildTools/BuildStairsTool.h` | Place stairs between floor levels |
| **BuildFenceTool** | `BuildTools/BuildFenceTool.h` | Place decorative fences |
| **BuildHalfWallTool** | `BuildTools/BuildHalfWallTool.h` | Half-height walls |
| **BuildBasementTool** | `BuildTools/BuildBasementTool.h` | Excavate basements |
| **BuildPoolTool** | `BuildTools/BuildPoolTool.h` | Create pool areas |
| **BuildPortalTool** | `BuildTools/BuildPortalTool.h` | Generic portal placement (base for doors/windows) |
| **BuildGateTool** | `BuildTools/BuildGateTool.h` | Place gates in fences |
| **BuyObjectTool** | `BuyTools/BuyObjectTool.h` | Place furniture and objects |
| **BrushRaiseTool** | `BrushTools/BrushRaiseTool.h` | Raise terrain |
| **BrushLowerTool** | `BrushTools/BrushLowerTool.h` | Lower terrain |
| **BrushFlattenTool** | `BrushTools/BrushFlattenTool.h` | Flatten terrain to a height |
| **BrushFlattenLotTool** | `BrushTools/BrushFlattenLotTool.h` | Flatten entire lot |
| **BrushSmoothTool** | `BrushTools/BrushSmoothTool.h` | Smooth terrain edges |

Each tool has `ValidMaterial` and `InvalidMaterial` properties for placement preview feedback, plus optional sound effects (`MoveSound`, `CreateSound`, `DeleteSound`, `FailSound`).

### Equipping Tools

Tools are spawned and assigned via the pawn:

```
// From Blueprint on BurbPawn:
SetCurrentBuildTool(BuildToolReference)

// Or let the system auto-equip via:
EnsureToolEquipped()          // Spawns DefaultToolClass if nothing is active
EnsureDeletionToolEquipped()  // Spawns DeletionToolClass
```

The player controller's `HandleCatalogItemActivation()` is the primary way tools get equipped — it reads the catalog item type and spawns the right tool automatically.

---

## The Catalog & Data Asset System

BurbArchitect uses **UDataAsset** classes to define every item that appears in your build/buy catalog. This makes it easy to add new content without touching code.

### Catalog Hierarchy

```
UCatalogCategory          (Root category — e.g., "Comfort", "Surfaces", "Lighting")
  └─ UCatalogSubcategory  (Nested sub-category — e.g., "Seating" under "Comfort")

UCatalogItem              (Base item — display name, icon, cost, category)
  ├─ UArchitectureItem    (Build tools — walls, rooms, stairs, roofs)
  ├─ UFurnitureItem       (Placeable objects — chairs, tables, lamps)
  ├─ UFloorPattern        (Floor surface materials)
  ├─ UWallPattern         (Wall surface materials)
  ├─ UDoorItem            (Doors with portal size, snapping, meshes)
  ├─ UWindowItem          (Windows with portal size, snapping, meshes)
  ├─ UFenceItem           (Fences with panel/post meshes and spacing)
  └─ UGateItem            (Gates for fence openings)
```

### Creating a Catalog Category

1. In Content Browser: **Right-click → Miscellaneous → Data Asset**
2. Select `CatalogCategory` as the class
3. Name it (e.g., `DA_Cat_Comfort`)
4. Set `DisplayName`, `Icon`, `SortOrder`, and optionally `Description`

For subcategories, create a `CatalogSubcategory` data asset and set its `ParentCategory` to the root category you created.

### Creating a Furniture Item

1. **Right-click → Miscellaneous → Data Asset → FurnitureItem**
2. Fill in the base catalog fields:
   - **DisplayName** — shown in the UI
   - **Icon** — thumbnail brush
   - **Cost** — in-game price
   - **Category** / **Subcategory** — where it appears in the catalog
3. Set the **ClassToSpawn** to your `APlaceableObject` Blueprint (the actual actor that gets placed in the world)
4. Configure **PlacementRules**:

| Property | Description |
|---|---|
| `PlacementMode` | `Floor`, `WallHanging`, `CeilingFixture`, or `Surface` |
| `GridSize` | Footprint in tiles (e.g., `(2, 1, 1)` for a couch) |
| `bMustBeAgainstWall` | Requires wall adjacency (beds, toilets) |
| `bCanPlaceOutdoors` | Whether it can be placed outside rooms |
| `WallMountHeight` | Height for wall-hanging items (cm) |
| `WallOffset` | Distance from wall surface (cm) |
| `CeilingOffset` | Distance from ceiling for ceiling fixtures (cm) |
| `bRequiresFloor` | Must have a floor tile underneath |

### Creating a Floor or Wall Pattern

1. **Right-click → Miscellaneous → Data Asset → FloorPattern** (or `WallPattern`)
2. Set catalog fields (name, icon, cost, category)
3. Set the **BuildToolClass** to your floor/wall painting tool Blueprint
4. Assign textures:
   - `BaseTexture` — diffuse/albedo
   - `NormalMap` — surface normals
   - `RoughnessMap` — roughness
   - `DetailNormal` (optional) — extra surface detail with intensity control
   - `BaseMaterial` (optional) — material template override
5. Optionally enable **Color Swatches**:
   - Set `bUseColourSwatches = true`
   - Add colors to the `ColourSwatches` array
   - Enable `bUseColourMask` if your RMA texture has an alpha channel mask for recoloring

### Creating a Door or Window Item

1. **Right-click → Miscellaneous → Data Asset → DoorItem** (or `WindowItem`)
2. Set catalog fields and `BuildToolClass`
3. Configure portal properties:
   - `PortalSize` — width × height of the opening in cm
   - `PortalOffset` — position offset from the wall placement point
   - `HorizontalSnap` / `VerticalSnap` — snap increments when placing
   - `bSnapsToFloor` — whether the portal anchors to floor level
4. Set `ClassToSpawn` to your door/window Blueprint (extends `APortalBase`)
5. For doors: assign `DoorStaticMesh` and `DoorFrameMesh`
6. For windows: assign `WindowMesh`

### Creating a Fence Item

1. **Right-click → Miscellaneous → Data Asset → FenceItem**
2. Set catalog fields and `BuildToolClass`
3. Assign `FencePanelMesh` and `FencePostMesh`
4. Configure post placement rules (`PostSpacing`, `bPostsAtCorners`, `bPostsAtEnds`, `bPostsAtJunctions`)
5. Set `FenceHeight` and `PanelWidth` dimensions

### Activating Catalog Items at Runtime

When the player selects an item from your catalog UI, call:

```
// On BurbPlayerController:
HandleCatalogItemActivation(CatalogItem)
```

This function inspects the item's type and automatically:
- Spawns the correct `ABuildTool` subclass
- Assigns pattern data (for floor/wall patterns)
- Configures the tool for the selected item

Your UI just needs to hold references to your catalog data assets and call this one function.

---

## Wall Cutaway System

The cutaway system provides Sims-style wall visibility modes. Cycle through them with `NextCutawayMode()` / `PreviousCutawayMode()` on the BurbPawn, or set directly with `SetCutawayMode()`.

| Mode | Enum Value | Behavior |
|---|---|---|
| **Walls Up** | `ECutawayMode::WallsUp` | All walls and roofs visible |
| **Partial Interiors** | `ECutawayMode::PartialInteriors` | Interior walls visible, exterior walls facing camera hidden, roofs hidden |
| **Walls Cutaway** | `ECutawayMode::Partial` | All walls facing camera hidden (interior + exterior), roofs hidden |
| **Full Cutaway** | `ECutawayMode::Full` | All walls hidden — floor plan view, roofs hidden |

The `CutawayFacingThreshold` property (range: -1.0 to 1.0) controls how aggressively walls are culled based on camera direction. Higher values hide more walls.

Cutaway updates automatically as the camera rotates, using a sector-based system (45° sectors) to avoid per-frame recalculations.

---

## Camera Modes

BurbPawn supports two camera modes:

### Perspective Mode (Default)
- Free mouse rotation (horizontal + vertical)
- Scroll wheel zoom (adjusts spring arm length)
- Configurable pitch limits (`CameraMinPitch` / `CameraMaxPitch`)

### Isometric Mode
- Fixed pitch angle (`IsometricCameraPitch`)
- Four cardinal rotation angles (90° increments) — `RotateIsometricLeft()` / `RotateIsometricRight()`
- Orthographic projection with zoom via `OrthoWidth`
- Configurable zoom range (`IsometricMinOrthoWidth` / `IsometricMaxOrthoWidth`)

Toggle between modes with `ToggleCameraMode()` or set directly with `SetCameraMode(ECameraMode::Isometric)`.

---

## Save / Load System

BurbArchitect provides multiple ways to persist lot data:

### Slot-Based Saves (UE SaveGame)

```
// Save:
LotManager->SaveLotToSlot("MyHouse")

// Load:
LotManager->LoadLotFromSlot("MyHouse")

// Or via PlayerController shortcuts:
PlayerController->QuickSave()   // saves to "QuickSave" slot
PlayerController->QuickLoad()   // loads from "QuickSave" slot
```

### JSON Export/Import (File-Based)

Useful for sharing lots between players or debugging:

```
LotManager->ExportLotToFile("C:/MyLots/Mansion.json")
LotManager->ImportLotFromFile("C:/MyLots/Mansion.json")
```

### Pre-Built Lot Data Assets

Ship default houses with your game using `ULotDataAsset`:

1. Build a lot in-editor
2. Use `LotManager->SaveAsDataAsset("DA_StarterHome", "/Game/DefaultLots/")` (Editor only, available in the Details panel)
3. At runtime: `LotManager->LoadDefaultLot(MyLotDataAsset)`

`ULotDataAsset` supports metadata — `Category`, `Tags`, `Price`, and `Thumbnail` — for building lot selection UIs.

---

## Configuration Reference

### LotManager Properties

| Property | Category | Default | Description |
|---|---|---|---|
| `GridSizeX` / `GridSizeY` | — | `20` | Lot dimensions in tiles (8–100) |
| `GridTileSize` | — | `100` | Tile size in cm |
| `DefaultWallHeight` | Building | `300` | Wall height per floor (100–500 cm) |
| `Floors` | — | `1` | Above-ground floor count |
| `Basements` | — | `0` | Below-ground level count |
| `bRemoveTerrainUnderFloors` | Building | `true` | Auto-remove terrain geometry under floor tiles |
| `bRestoreTerrainOnFloorRemoval` | Building | `true` | Restore terrain when floor tiles are deleted |
| `bAllowTerrainCeilings` | Building | `true` | Use terrain as basement ceilings (more efficient) |
| `BasementCeilingOffset` | Building | `2.0` | Z-offset below terrain to prevent z-fighting (0–10) |
| `bEnableTerrainGeneration` | Feature Flags | `true` | Toggle terrain system on/off |
| `bEnableRoofGeneration` | Feature Flags | `true` | Toggle roof system on/off |
| `DefaultWallMaterial` | — | — | Material applied to new walls |
| `DefaultFloorMaterial` | — | — | Material applied to new floors |
| `DefaultTerrainMaterial` | — | — | Material applied to terrain |
| `ValidPreviewMaterial` | — | — | Green preview material for valid placement |
| `InvalidPreviewMaterial` | — | — | Red preview material for invalid placement |
| `GridMaterial` | — | — | Material for the grid mesh (renders lines procedurally) |
| `LineColour` | — | — | Grid line color |
| `LineOpacity` | — | — | Grid line opacity |

### BurbPawn Properties

| Property | Category | Default | Description |
|---|---|---|---|
| `DefaultCameraMode` | Camera > Mode | `Perspective` | Starting camera mode |
| `CameraRotationSpeed` | Camera > Rotation | — | Mouse rotation speed |
| `CameraDefaultRotation` | Camera > Defaults | — | Initial camera rotation |
| `CameraMaxZoom` / `CameraMinZoom` | Camera > Zoom | — | Zoom range (spring arm length) |
| `CameraZoomDefault` | Camera > Defaults | — | Starting zoom level |
| `CameraZoomIncrementValue` | Camera > Zoom | — | Zoom step per scroll tick |
| `CameraMinPitch` / `CameraMaxPitch` | Camera > Rotation | — | Pitch angle limits |
| `IsometricCameraPitch` | Camera > Isometric | — | Fixed pitch in iso mode |
| `IsometricOrthoWidth` | Camera > Isometric | — | Starting ortho width |
| `IsometricMinOrthoWidth` / `IsometricMaxOrthoWidth` | Camera > Isometric | — | Ortho zoom range |
| `CutawayFacingThreshold` | Cutaway | `-0.3` | How aggressively walls are hidden (-1.0 to 1.0) |
| `ZInterpSpeed` | Camera > Movement | — | Camera vertical interpolation speed when changing floors |

### Neighbourhood Properties (on LotManager)

| Property | Category | Description |
|---|---|---|
| `ParentNeighbourhood` | Neighbourhood | Reference to the `ANeighbourhoodManager` actor |
| `NeighbourhoodOffsetRow` / `NeighbourhoodOffsetColumn` | Neighbourhood | Lot position in neighbourhood grid coordinates |
| `bIsPlacedOnNeighbourhood` | Neighbourhood | Read-only — whether the lot is placed on a neighbourhood |

---

## Common Blueprint Workflows

### Switching from Build to Live Mode

```
Event BeginPlay
  → Get BurbPawn
  → Bind Event to OnModeChanged
    → [Custom Event: UpdateUI]
      → Branch on NewMode
        → Build: Show Build Panel, Show Catalog
        → Live: Hide Build Panel, Hide Catalog
```

### Responding to Floor Level Changes

```
// In your HUD Blueprint:
Event Tick (or bind to a delegate)
  → Get BurbPawn → CurrentLevel
  → Update Floor Indicator Text ("Floor: {Level}")
```

### Loading a Default Lot on Game Start

```
Event BeginPlay
  → Get LotManager reference
  → Load Default Lot (DA_StarterHome)
  → Branch on Return Value
    → True: Print "Lot loaded!"
    → False: Print "Failed to load lot"
```

### Hooking Up a Catalog Button

```
// On your catalog button widget:
Event On Clicked
  → Get Player Controller (cast to BurbPlayerController)
  → Handle Catalog Item Activation (pass your UCatalogItem data asset reference)
```

---

## Multiplayer Notes

BurbArchitect is built with multiplayer in mind:

- **LotManager** replicates `LotName`, `GridSizeX/Y`, `Floors`, `Basements`, and `CurrentLevel`
- **BurbPawn** replicates `CurrentMode`, `CurrentLot`, `CurrentLevel`, `CurrentBuildTool`, `DisplayName`, and `CameraMode`
- **BuildTool** operations use Server RPCs (`ServerClick`, `ServerMove`, `ServerDrag`, `ServerRelease`) with Multicast broadcasts to all clients
- **PlayerController** tool creation uses Server RPCs (`ServerTryCreateBuildTool`, `ServerTryCreateBuildToolWithPattern`)

The general pattern: client initiates → server validates and executes → multicast broadcasts the result. You don't need to set up replication yourself — it's handled by the base classes.

---

## Next Steps

- **[Build Tools Deep Dive](./UserGuide_BuildTools.md)** — detailed guide for each build tool
- **[Catalog System Guide](./UserGuide_Catalog.md)** — creating and managing your item catalog
- **[Neighbourhood System](./UserGuide_Neighbourhoods.md)** — world-level lot management
- **[Customization & Theming](./UserGuide_Customization.md)** — materials, patterns, and visual customization
- **[Save/Load & Serialization](./UserGuide_SaveLoad.md)** — in-depth save system guide

---

*BurbArchitect is developed by Brendan Miller-Young. For support, visit our [Discord](https://discord.com/invite/5qQjp9Zyjj) or [GitHub](https://github.com/Prazon/BurbArchitect).*

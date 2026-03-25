# BurbArchitect — Floors, Terrain & Basements

> **Audience:** Game developers integrating BurbArchitect into an Unreal Engine 5 project.
> **Prerequisite:** You should already have a `LotManager` placed in your level with a valid grid. See the *Getting Started* guide if you haven't done that yet.

---

## Table of Contents

1. [Concepts Overview](#1-concepts-overview)
2. [The Grid System](#2-the-grid-system)
3. [The Level System](#3-the-level-system)
4. [Floor Placement Tool](#4-floor-placement-tool)
5. [Floor Patterns & Materials](#5-floor-patterns--materials)
6. [Terrain Tools](#6-terrain-tools)
7. [Basement Creation Tool](#7-basement-creation-tool)
8. [Blueprint API Reference](#8-blueprint-api-reference)
9. [Tips & Troubleshooting](#9-tips--troubleshooting)

---

## 1. Concepts Overview

BurbArchitect uses a **tile-based grid** where every surface — floors, terrain, rooms — is defined on a uniform grid of square tiles. Each tile is further subdivided into **four triangles** (Top, Right, Bottom, Left) which allows the system to handle diagonal walls, partial floors, and per-triangle pattern assignment.

The key components you'll interact with:

| Component | Role |
|---|---|
| `UGridComponent` | Renders the visual build grid. One draw call per level. |
| `UFloorComponent` | Manages all floor geometry as merged procedural meshes. |
| `UTerrainComponent` | Manages terrain surface geometry and height maps. |
| `ABuildFloorTool` | Player-facing tool for placing and painting floors. |
| `ABuildBasementTool` | Specialized room tool for building below ground. |
| `ABrushRaiseTool` / `ABrushLowerTool` / etc. | Terrain sculpting brush tools. |

All of these live on or are spawned by the **LotManager** actor (`ALotManager`).

---

## 2. The Grid System

### How the Grid Works

The grid is defined by three properties on your `LotManager`:

| Property | Type | Description |
|---|---|---|
| `GridSizeX` | `int32` | Number of tiles along the X axis. |
| `GridSizeY` | `int32` | Number of tiles along the Y axis. |
| `GridTileSize` | `float` | World-unit size of each square tile (default: 100 cm). |

The `UGridComponent` renders this grid as a **merged procedural mesh** — one mesh section per level — driven by a material that draws grid lines procedurally. This keeps draw calls minimal even on large lots.

### Per-Level Grid Visibility

Each floor level has its own grid mesh section. You can show/hide individual levels:

```cpp
// Show only the current level's grid
GridComponent->SetLevelVisibility(2, true);   // Show level 2
GridComponent->SetLevelVisibility(1, false);  // Hide level 1
```

Or toggle the entire grid:

```cpp
GridComponent->SetGridVisibility(false);                 // Hide grid (keep boundary lines)
GridComponent->SetGridVisibility(false, true);           // Hide grid AND boundary lines (live mode)
```

### Tile Coordinates

Every tile is addressed by **(Row, Column, Level)**. Conversion between world positions and tile coordinates goes through the `LotManager`:

```cpp
int32 Row, Column;
bool bFound = LotManager->LocationToTile(WorldPosition, Row, Column);

FVector TileCenter;
LotManager->TileToGridLocation(Level, Row, Column, /*bCenter=*/true, TileCenter);
```

### The Triangle Subdivision

Each tile is divided into four triangles by its diagonals. This is fundamental to how BurbArchitect handles diagonal walls — a single tile can be split between two rooms, with each triangle independently assigned to a room, floor pattern, or state.

```
     TopLeft -------- TopRight
         |  \  Top  /  |
         |   \     /   |
         | L  \ C /  R |      C = center vertex
         |     \ /     |      T/R/B/L = triangle quadrants
         |     / \     |
         |   / Bot \   |
         |  /       \  |
     BotLeft -------- BotRight
```

Triangle types are defined in `ETriangleType`: `Top`, `Right`, `Bottom`, `Left`.

From the user's perspective, this subdivision is mostly invisible — floors look like solid tiles. It becomes relevant when diagonal walls split a tile or when you use the **Single Triangle** placement mode.

---

## 3. The Level System

BurbArchitect uses an integer-based level system where the **ground floor** is at index equal to the number of basements.

### Level Indexing

```
Level 0          → Bottom-most basement (if Basements ≥ 1)
Level 1          → Second basement (if Basements ≥ 2)
...
Level Basements  → Ground floor
Level Basements+1 → Second storey
Level Basements+2 → Third storey
...up to Basements + Floors
```

**Example:** If `Basements = 1` and `Floors = 2`:
- Level 0 = Basement
- Level 1 = Ground floor
- Level 2 = Second storey
- Level 3 = Third storey

### Switching Levels

Level switching is controlled through `LotManager->SetCurrentLevel(NewLevel)` and is typically driven by your game's UI. The `CurrentLevel` property on `LotManager` tracks the active level.

When a player switches levels:
- The grid updates to show only the current level's tiles.
- Floor meshes for other levels can be hidden/shown via `UFloorComponent::SetMeshSectionVisible`.
- The terrain is always ground-level only — it doesn't change with level.

### Basement View

When viewing basements, the `UBurbBasementViewSubsystem` automatically hides non-lot actors (trees, props, landscape) while preserving their shadow casting. This gives players a clean underground view while maintaining correct lighting.

```cpp
UBurbBasementViewSubsystem* BasementView = GetWorld()->GetSubsystem<UBurbBasementViewSubsystem>();
BasementView->SetBasementViewEnabled(true);   // Enter basement view
BasementView->SetBasementViewEnabled(false);  // Exit basement view
```

**Keep actors visible in basement view** by adding the gameplay tag `BasementView.AlwaysVisible` to any actor. Light actors and anything owned by the LotManager are automatically kept visible.

---

## 4. Floor Placement Tool

`ABuildFloorTool` is the primary tool for placing, replacing, and deleting floor tiles.

### Basic Placement (Drag-to-Paint)

1. **Click** on a tile to start placing.
2. **Drag** to paint a rectangular area of floor tiles.
3. **Release** to commit all tiles.

The tool creates preview meshes during the drag operation that show exactly what will be placed. Previews are rendered with a slight Z-offset (5 cm) to prevent Z-fighting with existing terrain or floors.

### Placement Modes

The tool supports two modes, toggled via the `PlacementMode` property:

| Mode | Enum Value | Behavior |
|---|---|---|
| **Full Tile** | `EFloorPlacementMode::FullTile` | Places all 4 triangles per tile (default). |
| **Single Triangle** | `EFloorPlacementMode::SingleTriangle` | Places only the triangle under the cursor. |

Toggle from Blueprint (for example, bind to Ctrl+F):

```cpp
FloorTool->PlacementMode = (FloorTool->PlacementMode == EFloorPlacementMode::FullTile)
    ? EFloorPlacementMode::SingleTriangle
    : EFloorPlacementMode::FullTile;
```

In Single Triangle mode, the tool tracks `CurrentHoveredTriangle` — which of the four triangles the cursor is over — and only places that triangle.

### Room Auto-Fill (Shift+Click)

Hold **Shift** while hovering over a room to see a preview of the entire room filled with the current floor pattern. **Shift+Click** commits the fill.

The auto-fill is room-aware: it respects diagonal wall boundaries and only fills triangles that are geometrically inside the room polygon (using centroid-based polygon testing for each triangle).

### Deletion Mode

Set `bDeletionMode = true` to switch the tool to deletion. In this mode, clicking/dragging removes floor tiles instead of placing them. The tool queries which triangles exist at each location and deletes all of them.

### Drag Boundary Enforcement

When dragging within a room, floor placement is constrained to that room. The `DragStartRoomID` is captured on click, and only tiles belonging to the same room are included during the drag.

### Repainting Existing Floors

Dragging over tiles that already have floors will **update** their pattern rather than creating duplicate geometry. The system distinguishes between "create" and "update" operations internally and routes them through appropriate commands.

### Multiplayer Support

All floor operations are replicated:
- `Server_BuildFloors` / `Multicast_BuildFloors` — for creation and update
- `Server_DeleteFloors` / `Multicast_DeleteFloors` — for deletion

The local client creates preview meshes, then on release sends authoritative RPCs through the server to all clients.

---

## 5. Floor Patterns & Materials

### What Is a Floor Pattern?

`UFloorPattern` is a data asset (inheriting from `UArchitectureItem` → `UCatalogItem`) that defines the visual appearance of a floor tile. Each pattern contains:

| Property | Type | Description |
|---|---|---|
| `BaseTexture` | `UTexture*` | The diffuse/albedo texture. |
| `NormalMap` | `UTexture*` | Normal map for surface detail. |
| `bUseDetailNormal` | `bool` | Enable a secondary detail normal layer. |
| `DetailNormal` | `UTexture*` | Detail normal map texture. |
| `DetailNormalIntensity` | `float` | Strength of detail normals (0–32). |
| `RoughnessMap` | `UTexture*` | Roughness/smoothness map. |
| `BaseMaterial` | `UMaterialInstance*` | Optional per-pattern base material override. |
| `bUseColourSwatches` | `bool` | Enable color swatch recoloring. |
| `bUseColourMask` | `bool` | Use RMA alpha channel as color mask. |
| `ColourSwatches` | `TArray<FLinearColor>` | Array of available recolor tints. |

### Creating a Custom Floor Pattern

1. **Create a new Data Asset** of type `FloorPattern` in the Content Browser.
2. Assign your textures (`BaseTexture`, `NormalMap`, `RoughnessMap`).
3. Optionally configure the detail normal layer.
4. Optionally set up color swatches for recoloring support.
5. Assign the pattern to the floor tool's `DefaultFloorPattern` or add it to your catalog UI.

### How Patterns Are Applied to Materials

When a floor is placed, the tool creates a `UMaterialInstanceDynamic` from the base material and sets these parameters:

| Material Parameter | Type | Source |
|---|---|---|
| `FloorMaterial` | Texture | `Pattern->BaseTexture` |
| `FloorNormal` | Texture | `Pattern->NormalMap` |
| `FloorRoughness` | Texture | `Pattern->RoughnessMap` |
| `bUseColourSwatches` | Scalar (0/1) | Whether swatch tinting is active |
| `bUseColourMask` | Scalar (0/1) | Whether color mask channel is used |
| `FloorColour` | Vector (Linear Color) | The selected swatch color |
| `ShowGrid` | Scalar (0/1) | Grid line overlay visibility |

**Your base material must expose these parameter names** for patterns to work correctly.

### Color Swatches

The swatch system allows players to recolor a pattern:

- **Index 0** = default (no tint applied).
- **Index 1+** = maps to `ColourSwatches[Index - 1]`.

Set `SelectedSwatchIndex` on the floor tool to control which swatch is applied:

```cpp
FloorTool->SelectedSwatchIndex = 3;  // Use the third color swatch
```

When `bUseColourMask` is enabled, only the regions defined by the RMA texture's alpha channel are tinted — useful for patterns like hardwood where you want to recolor the wood but not the grout.

### Flood-Fill Pattern Detection

The floor tool includes **paint-bucket-style** pattern detection:

- `GetDominantPatternAtTile()` — Returns the most common pattern at a tile.
- `FloodFillPatternRegion()` — Finds all contiguous tiles with the same pattern (tile-level).
- `FloodFillPatternRegionTriangles()` — Triangle-level flood fill that correctly handles diagonal walls.

These are used internally for intelligent fill operations and are available for Blueprint use.

---

## 6. Terrain Tools

BurbArchitect includes five terrain sculpting tools. All terrain tools operate only on the **ground floor level** and use the `UTerrainComponent`'s height map system.

### Terrain Architecture

Terrain uses a **corner-based height map** (`FTerrainHeightMap`) where heights are stored at tile intersections (corners), not tile centers. This means:

- A 10×10 tile grid has an 11×11 corner grid.
- Adjacent tiles share corner vertices, ensuring seamless terrain.
- Heights are stored **relative to the lot's BaseZ** position.

The terrain mesh uses exactly two mesh sections:
- **Section 0:** Top surface (grass/terrain material, editable heights)
- **Section 1:** Sides + Bottom (cliff/dirt material, static geometry)

### Terrain Constraints

| Property | Default | Description |
|---|---|---|
| `TerrainThickness` | 600 | Downward extrusion depth (cm). |
| `MaxTerrainHeight` | 900 | Maximum height above lot base (cm). |
| `MinTerrainHeight` | -(Thickness - 10) | Maximum depth below lot base. |

### Raise Tool (`ABrushRaiseTool`)

Raises terrain within a circular brush. Click-and-hold to continuously raise.

| Property | Default | Description |
|---|---|---|
| `Radius` | 500 | Brush radius in world units. |
| `TimerDelay` | 0.1s | Interval between terrain updates while held. |

The raise amount per tick uses a **span-based falloff** calculated from the 9 surrounding tile centers, creating a smooth, natural-looking raise profile rather than hard edges.

### Lower Tool (`ABrushLowerTool`)

Identical to the Raise Tool but pushes terrain downward. Same properties and brush behavior.

| Property | Default | Description |
|---|---|---|
| `Radius` | 500 | Brush radius in world units. |
| `TimerDelay` | 0.1s | Interval between terrain updates while held. |

### Smooth Tool (`ABrushSmoothTool`)

Uses **Laplacian smoothing** to blend terrain heights toward the average of their neighbors. Great for softening harsh terrain edits.

| Property | Default | Description |
|---|---|---|
| `Radius` | 500 | Brush radius in world units. |
| `SmoothingStrength` | 0.5 | Blend factor: 0.0 = no change, 1.0 = full average. |
| `TimerDelay` | 0.1s | Interval between smoothing passes while held. |

The smoothing operates on the **corner grid**, not the tile grid, giving sub-tile precision.

### Flatten Tool (`ABrushFlattenTool`)

Flattens terrain within a brush radius to a **sampled target height**. On click, the tool samples the terrain height at the cursor position, then all corners within the radius are set to that height as you hold/drag.

| Property | Default | Description |
|---|---|---|
| `Radius` | 200 | Brush radius in world units. |
| `TimerDelay` | 0.1s | Interval between flatten updates while held. |

This is useful for creating flat building pads — click on the height you want, then paint the surrounding area flat.

### Flatten Lot Tool (`ABrushFlattenLotTool`)

A one-click tool that flattens the **entire lot** to a specified height. Inherits from `ABrushFlattenTool` but operates on every tile simultaneously.

| Property | Default | Description |
|---|---|---|
| `DefaultHeight` | 0.0 | Target height relative to lot BaseZ. |
| `bHideDecal` | true | Hides the brush circle (not needed for whole-lot ops). |

Use this as a "reset terrain" button or to establish a baseline height.

### Brush Visualization

All brush tools use a `UDecalComponent` projected downward to show the brush radius. The decal follows the cursor in real-time. Brush radius and visual size are linked — changing `Radius` automatically scales the decal.

### Automatic Terrain Flattening

The system automatically flattens terrain under placed floors and along wall segments. This prevents Z-fighting between terrain and floors and ensures clean building foundations:

```cpp
// These are called automatically by the build pipeline:
TerrainComponent->FlattenTerrainUnderFloor(Level, Row, Column, TileSectionState, TargetHeight);
TerrainComponent->FlattenTerrainAlongWall(Level, StartRow, StartCol, EndRow, EndCol, TargetHeight);
```

### Batch Operations

When sculpting terrain, the system uses batch operations to avoid per-tile mesh rebuilds:

```cpp
TerrainComponent->BeginBatchOperation();
// ... make many terrain changes ...
TerrainComponent->EndBatchOperation();  // Single rebuild for all changes
```

All built-in brush tools handle this automatically. If you write custom terrain manipulation, wrap your changes in batch operations for performance.

---

## 7. Basement Creation Tool

`ABuildBasementTool` extends `ABuildRoomTool` (the standard room drawing tool) to support building underground levels.

### How It Works

The basement tool behaves contextually based on which level you're viewing:

| Viewing Level | Build Target | Behavior |
|---|---|---|
| Ground floor (`Basements`) | One level below (top basement) | Draws basement walls while viewing the ground floor above. |
| Any basement level | Current level | Behaves as a normal room tool. |
| Upper floors | Current level | Behaves as a normal room tool. |

This means you can draw a basement layout **while looking at the ground floor**, which is more intuitive than switching to an empty underground view first.

### Usage

1. Select the Basement Tool.
2. While on the ground floor, **click and drag** to define a rectangular room.
3. On release, walls are created at the basement level.
4. Switch to the basement level to add floors, doors, and furnishings.

The tool snaps to the basement-level grid (offset by the level height) but renders wall previews that are visible from the ground floor perspective.

### Level Targeting Logic

The `GetTargetLevel()` helper determines where to build:

```
Ground floor level = LotManager->Basements (e.g., 1 if one basement configured)
If CurrentLevel == GroundFloorLevel → target = GroundFloorLevel - 1
Else → target = CurrentLevel
```

### Basement View System

When switching to view a basement level, enable the `UBurbBasementViewSubsystem` to get a clean underground view:

```cpp
auto* Sub = GetWorld()->GetSubsystem<UBurbBasementViewSubsystem>();
Sub->SetBasementViewEnabled(true);
```

**Actor filtering rules** (what stays visible in basement view):
1. LotManager and all BuildTool actors → always visible
2. Light actors (directional, point, spot, sky) → always visible
3. Actors with light components (lamps, fixtures) → always visible
4. Actors tagged with `BasementView.AlwaysVisible` → always visible
5. Everything else → hidden (but still casts shadows)

---

## 8. Blueprint API Reference

### Floor Operations

#### UFloorComponent

```cpp
// Place a floor triangle
UFUNCTION(BlueprintCallable)
void AddFloorTile(const FFloorTileData& TileData, UMaterialInstance* Material);

// Remove a floor triangle
void RemoveFloorTile(int32 Level, int32 Row, int32 Column, ETriangleType Triangle);

// Check if any floor exists at a tile
bool HasAnyFloorTile(int32 Level, int32 Row, int32 Column) const;

// Get which triangles exist at a tile
FTileSectionState GetExistingTriangles(int32 Level, int32 Row, int32 Column) const;

// Find a specific floor triangle (returns nullptr if not found)
FFloorTileData* FindFloorTile(int32 Level, int32 Row, int32 Column, ETriangleType Triangle) const;

// Get all triangles at a tile (up to 4)
TArray<FFloorTileData*> GetAllTrianglesAtTile(int32 Level, int32 Row, int32 Column);

// Batch operations (wrap bulk edits for performance)
UFUNCTION(BlueprintCallable)
void BeginBatchOperation();

UFUNCTION(BlueprintCallable)
void EndBatchOperation();
```

#### ABuildFloorTool

```cpp
// Change placement mode
UPROPERTY(BlueprintReadWrite, EditAnywhere)
EFloorPlacementMode PlacementMode;

// Set the active pattern
UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
UFloorPattern* DefaultFloorPattern;

// Set the active swatch (0 = default, 1+ = swatch index)
UPROPERTY(BlueprintReadWrite)
int32 SelectedSwatchIndex;

// Toggle deletion mode
UPROPERTY(BlueprintReadWrite, EditAnywhere)
bool bDeletionMode;

// Query tiles in a room
UFUNCTION(BlueprintCallable)
TArray<FTileData> GetTilesInRoom(int32 RoomID, int32 Level);

// Get dominant pattern at a tile
UFUNCTION(BlueprintCallable)
UFloorPattern* GetDominantPatternAtTile(int32 Level, int32 Row, int32 Column);

// Flood-fill to find all tiles with matching pattern
UFUNCTION(BlueprintCallable)
TArray<FIntVector> FloodFillPatternRegion(int32 Level, int32 StartRow, int32 StartColumn,
                                          UFloorPattern* TargetPattern, int32 RoomID);

// Create a patterned dynamic material
UFUNCTION(BlueprintCallable)
UMaterialInstanceDynamic* CreatePatternedMaterial(UMaterialInstance* BaseMaterial, UFloorPattern* Pattern);
```

### Terrain Operations

#### UTerrainComponent

```cpp
// Sample terrain height at a tile position
UFUNCTION(BlueprintCallable)
float SampleTerrainElevation(int32 Level, int32 Row, int32 Column);

// Flatten a rectangular region
UFUNCTION(BlueprintCallable)
void FlattenRegion(int32 FromRow, int32 FromCol, int32 ToRow, int32 ToCol,
                   float TargetHeight, int32 Level);

// Smooth a circular region (Laplacian)
UFUNCTION(BlueprintCallable)
void SmoothCornerRegion(int32 Level, int32 CenterRow, int32 CenterCol,
                        float Radius, float Strength);

// Flatten terrain under a floor tile
UFUNCTION(BlueprintCallable)
void FlattenTerrainUnderFloor(int32 Level, int32 Row, int32 Column,
                              const FTileSectionState& TileSectionState,
                              float TargetHeight, bool bBypassLock = false);

// Flatten terrain along a wall
UFUNCTION(BlueprintCallable)
void FlattenTerrainAlongWall(int32 Level, int32 StartRow, int32 StartColumn,
                             int32 EndRow, int32 EndColumn,
                             float TargetHeight, bool bBypassLock = false);

// Remove terrain tiles (for stairs, openings)
UFUNCTION(BlueprintCallable)
void RemoveTerrainTile(int32 Level, int32 Row, int32 Column);

UFUNCTION(BlueprintCallable)
void RemoveTerrainRegion(int32 Level, int32 FromRow, int32 FromCol, int32 ToRow, int32 ToCol);

// Reset entire lot terrain
UFUNCTION(BlueprintCallable)
void DestroyAllTerrain();

// Apply a material to all terrain
UFUNCTION(BlueprintCallable)
void ApplySharedTerrainMaterial(UMaterialInstance* Material);

// Batch operations
UFUNCTION(BlueprintCallable)
void BeginBatchOperation();

UFUNCTION(BlueprintCallable)
void EndBatchOperation();
```

#### Height Map Access

```cpp
// Get/create height map for a level
FTerrainHeightMap* GetOrCreateHeightMap(int32 Level);

// Update a single corner height
void UpdateCornerHeight(int32 Level, int32 CornerRow, int32 CornerColumn,
                        float NewHeight, bool bBypassLock = false);
```

The `FTerrainHeightMap` struct provides direct corner manipulation:

```cpp
FTerrainHeightMap* HeightMap = TerrainComponent->GetOrCreateHeightMap(Level);
float Height = HeightMap->GetCornerHeight(CornerRow, CornerColumn);
HeightMap->SetCornerHeight(CornerRow, CornerColumn, NewHeight);
HeightMap->AdjustCornerHeight(CornerRow, CornerColumn, DeltaHeight);
float CenterHeight = HeightMap->GetTileCenterHeight(TileRow, TileColumn);
```

### Grid Operations

#### UGridComponent

```cpp
// Generate the full grid mesh
UFUNCTION(BlueprintCallable)
void GenerateGridMesh(const TArray<FTileData>& GridData, int32 GridSizeX, int32 GridSizeY,
                      float GridTileSize, UMaterialInterface* GridMaterial, int32 ExcludeLevel = -1);

// Rebuild a single level's grid
UFUNCTION(BlueprintCallable)
void RebuildGridLevel(int32 Level, const TArray<FTileData>& GridData,
                      UMaterialInterface* GridMaterial, int32 ExcludeLevel = -1);

// Show/hide the grid
UFUNCTION(BlueprintCallable)
void SetGridVisibility(bool bShowGrid, bool bHideBoundaryLines = false);

// Show/hide a specific level's grid
UFUNCTION(BlueprintCallable)
void SetLevelVisibility(int32 Level, bool bShow);

// Debug: color tiles by room ID
UFUNCTION(BlueprintCallable)
void UpdateAllTileColorsFromRoomIDs(const TArray<FTileData>& GridData);
```

---

## 9. Tips & Troubleshooting

### Floor Z-Fighting

If you see flickering between floors and terrain, verify that:
- `FloorTopOffset` on `UFloorComponent` is set to a small positive value (default: 0.1 cm).
- Terrain auto-flattening is working (`LotManager->bRemoveTerrainUnderFloors` should be `true`).
- Preview floors use the built-in `PreviewZOffset` (5 cm) — don't set this to zero.

### Custom Terrain Manipulation

When writing custom terrain code, **always** wrap changes in batch operations:

```cpp
TerrainComponent->BeginBatchOperation();

for (/* each tile */)
{
    TerrainComponent->UpdateCornerHeight(Level, Row, Col, NewHeight);
}

TerrainComponent->EndBatchOperation();  // Rebuilds mesh once
```

Without batch operations, each corner change triggers a full mesh rebuild.

### Terrain Tools Only Work on Ground Floor

All brush tools enforce `CurrentLevel == Basements` before operating. This is by design — terrain exists only at the ground floor. If you need to modify terrain from a different level context, call `TerrainComponent` methods directly with the correct level parameter (usually `LotManager->Basements`).

### Per-Triangle Floor Data

The `FFloorTileData` struct stores data per-triangle, not per-tile. When iterating floor data, remember that one visual "tile" can have up to 4 entries in the spatial map. Use `GetAllTrianglesAtTile()` to retrieve all triangles at a grid position.

### Material Parameter Names

If your custom base material doesn't respond to pattern changes, ensure it exposes these exact parameter names:
- `FloorMaterial` (Texture2D)
- `FloorNormal` (Texture2D)
- `FloorRoughness` (Texture2D)
- `FloorColour` (Vector/LinearColor)
- `bUseColourSwatches` (Scalar)
- `bUseColourMask` (Scalar)
- `ShowGrid` (Scalar)

### Spatial Key Packing

Floor and terrain components use packed integer keys for O(1) spatial lookups:

```cpp
// Floor: 8 bits level | 10 bits row | 10 bits column | 2 bits triangle
int32 Key = UFloorComponent::MakeGridKey(Level, Row, Column, Triangle);

// Terrain: 8 bits level | 12 bits row | 12 bits column
int32 Key = UTerrainComponent::MakeGridKey(Level, Row, Column);
```

This means floor grids support up to 1024×1024 tiles per level, and terrain supports up to 4096×4096. These limits are more than sufficient for typical lot sizes.

### Undo/Redo

All floor and terrain operations go through the **Command** system (`UFloorCommand`, `UTerrainCommand`) via `UBuildServer`. Operations are undoable by default when committed through the standard tool pipeline. If you build floors or modify terrain through `BuildServer`, undo/redo is handled automatically.

### Multiplayer Considerations

Floor tool operations are replicated via Server → Multicast RPCs. The pattern goes:
1. Local client creates preview meshes (visual only, not replicated).
2. On release, client sends `Server_BuildFloors` or `Server_DeleteFloors`.
3. Server multicasts to all clients, where `BuildServer` executes the actual commands.

Terrain brush tools currently execute locally. If you need replicated terrain, route through your own server RPCs calling the same `BuildServer` methods.

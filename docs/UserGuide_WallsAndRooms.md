# Walls & Rooms — User Guide

> **BurbArchitect Plugin for Unreal Engine 5**
>
> This guide covers the wall drawing tools, room detection system, wall pattern application, and the selection tool. Everything here is accessible from Blueprint and designed for runtime building gameplay (Sims-style).

---

## Table of Contents

1. [Overview](#overview)
2. [Wall Drawing Tools](#wall-drawing-tools)
   - [Single Wall Tool](#single-wall-tool-abuildwalltool)
   - [Room Tool](#room-tool-abuildroomtool)
   - [Diagonal Room Tool](#diagonal-room-tool-abuilddiagonalroomtool)
   - [Half Wall Tool](#half-wall-tool-abuildhalfwalltool)
3. [How Walls Work (The Wall Graph)](#how-walls-work-the-wall-graph)
4. [Automatic Room Detection](#automatic-room-detection)
5. [Selection Tool](#selection-tool-aselectiontool)
6. [Wall Pattern Tool (Wallpaper/Paint)](#wall-pattern-tool-awallpatterntool)
7. [Creating Custom Wall Patterns](#creating-custom-wall-patterns)
8. [Blueprint API Reference](#blueprint-api-reference)
9. [Undo & Redo](#undo--redo)
10. [Tips & Common Patterns](#tips--common-patterns)

---

## Overview

BurbArchitect's wall and room system follows a **click-drag-release** workflow common to life simulation games:

1. The player selects a **build tool** (wall, room, etc.).
2. They **click** to set a start point, **drag** to preview, and **release** to commit.
3. On commit, walls are sent through the **BuildServer** subsystem, which records them for undo/redo and triggers **automatic room detection**.
4. When walls form a closed boundary, the **RoomManager** detects the enclosed space as a room and auto-generates floors and ceilings.

All build tools inherit from `ABuildTool` and share the same input contract: `Click → Move/Drag → Release`. They are fully replicated for multiplayer.

---

## Wall Drawing Tools

### Single Wall Tool (`ABuildWallTool`)

**Class:** `ABuildWallTool` (parent of all wall-type tools)

Draws a line of individual wall segments from a start point to the cursor position.

| Property | Type | Description |
|---|---|---|
| `DefaultWallPattern` | `UWallPattern*` | The pattern (material/texture) applied to newly created walls. Set this in your tool's Blueprint defaults. |
| `BaseMaterial` | `UMaterialInstance*` | The base material instance used for wall rendering. |
| `WallHeight` | `float` | Height of walls in centimetres. Defaults to 300 (auto-read from `LotManager.DefaultWallHeight` at BeginPlay). |

**How it works:**

1. **Click** — Records the start tile-corner position.
2. **Drag** — As the cursor moves, the tool snaps the drag direction to one of **8 directions** (N, S, E, W, NE, NW, SE, SW). It then generates preview `UWallComponent` segments along that line, one per grid tile. Previews are cached and reused for performance — only new or removed segments are created/destroyed each frame.
3. **Release** — All preview wall segments are committed through the `BuildServer`. Duplicate walls (ones that already exist on the lot) are automatically skipped. If multiple segments are committed at once, they are wrapped in a **batch** for single-step undo.

**Deletion Mode:** Set `bDeletionMode = true` on the tool to switch to wall deletion. Dragging in deletion mode highlights existing walls for removal instead of creation.

**Blueprint-callable functions:**

```
CreateWallPreview(Level, Index, StartLocation, Direction)
```
Creates a single wall preview segment. Useful if you want to drive wall creation from custom Blueprint logic rather than the built-in click-drag flow.

---

### Room Tool (`ABuildRoomTool`)

**Class:** `ABuildRoomTool` — inherits from `ABuildWallTool`

Draws a rectangular room (4 walls) with a single click-drag gesture.

**How it works:**

1. **Click** — Records a corner position.
2. **Drag** — Calculates a bounding rectangle from the start to current cursor position. Generates preview walls along all four edges (top, bottom, left, right). The minimum size is clamped to 1×1 tile to prevent degenerate single-line walls.
3. **Release** — Commits all perimeter walls through the BuildServer as a batch. Existing walls that overlap are automatically skipped (e.g., shared walls between adjacent rooms). Room detection fires automatically after commit.

> **Tip:** Drawing rooms next to each other shares walls automatically. If room A and room B share a wall segment, only one wall exists in the graph — it simply has different room IDs on each side.

---

### Diagonal Room Tool (`ABuildDiagonalRoomTool`)

**Class:** `ABuildDiagonalRoomTool` — inherits from `ABuildWallTool`

Draws diamond-shaped (rotated square) rooms using diagonal walls.

| Property | Type | Description |
|---|---|---|
| `DiamondRadius` | `int32` | Size of the diamond in tiles (distance from centre to each corner). A radius of 1 creates a 3×3 diamond, radius 2 a 5×5, etc. |

**How it works:**

1. **Click** — Records a corner position.
2. **Drag** — The tool calculates a **square** bounding box from the drag rectangle (using the larger dimension for both axes, rounded up to an even number). It then places the four diamond corners at the midpoints of each rectangle edge, all snapped to grid corners.
3. Four edges of diagonal wall segments (NE, SE, SW, NW) are generated between the diamond corners.
4. **Release** — All diagonal segments are committed as a batch.

> **Note:** The bounding box is always forced to a square so the diamond shape has proper 45° angles on all sides.

---

### Half Wall Tool (`ABuildHalfWallTool`)

**Class:** `ABuildHalfWallTool` — inherits from `ABuildTool` (not `ABuildWallTool`)

Creates decorative half-height walls (railings, room dividers, balcony edges).

| Property | Type | Description |
|---|---|---|
| `WallHeight` | `float` | Defaults to **150** (50% of standard 300). Auto-calculated as `LotManager.DefaultWallHeight * 0.5`. |

**Key difference from full walls:**

| | Full Wall | Half Wall |
|---|---|---|
| Height | 300 (default) | 150 (default) |
| Creates graph edge | ✅ Yes | ❌ No (`WallEdgeID = -1`) |
| Triggers room detection | ✅ Yes | ❌ No |
| Supports wallpaper | ✅ Yes | ✅ Yes |
| Blocks pathfinding | ✅ (depends on setup) | ❌ Decorative only |

Half walls bypass the wall graph entirely — they are rendered directly via `WallComponent` without creating edges in `WallGraphComponent`. This means they **never** form rooms, never appear as room boundaries, and never affect room detection.

The draw interaction (click-drag-release) is otherwise identical to the single wall tool, including 8-direction snapping.

---

## How Walls Work (The Wall Graph)

Under the hood, all full walls are stored as a **graph** on the `WallGraphComponent` (attached to your `LotManager`):

- **Nodes** — Points where walls meet (grid intersections). Each node has a position, grid row/column, and floor level.
- **Edges** — The actual wall segments connecting two nodes. Each edge stores its height, thickness, pattern, and the **two room IDs** on either side (`Room1` and `Room2`, where 0 = outside/no room).

**Why this matters to you:**

- **Shared walls are automatic.** When you draw a wall between two rooms, that single edge knows both rooms (one on each side). There's no wall duplication.
- **Room detection is graph-based.** The system traces closed loops of edges to find rooms — it doesn't scan tiles.
- **Queries are fast.** The wall graph uses spatial indexing so lookups like "is there a wall between tile A and tile B?" are O(1).

**Key Blueprint functions on `UWallGraphComponent`:**

| Function | Returns | Description |
|---|---|---|
| `IsWallBetweenTiles(RowA, ColA, RowB, ColB, Level)` | `bool` | Check if a wall exists between two adjacent tiles |
| `IsWallBetweenPositions(PosA, PosB, Level)` | `bool` | Same, but using world positions |
| `GetEdgesAtNode(NodeID)` | `TArray<int32>` | All wall edge IDs meeting at a node |
| `GetEdgesInTile(Row, Column, Level)` | `TArray<int32>` | All wall edges passing through a grid tile |
| `GetEdgesBoundingRoom(RoomID)` | `TArray<int32>` | All wall edges that bound a specific room |
| `GetEdgesAtLevel(Level)` | `TArray<int32>` | All wall edges on a floor level |
| `AddEdge(FromNodeID, ToNodeID, Height, Thickness, Pattern)` | `int32` | Programmatically add a wall edge |
| `RemoveEdge(EdgeID)` | `bool` | Remove a wall edge (also removes orphaned nodes) |
| `GetNodeCount()` | `int32` | Total nodes in the graph |
| `GetEdgeCount()` | `int32` | Total edges in the graph |

> **Important:** For gameplay code, prefer going through the **BuildServer** rather than calling `AddEdge`/`RemoveEdge` directly. The BuildServer handles undo/redo, room detection, and floor/ceiling generation automatically. Direct graph manipulation is for advanced use cases only.

---

## Automatic Room Detection

The **`URoomManagerComponent`** (also on your `LotManager`) handles room detection. Rooms are detected **automatically** when walls are built or removed through the BuildServer — you don't need to trigger it manually.

### How Detection Works

1. When a wall is committed, the BuildServer calls the RoomManager.
2. The RoomManager uses **flood fill** across a triangle grid, where walls act as barriers between triangles.
3. Any enclosed region that doesn't touch the lot boundary is classified as a **room**.
4. The room gets assigned a unique `RoomID`, its boundary edges and interior tiles/triangles are cached, and floors/ceilings are auto-generated.

### Room Data (`FRoomData`)

Each detected room provides:

| Field | Type | Description |
|---|---|---|
| `RoomID` | `int32` | Unique identifier (> 0 for rooms, 0 = outside) |
| `BoundaryEdges` | `TArray<int32>` | Ordered wall edge IDs forming the room perimeter |
| `BoundaryVertices` | `TArray<FVector>` | Polygon vertices in clockwise order (world space) |
| `InteriorTriangles` | `TArray<FTriangleCoord>` | All triangles inside this room |
| `InteriorTiles` | `TArray<FIntVector>` | All tiles inside this room (derived from triangles) |
| `Centroid` | `FVector` | Area-weighted geometric centre |
| `Level` | `int32` | Floor level |
| `ParentRoomID` | `int32` | For nested rooms (0 = top-level) |
| `ChildRoomIDs` | `TArray<int32>` | Rooms contained within this room |

### Room Lifecycle Events

The RoomManager broadcasts `OnRoomsChanged` when rooms are added, modified, or removed:

```cpp
// C++ delegate — bind in Blueprint via wrapper or directly in C++
FOnRoomsChanged OnRoomsChanged;
// Parameters: AddedRoomIDs, ModifiedRoomIDs, RemovedRoomIDs
```

### Key Scenarios

| Action | What Happens |
|---|---|
| Draw walls forming a closed shape | New room detected, floors/ceilings auto-generated |
| Add a wall through an existing room | Room **splits** into two rooms; old room invalidated, two new rooms created |
| Remove a wall between two rooms | Rooms **merge** into one; two old rooms invalidated, one new room created |
| Remove a wall on the boundary | Room **destroyed** (no longer enclosed); floors/ceilings removed |

### Blueprint Functions on `URoomManagerComponent`

| Function | Returns | Description |
|---|---|---|
| `GetRoom(RoomID, OutRoom)` | `bool` | Get full room data by ID |
| `GetRoomAtTile(TileCoord)` | `int32` | Which room owns a tile (0 = outside) |
| `GetRoomAtTriangleCoords(Row, Col, Level, Triangle)` | `int32` | Room at a specific triangle (most precise) |
| `GetRoomsAtTile(TileCoord)` | `TArray<int32>` | All rooms present in a tile (diagonal walls can split tiles) |
| `GetRoomCount()` | `int32` | Number of active rooms |
| `DetectAllRooms(Level)` | `int32` | Force full room re-detection on a level |
| `SelectRoom(RoomID)` | `void` | Mark a room as selected |
| `DeselectRoom()` | `void` | Clear room selection |
| `GetSelectedRoom(OutRoom)` | `bool` | Get data for the currently selected room |
| `InvalidateAllRooms()` | `void` | Mark all rooms for recalculation |

**Static utility functions (callable from any Blueprint):**

| Function | Description |
|---|---|
| `CalculatePolygonCentroid(Vertices)` | Area-weighted centroid of a polygon |
| `CalculatePolygonArea(Vertices)` | Area via shoelace formula |
| `IsPointInPolygon(Point, Vertices)` | Ray-cast point-in-polygon test |
| `IsValidRoomPolygon(Vertices)` | Validates polygon (no self-intersection, enough vertices) |

---

## Selection Tool (`ASelectionTool`)

**Class:** `ASelectionTool` — inherits from `ABuildTool`

The selection tool lets players click on building elements (walls, floors, stairs, roofs) to select them, and click on walls to select the **room** behind them.

### Component Detection

The tool automatically identifies what the cursor is hovering over and fills a `FComponentData` struct:

| `ESectionComponentType` | When |
|---|---|
| `WallComponent` | Cursor hits a wall mesh |
| `FloorComponent` | Cursor hits a floor mesh |
| `StairsComponent` | Cursor hits a committed staircase |
| `RoofComponent` | Cursor hits a roof mesh |
| `RoomComponent` | (Reserved for room-level operations) |

### Room Selection

When the player **clicks** a wall, the selection tool determines which room is behind that wall and selects it:

1. The wall's `WallEdgeID` is looked up in the wall graph.
2. The edge's `Room1`/`Room2` values identify adjacent rooms.
3. The hit normal determines which side of the wall was clicked, selecting the corresponding room.

When a room is selected:
- **Footprint lines** are drawn around the room boundary (green polygon on the floor).
- A **Room Control Widget** is spawned at the room centroid (configurable via `RoomControlWidgetClass`).
- Clicking empty space or a different wall deselects the current room.

| Property | Type | Description |
|---|---|---|
| `RoomControlWidgetClass` | `TSubclassOf<UUserWidget>` | Assign your room control UI widget class here |
| `RoomControlWidgetZOffset` | `float` | Height above floor for the widget (default: 100) |

### Blueprint Events

| Event | Description |
|---|---|
| `OnSelectedSection(ComponentData)` | Fired when any building component is hovered/selected. Override in Blueprint for custom UI. |
| `OnReleasedSection()` | Fired when selection is cleared |

### Room Selection Functions (Blueprint-callable)

```
SelectRoom(RoomID)          — Select a room by ID
DeselectRoom()              — Clear room selection
DrawRoomFootprintLines(ID)  — Manually trigger footprint drawing
ClearRoomFootprintLines()   — Clear footprint lines
ShowRoomControlWidget(Loc)  — Show widget at location
HideRoomControlWidget()     — Hide widget
RefreshRoomSelection()      — Refresh visuals (e.g., after cutaway mode change)
```

### Deletion

The selection tool also supports deleting selected elements:

```
DeleteSection(ComponentData) — Delete the selected wall/floor/stairs/roof
ServerDelete()               — Networked deletion (calls the above on server)
```

### Rotation

For roofs and stairs in edit mode, the selection tool forwards rotation commands:

```
RotateLeft()   — Rotate selected element left (90° or custom per tool)
RotateRight()  — Rotate selected element right
```

---

## Wall Pattern Tool (`AWallPatternTool`)

**Class:** `AWallPatternTool` — inherits from `ABuildTool`

Applies wallpaper, paint, and material patterns to wall surfaces. Supports **single-wall**, **whole-room**, and **drag-paint** application modes.

### Core Properties

| Property | Type | Description |
|---|---|---|
| `SelectedWallPattern` | `UWallPattern*` | The pattern to apply. Set from your catalog/picker UI. |
| `SelectedSwatchIndex` | `int32` | Index into the pattern's `ColourSwatches` array (for colour variants). |
| `BaseMaterial` | `UMaterialInstance*` | Base material template. |

### Wall Faces

Each wall has **two main faces** and two end caps:

| `EWallFace` | Description |
|---|---|
| `PosY` | Face on the Room1 side of the wall |
| `NegY` | Face on the Room2 side of the wall |
| `StartCap` | End cap at wall start |
| `EndCap` | End cap at wall end |

The tool automatically determines which face the player clicked using hit normals and wall geometry.

### Application Modes

**Single Wall** — Hover over a wall face and click. The pattern is applied to that one face only.

**Room Walls** — Hold Shift (or your configured modifier) and click a wall. The pattern applies to **all walls** on that side of the room:
- If clicked from inside a room: all interior-facing walls in that room get the pattern.
- If clicked from outside: all connected exterior walls get the pattern (using BFS traversal from the clicked wall to find the continuous exterior structure).

**Drag Paint** — Click and drag across multiple walls. Each wall face you sweep over gets painted. On release, all painted faces are committed in a single networked call.

### Live Preview

The tool shows a **live preview** before committing. When hovering, the targeted face(s) temporarily display the selected pattern. Moving the cursor or changing tools restores original textures instantly.

### Networked RPCs

All pattern application is server-authoritative and multicast:

| RPC | Description |
|---|---|
| `Server_ApplySingleWallPattern(WallIndex, Face, Pattern, SwatchIndex)` | Apply to one wall face |
| `Server_ApplyRoomWallPatterns(RoomID, Level, bInterior, StartEdgeID, Pattern, SwatchIndex)` | Apply to all room walls |
| `Server_ApplyDragPaintedWalls(WallIndices[], FaceTypes[], Pattern, SwatchIndex)` | Apply to drag selection |

Each has a corresponding `Multicast_*` that applies the change on all clients.

### Blueprint-Callable Helper

```
GetWallsInRoom(RoomID, Level, bInterior) → TArray<FWallSegmentData>
```
Returns all wall segments for a room. Set `bInterior = true` for walls facing into the room, `false` for exterior-facing walls.

---

## Creating Custom Wall Patterns

Wall patterns are **Data Assets** — you create them in the Content Browser without writing any code.

### Step-by-Step

1. **Right-click** in the Content Browser → **Miscellaneous** → **Data Asset**.
2. Select **`WallPattern`** as the class.
3. Name it something descriptive (e.g., `WP_BrickRed`, `WP_WallpaperFloral`).
4. Open it and configure:

### `UWallPattern` Properties

| Property | Required | Description |
|---|---|---|
| `DisplayName` | ✅ | Name shown in the catalog UI |
| `Icon` | ✅ | Thumbnail for the catalog picker |
| `Cost` | ✅ | Price in your game's currency |
| `Category` | ✅ | Catalog category (Data Asset of type `UCatalogCategory`) |
| `Subcategory` | Optional | Catalog subcategory (Data Asset of type `UCatalogSubcategory`) |
| `BaseTexture` | ✅ | Diffuse/albedo texture |
| `NormalMap` | ✅ | Normal map texture |
| `RoughnessMap` | ✅ | Roughness/specular map |
| `bUseDetailNormal` | Optional | Enable a second detail normal layer |
| `DetailNormal` | Optional | Detail normal map texture (shown when `bUseDetailNormal` is true) |
| `DetailNormalIntensity` | Optional | Strength of detail normal (0.0–32.0, default 1.0) |
| `BaseMaterial` | Optional | Override base material for this specific pattern. If unset, the tool's default `BaseMaterial` is used. |
| `bUseColourSwatches` | Optional | Enable colour recolouring |
| `bUseColourMask` | Optional | Use the RMA texture's alpha channel as a colour mask (only affects masked areas) |
| `ColourSwatches` | Optional | Array of `FLinearColor` values the player can pick from |

### Colour Swatch System

If you enable `bUseColourSwatches`, the player can pick from your predefined colour options:

```
ColourSwatches:
  [0] → (0.9, 0.85, 0.8, 1.0)   // Cream
  [1] → (0.6, 0.7, 0.65, 1.0)   // Sage Green
  [2] → (0.4, 0.5, 0.7, 1.0)    // Dusty Blue
```

The `SelectedSwatchIndex` on the `WallPatternTool` controls which swatch is active. Your UI should set this when the player picks a colour.

If `bUseColourMask` is enabled, only the masked region of the texture is recoloured — this is useful for patterns where you want to tint the pattern but leave the base wall colour alone.

### Floor Patterns

Floor patterns (`UFloorPattern`) use the exact same property set as wall patterns. Create them the same way, just select `FloorPattern` as the data asset class.

---

## Blueprint API Reference

### BuildServer (`UBuildServer` — World Subsystem)

The BuildServer is the **primary interface** for all building operations. Access it in Blueprint via `Get World Subsystem → BuildServer`.

#### Wall Operations

| Function | Description |
|---|---|
| `BuildWall(Level, StartLoc, EndLoc, Height, Pattern, BaseMaterial)` | Build a single wall segment |
| `DeleteWall(WallData)` | Delete a specific wall segment |
| `BuildRoom(Level, StartCorner, EndCorner, WallHeight)` | Build a rectangular room (4 walls as one operation) |

#### Batch Operations

| Function | Description |
|---|---|
| `BeginBatch(Description)` | Start grouping commands into one undo step |
| `EndBatch()` | Commit the batch |
| `CancelBatch()` | Discard the batch without committing |
| `IsInBatch()` | Check if currently batching |

#### Undo / Redo

| Function | Description |
|---|---|
| `Undo()` | Undo the last operation (or batch) |
| `Redo()` | Redo the last undone operation |
| `CanUndo()` | Returns `true` if there's something to undo |
| `CanRedo()` | Returns `true` if there's something to redo |
| `GetUndoDescription()` | Human-readable label for the next undo step |
| `GetRedoDescription()` | Human-readable label for the next redo step |
| `ClearHistory()` | Wipe all undo/redo history |

### WallGraphComponent Queries

| Function | Returns | Category |
|---|---|---|
| `IsWallBetweenTiles(...)` | `bool` | Quick existence check |
| `IsWallBetweenPositions(...)` | `bool` | Quick existence check (world space) |
| `FindNodeAt(Row, Col, Level)` | `int32` | Find a node at grid coords (-1 if none) |
| `FindEdgeBetweenNodes(NodeA, NodeB)` | `int32` | Find edge connecting two nodes |
| `GetEdgesAtNode(NodeID)` | `TArray<int32>` | Edges connected to a node |
| `GetEdgesInTile(Row, Col, Level)` | `TArray<int32>` | Edges passing through a tile |
| `GetEdgesBoundingRoom(RoomID)` | `TArray<int32>` | All boundary edges of a room |
| `GetEdgesAtLevel(Level)` | `TArray<int32>` | All edges on a level |
| `GetNodesAtLevel(Level)` | `TArray<int32>` | All nodes on a level |
| `DoesPathExistBetweenNodes(Start, End, ExcludeEdge)` | `bool` | Check connectivity (used for loop detection) |
| `IsPositionNearJunction(EdgeID, Pos, Threshold)` | `bool` | Check if position is near a T-junction or corner |
| `IsPortalWithinWallBounds(EdgeID, Center, Extent, Rotation)` | `bool` | Validate portal placement |
| `AddNode(Position, Level, Row, Column)` | `int32` | Add a node (advanced) |
| `AddEdge(FromNode, ToNode, Height, Thickness, Pattern)` | `int32` | Add an edge (advanced) |
| `RemoveEdge(EdgeID)` | `bool` | Remove an edge (advanced) |
| `RemoveNode(NodeID)` | `bool` | Remove a node (advanced) |
| `ClearGraph()` | `void` | Remove everything |
| `RebuildIntersections()` | `void` | Recalculate corner mitring data |
| `GetNodeCount()` / `GetEdgeCount()` | `int32` | Graph statistics |

### RoomManagerComponent Queries

| Function | Returns | Description |
|---|---|---|
| `GetRoom(RoomID, OutRoom)` | `bool` | Full room data by ID |
| `GetRoomCount()` | `int32` | Total active rooms |
| `GetRoomAtTile(TileCoord)` | `int32` | Dominant room at a tile |
| `GetRoomAtTriangleCoords(Row, Col, Level, Tri)` | `int32` | Room at a specific triangle |
| `GetRoomsAtTile(TileCoord)` | `TArray<int32>` | All rooms in a tile |
| `DetectAllRooms(Level)` | `int32` | Force full detection on a level |
| `DetectRoomFromNewEdge(EdgeID)` | `int32` | Incremental detection from a new wall |
| `OnWallsModified(AddedWalls, RemovedWalls)` | `void` | Notify of wall changes (triggers incremental detection) |
| `InvalidateRoom(RoomID)` | `void` | Mark a room for recalculation |
| `InvalidateAllRooms()` | `void` | Mark all rooms dirty |
| `RebuildRoom(RoomID)` | `bool` | Rebuild a specific room |
| `ClearRoomCache()` | `void` | Wipe all room data |
| `SelectRoom(RoomID)` | `void` | Select a room |
| `DeselectRoom()` | `void` | Deselect |
| `GetSelectedRoom(OutRoom)` | `bool` | Get selected room data |

---

## Undo & Redo

All wall and room operations routed through the **BuildServer** are automatically undoable.

### Single Operations

Each individual `BuildWall()` or `DeleteWall()` call creates one undo step.

### Batch Operations

When a tool commits multiple walls at once (room tool, diagonal room, multi-segment drag), they are wrapped in `BeginBatch` / `EndBatch`. The entire batch is undone/redone as **one step**.

Example from Blueprint:
```
BuildServer → BeginBatch("Build Room (8 walls)")
  BuildServer → BuildWall(...)  // wall 1
  BuildServer → BuildWall(...)  // wall 2
  ...                           // walls 3-8
BuildServer → EndBatch()

// Later:
BuildServer → Undo()  // Removes all 8 walls at once
BuildServer → Redo()  // Restores all 8 walls at once
```

### What Gets Undone

An undo step for wall operations reverses:
- The wall edge in the wall graph
- The visual wall component
- Room detection changes (rooms that formed or dissolved)
- Auto-generated floors and ceilings

### Blueprint Undo UI

Use these to wire up undo/redo buttons:
```
CanUndo() → Enable/disable undo button
CanRedo() → Enable/disable redo button
GetUndoDescription() → Tooltip text (e.g., "Build Room (8 walls)")
GetRedoDescription() → Tooltip text
Undo() → Execute undo
Redo() → Execute redo
```

---

## Tips & Common Patterns

### Accessing Wall and Room Systems at Runtime

All wall/room systems live on the `LotManager` actor:

```
LotManager → WallGraph        (UWallGraphComponent)
LotManager → RoomManager      (URoomManagerComponent)
LotManager → WallComponent    (UWallComponent — visual/render data)
```

The `BuildServer` is a **World Subsystem** — get it anywhere:
```
GetWorld() → GetSubsystem<UBuildServer>()
// Blueprint: "Get World Subsystem" node → select BuildServer
```

### Querying "What Room Am I In?"

```
// From a world position:
RoomManager → GetRoomAtTile(FIntVector(Row, Column, Level))

// From a precise triangle coordinate:
RoomManager → GetRoomAtTriangleCoords(Row, Column, Level, ETriangleType)
```

### Checking Wall Existence Before Building

```
WallGraph → IsWallBetweenTiles(RowA, ColA, RowB, ColB, Level)
```
The build tools already do this check internally, but it's useful for custom validation logic.

### Listening for Room Changes

In C++, bind to the multicast delegate:
```cpp
LotManager->RoomManager->OnRoomsChanged.AddUObject(this, &UMyComponent::HandleRoomsChanged);

void UMyComponent::HandleRoomsChanged(
    const TArray<int32>& Added,
    const TArray<int32>& Modified,
    const TArray<int32>& Removed)
{
    // React to room changes
}
```

### Custom Build Tool

To create your own wall-related build tool:

1. Create a Blueprint class inheriting from `ABuildWallTool` (for full walls) or `ABuildTool` (for custom behavior).
2. Override `Move`, `Click`, `Drag`, and `BroadcastRelease` in Blueprint.
3. Use `BuildServer → BuildWall()` and `BuildServer → DeleteWall()` for commits.
4. Wrap multi-wall operations in `BeginBatch` / `EndBatch`.

### Multi-Level Building

All tools accept a `Level` parameter (the floor level, 0-indexed). The level is determined by the trace hit from the camera — the system handles vertical stacking automatically. Walls on different levels are independent in the wall graph but can share the same grid coordinates.

### Multiplayer

All build tools use Unreal's replication:
- `Click` / `Move` / `Drag` / `Release` are called locally, then forwarded to the server via `ServerClick` / `ServerMove` / `ServerDrag` / `ServerRelease`.
- The server executes the operation and broadcasts results to all clients via `Multicast` RPCs.
- Wall pattern application uses dedicated `Server_Apply*` / `Multicast_Apply*` RPCs for efficient replication.

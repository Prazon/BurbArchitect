# BurbArchitect — Roofs User Guide

> **Audience:** Game developers integrating BurbArchitect into a Sims-style building game.  
> **Engine:** Unreal Engine 5  
> **Plugin version:** See your installed BurbArchitect package for the exact build.

---

## Table of Contents

1. [Overview](#overview)  
2. [Roof Types](#roof-types)  
3. [Placing a Roof](#placing-a-roof)  
4. [Editing a Placed Roof](#editing-a-placed-roof)  
5. [Gizmo Controls (Scale Tools)](#gizmo-controls-scale-tools)  
6. [Rotation](#rotation)  
7. [Supporting Walls](#supporting-walls)  
8. [Roof Materials](#roof-materials)  
9. [Multi-Story Roofing](#multi-story-roofing)  
10. [Deleting Roofs](#deleting-roofs)  
11. [Undo / Redo](#undo--redo)  
12. [Blueprint API Reference](#blueprint-api-reference)  
13. [Extending with Custom Roof Types](#extending-with-custom-roof-types)  
14. [Known Limitations](#known-limitations)  

---

## Overview

BurbArchitect's roof system lets players place, resize, rotate, and delete roofs at runtime — similar to *The Sims*' roof tools. Every roof is a self-contained **Actor** (`ARoofBase` subclass) that owns:

| Component | Purpose |
|---|---|
| `URoofComponent` | Procedural mesh generation (extends `UProceduralMeshComponent`) |
| `UFootprintComponent` | Grid-aligned boundary lines shown during placement & editing |
| `UGizmoComponent` × 5 | Interactive drag handles for height and each extent direction |

Roofs snap to the **LotManager** grid. When committed they automatically generate supporting walls and trigger room/floor detection so the space beneath functions as an enclosed room.

---

## Roof Types

### Gable Roof (`AGableRoof`)

```
        /\
       /  \
      /    \
     /______\
     |      |
```

- Two opposing slopes meeting at a central **ridge**.
- Triangular **gable end walls** auto-generated at the front and back.
- Supports independent overhang control on all four sides (front/back rakes and left/right eaves).
- **Best for:** Traditional houses, rectangular rooms, A-frame cabins.

### Hip Roof (`AHipRoof`)

```
       ____
      /    \
     / ---- \
    /________\
```

- All four sides slope inward toward a ridge (or a single peak if the footprint is roughly square).
- **No vertical gable end walls** — every face is sloped.
- Overhang gizmos for individual rakes/eaves are disabled; overhangs are set uniformly.
- Automatically detects a square footprint (length ≈ width within 10%) and builds a **pyramid** instead of a ridged hip.
- **Best for:** Ranch-style homes, buildings that need weather protection on all sides.

### Shed Roof (`AShedRoof`)

```
     ________
    /        |
   /         |
  /__________|
```

- A single sloped surface — high edge at the front, low edge at the back.
- Auto-generates **triangular side walls** on left and right plus the tall front wall.
- Slope direction follows `RoofDirection`; rotating the roof changes which side is high.
- Individual rake/eave gizmos are disabled; only extent and height gizmos are shown.
- **Best for:** Modern/contemporary architecture, lean-tos, carports, asymmetric designs.

### Quick comparison

| Feature | Gable | Hip | Shed |
|---|:---:|:---:|:---:|
| Sloped faces | 2 | 4 | 1 |
| Auto-generated gable walls | ✔ (front & back) | ✘ | ✔ (sides + front) |
| Perimeter walls | ✔ | ✘ | ✘ |
| Individual overhang gizmos | ✔ | ✘ | ✘ |
| Pyramid mode | — | ✔ (if square) | — |

---

## Placing a Roof

### Build Tools

Each roof type has a dedicated build tool:

| Tool Class | Spawns |
|---|---|
| `ABuildGableRoofTool` | `AGableRoof` |
| `ABuildHipRoofTool` | `AHipRoof` |
| `ABuildShedRoofTool` | `AShedRoof` |

All three inherit from `ABuildRoofTool`, which itself extends `ABuildTool`. The only thing each subclass overrides is `GetRoofType()` — so wiring up a new roof style in the UI is trivial.

### Placement Workflow

1. **Activate the tool** — e.g. from a UI button that tells the `BurbPawn` to equip `ABuildGableRoofTool`.
2. **Move the cursor** — the tool creates a live **preview roof** that follows the mouse, snapping to the LotManager grid. The preview uses a translucent material (`LotManager->ValidPreviewMaterial`).
3. **Click to place** — `Click_Implementation()` calls `CreateRoof()` which:
   - Applies the final `DefaultRoofMaterial`.
   - Calls `CommitRoof()` on the preview actor (generates supporting walls, applies material, marks committed).
   - Immediately enters **edit mode** so the player can adjust gizmos right away.
   - The build tool then returns to the default tool (one-shot usage).

### Tool Properties (Editable in Blueprint Defaults)

| Property | Type | Default | Description |
|---|---|---|---|
| `DefaultRoofMaterial` | `UMaterialInstance*` | — | Material applied on commit |
| `RoofThickness` | `float` | `15.0` | Thickness of the roof slab |
| `GableThickness` | `float` | `20.0` | Thickness of auto-generated gable end walls |
| `Height` | `float` | `298.0` (wall height) | Peak height; defaults to `LotManager->DefaultWallHeight` at BeginPlay |
| `FrontUnits` / `BackUnits` | `float` | `3.0` | Extent in grid tiles (front/back) |
| `LeftUnits` / `RightUnits` | `float` | `3.0` | Extent in grid tiles (left/right) |
| `FrontRake` / `BackRake` | `float` | `50.0` | Overhang beyond the wall line (front/back) |
| `LeftEve` / `RightEve` | `float` | `50.0` | Overhang beyond the wall line (left/right) |
| `RoofActorClass` | `TSubclassOf<ARoofBase>` | *(auto)* | Override with a Blueprint subclass if needed |

> **Tip:** If you leave `RoofActorClass` empty, the tool automatically selects the correct C++ class (`AGableRoof`, `AHipRoof`, or `AShedRoof`). Set it only if you've created a custom Blueprint subclass.

### Level Detection

- The tool traces under the cursor. If the cursor hits a **wall**, the roof is placed one level above that wall's level.
- Otherwise, the traced level from the camera is used.
- **Roofs cannot be placed on Level 0 (basement).** The tool silently blocks this.

---

## Editing a Placed Roof

After a roof is committed (placed), players interact with it directly:

| Action | Result |
|---|---|
| **Click on a committed roof** | Enters **edit mode** — shows gizmo handles and footprint lines, highlights the roof with custom depth stencil |
| **Click away from the roof** | Exits edit mode — hides gizmos and footprint |
| **Press DEL** (while selected) | Deletes the roof (see [Deleting Roofs](#deleting-roofs)) |
| **Rotate keys** (while selected or during placement) | Rotates by 45° increments (see [Rotation](#rotation)) |

Edit mode is tracked by the `bInEditMode` flag and registered with `BurbPawn->CurrentEditModeActor` so only one actor is edited at a time.

---

## Gizmo Controls (Scale Tools)

When a roof enters edit mode, interactive **gizmo spheres** appear on its geometry. Drag any gizmo to adjust the corresponding dimension.

### Available Gizmos

| Gizmo | Axis | Behavior | Snapping |
|---|---|---|---|
| **Height** (peak) | Z-axis (vertical) | Raises/lowers the ridge or peak | Free (no grid snap) — minimum 50 units |
| **Front Extent** | Along `RoofDirection` | Extends/shrinks the front edge | Snaps to grid tile size on release |
| **Back Extent** | Opposite `RoofDirection` | Extends/shrinks the back edge | Snaps to grid tile size on release |
| **Left Extent** | Perpendicular left | Extends/shrinks the left edge | Snaps to grid tile size on release |
| **Right Extent** | Perpendicular right | Extends/shrinks the right edge | Snaps to grid tile size on release |

### How Drag Works

1. **Click a gizmo** — the system records the starting dimensions (`OriginalDragDimensions`) and projects the mouse ray onto the gizmo's constrained axis.
2. **Drag** — the roof mesh updates **smoothly** in real-time (no snapping during drag). Meanwhile, a separate `SnappedDimensions` struct tracks the nearest grid-snapped value. The **footprint lines update only when the snapped value crosses a tile boundary**, giving the player clear visual feedback.
3. **Release** — the snapped dimensions are applied as the final values. The mesh regenerates to the clean grid-aligned size, gizmos reposition, and supporting walls are regenerated.

### Gizmo Availability Per Roof Type

Not every roof type exposes every gizmo. The `IsScaleToolValid()` override on each subclass controls which gizmos appear:

| Gizmo | Gable | Hip | Shed |
|---|:---:|:---:|:---:|
| Height | ✔ | ✔ | ✔ |
| Front Extent | ✔ | ✔ | ✔ |
| Back Extent | ✔ | ✔ | ✔ |
| Left Extent | ✔ | ✔ | ✔ |
| Right Extent | ✔ | ✔ | ✔ |
| Individual Overhangs (Rake/Eave) | ✔ | ✘ | ✘ |

> **Note:** Overhang gizmos (FrontRake, BackRake, RightEve, LeftEve) are currently defined in the enum but their physical gizmo components have been removed from the base class — only the five extent/height gizmos exist as `UGizmoComponent` sub-objects. Overhang values can still be set via Blueprint or C++ on `FRoofDimensions`.

### Shed Roof Height Gizmo

The shed roof overrides `SetupScaleTools()` to position the height gizmo at the **high edge** (front), since the slope runs from front to back.

---

## Rotation

Roofs can be rotated in **45-degree increments**, both during placement preview and after being committed.

### During Placement

The `BuildRoofTool`'s `RotateLeft_Implementation()` / `RotateRight_Implementation()` respond to your game's rotate input. They:
- Update the internal `RoofDirection` vector via a 2D rotation matrix.
- Rotate the preview actor's yaw by ±45°.
- Refresh the footprint lines.

### After Placement (Edit Mode)

If a committed roof is selected, the same rotate input is forwarded to `ARoofBase::RotateLeft()` / `RotateRight()`. This:
- Uses **quaternion math** for numerical stability (avoids floating-point drift over many rotations).
- Snaps yaw to exact 45° multiples and normalizes to `[0, 360)`.
- Regenerates the mesh, supporting walls, footprint, and gizmo positions.

> **Important:** Rotation changes `RoofDirection`, but the `FRoofDimensions` values (FrontDistance, BackDistance, etc.) stay constant. The same dimensions produce a visually rotated roof because vertex calculation is relative to `RoofDirection`.

### Diagonal Roofs

Because rotation snaps to 45° increments, you can place roofs at diagonal angles (45°, 135°, etc.). The procedural mesh generation handles arbitrary direction vectors, so diagonal roofs render correctly — but keep in mind that diagonal roofs may align imperfectly with the orthogonal wall grid.

---

## Supporting Walls

When a roof is committed, it can auto-generate walls that close the space beneath into a proper room.

### How It Works

Each roof subclass overrides `GenerateSupportingWalls()`:

| Roof Type | Walls Generated |
|---|---|
| **Gable** | 4 perimeter walls (rectangular base) + 2 triangular gable end walls at front and back |
| **Hip** | None — all four sides slope to the ground, so no vertical walls are needed |
| **Shed** | Triangular side walls (left + right) via `GenerateShedEndWalls()` |

### Wall Flags

The `ERoofWallFlags` bitmask (`Front`, `Back`, `Left`, `Right`) stored in `FRoofDimensions::WallFlags` controls which sides get walls. Default values are set automatically:

- **Gable:** `Front | Back`
- **Shed:** `Front | Left | Right`
- **Hip:** `None` (0)

### Wall Pattern

Roofs accept a `DefaultWallPattern` (type `UWallPattern*`) that is applied to auto-generated walls. Set this on the roof actor or build tool to control the look of supporting walls.

### Room Detection

After walls are generated, the system:
1. Detects enclosed rooms at the roof's level via `RoomManagerComponent`.
2. Marks them as **roof rooms** (`bIsRoofRoom = true`).
3. Generates floor tiles for those rooms but **skips ceiling generation** (since the roof itself provides the ceiling).

### Wall Cleanup

Walls are tracked in `CreatedWallIndices` on the roof actor. They are automatically cleaned up when:
- The roof is deleted
- The roof is resized (old walls removed, new walls regenerated)
- The roof is rotated
- Undo is triggered

---

## Roof Materials

### Setting the Material

- **During placement:** The build tool's `DefaultRoofMaterial` property (`UMaterialInstance*`) is applied when the roof is committed.
- **At runtime via Blueprint:** Set `ARoofBase::RoofMaterial` then call `UpdateRoofMesh()` — the material is applied to mesh section 0.
- **In the Blueprint editor:** Set `RoofMaterial` in the details panel; `OnConstruction` generates a preview mesh with that material.

### Preview Material

Before a roof is committed, it renders with `LotManager->ValidPreviewMaterial` — typically a translucent green material indicating valid placement. On commit, it switches to the real `RoofMaterial`.

### Material Sections

The procedural mesh is generated as a **single mesh section** (section index 0). Apply your material to that section. If you need per-face materials (e.g., different materials for each slope), you would need to extend the mesh generation in a subclass.

---

## Multi-Story Roofing

### Level Assignment

Each roof stores its floor level in `ARoofBase::Level`. The build tool auto-detects the correct level:
- If the cursor hits a wall, the roof is placed **one level above** that wall.
- Otherwise, the current camera trace level is used.

### Stacking Roofs

You can place separate roofs on different levels. Each roof independently generates its own supporting walls and room detection. The room system intelligently handles floor/ceiling relationships:

- Floor tiles generated by a roof room on Level 2 can simultaneously serve as ceiling tiles for a room on Level 1.
- When a roof is deleted, floor tiles that serve as ceilings for rooms below are **preserved** — only "pure roof floors" are removed.

### Basement Restriction

Roofs **cannot be placed on Level 0** (basement). Both the build tool and the wall generation functions enforce this restriction.

---

## Deleting Roofs

Roofs implement the `IDeletable` interface, enabling DEL-key deletion.

### Requirements

- The roof must be **committed** (`bCommitted == true`).
- The roof must be **selected** (in edit mode / `bInEditMode == true`).

Uncommitted preview roofs are cleaned up by the build tool's `EndPlay`, not by the delete system.

### What Happens on Deletion

`OnDeleted_Implementation()` performs a multi-step cleanup:

1. **Identifies roof walls** — collects `WallEdgeID` values from all walls created by this roof.
2. **Finds roof rooms** — scans for rooms at the same level whose boundary edges match the roof's walls.
3. **Removes floor tiles** — deletes floor tiles belonging to those roof rooms, **except** tiles that also serve as ceilings for rooms on the level below.
4. **Removes walls** — calls `CleanupWalls()` to remove all auto-generated walls.
5. **Invalidates rooms** — removes the roof rooms from the `RoomManager`.
6. **Exits edit mode** — clears selection state and unregisters from `BurbPawn`.

After `OnDeleted()` completes, `RequestDeletion()` (from `IDeletable`) destroys the actor.

---

## Undo / Redo

Roof operations are wrapped in the command pattern via `URoofCommand`:

| Action | Method |
|---|---|
| **Create** | `Commit()` — spawns a roof actor via `LotManager->SpawnRoofActor()`, then calls `CommitRoof()` |
| **Undo** | `Undo()` — removes the roof actor via `LotManager->RemoveRoofActor()` (walls cleaned up automatically) |
| **Redo** | `Redo()` — re-spawns and re-commits the roof with the same parameters |

The command stores all necessary state: location, direction, dimensions, thickness values, and material reference.

---

## Blueprint API Reference

All major functions are exposed as `UFUNCTION(BlueprintCallable)`. Here's a quick reference for the most useful ones:

### ARoofBase (and subclasses)

```
// Initialize a roof with all parameters
void InitializeRoof(ALotManager* InLotManager, const FVector& Location,
    const FVector& Direction, const FRoofDimensions& InDimensions,
    float InRoofThickness = 15.0f, float InGableThickness = 20.0f);

// Regenerate the procedural mesh
void GenerateRoofMesh();
void UpdateRoofMesh();

// Finalize placement (applies material, generates walls, detects rooms)
void CommitRoof();

// Edit mode
void EnterEditMode();
void ExitEditMode();
void ToggleEditMode();

// Rotation (45° increments)
void RotateLeft();
void RotateRight();

// Gizmo management
void SetupScaleTools();
void ShowScaleTools();
void HideScaleTools();

// Wall management
void GenerateSupportingWalls();  // virtual — overridden per roof type
void CleanupWalls();

// Footprint visualization
void DrawFootprintLines(const FRoofDimensions* CustomDimensions = nullptr);
void ClearFootprintLines();

// Query
ERoofType GetRoofType() const;           // BlueprintPure
bool IsScaleToolValid(EScaleToolType ToolType) const;  // BlueprintPure
```

### FRoofDimensions (Blueprint Struct)

```cpp
ERoofType RoofType;           // Gable, Hip, or Shed
int32 WallFlags;              // Bitmask: Front|Back|Left|Right
float FrontDistance;           // Extent forward from center (world units)
float BackDistance;            // Extent backward from center
float RightDistance;           // Extent right from center
float LeftDistance;            // Extent left from center
float FrontRake;              // Front overhang beyond the wall line
float BackRake;               // Back overhang
float RightEve;               // Right eave overhang
float LeftEve;                // Left eave overhang
float Height;                 // Peak height above base
float Pitch;                  // Slope angle in degrees (alternative to Height)
bool bUsePitchInsteadOfHeight; // If true, Height is calculated from Pitch

// Helpers
float GetWidth();   // LeftDistance + RightDistance
float GetLength();  // FrontDistance + BackDistance
```

### URoofComponent

```
// Static helpers
static FRoofVertices CalculateRoofVertices(const FVector& Location,
    const FVector& FrontDirection, const FRoofDimensions& Dimensions);
static float CalculateHeightFromPitch(float Pitch, float Width);
static float CalculatePitchFromHeight(float Height, float Width);
```

### ABuildRoofTool

```
void CreateRoofPreview();       // Spawn a translucent preview actor
void CreateRoof();              // Commit the preview, apply material, enter edit mode
void AdjustRoofPosition(...);   // Snap preview to grid under cursor
```

### ALotManager

```
ARoofBase* SpawnRoofActor(const FVector& Location, const FVector& Direction,
    const FRoofDimensions& Dims, float RoofThickness, float GableThickness,
    UMaterialInstance* Material);
void RemoveRoofActor(ARoofBase* RoofActor);
int32 GetRoofActorCount() const;
```

---

## Extending with Custom Roof Types

To create a new roof shape:

1. **Subclass `ARoofBase`** in C++ or Blueprint.
2. Override:
   - `GenerateRoofMesh()` — call your custom mesh generation on `RoofMeshComponent`.
   - `GenerateSupportingWalls()` — generate the right walls for your shape.
   - `GetRoofType()` — return the appropriate `ERoofType` (or extend the enum).
   - `IsScaleToolValid()` — declare which gizmos are relevant.
   - Optionally `SetupScaleTools()` to reposition gizmo handles for your geometry.
3. **Create a build tool subclass** from `ABuildRoofTool`, override `GetRoofType()`, and/or set `RoofActorClass` in Blueprint defaults to point at your new class.

The plugin's architecture is intentionally modular: each roof type is a thin subclass that only implements shape-specific logic.

---

## Known Limitations

| Limitation | Details |
|---|---|
| **No basement roofs** | Roofs cannot be placed on Level 0. The build tool and wall generation both enforce this. |
| **Single mesh section** | Each roof generates one procedural mesh section. Per-slope materials require subclass customization. |
| **No auto-trim to room shape** | Roofs are always rectangular. L-shaped or complex floorplans require multiple overlapping roofs (manual placement). |
| **Diagonal alignment** | 45° rotated roofs work geometrically but may not align perfectly with the orthogonal wall grid, creating small visual gaps at wall junctions. |
| **Overhang gizmos not wired** | `FrontRake`, `BackRake`, `RightEve`, and `LeftEve` values exist in `FRoofDimensions` and affect mesh generation, but the corresponding interactive gizmo components have been removed from the base class. Adjust overhangs via Blueprint properties or C++ for now. |
| **Hip roofs generate no walls** | Hip roofs slope on all sides, so no supporting walls are auto-created. If your design needs a short knee-wall under a hip roof, you'll need to place walls manually. |
| **No dormers or skylights** | The roof system generates solid sloped surfaces. Cut-outs for dormers, skylights, or chimneys are not supported natively. |
| **Pitch-based height** | `bUsePitchInsteadOfHeight` and `Pitch` are defined in `FRoofDimensions` and helper functions exist (`CalculateHeightFromPitch`), but the gizmo system always operates on `Height` directly. Pitch-based editing would need custom UI. |
| **No multi-select** | Only one roof can be in edit mode at a time (tracked by `BurbPawn->CurrentEditModeActor`). |

---

*Last updated: February 2026 — BurbArchitect Roof System Documentation*

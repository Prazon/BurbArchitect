# BurbArchitect — Doors, Windows & Portals

## Table of Contents

1. [Portal System Overview](#portal-system-overview)
2. [Class Hierarchy](#class-hierarchy)
3. [Placing Doors on Walls](#placing-doors-on-walls)
4. [Placing Windows on Walls](#placing-windows-on-walls)
5. [Wall Cutout Behavior](#wall-cutout-behavior)
6. [Door Animation System](#door-animation-system)
7. [Creating Custom Door Data Assets](#creating-custom-door-data-assets)
8. [Creating Custom Window Data Assets](#creating-custom-window-data-assets)
9. [Portal Validation & Placement Rules](#portal-validation--placement-rules)
10. [Deletion & Wall Restoration](#deletion--wall-restoration)
11. [Blueprint API Reference](#blueprint-api-reference)
12. [Multiplayer Notes](#multiplayer-notes)

---

## Portal System Overview

In BurbArchitect, **doors and windows are both "portals."** A portal is any object that gets embedded into a wall and automatically cuts a hole through the wall geometry. The system is designed so that doors and windows share a common placement pipeline and cutout renderer — they differ only in their visual representation and interaction behavior.

**Key concepts:**

| Concept | Description |
|---------|-------------|
| **Portal** | An actor placed on a wall that cuts a rectangular hole in the wall mesh. |
| **Portal Size** | Width × Height of the cutout, in centimeters (defined per data asset). |
| **Portal Offset** | Optional offset of the cutout from the portal's anchor point (X = horizontal, Y = vertical, in cm). |
| **Build Tool** | The transient tool actor that handles placement preview, snapping, validation, and spawning. Destroyed after the player finishes placing. |
| **Data Asset** | A `UDoorItem` or `UWindowItem` that defines everything about a catalog entry — mesh references, portal size, snap increments, and the actor class to spawn. |
| **BuildServer** | A `UWorldSubsystem` that executes portal creation through a command pattern, providing undo/redo support. |

### How it works at a high level

1. The player selects a door or window from the catalog UI.
2. A **Build Tool** (`ABuildDoorTool` or `ABuildWindowTool`) is spawned and attached to the cursor.
3. As the cursor moves over walls, the tool snaps to the wall surface, shows a live **preview actor** (with real-time wall cutouts), and validates the placement.
4. On click, the tool asks the **BuildServer** to place the portal. The BuildServer spawns the final actor, registers it with the affected wall sections, and renders the permanent cutout.
5. The build tool is destroyed.

---

## Class Hierarchy

```
UCatalogItem (UDataAsset)
  └── UArchitectureItem
        ├── UDoorItem          ← data asset for doors
        └── UWindowItem        ← data asset for windows

AActor
  └── ABuildTool
        └── ABuildPortalTool   ← shared wall-tracing, snapping, validation
              ├── ABuildDoorTool    ← door preview + placement
              └── ABuildWindowTool  ← window preview + placement

APlaceableObject (AActor)
  └── APortalBase              ← shared portal logic (cutout box, selection, IDeletable)
        ├── ADoorBase          ← skeletal mesh animation + door panel/frame static meshes
        └── AWindowBase        ← single static mesh for frame/glass

UBoxComponent
  └── UPortalBoxComponent      ← box that defines the cutout rectangle; extent is driven by PortalSize
```

> **Tip:** You will typically create a **Blueprint child** of `ADoorBase` or `AWindowBase` to set the skeletal mesh (doors) or tweak default settings. The static meshes for appearance are set at runtime from the data asset — you don't bake them into the Blueprint.

---

## Placing Doors on Walls

### What happens when the player activates the door tool

1. **Tool spawns** — `ABuildDoorTool` is instantiated. It reads its configuration from the selected `UDoorItem` data asset:
   - `ClassToSpawn` → which `ADoorBase` Blueprint to spawn
   - `PortalSize` → cutout dimensions
   - `PortalOffset` → cutout offset
   - `HorizontalSnap` / `VerticalSnap` → grid increments
   - `bSnapsToFloor` → `true` for doors (locks Z to the wall's base)
   - `DoorStaticMesh` / `DoorFrameMesh` → mesh assets for panel and frame

2. **Preview door** — A live `ADoorBase` actor is spawned and attached to the tool. It follows the cursor and shows the door's actual meshes. Wall cutouts are rendered in real time as you hover.

3. **Snapping** — The door snaps along the wall direction in `HorizontalSnap` increments (default 50 cm). Because `bSnapsToFloor = true`, the door's Z position is locked to the bottom of the wall segment.

4. **Validation** — The tool checks placement rules (see [Portal Validation](#portal-validation--placement-rules)). Invalid placements tint the preview with `InvalidMaterial`.

5. **Placement** — On click, the tool calls `UBuildServer::BuildPortal(...)` with the class, transform, affected wall indices, portal size/offset, and mesh references. The server spawns the final `ADoorBase`, registers it with the wall, and re-renders the cutout.

### Door mesh architecture (modular system)

Doors use a **skeleton + skin** pattern:

- **Skeletal Mesh** (`DoorSkeletalMesh`) — Set in your Blueprint subclass. Provides the animation skeleton (bones for the door panel and frame). The skeletal mesh itself is invisible at runtime.
- **Door Panel Static Mesh** (`DoorStaticMesh`) — Attached to a bone on the skeletal mesh. Set from the `UDoorItem` data asset at runtime.
- **Door Frame Static Mesh** (`DoorFrameMesh`) — Attached to a bone on the skeletal mesh. Set from the `UDoorItem` data asset at runtime.

This means you create **one** Blueprint with the animation skeleton, and many `UDoorItem` data assets that swap in different panel/frame meshes. Different visual styles share the same open/close animation.

---

## Placing Windows on Walls

Windows follow the same pipeline as doors with these differences:

| | Doors | Windows |
|---|---|---|
| Base class | `ADoorBase` | `AWindowBase` |
| Build tool | `ABuildDoorTool` | `ABuildWindowTool` |
| Data asset | `UDoorItem` | `UWindowItem` |
| Snaps to floor | Yes (`bSnapsToFloor = true`) | No — floats freely on the wall |
| Vertical movement | Locked to wall base Z | Slides vertically, snapped by `VerticalSnap`, clamped to wall bounds |
| Mesh setup | Skeletal mesh + 2 static meshes (panel + frame) | Single static mesh (frame + glass) |
| Animation | Open/close via skeletal anim | None (static) |
| Pool walls | **Not allowed** | Allowed |

### Vertical placement

Windows can be placed anywhere on the wall surface vertically. The tool:

1. Takes the cursor's impact point Z on the wall.
2. Snaps it to `VerticalSnap` increments.
3. Clamps the result so the entire portal rectangle stays within the wall's vertical bounds (base Z + half-height to top Z − half-height).

This means a window can never poke above or below the wall.

---

## Wall Cutout Behavior

When a portal is placed on (or previewed against) a wall, BurbArchitect automatically cuts a rectangular hole in the wall's procedural mesh.

### How cutouts work

1. Each wall segment (`FWallSegmentData`) maintains a `PortalArray` — a list of `APortalBase*` actors attached to that wall.
2. When the wall is rendered (or re-rendered), the `WallComponent` iterates through each portal in the array.
3. For each portal, it calls `APortalBase::DrawPortal()`, which stamps an opaque white rectangle onto the wall's render texture at the portal's position.
4. The wall mesh generator uses this texture as a mask to cut geometry out of the wall.

### Portal size and offset

The cutout rectangle is defined by two properties on the portal actor:

- **`PortalSize`** (`FVector2D`) — Width (X) and height (Y) in centimeters.
- **`PortalOffset`** (`FVector2D`) — Shifts the cutout rectangle from the portal's anchor point. X shifts horizontally, Y shifts vertically. Useful for doors where the cutout starts at floor level but the door's origin is at its center.

These values flow from the data asset → build tool → spawned portal actor.

### Live preview cutouts

During placement, the preview actor is temporarily registered with the wall's `PortalArray`. The wall sections are re-rendered each time the preview moves, giving the player real-time feedback of the hole being cut. When the tool is deactivated, the preview is unregistered and walls are restored.

### Multi-section portals

Large portals can span multiple wall sections (e.g., a wide picture window bridging two wall segments). The system uses `WallComponent::GetMultiSectionIDFromHitResult()` to determine all affected sections by checking the portal's bounding box against every wall segment. All affected sections receive the portal in their `PortalArray` and are re-rendered together.

---

## Door Animation System

Doors use a **bone-driven animation** approach:

### Component setup (in your ADoorBase Blueprint)

```
ADoorBase
  └── RootSceneComponent
        └── PortalBoxComponent (defines cutout, hidden in game)
        └── DoorSkeletalMesh (invisible skeleton — has bones for panel + frame)
              ├── DoorStaticMesh (attached to panel bone)
              └── DoorFrameMesh (attached to frame bone)
```

- The **skeletal mesh** is set in the Blueprint and is invisible at runtime (`SetVisibility(false)`). It exists solely to provide bones and play animations.
- The **static meshes** are attached to specific bones on the skeleton. When the skeleton plays an open animation, the bones drive the static meshes.

### Door state and RPCs

`ADoorBase` tracks an `bIsOpen` boolean that is replicated to all clients:

```cpp
// Toggle the door (works from any client or server)
DoorActor->ToggleDoor();

// Explicit open/close
DoorActor->OpenDoor();
DoorActor->CloseDoor();
```

These functions handle authority checks internally:
- **On server:** Sets `bIsOpen` directly; replication pushes the new state to clients.
- **On client:** Sends a `Server_OpenDoor` / `Server_CloseDoor` RPC to the server.

When `bIsOpen` replicates, `OnRep_IsOpen()` fires on clients — this is where you play the animation.

### Setting up the animation

In your `ADoorBase` Blueprint subclass:

1. Assign a `USkeletalMesh` to `DoorSkeletalMesh` that has bones for the panel and frame.
2. Create an `UAnimMontage` or `UAnimationAsset` for open and close.
3. In the Blueprint Event Graph, override or extend `OnRep_IsOpen` (or use the C++ hook) to call `PlayAnimation` on the skeletal mesh component based on `bIsOpen`.
4. Attach `DoorStaticMesh` to the panel bone and `DoorFrameMesh` to the frame bone in the Blueprint's component tree.

> **Note:** The static meshes assigned in the Blueprint's component tree are **defaults**. At runtime, the `UDoorItem` data asset overrides them via `ApplyDoorMesh()` and `ApplyDoorFrameMesh()`. This lets one Blueprint serve many visual styles.

---

## Creating Custom Door Data Assets

### Step-by-step

1. **Right-click in Content Browser** → **Miscellaneous** → **Data Asset**.
2. Select **DoorItem** as the class.
3. Name it descriptively (e.g., `DA_Door_SingleWood`).

### Configure the data asset

| Property | Category | Description |
|----------|----------|-------------|
| `DisplayName` | Catalog | Name shown in the build catalog UI. |
| `Icon` | Catalog | Thumbnail brush for the catalog. |
| `Cost` | Catalog | Simoleon cost. |
| `Category` | Catalog | Root catalog category (e.g., "Doors"). |
| `Subcategory` | Catalog | Optional subcategory (e.g., "Interior Doors"). |
| `BuildToolClass` | Catalog | Set to `ABuildDoorTool` (or your subclass). |
| **`PortalSize`** | Portal \| Shape | Width × Height of the wall cutout in cm (e.g., `100 × 210`). |
| **`PortalOffset`** | Portal \| Shape | Offset of the cutout from the placement point in cm. Use `(0, 0)` if the door origin is centered on the cutout. |
| **`HorizontalSnap`** | Portal \| Snapping | Snap increment along the wall (cm). Default: `50`. |
| **`VerticalSnap`** | Portal \| Snapping | Snap increment vertically (cm). Default: `25`. |
| **`bSnapsToFloor`** | Portal \| Snapping | Should be `true` for doors. Locks the door to the wall's base Z. |
| **`ClassToSpawn`** | Portal \| Spawning | The `ADoorBase` Blueprint class to instantiate (e.g., `BP_Door_Standard`). |
| **`DoorStaticMesh`** | Portal \| Mesh | Static mesh for the door panel (swinging part). |
| **`DoorFrameMesh`** | Portal \| Mesh | Static mesh for the door frame (stationary surround). |

### Mesh preparation tips

- Model the door panel and frame as **separate static meshes**.
- The panel mesh pivot should be at the hinge edge so bone-driven rotation looks correct.
- Both meshes will be attached to bones on the skeletal mesh defined in your `ADoorBase` Blueprint — make sure the bone names and transforms match your mesh origins.
- Portal size should match the hole you want, not the mesh bounding box. A door with a 100 cm wide frame might need a `PortalSize` of `(90, 200)` if the frame overlaps the wall.

---

## Creating Custom Window Data Assets

### Step-by-step

1. **Right-click in Content Browser** → **Miscellaneous** → **Data Asset**.
2. Select **WindowItem** as the class.
3. Name it descriptively (e.g., `DA_Window_DoublePaned`).

### Configure the data asset

| Property | Category | Description |
|----------|----------|-------------|
| `DisplayName` | Catalog | Name shown in the build catalog UI. |
| `Icon` | Catalog | Thumbnail brush for the catalog. |
| `Cost` | Catalog | Simoleon cost. |
| `Category` | Catalog | Root catalog category (e.g., "Windows"). |
| `Subcategory` | Catalog | Optional subcategory. |
| `BuildToolClass` | Catalog | Set to `ABuildWindowTool` (or your subclass). |
| **`PortalSize`** | Portal \| Shape | Width × Height of the wall cutout in cm (e.g., `120 × 80`). |
| **`PortalOffset`** | Portal \| Shape | Offset of the cutout from the placement point. Useful to shift the cutout up from the window mesh's origin. |
| **`HorizontalSnap`** | Portal \| Snapping | Snap increment along the wall (cm). Default: `50`. |
| **`VerticalSnap`** | Portal \| Snapping | Snap increment vertically (cm). Default: `25`. |
| **`bSnapsToFloor`** | Portal \| Snapping | Typically `false` for windows (lets them float vertically). Set to `true` if you want a floor-level window. |
| **`ClassToSpawn`** | Portal \| Spawning | The `AWindowBase` Blueprint class to instantiate (e.g., `BP_Window_Standard`). |
| **`WindowMesh`** | Portal \| Mesh | Static mesh for the window frame and glass (single mesh). |

### Mesh preparation tips

- Windows use a **single static mesh** — combine the frame and glass into one asset.
- The mesh pivot point should be at the center of the window. The system positions the actor at the portal's anchor point on the wall.
- Glass should use a translucent material on its own material slot.

---

## Portal Validation & Placement Rules

The portal placement system enforces several rules to prevent invalid placements. When a rule fails, the preview turns to the `InvalidMaterial` (typically a red translucent material) and clicks are rejected.

### Validation rules (in order of evaluation)

| Rule | Description |
|------|-------------|
| **Must hit a wall** | The cursor trace must hit a `UWallComponent`. If it hits open ground or another actor type, placement is invalid. The preview follows the cursor at ground level with the invalid material. |
| **Valid wall section** | The hit must resolve to a valid wall array index via `GetWallArrayIndexFromHitLocation()`. |
| **Correct level** | The wall section must be on the same level the player is currently building on. |
| **Full-height walls only** | The wall must have a `WallEdgeID` linked to the WallGraph. Decorative walls (half walls, fences) return `EdgeID == -1` and are rejected. |
| **No basement exterior walls** | If the wall is on a basement level and one of its adjacent rooms is the outside (Room ID 0), portals are rejected. Underground exterior walls cannot have doors or windows. |
| **No doors on pool walls** | Pool walls (`bIsPoolWall == true`) reject door placement. Windows are allowed on pool walls. The system uses `bSnapsToFloor` as a proxy to distinguish doors from windows. |
| **Junction clearance** | The portal position must be at least **15 cm** away from any wall junction where 3 or more walls meet (`IsPositionNearJunction()`). |
| **Within wall bounds** | The portal's bounding box (derived from `PortalBoxComponent`) must fit entirely within the wall segment's horizontal extent (`IsPortalWithinWallBounds()`). A portal cannot hang off the end of a wall. |
| **Vertical bounds** | For doors: the top of the portal cannot exceed the wall's top. For windows: both top and bottom are clamped so the portal stays within the wall's vertical extent. |

### Snapping behavior

- **Horizontal:** The cursor position is projected onto the wall's line direction and snapped to `HorizontalSnap` increments, then clamped to `[0, WallLength]`.
- **Vertical (windows):** The impact Z is snapped to `VerticalSnap` increments, then clamped so the full portal stays within `[WallBaseZ + HalfHeight, WallTopZ - HalfHeight]`.
- **Vertical (doors):** Z is locked to `WallBaseZ` (the wall's start Z).

---

## Deletion & Wall Restoration

### How deletion works

Both `ABuildDoorTool` and `ABuildWindowTool` implement `Delete_Implementation()`:

1. The tool enters deletion mode (triggered by your UI).
2. As the player hovers over walls, the tool identifies the wall section under the cursor.
3. It searches the wall section's `PortalArray` for a portal within 100 cm of the cursor's snapped position.
4. When found and clicked:
   - The portal is removed from the wall's `PortalArray`.
   - The portal actor is destroyed.
   - `WallComponent::RegenerateWallSection()` is called, which re-renders the wall **without** the portal's cutout — the wall is seamlessly restored.

### What happens to the wall

Since walls are procedurally generated, restoring a wall after portal deletion is automatic. The wall simply re-renders without the portal in its `PortalArray`, producing a solid wall again. There is no residual damage or hole.

### IDeletable interface

All portals implement `IDeletable`, providing:

- `CanBeDeleted()` — Returns `true` (portals can always be deleted once placed).
- `OnDeleted()` — Cleans up selection state (disables custom depth highlighting).
- `IsSelected()` — Returns the portal's current selection state.

---

## Blueprint API Reference

### APortalBase (shared by all portals)

| Function | Category | Description |
|----------|----------|-------------|
| `Select()` | Selection | Highlights the portal (enables custom depth stencil on all mesh components). |
| `Unselect()` | Selection | Removes selection highlight. |
| `ToggleSelection()` | Selection | Toggles between selected/unselected. |
| `DrawPortal(UCanvas*, FVector2D)` | — | Draws the portal's cutout rectangle onto a canvas at the given texture position. Called internally by the wall renderer. |

| Property | Type | Description |
|----------|------|-------------|
| `PortalSize` | `FVector2D` | Width (X) and height (Y) of the cutout in cm. Replicated. |
| `PortalOffset` | `FVector2D` | Offset of the cutout from the actor origin. Replicated. |
| `Box` | `UPortalBoxComponent*` | The box component that defines the cutout volume. Read-only. |
| `CurrentLot` | `ALotManager*` | The lot this portal belongs to. |
| `CurrentWallComponent` | `UWallComponent*` | The wall component this portal is placed on. |
| `bIsSelected` | `bool` | Whether the portal is currently selected. Read-only. |

### ADoorBase

| Function | Category | Description |
|----------|----------|-------------|
| `OpenDoor()` | Door | Opens the door. Handles client→server RPC automatically. |
| `CloseDoor()` | Door | Closes the door. Handles client→server RPC automatically. |
| `ToggleDoor()` | Door | Toggles between open and closed. |
| `ApplyDoorMesh()` | Door | Loads and applies the `DoorMeshAsset` to the `DoorStaticMesh` component. |
| `ApplyDoorFrameMesh()` | Door | Loads and applies the `DoorFrameMeshAsset` to the `DoorFrameMesh` component. |

| Property | Type | Description |
|----------|------|-------------|
| `DoorSkeletalMesh` | `USkeletalMeshComponent*` | Animation skeleton (set in Blueprint). |
| `DoorStaticMesh` | `UStaticMeshComponent*` | Door panel mesh component. |
| `DoorFrameMesh` | `UStaticMeshComponent*` | Door frame mesh component. |
| `DoorMeshAsset` | `TSoftObjectPtr<UStaticMesh>` | Replicated soft reference to the door panel mesh. |
| `DoorFrameMeshAsset` | `TSoftObjectPtr<UStaticMesh>` | Replicated soft reference to the door frame mesh. |
| `bIsOpen` | `bool` | Current door state. Replicated. Read-only (use `OpenDoor()`/`CloseDoor()`). |

### AWindowBase

| Function | Category | Description |
|----------|----------|-------------|
| `ApplyWindowMesh()` | Window | Loads and applies the `WindowMeshAsset` to the `WindowMesh` component. |

| Property | Type | Description |
|----------|------|-------------|
| `WindowMesh` | `UStaticMeshComponent*` | Window frame/glass mesh component. |
| `WindowMeshAsset` | `TSoftObjectPtr<UStaticMesh>` | Replicated soft reference to the window mesh. |

### UBuildServer (World Subsystem)

| Function | Category | Description |
|----------|----------|-------------|
| `BuildPortal(...)` | Build Server \| Portals | Places a portal on a wall with full undo/redo support. |

**`BuildPortal` signature:**

```cpp
void BuildPortal(
    TSubclassOf<APortalBase> PortalClass,    // Blueprint class to spawn
    const FVector& Location,                  // World position on the wall
    const FRotator& Rotation,                 // Wall-aligned rotation
    const TArray<int32>& WallArrayIndices,    // Affected wall section indices
    const FVector2D& PortalSize,              // Cutout dimensions (cm)
    const FVector2D& PortalOffset,            // Cutout offset (cm)
    TSoftObjectPtr<UStaticMesh> WindowMesh,   // Window mesh (nullptr for doors)
    TSoftObjectPtr<UStaticMesh> DoorStaticMesh, // Door panel mesh (nullptr for windows)
    TSoftObjectPtr<UStaticMesh> DoorFrameMesh   // Door frame mesh (nullptr for windows)
);
```

> **Usage from Blueprint:** Access the BuildServer via `GetWorld()->GetSubsystem<UBuildServer>()` in C++ or the **Get Subsystem** node in Blueprint (select `BuildServer`).

---

## Multiplayer Notes

The portal system is fully replicated for listen-server multiplayer:

- **Build tools** replicate `TargetLocation`, `TargetRotation`, `bValidPlacementLocation`, and all portal configuration properties so all clients see the same preview state.
- **Preview actors** are replicated (`PreviewDoor`/`PreviewWindow` are `UPROPERTY(Replicated)`), so all players see the preview door/window as it's being placed.
- **Portal actors** replicate their mesh assets via `OnRep` callbacks (`OnRep_DoorMeshAsset`, `OnRep_WindowMeshAsset`, etc.) and their size/offset via `OnRep_PortalSize`/`OnRep_PortalOffset`. `PostNetInit()` ensures all properties are applied on late-joining clients.
- **Door state** (`bIsOpen`) is replicated with `OnRep_IsOpen`. Door open/close uses Server RPCs (`Server_OpenDoor`, `Server_CloseDoor`), so any client can interact with doors.
- **Placement validation** runs on the server (authority) for doors. The server's validated position is replicated to clients. Windows run validation on all machines using replicated `PortalSize` to avoid needing the preview actor.

---

*This document covers BurbArchitect's portal system as of the current source revision. For wall construction, floors, roofs, and terrain, see the corresponding user guide sections.*

# BurbArchitect

An intuitive building system plugin for Unreal Engine 5.7. Grid-based construction of walls, floors, roofs, terrain, and stairs with full multiplayer support.

Built for community-driven life simulation games.

** THIS IS HEAVILY WORK-IN-PROGRESS, MULTI-FLOOR FEATURES LIKE STAIRS, ROOFS ARE INCOMPLETE. **

## Features

- **Tile-based grid system** with configurable lot sizes and multi-story support
- **Wall system** with dual-component architecture (graph logic + procedural mesh rendering)
- **Floor system** with merged mesh optimization (one draw call per level)
- **Roof generation** with configurable pitch and automatic gable ends
- **Staircase placement** between levels
- **Terrain sculpting** with raise, lower, and flatten brushes
- **Portal system** for doors and windows with automatic wall cutouts
- **Object placement** with catalog/buy system
- **Wallpapering** with per-face material assignment
- **Undo/Redo** via command pattern
- **Multiplayer** with server-authoritative RPC architecture
- **Room detection** via flood-fill with automatic room ID assignment

## Requirements

- Unreal Engine 5.7+
- C++ project (plugin includes C++ source)

## Installation

1. Clone or download this repository into your project's `Plugins/` folder:
   ```
   YourProject/
   └── Plugins/
       └── BurbArchitect/
   ```
2. Regenerate project files (right-click `.uproject` > "Generate Visual Studio project files")
3. Build and launch the editor

## Quick Start

1. Place an `ALotManager` actor in your level
2. Configure grid size (`GridSizeX`, `GridSizeY`, `GridTileSize`)
3. Call `GenerateGrid()` to create the tile grid
4. Spawn build tools (`ABuildWallTool`, `ABuildFloorTool`, etc.) and assign them to the lot
5. See the `Content/` folder for example Blueprints and a sample HUD

## Architecture

The plugin follows a clean separation of concerns:

- **LotManager** — Central manager owning the grid and all building components
- **WallGraphComponent** — Source of truth for wall logic and O(1) spatial queries
- **WallComponent** — Procedural mesh rendering for walls
- **FloorComponent** — Merged mesh floor rendering with spatial hash lookups
- **BuildTool** — Abstract base class with state machine for all tools
- **BuildServer** — World subsystem managing command pattern (undo/redo)

## Multiplayer

Designed for listen-server architecture. Clients receive replicated parameters and reconstruct geometry locally, keeping bandwidth minimal.

## License

MIT License. See [LICENSE.md](LICENSE.md).

## Documentation

Full documentation: [https://prazon.github.io/BurbArchitect/](https://prazon.github.io/BurbArchitect/)

## Community

- [Discord](https://discord.com/invite/5qQjp9Zyjj)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

# Contributing to BurbArchitect

Thanks for your interest in contributing! This project aims to be the foundation for community-driven life simulation games built in Unreal Engine.

## How to Contribute

### Reporting Bugs

- Open a GitHub Issue with steps to reproduce
- Include your UE version and platform
- Screenshots or videos are very helpful

### Suggesting Features

- Open a GitHub Issue tagged as a feature request
- Describe the use case, not just the solution
- Check existing issues first to avoid duplicates

### Submitting Code

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make your changes
4. Test in a UE project
5. Submit a Pull Request with a clear description of what and why

### Code Guidelines

- Follow Unreal Engine C++ coding standards (Epic naming conventions)
- Prefix classes: `A` for Actors, `U` for UObjects/Components, `F` for structs, `E` for enums
- Use `UPROPERTY`/`UFUNCTION` macros for anything that needs editor/Blueprint exposure
- Keep wall logic in `WallGraphComponent`, rendering in `WallComponent`
- Use grid-based lookups over geometric calculations where possible
- Add multiplayer RPC variants for any tool actions

### Architecture Rules

- **WallGraphComponent** is the source of truth for wall data — never query `WallComponent` for spatial logic
- **BuildServer** command pattern is required for wall/floor creation (enables undo/redo)
- **Grid coordinates** over world coordinates wherever possible
- **Merged mesh** approach for floors — one mesh section per level, not per tile

## License

By contributing, you agree that your contributions will be licensed under the MIT License. See [LICENSE.md](LICENSE.md).

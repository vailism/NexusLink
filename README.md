# NexusLink

NexusLink is a cross-platform GUI application built with Dear ImGui, GLFW, and OpenGL. It is designed for both Windows and macOS, and is always built in GUI mode (no CLI fallback).

## Features
- Modern GUI using Dear ImGui
- Cross-platform: Windows (MinGW) and macOS
- OpenGL rendering backend
- No CLI mode: GUI is always enabled
- Validated, ready-to-run builds for both platforms

## Building

### macOS
- Requires CMake, Xcode command line tools, and OpenGL
- Build with:
  ```sh
  cmake -B build-gui -DNEXUSLINK_ENABLE_GUI=ON
  cmake --build build-gui --config Release
  ```
- The app bundle will be in `build-gui/NexusLink.app`.

### Windows (Cross-compile from macOS)
- Requires CMake, MinGW-w64, and OpenGL/GLFW dependencies
- Build with:
  ```sh
  cmake -B build-gui -G "MinGW Makefiles" -DNEXUSLINK_ENABLE_GUI=ON
  cmake --build build-gui --config Release
  ```
- The executable will be in `build-gui/NexusLink.exe`.

## Distribution
- The `NEXUS-share` folder contains the ready-to-distribute executable for each platform.

## License
MIT License

---

For more details, see the source code and CMake configuration.

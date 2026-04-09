# Building GCPad

## Prerequisites

### Windows
- **Visual Studio 2022** or later
- **CMake 3.15** or later
- **Windows 10/11** (64-bit)

### Linux
- **CMake 3.15** or later
- **X11 development libraries** (libx11-dev)
- **XTest extension** (libxtst-dev)
- **Xi extension** (libxi-dev)
- **SDL2 development libraries** (libsdl2-dev)

### Installing Linux Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install cmake libx11-dev libxtst-dev libxi-dev libsdl2-dev

# Fedora
sudo dnf install cmake libX11-devel libXtst-devel libXi-devel SDL2-devel

# Arch Linux
sudo pacman -S cmake xorg-libs libxtst libxi sdl2
```

---

## Building on Windows

### Using Visual Studio

1. Open a Developer Command Prompt for VS2022
2. Navigate to the project directory
3. Run:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Build Options

```powershell
# Default build (frontend + GUI + remapper)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Library only (no frontends)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DGCPAD_BUILD_FRONTEND=OFF -DGCPAD_BUILD_FRONTEND_GUI=OFF

# Specific components
cmake -B build -G "Visual Studio 17 2022" -A x64 -DGCPAD_BUILD_REMAP=ON -DGCPAD_BUILD_FRONTEND=ON -DGCPAD_BUILD_FRONTEND_GUI=OFF
```

### Output Artifacts

After building, you'll find:

| Component | Path |
|-----------|------|
| gcpad.dll | `build/GCPad_Lib/Release/gcpad.dll` |
| gcpad.lib | `build/GCPad_Lib/Release/gcpad.lib` |
| gcpad_remap.lib | `build/GCPad_Remap/Release/gcpad_remap.lib` |
| GCPad_Frontend_CMD.exe | `build/GCPad_Frontend_CMD/Release/GCPad_Frontend_CMD.exe` |
| GCPad_Frontend_GUI.exe | `build/GCPad_Frontend_GUI/Release/GCPad_Frontend_GUI.exe` |

---

## Building on Linux

### Configure and Build

```bash
# Configure
cmake -B build

# Build
cmake --build build -j$(nproc)
```

### Build Options

```bash
# Library only
cmake -B build -DGCPAD_BUILD_FRONTEND=OFF -DGCPAD_BUILD_FRONTEND_GUI=OFF

# With GUI (requires SDL2)
cmake -B build -DGCPAD_BUILD_FRONTEND_GUI=ON
```

### Output Artifacts

| Component | Path |
|-----------|------|
| libgcpad.so | `build/GCPad_Lib/libgcpad.so` |
| libgcpad_remap.a | `build/GCPad_Remap/libgcpad_remap.a` |
| GCPad_Frontend_GUI | `build/GCPad_Frontend_GUI/GCPad_Frontend_GUI` |

---

## Running the Frontends

### Windows

```powershell
# CMD Frontend
.\build\GCPad_Frontend_CMD\Release\GCPad_Frontend_CMD.exe

# GUI Frontend
.\build\GCPad_Frontend_GUI\Release\GCPad_Frontend_GUI.exe
```

### Linux

```bash
# GUI Frontend
./build/GCPad_Frontend_GUI/GCPad_Frontend_GUI
```

---

## Troubleshooting

### Windows

**Missing Visual Studio**
> Ensure Visual Studio 2022 is installed with "Desktop development with C++" workload.

**CMake not found**
> Add CMake to PATH or use "Developer Command Prompt for VS2022" which has it pre-configured.

**SDL2.dll not found**
> The build process automatically copies SDL2.dll next to the executable. If you move the .exe, ensure SDL2.dll is in the same directory.

### Linux

**X11 not found**
> Install libx11-dev: `sudo apt-get install libx11-dev`

**XTest extension errors**
> Install libxtst-dev: `sudo apt-get install libxtst-dev`

**Permission denied**
> You may need to run with elevated permissions or configure X11 permissions for input injection.

---

## Integration

To use GCPad in your own project, see [Implementing a Frontend](Frontend-Implementation.md).

Work in progress controller API for UnionCrax.Direct, mainly intended for people who do not want to install Steam or cannot. Currently targeting Windows, but hoping to support Linux too soon, if I can figure it out as it is right now.

All I have currently is a Dualshock 4 controller. I would be grateful for help getting properly working with all others listed and not listed, any help at all with this will be more than appreciated. I believe at this point I am stumped. I may also be just going about this hella wrong though, so please sanity check me and the nightmare I'm releasing here.

I wrote it based on how I assume Steam Input works, so that should explain a lot of it lol. 

~vee

gcpad = gayme controller pad, cause this shit is kinda gay ngl lmao. its busted and it aint a good busted either. xD
definitely open to better names.

## Dependencies setup (vcpkg)

A helper script is available to pull required dependencies and copy the needed libraries into `libs/`:

- `scripts\setup-deps.ps1`

Run:

```powershell
cd c:\Users\mikek\OneDrive\Documents\GitHub2\DS4Windows\GCPad_API
powershell -ExecutionPolicy Bypass -File scripts\setup-deps.ps1
```

Then configure CMake with vcpkg toolchain:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Debug
```

This downloads SDL2 and DirectX dev packages and copies xinput/dinput/dxguid libs if they exist in the Windows SDK. 
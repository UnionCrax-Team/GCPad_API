## Good luck getting this to build, I barely was able to. Use Visual Studio 2022. I'm gonna try dropping cmake entirely and go full msbuild. I'm gonna take a short break from this though.  

Work in progress controller API for UnionCrax.Direct, mainly intended for people who do not want to install Steam or cannot. Currently targeting Windows, but hoping to support Linux too soon, if I can figure it out as it is right now.

All I have currently is a Dualshock 4 controller. I would be grateful for help getting properly working with all others listed and not listed, any help at all with this will be more than appreciated. I believe at this point I am stumped. I may also be just going about this hella wrong though, so please sanity check me and the nightmare I'm releasing here.

I wrote it based on how I assume Steam Input works, so that should explain a lot of it lol. 

~vee

gcpad = gayme controller pad, cause this shit is kinda gay ngl lmao. its busted and it aint a good busted either. xD
definitely open to better names.


```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

This __should__ work, but as I really don't want to include the entire vcpkg bs and barely want to leave SDL2 in too, I've copied the files that I assume are what it uses to a cmake folder. I haven't tested it yet, and it's just the way I like it too. XD
Untested, likely unnecessary, and probably breaks everything too. XD

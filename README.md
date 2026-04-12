### GCPad_API 

Work in progress controller API for UnionCrax.Direct, mainly intended for people who do not want to install Steam or cannot. Currently targeting Windows, and starting on Linux support currently.

All I have currently is a Dualshock 4 controller. I would be grateful for help getting properly working with all others listed and not listed, any help at all with this will be more than appreciated. I believe at this point I am stumped. I may also be just going about this hella wrong though, so please sanity check me and the nightmare I'm releasing here.

I wrote it based on how I assume Steam Input works, so that should explain a lot of it lol. 

~vee

- GCPad_Frontend_CMD
  - Terminal based example tester / frontend for GCPad_API.
- GCPad_Frontend_GUI
  - Dear Imgui based example tester / frontend for GCPad_API, should be more of an example than the other.
- GCPad_Lib
  - Where everything is basically.
- GCPad_Remap
  - Remapper code, this is what will translate your inputs.

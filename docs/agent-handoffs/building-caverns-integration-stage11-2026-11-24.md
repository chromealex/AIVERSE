# Building preview on Metal

Commit `85dd2b78cb6c39b6ac1e7b53e1e65b29496db7c9` fixes the RGBA8 attachment format in `engine/gfx/metal/GfxMetal.mm`. A standalone offscreen device has no CAMetalLayer. Reading its pixelFormat returned an invalid color format, although pipeline creation reported success.

The native Studio Building preview test passed on Mac with 83,374 pixels changing between two camera angles. This targeted test used the Stage 8 gate checkout with the Metal fix applied. The next step is to run the full Building gate on the integrated tree.

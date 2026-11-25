# Project pointer cursors (#159)

Acceptance covers hotspot alignment, normal and targeting states, edge directions, UI cursor names, invalid assets, animation and window teardown. Runtime checks deferred.
5709dfceea8a6ae3139cd7949c4f24308452ab26
Platform backend stage adds Cocoa, Win32, X11 and browser cursors. X11 uses ARGB with libXcursor and monochrome fallback with base Xlib. Mobile has a no-op. Client state selection and asset frames follow.
e56a20b61b6c370bfdb49c901512daed5a076e6e
Client consumes game.json cursors, caches decoded frames, resolves mouse state and RmlUi cursor names; default cursor restored on shutdown. Runtime and native integration checks pending.
eef998028c076a5364910b951d5f771fc8927ad8
Active ability levels now select required target kind. docs/game-format.md documents animated frames, coordinate units, fallback and platform support. No runtime tests executed in this stage.
6b9f2786b7e4fe6467e2a7c7275896ab6d56994a
Mac compiler found GameClient.cpp called placeCentre without the required success flag. The placement cursor now rejects off-map picks before checking the build footprint. Recompile and visual checks pending.

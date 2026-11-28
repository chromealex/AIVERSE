# Studio HTML5 build console

The ImGui ConsoleWindow exposed the builder transcript while HTML Console exposed editor activity. The stage adds a build view to Console using Workspace and the existing build bridge. Publish and Console both follow the run identifier and UTF-8 byte offset. The new view searches the complete log and renders up to 120 rows per page.

The Mac bundle displayed a 49-line size estimate from the native packager in a disposable project. Copy full log matched all 3103 bytes on disk. The capture is in docs/evidence/studio-console-html5/build-output.png. Remaining checks include a failing build on Windows, Finder destination and end-to-end migration gates.

# Window menu categories and Wind localization (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 9fccaa6af964ce380d4f2ad0a8048e3eb75dc119. Registry grouping 3d26f64591e87479dd15386b90eba2f5c18f7e7d; menu b52cdbbd82ea310cf1097256d227c70c5d7a39ae; Wind localization b7be378c00a063c54c0d99f68db4d95d8da13560; fixture da292eb7a02e67dcb1d3e9944978c32de704dc33.

- Replace the flat duplicated open/all panel listing with ten semantic submenus and one Workspaces submenu. Preserve checkmarks, existing docking behavior, floating windows, and searchable Command Palette. Each panel has exactly one category; adding new panel requires a category in the exhaustive typed mapping.
- Translate Wind registry title and all fixed visible text in WindPanel (controls, debug views, warnings header, mask and profile labels). Native-provided warning messages are unchanged.
- Static source readback verified 61/61 registered panels, no missing or duplicate categories, and no Cyrillic literals in WindPanel. Shell fixture added. Frontend typecheck, npm tests, native visual QA NOT RUN.
- User interrupted S6f.4b.2 to prioritize Window usability. The three preliminary checkpoint changes were restored to the previous source baseline, with their commits retained in branch history. Resume that work after UI task; no claim of atomic map history persistence.

# Stage 89 — named project Replay Presets

Parent `e32ff7c174b457916b22b1348f69eb46d158670f`; branch `feature/spectrafx-html5-data-interfaces-20261123`.

New Workspace/MCP operations fx_replay_preset_list/get/save, also allowed in Studio. Assets stored under project/content/spectrafx/replays as name.spectra-replay.json. Immutable named saves, bounded 64 KiB UTF-8 serialized file and 256 entries, strict names/project/graph-v2 identity, timeline/camera/seed/scalar validation, symlink checks and temporary-file publication. Get can check exact authored graph signature. UI lists, loads and saves named presets without direct filesystem access. Named saves are reproducible recipe snapshots, not serialized GPU particles (SPFT remains separate); external referenced resource content fingerprints and relocation of project-bound presets remain future work. Existing project file concurrency is serialized by Workspace, not an OS-wide exclusive writer guarantee.

Workspace regressions cover save/list/get, duplicate, stale graph/project, unsafe names. No native or web tests executed. Main untouched.

# Stage66 — demo persistence and diagnostics
Branch: feature/world-processes-stage31-bidirectional-handles; main unchanged.
5b3eaf3c replaces manual PHYSIS tick with native physis::install using ExternalAuthority flood mode; adds --load FILE and --save FILE through Simulation deserialize/serialize and forest statistics (rooted, revision, tracked, dead, recruited, lastStep).
Example: aiverse_evolve_forest_demo examples/native/world_processes/evolve-forest-demo.json 1024 --replay --save demo.bin
Resume with --load demo.bin. Save files require identical setup and schema. No compile, execution or save roundtrip performed. Next: integration source audit, graphical visualization if needed, then acceptance.

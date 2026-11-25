# Building Graph / Caverns integration — Stage 5

Owner: integration/building-caverns-20261124. Code commit: 6cb62d66f.

The first KINGSTON Mac Building/Studio gate ran on b73290cc9. MCP build and registry audit passed (368 tools; Building diagnostics registered), scoped Building MCP smoke passed, npm install/web tests/web build passed, and Studio Python tests passed. Native Studio build failed: `engine/systems/BuildingStructure.h:269` used undeclared `style` in `architecturalBody`, while the current style variable in that scope is `st`. The report is `/Volumes/KINGSTON/AIVERSE-builds/building-caverns-gate-build/stage37-studio/reports/20261124T183158Z-92365/studio-build.log`. This is failed gate evidence, not a pass.

Replaced `style.band.material` with `st.band.material` in code commit 6cb62d66f. Retest of this new commit is **NOT RUN** at handoff creation. Earlier successful standalone merged-tree checks: Studio typecheck/build, web tests and two native graph/Caverns CTests on b73290cc9. Next: update the KINGSTON checkout to this exact commit, rerun the failed gate and then current Caverns scenario acceptance.

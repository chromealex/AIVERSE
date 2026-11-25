# Building Graph / Caverns integration — Stage 6

Owner: integration/building-caverns-20261124. Code commit: ef9dd165c.

The second and third KINGSTON Building/Studio gate attempts on 38934ccb6 passed MCP registry, Building MCP smoke, web tests/build and Studio Python checks, then failed during `aiverse_hosted_client` compilation. `GameClient.cpp` reached Caverns visual payloads whose public value types include `box3d/box3d.h`, but the hosted client target lacked that include path; `HostedClient.cpp` also instantiated `GameClient` with an implicit constructor while its `unique_ptr<CaveClientVisualHost>` member was incomplete. This failed with both `AIVERSE_ENABLE_PHYSICS=OFF` and `ON`. Reports: `/Volumes/KINGSTON/AIVERSE-builds/building-caverns-gate-build/stage37-studio/reports/20261124T184346Z-15044` and `20261124T184759Z-25237`.

The code declares and defines `GameClient`'s constructor out of line and makes the Box3D value-type include path explicit on `aiverse_hosted_client`; no physics runtime target is forced on. Verification: Mac CMake configure with `AIVERSE_ENABLE_PHYSICS=OFF` exit 0, `cmake --build ... --target aiverse_hosted_client -j 4` exit 0, `git diff --check` exit 0. Full Studio gate and Caverns native scenarios on this exact commit are **NOT RUN**. Next: update the KINGSTON checkout and rerun both gates before considering main.

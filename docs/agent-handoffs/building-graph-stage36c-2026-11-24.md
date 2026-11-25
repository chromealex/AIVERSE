# Stage 36C — native Mac build blocker repair

Owner: Building Graph integration. Branch `integration/building-graph-stage36b-20261124`; previous HEAD 1632bffc5a624a108f8c0ff0e6c19bcba7992868. User supplied `build(6).log` from Mac checkout `AIVERSE-building-stage36c`: build stopped with make Error 2 at 22%, first compiler error BuildingStyle.h:13:10 `BuildingGen.h` file not found, target `aiverse_library_building_feature_tests`. CTest did not run. Log does not contain exact tested SHA or complete invocation; no inference of passed tests from linked binaries.

Repository tree confirms canonical header `engine/systems/BuildingGen.h`; old same-directory include no longer exists. Fixed `libraries/include/aiverselib/systems/BuildingStyle.h` to include canonical path in 04d61eb64f7b972da373722d94dd3ffdc72586f3. Manifest records blocker at 8288d64c2dad84a19c673e74bf67e4751ed0f9df. No duplicate header, include-path workaround or gameplay changes. Source readback verified. No post-fix native compile, CTest, runtime, MCP or visual verification yet.

Next: pull exact branch head on Mac, retain KINGSTON build directory, rerun `./run-tests.sh --quick`, review new build/test logs. No main merge before merged-tree gates pass.

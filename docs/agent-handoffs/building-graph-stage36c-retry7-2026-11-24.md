# Stage 36C — retry 7: direct vehicle persistence include

User build(20261124-082140).log: native build FAIL at 82%; CTest NOT RUN; exact tested SHA not printed. Previous Building headers and generator targets rebuilt and linked; runtime-building world example and MetaHuman adapter tests linked. Zero compiler warnings. Two errors and 50 associated overload candidate notes from DurableVehicleComponents.h:13 and :18: qualified lm::io cannot resolve VehicleState and VehicleSeatState.

Repair 8d3af624406b83ff30ef5f1903a468ecc4fb585f: add direct engine/mechanics/VehiclePersistence.h include to engine/vehicles/DurableVehicleComponents.h, whose inline registration invokes both existing serializers. Canonical overload definitions verified in VehiclePersistence.h; no duplicated serialization, layout change, new dependency on libraries, authority or behavior change. Feature manifest scope and test record updated. Actual post-fix native compile and CTest NOT RUN.

Next: fast-forward the existing integration worktree to current HEAD, print SHA and reuse the previous KINGSTON-backed build directory. Run full quick gate, preserve configure/build/CTest reports, fix remaining diagnostics. Do not merge main before required gate passes.

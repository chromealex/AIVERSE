# Animation Release test hardening — batch 4

Date: 2026-11-20
Branch: integration/prisma-motionweave-view
Paths and prior asserts: [{"name":"motionweave_workers.cpp","asserts":5},{"name":"ragdoll.cpp","asserts":3},{"name":"recovery.cpp","asserts":3},{"name":"root_motion_bake.cpp","asserts":4}].
Use tests/AlwaysCheck.h for all CTest assertions (including root-motion-bake accumulated checksum). Release no longer removes these evaluations. No native compiler or CTest was available, so results NOT RUN.

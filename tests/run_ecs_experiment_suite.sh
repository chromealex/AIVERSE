#!/usr/bin/env bash
# Run from any directory: bash tests/run_ecs_experiment_suite.sh [--bench] [--sanitize]
# Keep this file LF-only; .gitattributes enforces eol=lf for *.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-c++}"
SAN=""
TSAN=0
DO_BENCH=0
for arg in "$@"; do
    case "$arg" in
        --bench) DO_BENCH=1 ;;
        --sanitize) SAN="-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
        --tsan) TSAN=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 2 ;;
    esac
done
if [[ "$TSAN" == 1 && -n "$SAN" ]]; then
    echo "Use --tsan and --sanitize in separate runs" >&2
    exit 2
fi
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/aiverse-ecs-tests.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
COMMON=(-std=c++20 -O2 -pthread -I. -Wall -Wextra)
if [[ -n "$SAN" ]]; then
    COMMON+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
if [[ "$TSAN" == 1 ]]; then
    COMMON+=(-fsanitize=thread -fno-omit-frame-pointer)
fi
compile_run() {
    local name="$1"
    shift
    echo "=== $name ==="
    "$CXX" "${COMMON[@]}" "$@" -o "$BUILD_DIR/$name"
    "$BUILD_DIR/$name"
}
compile_run ecs-experiments tests/ecs_experiments_test.cpp
# Public AIVERSE::libraries headers; unrelated shooter/strategy hosts must
# compile without linking the genre modules.
compile_run ecs-cross-genre -Ilibraries/include tests/ecs_cross_genre_mechanics_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-projectile-composition -Ilibraries/include \
    tests/ecs_projectile_composition_test.cpp \
    engine/world/SpatialWorld.cpp engine/world/TriangleCollision.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-cross-genre-projectile -Ilibraries/include \
    tests/ecs_cross_genre_projectile_test.cpp \
    engine/world/SpatialWorld.cpp engine/world/TriangleCollision.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-cross-genre-weapon-pipeline -Ilibraries/include \
    tests/ecs_cross_genre_weapon_pipeline_test.cpp \
    engine/world/SpatialWorld.cpp engine/world/TriangleCollision.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-kinematic-projectile -Ilibraries/include \
    tests/ecs_kinematic_projectile_test.cpp \
    engine/world/SpatialWorld.cpp engine/world/TriangleCollision.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-impact-queue -Ilibraries/include \
    tests/ecs_cross_genre_impact_queue_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-integer-health -Ilibraries/include \
    tests/ecs_integer_health_commit_test.cpp \
    engine/core/Fixed.cpp
compile_run ecs-hitscan-selection -Ilibraries/include \
    tests/ecs_hitscan_selection_test.cpp \
    engine/world/SpatialWorld.cpp engine/world/TriangleCollision.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-hitscan-resolution -Ilibraries/include \
    tests/ecs_hitscan_resolution_test.cpp \
    engine/world/SpatialWorld.cpp engine/world/TriangleCollision.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
# CI exercises all NEREID tests, independently of genre/runtime/render targets.
NEREID_CORE=(
    engine/sim/Simulation.cpp engine/core/Fixed.cpp engine/core/FixedMath.cpp
    engine/data/Json.cpp engine/data/JsonCompose.cpp
)
for nereid in water buoyancy geometry query_index native_pipeline; do
    compile_run "ecs-nereid-$nereid" -Ilibraries/include \
        "tests/ecs_nereid_${nereid}_test.cpp" "${NEREID_CORE[@]}"
done
# CPU presentation manifests call worldInteger/worldVector from WorldAssets.cpp;
# the translation unit also references Sha256 from verifiedReader, so compile
# both sources in this standalone runner (CMake tests link aiverse_world).
NEREID_WORLD_MANIFEST=(
    engine/world/WorldAssets.cpp engine/pack/Sha256.cpp
)
# Renderer/JSON contracts do not create a graphics Device or window.
compile_run ecs-nereid-renderer-contract -Ilibraries/include \
    tests/ecs_nereid_renderer_contract_test.cpp "${NEREID_CORE[@]}" \
    "${NEREID_WORLD_MANIFEST[@]}" engine/render/MeshBuilder.cpp
compile_run ecs-nereid-confluence-contract -Ilibraries/include \
    tests/ecs_nereid_confluence_contract_test.cpp "${NEREID_CORE[@]}" \
    "${NEREID_WORLD_MANIFEST[@]}"
# Legacy bridge imports only from the editor-time RtsWater geometry code.
# Presentation geometry must compile on both Linux and macOS independently of RHI.
compile_run ecs-nereid-surface-mesh -Ilibraries/include \
    tests/ecs_nereid_surface_mesh_test.cpp "${NEREID_CORE[@]}"
compile_run ecs-nereid-legacy-raster -Ilibraries/include \
    tests/ecs_nereid_legacy_raster_test.cpp engine/view/game/GameWater.cpp \
    "${NEREID_CORE[@]}"
compile_run ecs-native-persistence tests/ecs_native_persistence_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp engine/core/FixedMath.cpp \
    engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-cross-genre-timed-mechanics -Ilibraries/include \
    tests/ecs_cross_genre_timed_mechanics_test.cpp \
    engine/world/SpatialWorld.cpp engine/world/TriangleCollision.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-cross-genre-locomotion -Ilibraries/include \
    tests/ecs_cross_genre_locomotion_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-cross-genre-inventory -Ilibraries/include \
    tests/ecs_cross_genre_inventory_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-fixed-regeneration-policy -Ilibraries/include \
    tests/ecs_fixed_regeneration_policy_test.cpp \
    engine/core/Fixed.cpp
compile_run ecs-waves tests/ecs_wave_executor_test.cpp
compile_run ecs-guarded tests/ecs_guarded_world_test.cpp
compile_run ecs-snapshot-delta tests/ecs_snapshot_delta_test.cpp
compile_run ecs-scene-hierarchy tests/ecs_scene_hierarchy_test.cpp
compile_run ecs-scene-hierarchy-simulation tests/ecs_scene_hierarchy_simulation_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-cow-snapshot tests/ecs_cow_snapshot_test.cpp
compile_run ecs-cow-exception tests/ecs_cow_exception_test.cpp
compile_run ecs-cow-parallel tests/ecs_cow_parallel_test.cpp
compile_run ecs-cow-rollback tests/ecs_cow_rollback_session_test.cpp \
    engine/net/Rollback.cpp engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-guarded-parallel tests/ecs_guarded_parallel_test.cpp
compile_run ecs-world-replay tests/ecs_wave_replay_test.cpp
compile_run ecs-simulation-bridge tests/ecs_simulation_bridge_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
compile_run ecs-guarded-failure tests/ecs_guarded_failure_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
if [[ "$DO_BENCH" == 1 ]]; then
    compile_run ecs-query-bench tests/ecs_architecture_bench.cpp
    compile_run ecs-bitmap-bench tests/ecs_bitmap_query_bench.cpp
    compile_run ecs-hier-bench tests/ecs_hierarchical_bench.cpp
    compile_run ecs-persistent-bench tests/ecs_persistent_jobs_bench.cpp
    compile_run ecs-adaptive-bench tests/ecs_adaptive_jobs_bench.cpp
    compile_run ecs-nereid-sampling-bench -Ilibraries/include \
        tests/ecs_nereid_sampling_bench.cpp "${NEREID_CORE[@]}"
    compile_run ecs-cow-bench tests/ecs_cow_bench.cpp
    compile_run ecs-hierarchy-bench tests/ecs_scene_hierarchy_bench.cpp
fi
echo "PASS ECS experiment suite"

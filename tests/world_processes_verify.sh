#!/usr/bin/env bash
# Dedicated local/CI acceptance for the experimental world process ECS systems.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-c++}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
COMMON=(-std=c++20 -O1 -Wall -Wextra -I. -Ilibraries/include -pthread)
if [[ "${AIVERSE_SANITIZE:-0}" == "1" ]]; then
    COMMON+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
LINK=(engine/sim/Simulation.cpp engine/core/Fixed.cpp engine/core/FixedMath.cpp
      engine/data/Json.cpp engine/data/JsonCompose.cpp)
for entry in world_processes_test ecs_echo_acoustics_test ecs_fracture_structure_test ecs_aether_gas_test world_processes_inspector_test world_processes_authoring_test world_processes_map_hydration_test world_processes_linked_test; do
    if [[ "$entry" == world_processes_test ]]; then
        src=tests/libraries/world_processes_test.cpp
        "$CXX" "${COMMON[@]}" -Werror "$src" -o "$WORK/$entry"
    else
        src="tests/$entry.cpp"
        "$CXX" "${COMMON[@]}" "$src" "${LINK[@]}" -o "$WORK/$entry"
    fi
    echo "RUN $entry"
    "$WORK/$entry"
done
echo "PASS world-processes focused native acceptance"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
compiler="${CXX:-c++}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"$compiler" -std=c++20 -Wall -Wextra -Werror -pedantic -I . -I libraries/include \
    tests/libraries/world_processes_test.cpp -o "$out/world-processes-test"
"$out/world-processes-test"
echo "World-process experimental tests passed"

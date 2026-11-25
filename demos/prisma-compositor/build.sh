#!/usr/bin/env bash
set -euo pipefail
out=${1:?Supply an external build directory}
root=$(cd "$(dirname "$0")/../.." && pwd)
if [[ $(uname -s) == Darwin ]]; then
    [[ -d /Volumes/KINGSTON && "$out" == /Volumes/KINGSTON/AIVERSE-builds/* ]] || exit 2
fi
mkdir -p "$out"
cd "$root"
em++ -std=c++20 -O2 -fexceptions -I. demos/prisma-compositor/demo.cpp \
    engine/core/Fixed.cpp engine/data/Json.cpp engine/gfx/gl/GfxGL.cpp engine/render/graph/ShaderGraph.cpp \
    engine/render/{Prisma,PrismaData,PrismaCompositor,PrismaScreenLighting,PrismaVolumeData,PrismaDlssStub,PhysicalSky,LocalFog}.cpp \
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH -sNO_EXIT_RUNTIME=1 \
    -sEXPORTED_RUNTIME_METHODS=ccall -o "$out/prisma.js"
cp demos/prisma-compositor/index.html "$out/"

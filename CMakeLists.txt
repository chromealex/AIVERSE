cmake_minimum_required(VERSION 3.22)
project(AIVERSE VERSION 3.1.0 LANGUAGES C CXX)
# Apple ld: the same static library listed twice in a link line is harmless (CMake repeats them for dependencies)
if(APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,-no_warn_duplicate_libraries" AIVERSE_LD_NO_DUP_WARN)
    if(AIVERSE_LD_NO_DUP_WARN)
        add_link_options("-Wl,-no_warn_duplicate_libraries")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set(AIVERSE_PLATFORM web)
elseif(ANDROID)
    set(AIVERSE_PLATFORM android)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(AIVERSE_PLATFORM ios)
elseif(APPLE)
    set(AIVERSE_PLATFORM macos)
elseif(WIN32)
    set(AIVERSE_PLATFORM windows)
else()
    set(AIVERSE_PLATFORM linux)
endif()
message(STATUS "AIVERSE platform: ${AIVERSE_PLATFORM}")

# Web Release / MinSizeRel: optimize for download size and link-time code removal across all libraries.
# Measured on skirmish (2026-11-21): index.wasm 14.95 MB -> 8.34 MB (brotli 3.50 -> 2.41 MB). Applied before
# any target so engine, third-party and client objects share the LTO format. OFF restores -O3 without LTO.
if(EMSCRIPTEN)
    option(AIVERSE_WEB_OPTIMIZE_SIZE "Web Release: -Oz and LTO" ON)
    if(AIVERSE_WEB_OPTIMIZE_SIZE)
        add_compile_options("$<$<CONFIG:Release,MinSizeRel>:-Oz>" "$<$<CONFIG:Release,MinSizeRel>:-flto>")
        add_link_options("$<$<CONFIG:Release,MinSizeRel>:-Oz>" "$<$<CONFIG:Release,MinSizeRel>:-flto>")
    endif()
endif()

set(AIVERSE_DESKTOP OFF)
if(AIVERSE_PLATFORM MATCHES "^(macos|windows|linux)$")
    set(AIVERSE_DESKTOP ON)
endif()

option(AIVERSE_BUILD_TESTS "Build unit tests" ${AIVERSE_DESKTOP})
# Languages must be enabled in the highest common directory of their targets.
# VIVID's real Metal GPU test is Objective-C++ (.mm); enabling OBJCXX only
# inside engine/character/vivid left CMAKE_OBJCXX_COMPILE_OBJECT undefined
# while generating the macOS build. Keep this opt-in for desktop tests only.
if(AIVERSE_PLATFORM STREQUAL "macos" AND AIVERSE_BUILD_TESTS)
    enable_language(OBJCXX)
endif()
option(AIVERSE_BUILD_HEADLESS "Build headless runner" ${AIVERSE_DESKTOP})
option(AIVERSE_BUILD_CLIENT "Build the game client" ON)
option(AIVERSE_BUILD_EDITOR "Build the map editor" ${AIVERSE_DESKTOP})
option(AIVERSE_BUILD_TOOLS "Build aiversebuild packager" ${AIVERSE_DESKTOP})
option(AIVERSE_BUILD_SERVER "Build the network game server and the network bot client" ${AIVERSE_DESKTOP})

# Register Objective-C++ in the ROOT directory before creating any targets.
# CMake does not propagate a language enabled only in a child directory back
# to parent-scoped targets; otherwise it can fail with CMAKE_OBJCXX_COMPILE_OBJECT.
# The HTML5 Studio is the main editor on macOS and Windows: built by default once its web bundle exists
# (studio_mac.command / studio_windows.cmd build the bundle first; -DAIVERSE_BUILD_STUDIO_HTML5=OFF skips it).
set(_aiverse_studio_default OFF)
if((APPLE OR WIN32) AND NOT IOS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/studio-web/dist/index.html")
    set(_aiverse_studio_default ON)
endif()
option(AIVERSE_BUILD_STUDIO_HTML5 "Build the HTML5 Studio (main editor; macOS and Windows)" ${_aiverse_studio_default})
if(APPLE AND AIVERSE_BUILD_STUDIO_HTML5)
    enable_language(OBJCXX)
endif()

# Explicit system Khronos libktx support for BasisLZ/ETC1S and UASTC.
# OFF by default: no hidden vendoring, network fetch or platform dependency.
option(AIVERSE_TEXTURE_WITH_LIBKTX "Use installed libktx Basis Universal KTX2 transcoder" OFF)
option(AIVERSE_TEXTURE_WITH_OPENEXR "Use installed OpenEXR 3 for ZIP/PIZ/tiled HDR textures" OFF)
if(AIVERSE_TEXTURE_WITH_OPENEXR)
    find_package(OpenEXR 3 CONFIG REQUIRED)
    if(NOT TARGET OpenEXR::OpenEXR)
        message(FATAL_ERROR "AIVERSE_TEXTURE_WITH_OPENEXR requires OpenEXR::OpenEXR target")
    endif()
endif()
if(AIVERSE_TEXTURE_WITH_LIBKTX)
    find_path(AIVERSE_LIBKTX_INCLUDE_DIR NAMES ktx.h REQUIRED)
    find_library(AIVERSE_LIBKTX_LIBRARY NAMES ktx REQUIRED)
    add_library(aiverse_texture_libktx INTERFACE)
    target_include_directories(aiverse_texture_libktx INTERFACE "${AIVERSE_LIBKTX_INCLUDE_DIR}")
    target_link_libraries(aiverse_texture_libktx INTERFACE "${AIVERSE_LIBKTX_LIBRARY}")
    target_compile_definitions(aiverse_texture_libktx INTERFACE AIVERSE_TEXTURE_WITH_LIBKTX=1)
endif()

# Sample games: the official template packages of the package catalog (AIVERSE-PACKAGES), installed into one
# folder by tools/pkg/fetch_templates.py and named by engine id (skirmish, mushrooms, ...). Tests, the default
# client content and CI read them from here; the engine repository itself carries no games.
if(DEFINED ENV{AIVERSE_TEMPLATES_DIR})
    set(_aiverse_templates_default "$ENV{AIVERSE_TEMPLATES_DIR}")
elseif(DEFINED ENV{AIVERSE_PACKAGES_HOME})
    set(_aiverse_templates_default "$ENV{AIVERSE_PACKAGES_HOME}/templates")
elseif(WIN32 AND DEFINED ENV{USERPROFILE})
    set(_aiverse_templates_default "$ENV{USERPROFILE}/.aiverse/packages/templates")
else()
    set(_aiverse_templates_default "$ENV{HOME}/.aiverse/packages/templates")
endif()
file(TO_CMAKE_PATH "${_aiverse_templates_default}" _aiverse_templates_default)
set(AIVERSE_TEMPLATES_DIR "${_aiverse_templates_default}" CACHE PATH "Installed template packages (sample games)")
file(TO_CMAKE_PATH "${AIVERSE_TEMPLATES_DIR}" AIVERSE_TEMPLATES_DIR)
option(AIVERSE_FETCH_TEMPLATES "Install the template packages at configure time when they are missing" ON)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(AIVERSE_FETCH_TEMPLATES AND NOT EXISTS "${AIVERSE_TEMPLATES_DIR}/skirmish/game.json")
    if(Python3_Interpreter_FOUND)
        message(STATUS "Installing the template packages into ${AIVERSE_TEMPLATES_DIR}")
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pkg/fetch_templates.py"
                                --out "${AIVERSE_TEMPLATES_DIR}" RESULT_VARIABLE _aiverse_fetch OUTPUT_QUIET)
        if(NOT _aiverse_fetch EQUAL 0)
            message(WARNING "Could not install the template packages (offline?): tests that play the sample games will fail. "
                            "Run python3 tools/pkg/fetch_templates.py later.")
        endif()
    endif()
endif()
add_compile_definitions(AIVERSE_TEMPLATES_DIR="${AIVERSE_TEMPLATES_DIR}")
add_custom_target(aiverse_fetch_templates
    COMMAND "$<IF:$<BOOL:${Python3_EXECUTABLE}>,${Python3_EXECUTABLE},python3>" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pkg/fetch_templates.py" --out "${AIVERSE_TEMPLATES_DIR}"
    COMMENT "Installing / updating the template packages" VERBATIM)

# Game being built (aiversebuild overrides these).
set(AIVERSE_CONTENT_DIR "${AIVERSE_TEMPLATES_DIR}/skirmish" CACHE PATH "Game content folder (contains game.json)")
# build folders configured when the sample games lived in the engine (games/, then templates/) keep working
if(NOT EXISTS "${AIVERSE_CONTENT_DIR}/game.json" AND AIVERSE_CONTENT_DIR MATCHES "/(games|templates)/([^/]+)/?$")
    if(EXISTS "${AIVERSE_TEMPLATES_DIR}/${CMAKE_MATCH_2}/game.json")
        set(AIVERSE_CONTENT_DIR "${AIVERSE_TEMPLATES_DIR}/${CMAKE_MATCH_2}" CACHE PATH "Game content folder (contains game.json)" FORCE)
    endif()
endif()
set(AIVERSE_ENGINE_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/engine/assets")
set(AIVERSE_GAME_NAME "AIVERSE Game" CACHE STRING "Display name")
set(AIVERSE_GAME_ID "com.aiverse.game" CACHE STRING "Bundle / application id")
set(AIVERSE_GAME_VERSION "0.1.0" CACHE STRING "Version string")
set(AIVERSE_GAME_BUILD "1" CACHE STRING "Build number")
set(AIVERSE_GAME_EXECUTABLE "AiverseGame" CACHE STRING "Executable / bundle file name")
set(AIVERSE_APPLE_TEAM_ID "" CACHE STRING "Apple development team id (iOS signing)")
set(AIVERSE_GEN_DIR "" CACHE PATH "Generated packaging assets from aiversebuild (icons)")
# Native plugins selected by aiversebuild (plugins/*/plugin.json): sources, frameworks, Info.plist keys.
# Exported projects (Android/iOS) carry plugins.cmake next to this file.
set(AIVERSE_PLUGINS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/plugins.cmake" CACHE FILEPATH "Generated by aiversebuild")
set(AIVERSE_PLUGIN_SOURCES "")
set(AIVERSE_PLUGIN_FRAMEWORKS "")
set(AIVERSE_PLUGIN_PLIST_XML "")
set(AIVERSE_PLUGIN_INCLUDE_DIRS "")
set(AIVERSE_PLUGIN_LIBRARIES "")
set(AIVERSE_PLUGIN_RUNTIME_FILES "")
if(EXISTS "${AIVERSE_PLUGINS_FILE}")
    include("${AIVERSE_PLUGINS_FILE}")
    message(STATUS "AIVERSE plugins: ${AIVERSE_PLUGIN_NAMES}")
endif()

if(MSVC)
    # /bigobj: big translation units (tests.cpp, editor WorldInspect.cpp) exceed the default COFF section limit
    add_compile_options(/W4 /permissive- /fp:precise /utf-8 /bigobj /wd4100 /wd4505)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -fno-fast-math)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
    if(WIN32)
        add_compile_options(-Wno-cast-function-type) # GetProcAddress casts are intended
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC only (Linux gcc, MinGW). The compact one-line style used across the engine trips
        # -Wmisleading-indentation, and -Wdangling-reference flags every `const Json& x = f(j[...], "path")`
        # although Json::operator[] returns a reference into the document. Both are disabled.
        add_compile_options(-Wno-misleading-indentation $<$<COMPILE_LANGUAGE:CXX>:-Wno-dangling-reference>)
        # GCC 12+ -O3 reports these from inside libstdc++ (vector::assign, std::sort, memcpy); they stay visible
        # as warnings but do not fail -DCMAKE_COMPILE_WARNING_AS_ERROR=ON builds.
        add_compile_options(-Wno-error=array-bounds -Wno-error=stringop-overflow -Wno-error=nonnull)
        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-error=dangling-pointer>)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Core: deterministic simulation (no platform code, no float in gameplay)
# ---------------------------------------------------------------------------
add_library(aiverse_engine STATIC
    engine/core/Fixed.cpp
    engine/core/FixedMath.cpp
    engine/data/Json.cpp
    engine/data/JsonCompose.cpp
    engine/sim/Simulation.cpp
    engine/sim/Replay.cpp
    engine/sim/ProjectileData.cpp)
target_include_directories(aiverse_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# PHYSIS nature simulation installs itself from a map "physis" block in
# Simulation::setup for every game module (engine/physis/PhysisAutoInstall.cpp).
# OFF drops it from the link; maps with a "physis" block then run without it.
option(AIVERSE_SIM_PHYSIS "Install PHYSIS automatically from map data" ON)
# PHYSIS worker lanes (engine/physis/PhysisParallel.h) use std::thread.
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(aiverse_engine PUBLIC Threads::Threads)
endif()
if(AIVERSE_SIM_PHYSIS)
    target_sources(aiverse_engine PRIVATE engine/physis/PhysisAutoInstall.cpp)
    target_compile_definitions(aiverse_engine PRIVATE AIVERSE_SIM_PHYSIS=1)
endif()
# Tests that rely on assert() must keep it in Release (CMake adds NDEBUG): undefine NDEBUG for the
# target on every compiler (GCC/Clang/AppleClang/clang-cl: -UNDEBUG, MSVC: /UNDEBUG).
function(aiverse_keep_test_asserts target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
endfunction()
# ECS 2.0 opt-in correctness suites imported from feature/ecs-architecture-benchmarks.
# This does NOT enable production parallel scheduling or change default gameplay.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    find_package(Threads REQUIRED)
    foreach(test_name IN ITEMS
            ecs_experiments_test
            ecs_wave_executor_test
            ecs_guarded_world_test
            ecs_snapshot_delta_test
            ecs_scene_hierarchy_test
            ecs_scene_hierarchy_simulation_test
            ecs_scene_runtime_test
            ecs_multiworld_test
            ecs_scene_load_coordinator_test
            ecs_scene_resource_pipeline_test
            ecs_shared_static_components_test
            ecs_prefab_static_catalog_test
            ecs_static_scene_integration_test
            ecs_static_scene_multidomain_test
            ecs_static_scene_editor_contract_test
            ecs_cow_snapshot_test
            ecs_cow_exception_test
            ecs_cow_parallel_test
            ecs_guarded_parallel_test
            ecs_guarded_failure_test
            ecs_wave_replay_test
            ecs_simulation_bridge_test)
        add_executable(aiverse_${test_name} tests/${test_name}.cpp)
        target_link_libraries(aiverse_${test_name} PRIVATE aiverse_engine Threads::Threads)
        add_test(NAME ${test_name} COMMAND aiverse_${test_name})
    endforeach()
    add_executable(aiverse_ecs_shared_static_bench tests/ecs_shared_static_bench.cpp)
    target_link_libraries(aiverse_ecs_shared_static_bench PRIVATE aiverse_engine Threads::Threads)
    add_executable(aiverse_ecs_shared_static_rss_bench
        tests/ecs_shared_static_rss_bench.cpp)
    target_link_libraries(aiverse_ecs_shared_static_rss_bench PRIVATE
        aiverse_engine Threads::Threads)
endif()

option(AIVERSE_ENABLE_PHYSICS "Build optional Box3D physics module" OFF)
if(AIVERSE_ENABLE_PHYSICS)
    add_subdirectory(engine/physics)
endif()
add_subdirectory(engine/meta)

# The one gameplay host: grid systems (units, orders, economy, combat, garrisons, AI) and, on world-space maps, actor systems
add_library(aiverse_game STATIC
    engine/game/HostSystems.cpp   # whole-match systems chosen by data: actor (engine/actor), voxel (engine/voxel)
    engine/game/GameData.cpp
    engine/game/MapGrid.cpp
    engine/game/MapHpa.cpp
    engine/game/MapGridSphere.cpp
    engine/game/MapHpaSphere.cpp
    engine/game/FlowField.cpp
    engine/game/Sphere.cpp
    engine/game/Surface.cpp
    engine/game/GameCore.cpp
    engine/game/Planets.cpp
    engine/game/Economy.cpp
    engine/game/Units.cpp
    engine/game/ProjectilePrograms.cpp
    engine/game/Combat.cpp
    engine/game/World.cpp
    engine/game/Vision.cpp
    engine/game/Triggers.cpp
    engine/game/Dungeon.cpp
    engine/game/Settlers.cpp
    engine/game/Lanes.cpp
    engine/game/Avoidance.cpp
    engine/game/EffectsData.cpp
    engine/game/Effects.cpp
    engine/game/SkillObjects.cpp
    engine/game/Skills.cpp
    engine/game/ComputerPlayer.cpp
    engine/game/OriginalsData.cpp
    engine/game/Originals.cpp
    engine/game/FidelityData.cpp
    engine/game/Fidelity.cpp
    engine/game/ModesData.cpp
    engine/game/Modes.cpp
    engine/game/ComputerPlayerOriginals.cpp
    engine/game/ComputerPlayerFidelity.cpp
    engine/game/ComputerPlayerTerrain.cpp
    engine/game/ComputerPlayerTactics.cpp)
target_link_libraries(aiverse_game PUBLIC aiverse_fps aiverse_voxelgame)   # actor / voxel systems: engine/actor, engine/voxel CMakeLists
target_link_libraries(aiverse_game PUBLIC aiverse_engine)
# The one vehicle system of every host (docs/driving.md, AGENTS.md "no genre split"): components, the per-tick step,
# the race adapter. The grid-map host (engine/game/Driving.cpp) and the metre-map host (engine/actor/ActorVehicles.cpp)
# only implement its world queries.
add_library(aiverse_vehicles STATIC engine/vehicles/Vehicles.cpp engine/vehicles/VehicleRace.cpp)
add_library(AIVERSE::vehicles ALIAS aiverse_vehicles)
target_include_directories(aiverse_vehicles PUBLIC "${PROJECT_SOURCE_DIR}")   # engine never includes libraries/ (docs/architecture-layers.md)
target_link_libraries(aiverse_vehicles PUBLIC aiverse_engine)
target_link_libraries(aiverse_game PUBLIC aiverse_vehicles)
# Experimental RTS transform preprocessing remains OFF by default.
option(AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS
    "Opt in to deterministic parallel RTS prevPos preprocessing" OFF)
if(AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS)
    find_package(Threads REQUIRED)
    target_link_libraries(aiverse_game PRIVATE Threads::Threads)
    target_compile_definitions(aiverse_game PRIVATE AIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS=1)
endif()

add_subdirectory(libraries)
# Projectile motion and impact use the shared ECS mechanics in engine/mechanics (moved down from libraries, L3);
# engine targets never link library packages.
# Prove public shared mechanics can execute in unrelated native genre hosts.
# The target uses AIVERSE::libraries, never aiverse_game or aiverse_fps.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_ecs_cross_genre_mechanics_test
        tests/ecs_cross_genre_mechanics_test.cpp)
    target_link_libraries(aiverse_ecs_cross_genre_mechanics_test PRIVATE
        aiverse_engine AIVERSE::libraries)
    add_test(NAME ecs_cross_genre_mechanics_test
        COMMAND aiverse_ecs_cross_genre_mechanics_test)
endif()
include(engine/game/native/Build.cmake)
# agent-mech: mechanics of the top-100 strategy games plan (stances, transports, ...)
file(GLOB AIVERSE_MECH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/game/Mech*.cpp)
target_sources(aiverse_game PRIVATE ${AIVERSE_MECH_SOURCES} engine/game/Save.cpp engine/game/Campaign.cpp   # + save states, campaigns
    engine/game/Strategic.cpp)   # strategic map: provinces, armies, battles on GameCore
# Optional simulation systems of packaged clients (packaging.engineModules, tools/aiversebuild).
# A system that runs only when its rules block is present can be left out: OFF compiles the adapter
# `stub` (engine/game/disabled/) instead of its sources; the adapter keeps the system inactive and
# makes rules that need it fail with a readable error. Packaged clients only - the editor, tests,
# headless runner, servers and HTML5 Studio always build every system.
function(aiverse_game_module var description stub)
    option(${var} "${description}" ON)
    if(${var})
        target_sources(aiverse_game PRIVATE ${ARGN})
    else()
        if(AIVERSE_BUILD_EDITOR OR AIVERSE_BUILD_TESTS OR AIVERSE_BUILD_HEADLESS OR AIVERSE_BUILD_SERVER OR AIVERSE_BUILD_STUDIO_HTML5)
            message(FATAL_ERROR "${var}=OFF is for packaged clients only; turn off the editor, tests, headless runner, server and HTML5 Studio")
        endif()
        target_sources(aiverse_game PRIVATE ${stub})
    endif()
endfunction()

# agent-bw-ai: Bunker Wars computer player (rules ai.controller "bw")
file(GLOB AIVERSE_BWAI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/game/BwAI*.cpp)
# Every game: hypnotised sends, per point counters, the controller registry and the registration point.
target_sources(aiverse_game PRIVATE engine/game/Hypnosis.cpp engine/game/AIControllers.cpp engine/game/GameExtensions.cpp)
aiverse_game_module(AIVERSE_GAME_WITH_BWAI "Include the Bunker Wars computer player (rules ai.controller \"bw\")"
    engine/game/disabled/BwAI.cpp ${AIVERSE_BWAI_SOURCES})

# Localization: string tables, plurals, language choice (presentation only, no sim dependency).
# RtsLocalize maps RTS data (types, resources, maps...) to string keys.
add_library(aiverse_i18n STATIC
    engine/i18n/Localization.cpp
    engine/i18n/GameLanguage.cpp
    engine/i18n/SystemLanguage.cpp
    engine/game/Localize.cpp)
target_link_libraries(aiverse_i18n PUBLIC aiverse_game)
if(APPLE)
    target_link_libraries(aiverse_i18n PUBLIC "-framework CoreFoundation")
endif()

# ---------------------------------------------------------------------------
# Resource packs (.rpak). The runtime links only the zstd decoder; host tools that write packs
# link aiverse_pack_writer, which carries the full zstd library (in host builds the decoder target
# forwards to it, see below).
# ---------------------------------------------------------------------------
set(AIVERSE_CONTENT_PACK "" CACHE FILEPATH "Prebuilt content.rpak to ship instead of loose content (set by aiversebuild)")
# Host builds (tools or tests on) also compile the full zstd for pack writers; there aiverse_zstd_dec
# forwards to it, so a binary that mixes runtime and tool libraries carries one zstd copy instead of two
# (GNU ld rejects the duplicate symbols). Packaged builds (aiversebuild: tools/tests OFF) keep the
# small decoder-only library.
if(AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_library(aiverse_zstd STATIC third_party/zstd/zstd.c)
    target_link_libraries(aiverse_zstd PUBLIC Threads::Threads)
    if(NOT MSVC)
        target_compile_options(aiverse_zstd PRIVATE -w)
    endif()
    add_library(aiverse_zstd_dec INTERFACE)
    target_link_libraries(aiverse_zstd_dec INTERFACE aiverse_zstd)
else()
    add_library(aiverse_zstd_dec STATIC third_party/zstd/zstddeclib.c)
    if(NOT MSVC)
        target_compile_options(aiverse_zstd_dec PRIVATE -w)
    endif()
endif()
# HTTP fetcher for chunk downloads, per platform (see engine/pack/Chunks.h).
if(AIVERSE_PLATFORM STREQUAL "windows")
    set(AIVERSE_PACK_HTTP engine/pack/http/HttpWin.cpp)
elseif(AIVERSE_PLATFORM STREQUAL "macos" OR AIVERSE_PLATFORM STREQUAL "ios")
    set(AIVERSE_PACK_HTTP engine/pack/http/HttpApple.mm)
elseif(AIVERSE_PLATFORM STREQUAL "android")
    set(AIVERSE_PACK_HTTP engine/pack/http/HttpAndroid.cpp)
elseif(AIVERSE_PLATFORM STREQUAL "web")
    set(AIVERSE_PACK_HTTP engine/pack/http/HttpWeb.cpp)
elseif(UNIX)
    set(AIVERSE_PACK_HTTP engine/pack/http/HttpCurl.cpp)
else()
    set(AIVERSE_PACK_HTTP engine/pack/http/HttpNone.cpp)
endif()
add_library(aiverse_pack STATIC engine/pack/Pack.cpp engine/pack/Sha256.cpp engine/pack/Chunks.cpp ${AIVERSE_PACK_HTTP})
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(aiverse_pack PUBLIC Threads::Threads)
endif()
if(AIVERSE_PLATFORM STREQUAL "windows")
    target_link_libraries(aiverse_pack PUBLIC winhttp)
elseif(AIVERSE_PLATFORM STREQUAL "macos" OR AIVERSE_PLATFORM STREQUAL "ios")
    set_source_files_properties(engine/pack/http/HttpApple.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(aiverse_pack PUBLIC "-framework Foundation")
elseif(AIVERSE_PLATFORM STREQUAL "web")
    target_link_options(aiverse_pack PUBLIC -sFETCH=1)
elseif(UNIX AND NOT ANDROID)
    target_link_libraries(aiverse_pack PUBLIC ${CMAKE_DL_LIBS})
endif()
target_include_directories(aiverse_pack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS)
    add_library(aiverse_pack_writer STATIC engine/pack/PackWriter.cpp engine/pack/Unpacker.cpp)
    target_link_libraries(aiverse_pack_writer PUBLIC aiverse_pack aiverse_engine aiverse_zstd)
endif()

# ---------------------------------------------------------------------------
# Networking: transports (UDP, WebSocket), rollback lockstep session, lobby client.
# The server part needs no game data and no graphics.
# ---------------------------------------------------------------------------
add_library(aiverse_net STATIC
    engine/net/Loopback.cpp
    engine/net/Socket.cpp
    engine/net/PlainHttp.cpp
    engine/net/Reliable.cpp
    engine/net/Udp.cpp
    engine/net/WebSocket.cpp
    engine/net/Protocol.cpp
    engine/net/Rollback.cpp
    engine/net/NetClient.cpp
    # secure battle transport + hosted battles (agent-net-mobile, docs/battle-net.md)
    engine/net/Crypto.cpp
    engine/net/MatchTicket.cpp
    engine/net/BattleProtocol.cpp
    engine/net/SimNet.cpp
    engine/net/SecureUdp.cpp
    engine/net/BattleJoin.cpp
    third_party/monocypher/monocypher.c)
set_source_files_properties(third_party/monocypher/monocypher.c PROPERTIES COMPILE_OPTIONS "$<IF:$<BOOL:${MSVC}>,/w,-w>")
target_link_libraries(aiverse_net PUBLIC aiverse_engine)
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_ecs_cow_rollback_session_test tests/ecs_cow_rollback_session_test.cpp)
    target_link_libraries(aiverse_ecs_cow_rollback_session_test PRIVATE aiverse_net Threads::Threads)
    add_test(NAME ecs_cow_rollback_session_test COMMAND aiverse_ecs_cow_rollback_session_test)
endif()

if(WIN32)
    target_link_libraries(aiverse_net PUBLIC ws2_32 bcrypt)
elseif(EMSCRIPTEN)
    target_link_options(aiverse_net PUBLIC -lwebsocket.js)
endif()

# Replica: generic server-authoritative replication (agent-replica, docs/replica.md). No game, physics or genre deps.
add_library(aiverse_net_replica STATIC
    engine/net/replica/ReplicaSchema.cpp
    engine/net/replica/ReplicaServer.cpp
    engine/net/replica/ReplicaClient.cpp)
target_link_libraries(aiverse_net_replica PUBLIC aiverse_net)
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    file(GLOB AIVERSE_REPLICA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/replica/*.cpp)
    add_executable(aiverse_replica_tests ${AIVERSE_REPLICA_TEST_SOURCES})
    target_link_libraries(aiverse_replica_tests PRIVATE aiverse_net_replica)
    add_test(NAME aiverse_replica_tests COMMAND aiverse_replica_tests)
endif()

# gRPC client + protobuf codegen (agent-grpc, docs/grpc.md). Libraries are opt-in (EXCLUDE_FROM_ALL);
# tests / probe build on desktop.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engine/grpc/CMakeLists.txt")
    add_subdirectory(engine/grpc)
endif()

add_library(aiverse_net_server STATIC
    engine/net/server/Server.cpp
    engine/net/server/ServerMatch.cpp
    engine/net/server/Matchmaker.cpp
    engine/net/server/ServerHosted.cpp
    engine/net/server/BattleControl.cpp
    engine/net/server/ServerReserved.cpp
    engine/net/server/ServerExtension.cpp)
target_link_libraries(aiverse_net_server PUBLIC aiverse_net)
# Game data on the network side: match loading like the game (tile maps) and the server's verdict
# (re-simulating a match nobody settled). Server, bot client and net tests share it.
# Not part of the game client: exported source trees (aiversebuild iOS / Android / web) don't ship apps/server.
# Hosted battles (agent-net-mobile, docs/battle-net.md): headless load harness with the HeroDuel stand-in
# module; used by aiverse_battle, aiverse_battle_tests and the editor's battle_net_simulate op.
if(NOT EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/battle/BattleLoad.cpp")
    add_library(aiverse_battle_load STATIC apps/battle/BattleLoad.cpp)
    target_link_libraries(aiverse_battle_load PUBLIC aiverse_net_server)
    target_include_directories(aiverse_battle_load PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # RTS-module battles (top-down projects: topdown_arena, extraction, Drone Fall) on the same harness (agent-df-hero-net)
    add_library(aiverse_battle_rts STATIC apps/battle/RtsBattle.cpp)
    target_link_libraries(aiverse_battle_rts PUBLIC aiverse_battle_load aiverse_game)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/server/GameData.cpp")
    add_library(aiverse_net_game STATIC apps/server/GameData.cpp)
    target_link_libraries(aiverse_net_game PUBLIC aiverse_net_server aiverse_game)
endif()

# ---------------------------------------------------------------------------
# Audio (presentation only): software mixer, sound banks from data, procedural synth, music
# streaming; the output device comes from miniaudio (CoreAudio, AAudio/OpenSL ES, Web Audio,
# WASAPI, ALSA/PulseAudio). Without a device it runs silently.
# ---------------------------------------------------------------------------
if(APPLE)
    enable_language(OBJC)   # miniaudio's AVAudioSession / CoreAudio code is Objective-C
    set(AIVERSE_MINIAUDIO_IMPL engine/audio/MiniaudioImpl.m)
else()
    set(AIVERSE_MINIAUDIO_IMPL engine/audio/MiniaudioImpl.c)
endif()
add_library(aiverse_audio STATIC
    engine/audio/AudioEngine.cpp
    engine/audio/AudioMixer.cpp
    engine/audio/AudioReverb.cpp
    engine/audio/AudioDecode.cpp
    engine/audio/Synth.cpp
    ${AIVERSE_MINIAUDIO_IMPL})
target_link_libraries(aiverse_audio PUBLIC aiverse_engine)
set_source_files_properties(${AIVERSE_MINIAUDIO_IMPL} PROPERTIES COMPILE_OPTIONS "$<IF:$<BOOL:${MSVC}>,/w,-w>")
if(APPLE)
    target_link_libraries(aiverse_audio PUBLIC "-framework CoreFoundation" "-framework AudioToolbox")
    if(AIVERSE_PLATFORM STREQUAL "ios")
        target_link_libraries(aiverse_audio PUBLIC "-framework AVFoundation")
    else()
        target_link_libraries(aiverse_audio PUBLIC "-framework CoreAudio")
    endif()
elseif(AIVERSE_PLATFORM STREQUAL "linux" OR AIVERSE_PLATFORM STREQUAL "android")
    target_link_libraries(aiverse_audio PUBLIC ${CMAKE_DL_LIBS})
    if(AIVERSE_PLATFORM STREQUAL "linux")
        find_package(Threads REQUIRED)
        target_link_libraries(aiverse_audio PUBLIC Threads::Threads m)
    endif()
endif()

add_library(aiverse_game_audio STATIC engine/audio/GameAudio.cpp engine/audio/AudioScriptSound.cpp)
target_link_libraries(aiverse_game_audio PUBLIC aiverse_game aiverse_audio)

# Particles: CPU simulation + RHI rendering (presentation only), and the RTS event bridge.
add_library(aiverse_fx STATIC engine/fx/FxEffect.cpp engine/fx/ParticleSystem.cpp engine/fx/FxEmit.cpp engine/fx/FxRender.cpp engine/fx/FxMeshes.cpp)
# Metal compute subset + portable CPU fallback (opt-in SpectraFX v1).
target_sources(aiverse_fx PRIVATE engine/fx/SpectraGpuParticles.cpp)
target_sources(aiverse_fx PRIVATE engine/fx/SpectraGraphRuntime.cpp)
# Shared presentation world unifies compatible effects across graph instances.
target_sources(aiverse_fx PRIVATE engine/fx/SpectraBatchWorld.cpp)
# Optional generic tick-cue/attachment scheduler over shared CPU BatchWorld.
target_sources(aiverse_fx PRIVATE engine/fx/SpectraCueScheduler.cpp)
# Genre-neutral pooled GPU emitter resource groups with shared CPU fallback.
target_sources(aiverse_fx PRIVATE engine/fx/SpectraGpuPoolWorld.cpp)
target_sources(aiverse_fx PRIVATE engine/fx/SpectraGpuCueBridge.cpp)
target_link_libraries(aiverse_fx PUBLIC aiverse_engine aiverse_shader_graph)
# SpectraFX is the official public name; retain aiverse_fx for existing clients.
add_library(AIVERSE::spectrafx ALIAS aiverse_fx)
add_library(aiverse_game_fx STATIC engine/fx/GameFx.cpp)
target_link_libraries(aiverse_game_fx PUBLIC aiverse_game aiverse_fx)

# ---------------------------------------------------------------------------
# Runtime: platform layer + RHI backend + renderer + Dear ImGui
# ---------------------------------------------------------------------------
add_library(aiverse_imgui STATIC
    third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp)
target_include_directories(aiverse_imgui PUBLIC third_party/imgui)
if(NOT MSVC)
    target_compile_options(aiverse_imgui PRIVATE -w)
endif()

# Models (models agent): glTF / OBJ / procedural models, PBR materials, skeletal and procedural
# animation. CPU side only (no platform code), so tests and tools can load models too.
add_library(aiverse_models STATIC
    engine/render/Geometry.cpp
    engine/render/GeometrySceneData.cpp
    engine/render/MeshBuilder.cpp
    engine/render/Model.cpp
    engine/render/ModelMerge.cpp   # static batching of parts that share a material
    engine/render/GlobalGeometryPool.cpp
    engine/render/ModelGltf.cpp
    engine/render/Animation.cpp
    engine/render/TextureCodec.cpp
    engine/render/ImageDds.cpp
    engine/render/TextureGen.cpp)
option(AIVERSE_BLP "Runtime BLP1/BLP2 image decoding" ON)
if(AIVERSE_BLP)
    target_sources(aiverse_models PRIVATE engine/render/ImageBlp.cpp)
else()
    target_compile_definitions(aiverse_models PRIVATE AIVERSE_NO_BLP=1)
endif()
option(AIVERSE_OBJ "Runtime OBJ model import" ON)
if(AIVERSE_OBJ)
    target_sources(aiverse_models PRIVATE engine/render/ModelObj.cpp)
else()
    target_compile_definitions(aiverse_models PRIVATE AIVERSE_NO_OBJ=1)
endif()
option(AIVERSE_LEGACY_MODELS "Runtime VOX/VXL, PLY/STL, 3DS, DAE, MDX/MDL, 3DO and W3D import" ON)
if(AIVERSE_LEGACY_MODELS)
    target_sources(aiverse_models PRIVATE
        engine/render/ModelVox.cpp engine/render/ModelPly.cpp
        engine/render/Model3ds.cpp engine/render/ModelDae.cpp
        engine/render/ModelMdx.cpp engine/render/ModelMdl.cpp
        engine/render/Model3do.cpp engine/render/ModelW3d.cpp)
else()
    target_compile_definitions(aiverse_models PRIVATE AIVERSE_NO_LEGACY_MODELS=1)
endif()
# baked lighting, CPU side (agent-lightbake): lightmap UVs of models (+ .lmuv sidecars), bake folder reader
target_sources(aiverse_models PRIVATE engine/render/LightmapUv.cpp engine/bake/LightmapData.cpp)
# ufbx (MIT / public domain): FBX parser, compiled as its own C library without warnings.
# AIVERSE_FBX=OFF keeps it out of size-sensitive builds (FBX files then fail with a readable error).
option(AIVERSE_FBX "FBX import in the runtime (ufbx, ~0.5 MB of code)" ON)
if(AIVERSE_FBX)
    add_library(aiverse_ufbx STATIC third_party/ufbx/ufbx.c)
    if(NOT MSVC)
        target_compile_options(aiverse_ufbx PRIVATE -w)
    endif()
    target_sources(aiverse_models PRIVATE engine/render/ModelFbx.cpp)
    target_link_libraries(aiverse_models PUBLIC aiverse_ufbx)
else()
    target_compile_definitions(aiverse_models PRIVATE AIVERSE_NO_FBX=1)
endif()
# KTX2 zstd supercompression: the decoder comes with aiverse_runtime (aiverse_zstd_dec) or, in host tools,
# with aiverse_pack_writer (full zstd); linking aiverse_zstd_dec here would pull both into tests and tools.
target_link_libraries(aiverse_models PUBLIC aiverse_engine)

# Programmable presentation animation graphs reuse the existing model sampler and IK.
target_sources(aiverse_models PRIVATE engine/render/SurfaceLocomotion.cpp engine/render/AnimationContacts.cpp engine/render/FullBodyIK.cpp engine/render/AnimationGraph.cpp engine/render/AnimationRig.cpp engine/render/AnimationMotion.cpp)
# Unity animation import (agent-unity-anim): clip sidecars and the Mecanim-style controller node.
target_sources(aiverse_models PRIVATE engine/render/ModelClips.cpp engine/render/AnimationController.cpp)
if((AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS) AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS)
    add_executable(aiverse_clipinfo tools/unity_anim/clipinfo.cpp)
    target_link_libraries(aiverse_clipinfo PRIVATE aiverse_models aiverse_zstd_dec)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_deferred_loading_tests tests/deferred_loading.cpp)
    target_link_libraries(aiverse_deferred_loading_tests PRIVATE aiverse_runtime aiverse_game_view aiverse_content_reader)
    add_test(NAME aiverse_deferred_loading_tests COMMAND aiverse_deferred_loading_tests)
    add_executable(aiverse_unity_anim_tests tests/unity_anim/controller.cpp)
    target_link_libraries(aiverse_unity_anim_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME unity_anim_controller COMMAND aiverse_unity_anim_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_adaptive_shadow_resolution_tests tests/render/adaptive_shadow_resolution.cpp)
    target_include_directories(aiverse_adaptive_shadow_resolution_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME adaptive_shadow_resolution COMMAND aiverse_adaptive_shadow_resolution_tests)
    add_executable(aiverse_model_batching_policy_tests tests/render/model_batching_policy.cpp)
    target_link_libraries(aiverse_model_batching_policy_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME model_batching_policy COMMAND aiverse_model_batching_policy_tests)
    add_executable(aiverse_static_impostor_lod_tests tests/render/static_impostor_lod.cpp)
    target_link_libraries(aiverse_static_impostor_lod_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME static_impostor_lod COMMAND aiverse_static_impostor_lod_tests)
    add_executable(aiverse_strata_auto_select_tests tests/render/strata_auto_select.cpp)
    target_link_libraries(aiverse_strata_auto_select_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME strata_auto_select COMMAND aiverse_strata_auto_select_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_tests tests/animation/graphs.cpp apps/editor/core/AnimationDocument.cpp)
    target_link_libraries(aiverse_animation_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME animation_graphs COMMAND aiverse_animation_tests)
endif()

# Genre-independent ECS animation presentation host for native C++ projects.
# MotionWeave core (engine/animation): the engine-side target, no libraries/ include path. Engine targets
# (aiverse_view) link this one; the public package targets below add the library-level animation headers.
add_library(aiverse_motionweave_core INTERFACE)
target_compile_features(aiverse_motionweave_core INTERFACE cxx_std_20)
target_include_directories(aiverse_motionweave_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(aiverse_motionweave_core INTERFACE aiverse_models Threads::Threads)
else()
    target_link_libraries(aiverse_motionweave_core INTERFACE aiverse_models)
endif()
add_library(aiverse_animation_ecs INTERFACE)
add_library(AIVERSE::animation_ecs ALIAS aiverse_animation_ecs)
# MotionWeave is the canonical public API name; the existing animation_ecs
# target stays available to native projects and tests without a breaking change.
add_library(aiverse_motionweave INTERFACE)
add_library(AIVERSE::MotionWeave ALIAS aiverse_motionweave)
add_library(AIVERSE::motionweave ALIAS aiverse_motionweave)
target_link_libraries(aiverse_motionweave INTERFACE AIVERSE::animation_ecs)
target_include_directories(aiverse_animation_ecs INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libraries/include")
target_link_libraries(aiverse_animation_ecs INTERFACE aiverse_motionweave_core)

# VIVID digital humans (presentation-only facial/runtime and portable GPU morph compute).
add_subdirectory(engine/character/vivid)
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_ecs_tests tests/animation/ecs_presentation.cpp)
    target_link_libraries(aiverse_animation_ecs_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_ecs_presentation COMMAND aiverse_animation_ecs_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_playback_tests tests/animation/notifies_root_motion.cpp)
    target_link_libraries(aiverse_animation_playback_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_playback COMMAND aiverse_animation_playback_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_generic_retarget_tests tests/animation/generic_retarget.cpp)
    target_link_libraries(aiverse_generic_retarget_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_generic_retarget COMMAND aiverse_generic_retarget_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_montage_tests tests/animation/montage.cpp)
    target_link_libraries(aiverse_animation_montage_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_montage COMMAND aiverse_animation_montage_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_warp_tests tests/animation/motion_warp.cpp)
    target_link_libraries(aiverse_animation_warp_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_motion_warp COMMAND aiverse_animation_warp_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_inertial_tests tests/animation/inertialization.cpp)
    target_link_libraries(aiverse_animation_inertial_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_inertialization COMMAND aiverse_animation_inertial_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_physical_pose_tests tests/animation/physical_pose.cpp)
    target_link_libraries(aiverse_animation_physical_pose_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_physical_pose COMMAND aiverse_animation_physical_pose_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_budget_tests tests/animation/budget.cpp)
    target_link_libraries(aiverse_animation_budget_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_budget_policy COMMAND aiverse_animation_budget_tests)
endif()
# Fixed-tick authored root motion for authoritative ECS motor integration.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_root_motor_tests tests/animation/root_motor.cpp)
    target_link_libraries(aiverse_animation_root_motor_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_root_motor COMMAND aiverse_animation_root_motor_tests)
endif()
# Stage 180: semantic facial Control Rig + existing native IK/ECS.
# Deferred until the coordinated Mac CMake/CTest/RHI acceptance campaign.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_facial_control_rig_tests
        tests/animation/facial_control_rig.cpp)
    target_link_libraries(aiverse_animation_facial_control_rig_tests
        PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME animation_facial_control_rig
        COMMAND aiverse_animation_facial_control_rig_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_humanoid_retarget_tests tests/animation/humanoid_retarget.cpp)
    target_link_libraries(aiverse_humanoid_retarget_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_humanoid_retarget COMMAND aiverse_humanoid_retarget_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_ragdoll_tests tests/animation/ragdoll.cpp)
    target_link_libraries(aiverse_animation_ragdoll_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_ragdoll_asset COMMAND aiverse_animation_ragdoll_tests)
endif()
if((AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS) AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS)
    add_executable(aiverse_root_motion_bake tools/animation/root_motion_bake.cpp)
    target_link_libraries(aiverse_root_motion_bake PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_root_motion_bake_tests tests/animation/root_motion_bake.cpp)
    target_link_libraries(aiverse_root_motion_bake_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_root_motion_bake COMMAND aiverse_root_motion_bake_tests)
endif()
# Optional Box3D ragdoll readback adapter, no link dependency in the base animation API.
if(TARGET aiverse_physics)
    add_library(aiverse_animation_physics INTERFACE)
    add_library(AIVERSE::animation_physics ALIAS aiverse_animation_physics)
    target_link_libraries(aiverse_animation_physics INTERFACE AIVERSE::animation_ecs AIVERSE::physics)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_recovery_tests tests/animation/recovery.cpp)
    target_link_libraries(aiverse_animation_recovery_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_recovery COMMAND aiverse_animation_recovery_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_profile_tests tests/animation/humanoid_profile.cpp)
    target_link_libraries(aiverse_animation_profile_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_humanoid_profile COMMAND aiverse_animation_profile_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_jobs_tests tests/animation/jobs.cpp)
    target_link_libraries(aiverse_animation_jobs_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_jobs COMMAND aiverse_animation_jobs_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_graph_notify_tests tests/animation/graph_notifies.cpp)
    target_link_libraries(aiverse_animation_graph_notify_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME animation_graph_notifies COMMAND aiverse_animation_graph_notify_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_hot_reload_tests tests/animation/hot_reload.cpp)
    target_link_libraries(aiverse_animation_hot_reload_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_hot_reload COMMAND aiverse_animation_hot_reload_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_model_dependencies_tests tests/animation/model_dependencies.cpp)
    target_link_libraries(aiverse_model_dependencies_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_model_dependencies COMMAND aiverse_model_dependencies_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_visual_asset_watcher_tests
        tests/animation/editor_visual_asset_service.cpp)
    target_link_libraries(aiverse_visual_asset_watcher_tests
        PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_editor_visual_asset_watcher
        COMMAND aiverse_visual_asset_watcher_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_resource_retirement_tests tests/render/resource_retirement.cpp)
    target_include_directories(aiverse_resource_retirement_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME resource_retirement_queue COMMAND aiverse_resource_retirement_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_async_resource_retirement_tests
        tests/render/async_resource_retirement.cpp)
    target_include_directories(aiverse_async_resource_retirement_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME async_resource_retirement_queue
        COMMAND aiverse_async_resource_retirement_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_model_resource_owner_tests
        tests/render/model_resource_provider_contract.cpp)
    target_link_libraries(aiverse_model_resource_owner_tests
        PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME model_resource_owner_contract COMMAND aiverse_model_resource_owner_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_model_asset_provider_api_tests
        tests/render/model_asset_provider_api.cpp)
    target_link_libraries(aiverse_model_asset_provider_api_tests
        PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME model_asset_provider_api COMMAND aiverse_model_asset_provider_api_tests)
endif()
# Stage 176: actual SceneLoadCoordinator and MotionWeave native type
# integration, prepared now and run in the final coordinated Mac suite.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_animation_scene_game_host_api_tests
        tests/animation/scene_game_host_api.cpp)
    target_link_libraries(aiverse_animation_scene_game_host_api_tests
        PRIVATE aiverse_models aiverse_zstd_dec aiverse_engine AIVERSE::View
                Threads::Threads)
    add_test(NAME animation_scene_game_host_api
        COMMAND aiverse_animation_scene_game_host_api_tests)

    # Stage 179: fixture validates real showcase SceneAsset/preloaded GLB
    # paths. The graphical --scene-preview is compiled with ModelView itself.
    add_executable(aiverse_animation_scene_preview_assets_tests
        tests/animation/scene_preview_assets.cpp)
    target_compile_definitions(aiverse_animation_scene_preview_assets_tests
        PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(aiverse_animation_scene_preview_assets_tests
        PRIVATE aiverse_engine)
    add_test(NAME animation_scene_preview_assets
        COMMAND aiverse_animation_scene_preview_assets_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_retirement_pressure_tests
        tests/render/retirement_pressure.cpp)
    target_include_directories(aiverse_retirement_pressure_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME render_retirement_pressure COMMAND aiverse_retirement_pressure_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_async_model_cpu_queue_tests
        tests/render/async_model_cpu_queue.cpp)
    target_link_libraries(aiverse_async_model_cpu_queue_tests
        PRIVATE aiverse_models aiverse_zstd_dec Threads::Threads)
    add_test(NAME render_async_model_cpu_queue COMMAND aiverse_async_model_cpu_queue_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_model_estimate_tests tests/render/model_estimate.cpp)
    target_link_libraries(aiverse_model_estimate_tests PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME render_model_estimate COMMAND aiverse_model_estimate_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_model_priority_tests tests/render/model_priority.cpp)
    target_link_libraries(aiverse_model_priority_tests PRIVATE aiverse_models aiverse_zstd_dec Threads::Threads)
    add_test(NAME render_model_priority COMMAND aiverse_model_priority_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_model_owner_estimates_tests
        tests/render/model_owner_estimates.cpp)
    target_link_libraries(aiverse_model_owner_estimates_tests
        PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME render_model_owner_estimates
        COMMAND aiverse_model_owner_estimates_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_model_parallel_jobs_tests
        tests/render/model_parallel_jobs.cpp)
    target_link_libraries(aiverse_model_parallel_jobs_tests
        PRIVATE aiverse_models aiverse_zstd_dec Threads::Threads)
    add_test(NAME render_model_parallel_jobs
        COMMAND aiverse_model_parallel_jobs_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_geometry_page_owners_tests
        tests/render/geometry_page_owners.cpp)
    target_link_libraries(aiverse_geometry_page_owners_tests
        PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME render_geometry_page_owners
        COMMAND aiverse_geometry_page_owners_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_render_resource_owner_lifetime_tests
        tests/render/resource_owner_lifetime.cpp)
    target_link_libraries(aiverse_render_resource_owner_lifetime_tests
        PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME render_resource_owner_lifetime
        COMMAND aiverse_render_resource_owner_lifetime_tests)
    add_executable(aiverse_retirement_failed_token_tests
        tests/render/retirement_failed_token.cpp)
    target_include_directories(aiverse_retirement_failed_token_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME render_retirement_failed_token
        COMMAND aiverse_retirement_failed_token_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_render_resource_owner_group_tests
        tests/render/resource_owner_group.cpp)
    target_link_libraries(aiverse_render_resource_owner_group_tests
        PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME render_resource_owner_group
        COMMAND aiverse_render_resource_owner_group_tests)
    add_executable(aiverse_portrait_appearance_api_tests
        tests/view/portrait_appearance_api.cpp)
    target_link_libraries(aiverse_portrait_appearance_api_tests
        PRIVATE aiverse_game_view AIVERSE::View)
    add_test(NAME view_portrait_appearance_api
        COMMAND aiverse_portrait_appearance_api_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_persistent_model_pool_tests
        tests/render/persistent_model_pool.cpp)
    target_link_libraries(aiverse_persistent_model_pool_tests
        PRIVATE aiverse_models aiverse_zstd_dec Threads::Threads)
    add_test(NAME render_persistent_model_pool
        COMMAND aiverse_persistent_model_pool_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_retirement_status_replay_tests
        tests/render/retirement_status_replay.cpp)
    target_include_directories(aiverse_retirement_status_replay_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME render_retirement_status_replay
        COMMAND aiverse_retirement_status_replay_tests)
    add_executable(aiverse_view_appearance_packet_tests
        tests/view/appearance_packet.cpp)
    target_link_libraries(aiverse_view_appearance_packet_tests
        PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_appearance_packet
        COMMAND aiverse_view_appearance_packet_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_scoped_resource_owner_tests
        tests/render/resource_owner_scope.cpp)
    target_link_libraries(aiverse_scoped_resource_owner_tests
        PRIVATE aiverse_models aiverse_zstd_dec Threads::Threads)
    add_test(NAME render_scoped_resource_owner
        COMMAND aiverse_scoped_resource_owner_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_retirement_diagnostic_ring_tests
        tests/render/retirement_diagnostic_ring.cpp)
    target_include_directories(aiverse_retirement_diagnostic_ring_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME render_retirement_diagnostic_ring
        COMMAND aiverse_retirement_diagnostic_ring_tests)
    add_executable(aiverse_appearance_authoring_tests
        tests/view/appearance_authoring.cpp)
    target_link_libraries(aiverse_appearance_authoring_tests
        PRIVATE aiverse_models AIVERSE::View aiverse_shader_graph aiverse_zstd_dec)
    add_test(NAME view_appearance_authoring
        COMMAND aiverse_appearance_authoring_tests)
    add_executable(aiverse_appearance_registry_tests
        tests/view/appearance_registry.cpp)
    target_link_libraries(aiverse_appearance_registry_tests
        PRIVATE aiverse_models AIVERSE::View aiverse_shader_graph aiverse_zstd_dec)
    add_test(NAME view_appearance_registry
        COMMAND aiverse_appearance_registry_tests)
    add_executable(aiverse_appearance_choice_tests
        tests/view/appearance_choice.cpp)
    target_link_libraries(aiverse_appearance_choice_tests
        PRIVATE aiverse_models AIVERSE::View aiverse_shader_graph aiverse_zstd_dec)
    add_test(NAME view_appearance_choice
        COMMAND aiverse_appearance_choice_tests)
    add_executable(aiverse_appearance_prepared_tests
        tests/view/appearance_prepared.cpp)
    target_link_libraries(aiverse_appearance_prepared_tests
        PRIVATE aiverse_models AIVERSE::View aiverse_shader_graph aiverse_zstd_dec)
    add_test(NAME view_appearance_prepared
        COMMAND aiverse_appearance_prepared_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_model_image_transaction_tests
        tests/render/model_image_transaction.cpp)
    target_link_libraries(aiverse_model_image_transaction_tests
        PRIVATE aiverse_models aiverse_zstd_dec Threads::Threads)
    add_test(NAME render_model_image_transaction
        COMMAND aiverse_model_image_transaction_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_gpu_program_retirement_tests
        tests/render/gpu_program_retirement.cpp)
    target_include_directories(aiverse_gpu_program_retirement_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(aiverse_gpu_program_retirement_tests PRIVATE Threads::Threads)
    add_test(NAME render_gpu_program_retirement
        COMMAND aiverse_gpu_program_retirement_tests)
endif()
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_device_epoch_tests tests/render/device_epoch.cpp)
    target_link_libraries(aiverse_device_epoch_tests
        PRIVATE aiverse_models aiverse_zstd_dec Threads::Threads)
    add_test(NAME render_device_epoch COMMAND aiverse_device_epoch_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_crowd_tests tests/animation/crowd.cpp)
    target_link_libraries(aiverse_animation_crowd_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_crowd_policy COMMAND aiverse_animation_crowd_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_cues_tests tests/animation/cues.cpp)
    target_link_libraries(aiverse_animation_cues_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_cues COMMAND aiverse_animation_cues_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_animation_asset_loader_tests tests/animation/asset_loader.cpp)
    target_link_libraries(aiverse_animation_asset_loader_tests PRIVATE AIVERSE::animation_ecs aiverse_zstd_dec)
    add_test(NAME animation_montage_asset_loader COMMAND aiverse_animation_asset_loader_tests)
endif()

# Public MotionWeave API compatibility/standalone consumer check.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_api_tests tests/animation/motionweave_api.cpp)
    target_link_libraries(aiverse_motionweave_api_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_api COMMAND aiverse_motionweave_api_tests)
endif()

# MotionWeave core: sampling, blend/sync, morph-free path, IK, rig, motion matching, foot slide.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_core_tests tests/animation/motionweave_core.cpp)
    target_link_libraries(aiverse_motionweave_core_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_core COMMAND aiverse_motionweave_core_tests)
endif()

# MotionWeave persistent host workers (no worker startup in simulation).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_worker_tests tests/animation/motionweave_workers.cpp)
    target_link_libraries(aiverse_motionweave_worker_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_workers COMMAND aiverse_motionweave_worker_tests)
endif()
# Hybrid MotionWeave scheduler regression; validates callback lane separation.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_hybrid_tests tests/animation/motionweave_hybrid.cpp)
    target_link_libraries(aiverse_motionweave_hybrid_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_hybrid COMMAND aiverse_motionweave_hybrid_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_cost_tests tests/animation/motionweave_cost.cpp)
    target_link_libraries(aiverse_motionweave_cost_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_cost_balancing COMMAND aiverse_motionweave_cost_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_backend_tests tests/animation/motionweave_backend.cpp)
    target_link_libraries(aiverse_motionweave_backend_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_backend_selection COMMAND aiverse_motionweave_backend_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_cache_tests tests/animation/motionweave_cache.cpp)
    target_link_libraries(aiverse_motionweave_cache_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_graph_cost_cache COMMAND aiverse_motionweave_cache_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_worker_reuse_tests tests/animation/motionweave_worker_reuse.cpp)
    target_link_libraries(aiverse_motionweave_worker_reuse_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_worker_reuse COMMAND aiverse_motionweave_worker_reuse_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_parallel_rig_tests tests/animation/motionweave_parallel_rig.cpp)
    target_link_libraries(aiverse_motionweave_parallel_rig_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_parallel_rig COMMAND aiverse_motionweave_parallel_rig_tests)
endif()
# Host-only, opt-in MotionWeave CPU performance suite (no benchmark in CTest).
if(AIVERSE_BUILD_TOOLS AND AIVERSE_DESKTOP)
    add_executable(aiverse_motionweave_bench tools/animation/motionweave_bench.cpp)
    target_link_libraries(aiverse_motionweave_bench PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    # 53-bone procedural humanoid: per-character cost of clip/blend/IK/motion matching.
    add_executable(aiverse_motionweave_humanoid_bench tools/animation/motionweave_humanoid_bench.cpp)
    target_link_libraries(aiverse_motionweave_humanoid_bench PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    # Locomotion review: foot sliding / pops / broken poses + CSV for contact sheets.
    add_executable(aiverse_motionweave_locomotion_review tools/animation/motionweave_locomotion_review.cpp)
    target_link_libraries(aiverse_motionweave_locomotion_review PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_phase_tests tests/animation/motionweave_phase.cpp)
    target_link_libraries(aiverse_motionweave_phase_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_phase_seed COMMAND aiverse_motionweave_phase_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_phase_transfer_tests tests/animation/backend_phase_transfer.cpp)
    target_link_libraries(aiverse_motionweave_phase_transfer_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_backend_phase_transfer COMMAND aiverse_motionweave_phase_transfer_tests)
endif()

# Genre-independent native MotionWeave snapshot-to-presentation bridge.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_source_bridge_tests tests/animation/motionweave_source_bridge.cpp)
    target_link_libraries(aiverse_motionweave_source_bridge_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_source_bridge COMMAND aiverse_motionweave_source_bridge_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_motionweave_action_edges_tests tests/animation/action_edges.cpp)
    target_link_libraries(aiverse_motionweave_action_edges_tests PRIVATE AIVERSE::MotionWeave aiverse_zstd_dec)
    add_test(NAME motionweave_action_edges COMMAND aiverse_motionweave_action_edges_tests)
endif()

# Enforce that MotionWeave core cannot import FPS/RTS/other game genres.
if(AIVERSE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME motionweave_genre_boundary
            COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/animation/check_motionweave_dependencies.py
                ${PROJECT_SOURCE_DIR})
    endif()
endif()

# The canonical AIVERSE View is a generic presentation layer, not RTS.
add_library(aiverse_view INTERFACE)
add_library(AIVERSE::view ALIAS aiverse_view)
add_library(AIVERSE::View ALIAS aiverse_view)
target_compile_features(aiverse_view INTERFACE cxx_std_20)
target_include_directories(aiverse_view INTERFACE "${PROJECT_SOURCE_DIR}")
target_link_libraries(aiverse_view INTERFACE aiverse_motionweave_core aiverse_shader_graph)

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_core_tests tests/view/view.cpp)
    target_link_libraries(aiverse_view_core_tests PRIVATE AIVERSE::view aiverse_zstd_dec)
    add_test(NAME view_genre_independent COMMAND aiverse_view_core_tests)
    add_executable(aiverse_view_camera_lod_tests tests/view/camera_lod.cpp)
    aiverse_keep_test_asserts(aiverse_view_camera_lod_tests)
    target_link_libraries(aiverse_view_camera_lod_tests PRIVATE AIVERSE::view aiverse_zstd_dec)
    add_test(NAME view_camera_lod COMMAND aiverse_view_camera_lod_tests)
endif()

# Enforce that engine-level View never includes genre-specific game modules.
if(AIVERSE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME view_genre_boundary
            COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/view/check_view_dependencies.py
                ${PROJECT_SOURCE_DIR})
    endif()
endif()

# View pass queue is independent of game modules and the graphics device.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_layer_tests tests/view/layers.cpp)
    target_link_libraries(aiverse_view_layer_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_layer_order COMMAND aiverse_view_layer_tests)
endif()

# A single mixed scene draw stream for any host genre.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_packet_tests tests/view/packets.cpp)
    target_link_libraries(aiverse_view_packet_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_scene_packets COMMAND aiverse_view_packet_tests)
endif()

# Universal View surface queries (no terrain/game module dependencies).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_surface_tests tests/view/surface.cpp)
    target_link_libraries(aiverse_view_surface_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_surface_composition COMMAND aiverse_view_surface_tests)
endif()

# Generic external View layer-provider API.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_provider_tests tests/view/layer_providers.cpp)
    target_link_libraries(aiverse_view_provider_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_layer_providers COMMAND aiverse_view_provider_tests)
endif()

# Compile-time generic/legacy visibility type parity.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_visibility_alias_tests tests/view/visibility_alias.cpp)
    target_link_libraries(aiverse_view_visibility_alias_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_visibility_alias COMMAND aiverse_view_visibility_alias_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_frustum_tests tests/view/frustum.cpp)
    target_link_libraries(aiverse_view_frustum_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_frustum_culling COMMAND aiverse_view_frustum_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_horizon_tests tests/view/horizon.cpp)
    target_link_libraries(aiverse_view_horizon_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_spherical_horizon COMMAND aiverse_view_horizon_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_grid_geometry_tests tests/view/grid_geometry.cpp)
    target_link_libraries(aiverse_view_grid_geometry_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_grid_geometry COMMAND aiverse_view_grid_geometry_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_triangle_facing_tests tests/view/triangle_facing.cpp)
    target_link_libraries(aiverse_view_triangle_facing_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_triangle_facing COMMAND aiverse_view_triangle_facing_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_view_geometry_bounds_tests tests/view/geometry_bounds.cpp)
    target_link_libraries(aiverse_view_geometry_bounds_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_geometry_bounds COMMAND aiverse_view_geometry_bounds_tests)
endif()

# RTS presentation adapter uses Rules but never constructs the legacy GameView.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_rts_selection_presentation_tests tests/view/rts_selection_presentation.cpp)
    target_link_libraries(aiverse_rts_selection_presentation_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_rts_selection_presentation COMMAND aiverse_rts_selection_presentation_tests)
endif()

# RTS preview authoring is independent of the legacy GameView renderer.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_rts_preview_presentation_tests tests/view/rts_preview_presentation.cpp)
    target_link_libraries(aiverse_rts_preview_presentation_tests PRIVATE AIVERSE::View aiverse_zstd_dec)
    add_test(NAME view_rts_preview_presentation COMMAND aiverse_rts_preview_presentation_tests)
endif()

set(AIVERSE_RUNTIME_SRC
    engine/platform/PlatformCommon.cpp
    engine/render/Renderer3D.cpp
    engine/render/Renderer3DModels.cpp
    engine/render/GeometryScene.cpp
    engine/render/Lights.cpp
    engine/render/QualityGovernor.cpp
    engine/render/Renderer3DReflect.cpp
    engine/render/Renderer3DTrace.cpp
    engine/render/ImGuiLayer.cpp)
# PRISMA post-processing: runtime / independent GPU-free policy and catalog
list(APPEND AIVERSE_RUNTIME_SRC engine/render/Prisma.cpp engine/render/PhysicalSky.cpp engine/render/LocalFog.cpp engine/render/PrismaScreenLighting.cpp)
# baked lighting on the GPU (agent-lightbake): lightmaps / probes in the model shaders
list(APPEND AIVERSE_RUNTIME_SRC engine/render/BakedLighting.cpp engine/render/Renderer3DBaked.cpp)
add_library(aiverse_shader_graph STATIC engine/render/graph/ShaderGraph.cpp)
target_link_libraries(aiverse_shader_graph PUBLIC aiverse_engine)

# Canonical post-processing implementation: PRISMA.
# Keep existing aiverse_postfx_data target alive for older native targets.
add_library(aiverse_postfx_data STATIC engine/render/PrismaData.cpp engine/render/PrismaVolumeData.cpp)
add_library(aiverse_prisma_data ALIAS aiverse_postfx_data)
add_library(AIVERSE::prisma_data ALIAS aiverse_postfx_data)
target_link_libraries(aiverse_postfx_data PUBLIC aiverse_engine aiverse_shader_graph)

if(AIVERSE_PLATFORM STREQUAL "macos")
    list(APPEND AIVERSE_RUNTIME_SRC engine/platform/macos/PlatformMac.mm engine/gfx/metal/GfxMetal.mm engine/platform/apple/PluginsApple.mm)
elseif(AIVERSE_PLATFORM STREQUAL "ios")
    list(APPEND AIVERSE_RUNTIME_SRC engine/platform/ios/PlatformIOS.mm engine/gfx/metal/GfxMetal.mm engine/platform/apple/PluginsApple.mm)
elseif(AIVERSE_PLATFORM STREQUAL "android")
    list(APPEND AIVERSE_RUNTIME_SRC engine/platform/android/PlatformAndroid.cpp engine/gfx/gl/GfxGL.cpp)
elseif(AIVERSE_PLATFORM STREQUAL "web")
    list(APPEND AIVERSE_RUNTIME_SRC engine/platform/web/PlatformWeb.cpp engine/gfx/gl/GfxGL.cpp)
elseif(AIVERSE_PLATFORM STREQUAL "windows")
    list(APPEND AIVERSE_RUNTIME_SRC engine/platform/windows/PlatformWin32.cpp engine/gfx/gl/GfxGL.cpp)
else()
    list(APPEND AIVERSE_RUNTIME_SRC engine/platform/linux/PlatformLinux.cpp engine/gfx/gl/GfxGL.cpp)
endif()

# Vulkan RHI backend next to GL (Windows, Linux, Android): opt-in at run time (AIVERSE_GFX=vulkan, --gfx vulkan,
# game.json "graphics"), GL stays the default and the fallback. The loader is opened at run time; shaders are
# the engine's GLSL compiled to SPIR-V with the vendored glslang (third_party/glslang). docs/vulkan.md
set(_aiverse_vulkan_default OFF)
if(AIVERSE_PLATFORM MATCHES "^(windows|linux|android)$")
    set(_aiverse_vulkan_default ON)
endif()
option(AIVERSE_VULKAN "Build the Vulkan RHI backend (Windows, Linux, Android)" ${_aiverse_vulkan_default})
if(AIVERSE_VULKAN)
    # SPIR-V optimizer (vendored SPIRV-Tools + SPIRV-Headers, third_party/spirv-tools): glslang runs the
    # performance passes on every program. Mobile drivers (Adreno) barely optimise SPIR-V themselves:
    # unoptimised shaders cost ~30% GPU time on a Galaxy A05s. Needs Python 3 on the build host (grammar tables).
    option(AIVERSE_SPIRV_OPT "Optimize Vulkan SPIR-V with the vendored SPIRV-Tools" ON)
    if(AIVERSE_SPIRV_OPT)
        set(SPIRV-Headers_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-headers)
        # the table generators need Python >= 3.9: take the newest interpreter, not the first on PATH
        set(Python3_FIND_STRATEGY VERSION)
        set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
        set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
        set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
        set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
        set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
        add_subdirectory(third_party/spirv-tools EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(third_party/glslang)
    add_library(aiverse_gfx_vulkan STATIC engine/gfx/vulkan/VkApi.cpp engine/gfx/vulkan/VkShaderCompiler.cpp engine/gfx/vulkan/VkSurface.cpp
        engine/gfx/vulkan/GfxVulkan.cpp engine/gfx/vulkan/VkResources.cpp engine/gfx/vulkan/VkDraw.cpp
        engine/gfx/vulkan/VkCompute.cpp)
    target_include_directories(aiverse_gfx_vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include)
    target_compile_definitions(aiverse_gfx_vulkan PUBLIC AIVERSE_WITH_VULKAN=1)
    target_link_libraries(aiverse_gfx_vulkan PRIVATE aiverse_glslang PUBLIC ${CMAKE_DL_LIBS})
    if(AIVERSE_BUILD_TESTS)   # offscreen device; skips without a Vulkan driver (CI: Mesa lavapipe)
        add_executable(aiverse_vulkan_tests tests/render/vulkan_tests.cpp)
        target_link_libraries(aiverse_vulkan_tests PRIVATE aiverse_gfx_vulkan)
        add_test(NAME vulkan_backend COMMAND aiverse_vulkan_tests)
        add_executable(aiverse_lighting_vulkan_compute_shader tests/render/lighting_vulkan_compute_shader.cpp)
        target_link_libraries(aiverse_lighting_vulkan_compute_shader PRIVATE aiverse_gfx_vulkan)
        add_test(NAME lighting_vulkan_compute_shader COMMAND aiverse_lighting_vulkan_compute_shader)
        set_tests_properties(lighting_vulkan_compute_shader PROPERTIES LABELS "Lighting")

        # Real headless Vulkan VIVID ID/depth + async one-pixel RHI oracle.
        # No driver is a registered SKIP, not an invented GPU PASS.
        add_executable(aiverse_vivid_vulkan_sculpt_id_tests
            tests/vivid/vulkan_sculpt_id.cpp)
        target_link_libraries(aiverse_vivid_vulkan_sculpt_id_tests PRIVATE
            aiverse_gfx_vulkan aiverse_models)
        target_compile_features(aiverse_vivid_vulkan_sculpt_id_tests
            PRIVATE cxx_std_20)
        add_test(NAME vivid_vulkan_sculpt_id
            COMMAND aiverse_vivid_vulkan_sculpt_id_tests)
        set_tests_properties(vivid_vulkan_sculpt_id
            PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# Native GLES3 VIVID GPU-ID + PBO/fence readback acceptance. Does not link a
# game client or platform window; skips on systems without EGL/GLES3 driver.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "linux")
    add_executable(aiverse_vivid_gles_sculpt_id_tests
        tests/vivid/gles_sculpt_id.cpp
        engine/gfx/gl/GfxGL.cpp)
    target_link_libraries(aiverse_vivid_gles_sculpt_id_tests PRIVATE
        aiverse_models EGL GLESv2)
    target_compile_features(aiverse_vivid_gles_sculpt_id_tests
        PRIVATE cxx_std_20)
    add_test(NAME vivid_gles_sculpt_id
        COMMAND aiverse_vivid_gles_sculpt_id_tests)
    set_tests_properties(vivid_gles_sculpt_id PROPERTIES SKIP_RETURN_CODE 77)
endif()

# GL backend redundant-state filter and native base-vertex draws on a real
# EGL/GLES3 context (Mesa llvmpipe is enough); skips without EGL/GLES3.
# AIVERSE_GL_BENCH=<draws> [AIVERSE_GL_BENCH_VARY=1] also times CPU submission.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "linux")
    add_executable(aiverse_gl_state_cache_gpu_tests
        tests/render/gl_state_cache_gpu.cpp
        engine/gfx/gl/GfxGL.cpp)
    target_include_directories(aiverse_gl_state_cache_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_gl_state_cache_gpu_tests PRIVATE EGL GLESv2)
    target_compile_features(aiverse_gl_state_cache_gpu_tests PRIVATE cxx_std_20)
    add_test(NAME gl_state_cache_gpu COMMAND aiverse_gl_state_cache_gpu_tests)
    set_tests_properties(gl_state_cache_gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "GLES;GPU")
endif()

# The platform files define main(); they are compiled into each executable through an OBJECT
# library so that the entry point lands in the final binary on every toolchain.
# Native plugins bridge + store/ads/analytics/privacy services (engine/platform/Plugins.h, engine/services). No window
# needed, so tests link it directly; platform backends live in the runtime sources.
# Desktop mock store/ads: on for development builds; aiversebuild turns it off in packaged games unless
# game.json lists a mock (plugins.cmake sets AIVERSE_MOCK_PLUGINS), so they are not even compiled in.
if(NOT DEFINED AIVERSE_MOCK_PLUGINS)
    set(AIVERSE_MOCK_PLUGINS ON)
endif()
add_library(aiverse_plugins STATIC
    engine/platform/Plugins.cpp
    engine/platform/PluginJson.cpp
    engine/services/Store.cpp
    engine/services/Ads.cpp
    engine/services/Analytics.cpp      # analytics / privacy (ATT, consent) services + their Lua tables
    engine/services/Privacy.cpp
    engine/services/ServicesLua.cpp
    engine/services/Social.cpp engine/services/Push.cpp engine/services/Links.cpp engine/services/Consent.cpp)   # social / push / links / consent services
target_include_directories(aiverse_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aiverse_plugins PUBLIC aiverse_lua)   # ServicesLua (analytics.* / privacy.* for the UI Lua)
if(AIVERSE_MOCK_PLUGINS)
    target_sources(aiverse_plugins PRIVATE engine/services/MockPlugins.cpp engine/services/MockClientPlugins.cpp)
    target_compile_definitions(aiverse_plugins PUBLIC AIVERSE_WITH_MOCK_PLUGINS=1)
endif()

# NVIDIA DLSS for the PRISMA "dlss" effect (engine/render/PrismaDlss.h): only with the user's
# own NGX SDK, never vendored (proprietary). Windows + Vulkan backend.
set(AIVERSE_DLSS_SDK_DIR "" CACHE PATH "NVIDIA DLSS SDK folder (include/, lib/Windows_x86_64/); empty = no DLSS")
set(AIVERSE_WITH_DLSS OFF)
if(AIVERSE_DLSS_SDK_DIR AND WIN32 AND AIVERSE_VULKAN)
    list(APPEND AIVERSE_RUNTIME_SRC engine/render/PrismaDlssNgx.cpp)
    set(AIVERSE_WITH_DLSS ON)
else()
    if(AIVERSE_DLSS_SDK_DIR)
        message(WARNING "AIVERSE_DLSS_SDK_DIR is set, but DLSS needs Windows and AIVERSE_VULKAN=ON; building without it")
    endif()
    list(APPEND AIVERSE_RUNTIME_SRC engine/render/PrismaDlssStub.cpp)
endif()
add_library(aiverse_runtime OBJECT ${AIVERSE_RUNTIME_SRC})
# Packaged clients link an archive of the runtime objects. The linker extracts
# only translation units needed by the client, while editor and test targets
# keep the existing complete OBJECT runtime contract.
add_library(aiverse_client_runtime STATIC $<TARGET_OBJECTS:aiverse_runtime>)
target_link_libraries(aiverse_client_runtime PUBLIC
    "$<TARGET_PROPERTY:aiverse_runtime,INTERFACE_LINK_LIBRARIES>")
# The archive must carry every usage requirement of the runtime, not only its
# libraries: Web link flags (-sUSE_WEBGL2, memory growth, idbfs) and public
# definitions/includes are set on aiverse_runtime later in this file.
target_link_options(aiverse_client_runtime INTERFACE
    "$<TARGET_PROPERTY:aiverse_runtime,INTERFACE_LINK_OPTIONS>")
target_compile_definitions(aiverse_client_runtime INTERFACE
    "$<TARGET_PROPERTY:aiverse_runtime,INTERFACE_COMPILE_DEFINITIONS>")
target_compile_options(aiverse_client_runtime INTERFACE
    "$<TARGET_PROPERTY:aiverse_runtime,INTERFACE_COMPILE_OPTIONS>")
target_include_directories(aiverse_client_runtime INTERFACE
    "$<TARGET_PROPERTY:aiverse_runtime,INTERFACE_INCLUDE_DIRECTORIES>")
if(AIVERSE_VULKAN)
    target_link_libraries(aiverse_runtime PUBLIC aiverse_gfx_vulkan)
endif()
target_link_libraries(aiverse_runtime PUBLIC aiverse_engine aiverse_models aiverse_imgui aiverse_pack aiverse_zstd_dec aiverse_plugins)
target_link_libraries(aiverse_runtime PUBLIC aiverse_postfx_data aiverse_shader_graph)   # postfx agent
if(AIVERSE_WITH_DLSS)
    find_library(AIVERSE_NGX_LIBRARY NAMES nvsdk_ngx_d nvsdk_ngx_s
        PATHS "${AIVERSE_DLSS_SDK_DIR}/lib/Windows_x86_64/x64" "${AIVERSE_DLSS_SDK_DIR}/lib/Windows_x86_64/x86_64"
        NO_DEFAULT_PATH)
    if(NOT AIVERSE_NGX_LIBRARY)
        message(FATAL_ERROR "AIVERSE_DLSS_SDK_DIR: nvsdk_ngx_d.lib not found under ${AIVERSE_DLSS_SDK_DIR}/lib/Windows_x86_64")
    endif()
    target_include_directories(aiverse_runtime PUBLIC "${AIVERSE_DLSS_SDK_DIR}/include")
    target_compile_definitions(aiverse_runtime PUBLIC AIVERSE_HAS_DLSS=1)
    target_link_libraries(aiverse_runtime PUBLIC "${AIVERSE_NGX_LIBRARY}")
endif()
if(AIVERSE_TEXTURE_WITH_LIBKTX)
    target_link_libraries(aiverse_runtime PUBLIC aiverse_texture_libktx)
endif()
if(AIVERSE_TEXTURE_WITH_OPENEXR)
    target_link_libraries(aiverse_runtime PUBLIC OpenEXR::OpenEXR)
    target_compile_definitions(aiverse_runtime PUBLIC AIVERSE_TEXTURE_WITH_OPENEXR=1)
endif()
# NEREID world headers are engine code (engine/world/nereid): the runtime links no library package.

if(APPLE)
    set_source_files_properties(engine/platform/macos/PlatformMac.mm engine/platform/ios/PlatformIOS.mm
        engine/gfx/metal/GfxMetal.mm engine/platform/apple/PluginsApple.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(aiverse_runtime PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    if(AIVERSE_PLATFORM STREQUAL "macos")
        target_link_libraries(aiverse_runtime PUBLIC "-framework Cocoa")
    else()
        # UIKit no longer re-exports CoreGraphics to the linker; PlatformIOS uses CGRect/CGPoint functions directly.
        target_link_libraries(aiverse_runtime PUBLIC "-framework UIKit" "-framework CoreGraphics")
    endif()
elseif(AIVERSE_PLATFORM STREQUAL "android")
    set(GLUE ${ANDROID_NDK}/sources/android/native_app_glue)
    add_library(native_app_glue STATIC ${GLUE}/android_native_app_glue.c)
    target_include_directories(native_app_glue PUBLIC ${GLUE})
    target_compile_options(native_app_glue PRIVATE -w)   # NDK sample source (unused parameters under -Werror)
    target_link_libraries(aiverse_runtime PUBLIC native_app_glue android EGL GLESv3 log)
elseif(AIVERSE_PLATFORM STREQUAL "web")
    option(AIVERSE_WEBGL_CONTEXT_ACCEPTANCE
        "Expose read-only WebGL2 lifecycle counters for browser GPU recovery acceptance" OFF)
    if(AIVERSE_WEBGL_CONTEXT_ACCEPTANCE)
        target_compile_definitions(aiverse_runtime PRIVATE AIVERSE_WEBGL_CONTEXT_ACCEPTANCE=1)
    endif()
    # -lidbfs.js: userDataDir() is persisted in IndexedDB (engine/platform/web)
    target_link_options(aiverse_runtime PUBLIC -sUSE_WEBGL2=1 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -lidbfs.js)
elseif(AIVERSE_PLATFORM STREQUAL "windows")
    target_link_libraries(aiverse_runtime PUBLIC opengl32 user32 gdi32 shell32)
else()
    target_link_libraries(aiverse_runtime PUBLIC X11 EGL GLESv2)
endif()

# Shared presentation visibility field; independent of game genre.
add_library(aiverse_view_visibility STATIC engine/view/VisibilityField.cpp)
add_library(AIVERSE::view_visibility ALIAS aiverse_view_visibility)
target_include_directories(aiverse_view_visibility PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(aiverse_view_visibility PUBLIC aiverse_models)

# Spline roads, shared by every project and client (docs/roads.md): data + geometry (Fixed, no GPU) and the view.
add_library(aiverse_roads STATIC engine/roads/RoadNet.cpp engine/roads/RoadTypes.cpp)
add_library(AIVERSE::roads ALIAS aiverse_roads)
target_include_directories(aiverse_roads PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(aiverse_roads PUBLIC aiverse_engine)
add_library(aiverse_roads_view STATIC engine/roads/RoadView.cpp engine/roads/RoadKit.cpp)
add_library(AIVERSE::roads_view ALIAS aiverse_roads_view)
target_link_libraries(aiverse_roads_view PUBLIC aiverse_roads aiverse_models aiverse_render2d)

add_library(aiverse_game_view STATIC engine/view/game/GameView.cpp engine/view/game/GameTerrain.cpp
    engine/view/game/GameShoreView.cpp
    engine/view/game/GameShapes.cpp engine/view/game/GameLook.cpp engine/view/game/GameAnimate.cpp
    engine/view/game/GameViewDraw.cpp engine/view/game/GamePortrait.cpp
    engine/view/game/GameLights.cpp engine/view/game/GameLightsData.cpp engine/view/game/GameGroundFx.cpp engine/view/game/GameTerrainRender.cpp engine/view/game/GameTerrainDetailMesh.cpp
    engine/view/game/GameFieldView.cpp
    engine/view/game/TopDownControls.cpp   # top-down hero sticks / buttons / follow camera (agent-topdown-control)
    engine/view/game/EncounterDecals.cpp   # boss encounters: telegraph decals, status rings (agent-df-encounters-glue)
    engine/view/game/GamePlanetRender.cpp engine/view/game/GamePlanetView.cpp engine/view/game/GamePlanetGroundFx.cpp engine/view/game/GamePlanetFlora.cpp)
# visual terrain detail surface (agent-terrain): shared by the view and the editor core (sculpt, validate)
# + the planet surface maths (GamePlanet*: float cube-sphere surface, detail grid synthesis; editor planet detail ops)
add_library(aiverse_terrain_detail STATIC engine/view/game/GameTerrainDetail.cpp engine/view/game/GamePlanet.cpp engine/view/game/GamePlanetDetail.cpp)
target_link_libraries(aiverse_terrain_detail PUBLIC aiverse_game)
target_link_libraries(aiverse_game_view PUBLIC aiverse_game aiverse_models aiverse_imgui aiverse_roads_view)
target_link_libraries(aiverse_game_view PUBLIC aiverse_terrain_detail)
target_link_libraries(aiverse_game_view PUBLIC aiverse_view_visibility)
# Transitional host adapter consumes the engine View. The dependency points
# from the game module into the engine, never back into an RTS implementation.
target_link_libraries(aiverse_game_view PUBLIC AIVERSE::view)
target_link_libraries(aiverse_game_view PUBLIC aiverse_world)   # GameWaterNereid.h: NEREID manifest parsing (engine/world)

# Helper: package content + platform metadata around an executable.
include(platform/cmake/ThirdPartyNotices.cmake)

option(AIVERSE_PRECOMPILE_METAL "Precompile macOS client Metal shaders after linking (requires GUI and Metal Toolchain)" OFF)
set(AIVERSE_METAL_CONTENT_DIR "" CACHE PATH "Resolved loose content override for native client shader capture")
set(AIVERSE_METAL_CAPTURE_MAP "0" CACHE STRING "Map index for build-time Metal variant capture")

# OFF: the client is not packaged with a copy of the content (the editor's per-project build: it runs the client on
# the project folder through AIVERSE_CONTENT, and the build dir lives inside that folder).
option(AIVERSE_CLIENT_BUNDLE_CONTENT "Copy the game content next to / into the client executable" ON)

function(aiverse_package_app target)
    aiverse_package_notices(${target})
    # the editor's project file (<Title>.aiverse, legacy .aiverse) is not game content: dropped from loose dev bundles
    file(GLOB _aiverse_projfiles RELATIVE "${AIVERSE_CONTENT_DIR}" "${AIVERSE_CONTENT_DIR}/*.aiverse" "${AIVERSE_CONTENT_DIR}/*.aiverse")
    set(_aiverse_rm_exe "")
    foreach(f ${_aiverse_projfiles})
        list(APPEND _aiverse_rm_exe "$<TARGET_FILE_DIR:${target}>/content/${f}")
    endforeach()
    if(AIVERSE_PLATFORM STREQUAL "macos" OR AIVERSE_PLATFORM STREQUAL "ios")
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE TRUE
            OUTPUT_NAME "${AIVERSE_GAME_EXECUTABLE}"
            MACOSX_BUNDLE_BUNDLE_NAME "${AIVERSE_GAME_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${AIVERSE_GAME_ID}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${AIVERSE_GAME_BUILD}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${AIVERSE_GAME_VERSION}"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${AIVERSE_GAME_ID}")
        if(AIVERSE_PLATFORM STREQUAL "ios" AND AIVERSE_GEN_DIR AND EXISTS "${AIVERSE_GEN_DIR}/Assets.xcassets")
            target_sources(${target} PRIVATE "${AIVERSE_GEN_DIR}/Assets.xcassets")
            set_source_files_properties("${AIVERSE_GEN_DIR}/Assets.xcassets" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
            set_target_properties(${target} PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
        endif()
        if(AIVERSE_PLATFORM STREQUAL "macos" AND AIVERSE_GEN_DIR AND EXISTS "${AIVERSE_GEN_DIR}/AppIcon.icns")
            target_sources(${target} PRIVATE "${AIVERSE_GEN_DIR}/AppIcon.icns")
            set_source_files_properties("${AIVERSE_GEN_DIR}/AppIcon.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        endif()
        if(AIVERSE_PLATFORM STREQUAL "ios")
            set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/Info.plist.in"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${AIVERSE_APPLE_TEAM_ID}"
                # CocoaPods' aggregate Pods_<target>.framework is built into PODS_CONFIGURATION_BUILD_DIR, but this
                # target's BUILT_PRODUCTS_DIR is CMake's own folder: the linker has to be told where to look
                XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS "$(inherited) $(PODS_CONFIGURATION_BUILD_DIR)"
                # static Swift pods (ad SDKs) pull swiftCompatibility* from the default toolchain; this app has no
                # Swift sources, and TOOLCHAIN_DIR may name another toolchain (Xcode 26: the Metal toolchain)
                XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS "$(inherited) $(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/$(PLATFORM_NAME) $(SDKROOT)/usr/lib/swift"
                XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "$(inherited) /usr/lib/swift @executable_path/Frameworks")
            set(res_dir "$<TARGET_BUNDLE_DIR:${target}>")
        else()
            set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in")
            set(res_dir "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources")
        endif()
        set(_aiverse_rm_bundle "")
        foreach(f ${_aiverse_projfiles})
            list(APPEND _aiverse_rm_bundle "${res_dir}/content/${f}")
        endforeach()
        if(AIVERSE_CONTENT_PACK)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${res_dir}/content"
                COMMAND ${CMAKE_COMMAND} -E copy "${AIVERSE_CONTENT_PACK}" "${res_dir}/content/content.rpak"
                COMMENT "Bundling content pack")
        elseif(AIVERSE_CLIENT_BUNDLE_CONTENT)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${res_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${AIVERSE_CONTENT_DIR}" "${res_dir}/content"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${res_dir}/content/Builds" "${res_dir}/content/logs" ${_aiverse_rm_bundle}   # project outputs, project file
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${AIVERSE_ENGINE_ASSETS}" "${res_dir}/content/engine"
                COMMENT "Bundling content")
        endif()
        if(AIVERSE_PRECOMPILE_METAL AND AIVERSE_PLATFORM STREQUAL "macos" AND target STREQUAL "aiverse_client")
            if(AIVERSE_CONTENT_PACK)
                message(FATAL_ERROR "Precompile Metal against loose staging content before creating content.rpak")
            endif()
            find_package(Python3 COMPONENTS Interpreter REQUIRED)
            set(_metal_content "${res_dir}/content")
            if(AIVERSE_METAL_CONTENT_DIR)
                set(_metal_content "${AIVERSE_METAL_CONTENT_DIR}")
            endif()
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/metal/precompile.py"
                    --executable "$<TARGET_FILE:${target}>" --content "${_metal_content}"
                    --work-dir "${CMAKE_CURRENT_BINARY_DIR}/metal-precompile" --map "${AIVERSE_METAL_CAPTURE_MAP}"
                COMMENT "Precompiling Metal libraries and host-GPU pipelines" VERBATIM)
        endif()
    elseif(AIVERSE_PLATFORM STREQUAL "web")
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
        if(AIVERSE_CONTENT_PACK)
            target_link_options(${target} PRIVATE "--preload-file=${AIVERSE_CONTENT_PACK}@/content/content.rpak")
            # index.data is written at link time: relink when only the content pack changed.
            set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${AIVERSE_CONTENT_PACK}")
        else()
            target_link_options(${target} PRIVATE
                "--preload-file=${AIVERSE_CONTENT_DIR}@/content"
                "--preload-file=${AIVERSE_ENGINE_ASSETS}@/content/engine")
        endif()
        target_link_options(${target} PRIVATE "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/platform/web/shell.html")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/shell.html")
    elseif(AIVERSE_PLATFORM STREQUAL "windows" OR AIVERSE_PLATFORM STREQUAL "linux")
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${AIVERSE_GAME_EXECUTABLE}")
        if(WIN32)
            set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
        endif()
        if(AIVERSE_CONTENT_PACK)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/content"
                COMMAND ${CMAKE_COMMAND} -E copy "${AIVERSE_CONTENT_PACK}" "$<TARGET_FILE_DIR:${target}>/content/content.rpak"
                COMMENT "Copying content pack next to the executable")
        elseif(AIVERSE_CLIENT_BUNDLE_CONTENT)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${AIVERSE_CONTENT_DIR}" "$<TARGET_FILE_DIR:${target}>/content"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${target}>/content/Builds" "$<TARGET_FILE_DIR:${target}>/content/logs" ${_aiverse_rm_exe}   # project outputs, .aiverse
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${AIVERSE_ENGINE_ASSETS}" "$<TARGET_FILE_DIR:${target}>/content/engine"
                COMMENT "Copying content next to the executable")
        endif()
    endif()
endfunction()

# AIVERSE_SCENE_CLIENT=ON (aiversebuild: game.json "module": "scene" or no "module"): the packaged
# client is apps/client/SceneMain.cpp on the platform runtime only - no simulation, renderer, UI,
# scripting or audio. AIVERSE_GAME_CLIENT_TARGETS lists the targets that receive the GameClient
# sources and libraries below; it is empty for the scene client.
option(AIVERSE_SCENE_CLIENT "Package the minimal scene client instead of the game client" OFF)
if(AIVERSE_SCENE_CLIENT AND (AIVERSE_BUILD_EDITOR OR AIVERSE_BUILD_TESTS OR AIVERSE_BUILD_STUDIO_HTML5))
    message(FATAL_ERROR "AIVERSE_SCENE_CLIENT=ON is for packaged clients only; turn off the editor, tests and HTML5 Studio")
endif()
if(AIVERSE_SCENE_CLIENT)
    set(AIVERSE_GAME_CLIENT_TARGETS "")
    set(_aiverse_client_entry apps/client/SceneMain.cpp)
else()
    set(AIVERSE_GAME_CLIENT_TARGETS aiverse_client main)
    set(_aiverse_client_entry apps/client/main.cpp)
endif()
if(AIVERSE_BUILD_CLIENT)
    if(AIVERSE_SCENE_CLIENT AND AIVERSE_PLATFORM STREQUAL "android")
        add_library(main SHARED ${_aiverse_client_entry})
        target_link_libraries(main PRIVATE aiverse_client_runtime)
        set_target_properties(main PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
        aiverse_package_notices(main)
        set(AIVERSE_CLIENT_TARGET main)
    elseif(AIVERSE_SCENE_CLIENT)
        add_executable(aiverse_client ${_aiverse_client_entry})
        target_link_libraries(aiverse_client PRIVATE aiverse_client_runtime)
        aiverse_package_app(aiverse_client)
        set(AIVERSE_CLIENT_TARGET aiverse_client)
    elseif(AIVERSE_PLATFORM STREQUAL "android")
        add_library(main SHARED apps/client/main.cpp)
        target_link_libraries(main PRIVATE aiverse_client_runtime aiverse_game_view aiverse_i18n AIVERSE::InputFlow)
        target_link_libraries(main PRIVATE aiverse_game_audio)
        # Keep ANativeActivity_onCreate from the glue library.
        set_target_properties(main PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
        aiverse_package_notices(main)
        set(AIVERSE_CLIENT_TARGET main)
    else()
        add_executable(aiverse_client apps/client/main.cpp)
        target_link_libraries(aiverse_client PRIVATE aiverse_client_runtime aiverse_game_view aiverse_i18n AIVERSE::InputFlow)
        target_link_libraries(aiverse_client PRIVATE aiverse_game_audio)
        aiverse_package_app(aiverse_client)
        set(AIVERSE_CLIENT_TARGET aiverse_client)
        target_compile_definitions(aiverse_client PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")   # actor / voxel hosts: engine dir in a dev build
        # actor (first / third person) and voxel projects run in the same client (apps/client/main.cpp dispatches
        # on game.json "module"); their hosts are added in engine/actor and engine/voxel (aiverse_client_hosts)
    endif()
    # Mach-O keeps otherwise unused functions from directly linked client
    # objects unless dead stripping is requested explicitly. Static archives
    # above select translation units; this also removes unreachable sections.
    if(AIVERSE_PLATFORM STREQUAL "macos")
        target_link_options(aiverse_client PRIVATE "LINKER:-dead_strip")
    endif()
    # Native plugins (platforms agent): C++ / Objective-C++ sources compiled into the game itself,
    # so AIVERSE_PLUGIN(...) static registration is never dropped by the linker.
    if(AIVERSE_PLUGIN_SOURCES)
        target_sources(${AIVERSE_CLIENT_TARGET} PRIVATE ${AIVERSE_PLUGIN_SOURCES})
        if(APPLE)
            foreach(src ${AIVERSE_PLUGIN_SOURCES})
                if(src MATCHES "\\.(m|mm)$")
                    set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
                endif()
            endforeach()
        endif()
    endif()
    foreach(fw ${AIVERSE_PLUGIN_FRAMEWORKS})
        target_link_libraries(${AIVERSE_CLIENT_TARGET} PRIVATE "-framework ${fw}")
    endforeach()
    # Desktop SDKs the developer installs (e.g. Steamworks): headers, import libraries, and shared
    # libraries copied next to the executable (rpath $ORIGIN / @loader_path finds them there).
    if(AIVERSE_PLUGIN_INCLUDE_DIRS)
        target_include_directories(${AIVERSE_CLIENT_TARGET} SYSTEM PRIVATE ${AIVERSE_PLUGIN_INCLUDE_DIRS})
    endif()
    if(AIVERSE_PLUGIN_LIBRARIES)
        target_link_libraries(${AIVERSE_CLIENT_TARGET} PRIVATE ${AIVERSE_PLUGIN_LIBRARIES})
    endif()
    if(AIVERSE_PLUGIN_RUNTIME_FILES)
        add_custom_command(TARGET ${AIVERSE_CLIENT_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AIVERSE_PLUGIN_RUNTIME_FILES} "$<TARGET_FILE_DIR:${AIVERSE_CLIENT_TARGET}>")
        if(UNIX AND NOT APPLE)
            set_target_properties(${AIVERSE_CLIENT_TARGET} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
        elseif(APPLE)
            set_target_properties(${AIVERSE_CLIENT_TARGET} PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Light baker (agent-lightbake): offline CPU path tracer (BVH, threads) for lightmaps + light probes,
# automatic lightmap UVs (xatlas, MIT), bake folder writer. Host only: editor, MCP, tests.
# ---------------------------------------------------------------------------
if(AIVERSE_BUILD_EDITOR OR AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_library(aiverse_xatlas STATIC third_party/xatlas/xatlas.cpp)
    target_include_directories(aiverse_xatlas PUBLIC third_party/xatlas)
    if(NOT MSVC)
        target_compile_options(aiverse_xatlas PRIVATE -w)
    endif()
    add_library(aiverse_bake STATIC engine/bake/Bvh.cpp engine/bake/Baker.cpp engine/bake/BakerProbes.cpp
        engine/bake/BakeScene.cpp engine/bake/LightmapUnwrap.cpp engine/bake/LightmapWrite.cpp engine/bake/Denoise.cpp)
    target_link_libraries(aiverse_bake PUBLIC aiverse_models aiverse_xatlas Threads::Threads)
    target_include_directories(aiverse_bake PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_bake_tests tests/bake/bake_tests.cpp)
    target_link_libraries(aiverse_bake_tests PRIVATE aiverse_bake aiverse_models aiverse_zstd_dec)
    target_compile_definitions(aiverse_bake_tests PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME bake_tests COMMAND aiverse_bake_tests)
endif()

if(AIVERSE_BUILD_EDITOR OR AIVERSE_BUILD_TOOLS)
    # Editor core: project/map documents and the operation registry shared by the editor and aiversemcp.
    add_library(aiverse_editor_core STATIC apps/editor/core/MapDoc.cpp apps/editor/core/MapDocPlanet.cpp apps/editor/core/Workspace.cpp apps/editor/core/Net.cpp apps/editor/core/McpInstall.cpp apps/editor/core/Projects.cpp
    apps/editor/core/DataOps.cpp apps/editor/core/BalanceOps.cpp
    apps/editor/core/SimTools.cpp apps/editor/core/ScenarioOps.cpp apps/editor/core/SchemaOps.cpp apps/editor/core/PruneOps.cpp apps/editor/core/CampaignOps.cpp apps/editor/core/StrategicOps.cpp
    apps/editor/core/ToolchainOps.cpp apps/editor/core/WorldInspect.cpp)   # scenario_run, rules_lint, rules_schema keys
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_game)
    target_link_libraries(aiverse_editor_core PUBLIC AIVERSE::lib_nereid_water)   # erosion zone ops (NereidErosion soils)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_i18n)   # editor messages (editorText)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_postfx_data)   # postfx_* ops (postfx agent)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/PrismaOps.cpp)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/LightBakeOps.cpp)   # lightbake_* ops (agent-lightbake)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/HumanCreatorOps.cpp)   # human_* ops (VIVID Human Creator)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_vivid_human)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_bake)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/NativeBuild.cpp apps/editor/core/NativeBuildOps.cpp)   # per-project editor build with the project's C++ modules
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_terrain_detail)
    target_include_directories(aiverse_editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    if(TARGET aiverse_battle_load)   # battle_net_simulate (agent-net-mobile)
        target_sources(aiverse_editor_core PRIVATE apps/editor/core/BattleNetOps.cpp)
        target_link_libraries(aiverse_editor_core PUBLIC aiverse_battle_load)
        target_compile_definitions(aiverse_editor_core PRIVATE AIVERSE_BATTLE_OPS=1)
    endif()
    if(WIN32)
        target_link_libraries(aiverse_editor_core PUBLIC ws2_32)
    endif()
endif()

if(AIVERSE_BUILD_EDITOR)
    # windows register themselves (AIVERSE_EDITOR_WINDOW): any .cpp in apps/editor/windows is part of the editor
    file(GLOB AIVERSE_EDITOR_WINDOWS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/apps/editor/windows/*.cpp)
    add_executable(aiverse_editor apps/editor/main.cpp apps/editor/Editor.cpp apps/editor/EditorShell.cpp apps/editor/EditorAgent.cpp apps/editor/EditorPlayOps.cpp apps/editor/EditorTheme.cpp apps/editor/EditorProjects.cpp apps/editor/EditorNativeBuild.cpp apps/editor/EditorGameWizard.cpp apps/editor/EditorMcpConnect.cpp apps/editor/EditorWorld.cpp apps/editor/DetailsPanel.cpp
                   ${AIVERSE_EDITOR_WINDOWS} $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_editor PRIVATE aiverse_runtime aiverse_game_view aiverse_editor_core)
    target_link_libraries(aiverse_editor PRIVATE aiverse_game_fx AIVERSE::InputFlow)
    target_compile_definitions(aiverse_editor PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(WIN32)
        target_sources(aiverse_editor PRIVATE apps/editor/branding/editor.rc)   # exe + window icon
    endif()
    if(APPLE)
        # per-project editor builds (apps/editor/core/NativeBuild.cpp) pass another id: the .aiverse default
        # handler (chosen by bundle id) stays the regular editor
        set(AIVERSE_EDITOR_BUNDLE_ID "com.aiverse.editor" CACHE STRING "Bundle identifier of AiverseEditor.app")
        set_target_properties(aiverse_editor PROPERTIES MACOSX_BUNDLE TRUE OUTPUT_NAME "AiverseEditor"
            MACOSX_BUNDLE_BUNDLE_NAME "AIVERSE Editor" MACOSX_BUNDLE_GUI_IDENTIFIER "${AIVERSE_EDITOR_BUNDLE_ID}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in")
        # application icon (CFBundleIconFile AppIcon; art: tools/branding/logo/make_logo.py)
        target_sources(aiverse_editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/apps/editor/branding/AppIcon.icns")
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/apps/editor/branding/AppIcon.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        # engine assets (UI font) inside the bundle, so the editor works when moved elsewhere
        add_custom_command(TARGET aiverse_editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${AIVERSE_ENGINE_ASSETS}" "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_editor>/Resources/content/engine"
            # editor fonts and icons (EditorTheme.cpp): content path engine/editor-fonts/
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts" "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_editor>/Resources/content/engine/editor-fonts")
    elseif(WIN32)
        set_target_properties(aiverse_editor PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Build-time audio compression (host tools and tests only; games just decode): IMA ADPCM and
# OGG Vorbis encoders (libvorbis, BSD), profiles from audio.json, used by aiversebuild per target.
# ---------------------------------------------------------------------------
if(AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS)
    add_library(aiverse_vorbisenc STATIC
        third_party/vorbis/src/ogg/bitwise.c third_party/vorbis/src/ogg/framing.c
        third_party/vorbis/src/vorbis/analysis.c third_party/vorbis/src/vorbis/bitrate.c third_party/vorbis/src/vorbis/block.c third_party/vorbis/src/vorbis/codebook.c third_party/vorbis/src/vorbis/envelope.c third_party/vorbis/src/vorbis/floor0.c third_party/vorbis/src/vorbis/floor1.c third_party/vorbis/src/vorbis/info.c third_party/vorbis/src/vorbis/lookup.c third_party/vorbis/src/vorbis/lpc.c third_party/vorbis/src/vorbis/lsp.c third_party/vorbis/src/vorbis/mapping0.c third_party/vorbis/src/vorbis/mdct.c third_party/vorbis/src/vorbis/psy.c third_party/vorbis/src/vorbis/registry.c third_party/vorbis/src/vorbis/res0.c third_party/vorbis/src/vorbis/sharedbook.c third_party/vorbis/src/vorbis/smallft.c third_party/vorbis/src/vorbis/synthesis.c third_party/vorbis/src/vorbis/vorbisenc.c third_party/vorbis/src/vorbis/window.c)
    target_include_directories(aiverse_vorbisenc PUBLIC third_party/vorbis/include PRIVATE third_party/vorbis/src/vorbis)
    target_compile_options(aiverse_vorbisenc PRIVATE $<IF:$<BOOL:${MSVC}>,/w,-w>)
    if(NOT MSVC)
        target_link_libraries(aiverse_vorbisenc PUBLIC m)
    endif()
    add_library(aiverse_audiopack STATIC tools/audiopack/AudioPack.cpp)
    target_link_libraries(aiverse_audiopack PUBLIC aiverse_audio aiverse_vorbisenc)
    add_executable(aiverse_audiopack_tool tools/audiopack/main.cpp)
    set_target_properties(aiverse_audiopack_tool PROPERTIES OUTPUT_NAME aiverse_audiopack)
    target_link_libraries(aiverse_audiopack_tool PRIVATE aiverse_audiopack)
endif()

if(AIVERSE_BUILD_TOOLS)
    add_executable(aiversebuild tools/aiversebuild/main.cpp tools/aiversebuild/FileRules.cpp tools/aiversebuild/Plugins.cpp)
    target_sources(aiversebuild PRIVATE tools/aiversebuild/TextureCompress.cpp)   # agent-unity-materials: ASTC packaging
    target_sources(aiversebuild PRIVATE tools/aiversebuild/Toolchains.cpp tools/aiversebuild/ToolchainInstall.cpp)   # toolchains agent
    target_link_libraries(aiversebuild PRIVATE aiverse_engine aiverse_pack_writer aiverse_audiopack aiverse_plugins)
    add_executable(assetpipe tools/assetpipe/main.cpp)
    target_link_libraries(assetpipe PRIVATE aiverse_pack_writer)
    target_compile_definitions(assetpipe PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(aiversebuild PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    # Building just the editor must also produce its packager. Native editors may disable tools.
    if(TARGET aiverse_editor)
        add_dependencies(aiverse_editor aiversebuild)
    endif()
    # MCP server for AI agents: drives the running editor live, or edits project files directly.
    add_executable(aiversemcp tools/mcp/main.cpp)
    target_link_libraries(aiversemcp PRIVATE aiverse_editor_core)
    target_compile_definitions(aiversemcp PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Model converter (models agent): formats the runtime does not read -> glTF sidecars (assimp, BSD-3).
# Host only. AIVERSE_ASSIMP=ON uses an installed assimp (find_package) or downloads v5.4.3 from GitHub
# at configure time (AIVERSE_FETCH_ASSIMP); OFF keeps the tools working with pre-converted sidecars.
if(AIVERSE_BUILD_TOOLS)
    option(AIVERSE_ASSIMP "Model converter (modelconv, aiversebuild) built with assimp" OFF)
    option(AIVERSE_FETCH_ASSIMP "Download assimp when AIVERSE_ASSIMP=ON and no installed one is found" ON)
    add_library(aiverse_modelconv STATIC tools/models/ModelConvert.cpp)
    target_link_libraries(aiverse_modelconv PUBLIC aiverse_pack_writer)
    if(AIVERSE_ASSIMP)
        find_package(assimp CONFIG QUIET)
        if(NOT assimp_FOUND AND AIVERSE_FETCH_ASSIMP)
            include(FetchContent)
            set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
            set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
            set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
            set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
            set(ASSIMP_BUILD_GLTF_EXPORTER ON CACHE BOOL "" FORCE)
            set(ASSIMP_BUILD_USD_IMPORTER OFF CACHE BOOL "" FORCE)
            set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
            set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
            set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(assimp GIT_REPOSITORY https://github.com/assimp/assimp.git GIT_TAG v5.4.3 GIT_SHALLOW TRUE)
            FetchContent_MakeAvailable(assimp)
        endif()
        if(TARGET assimp::assimp)
            target_link_libraries(aiverse_modelconv PRIVATE assimp::assimp)
        elseif(TARGET assimp)
            target_link_libraries(aiverse_modelconv PRIVATE assimp)
        else()
            message(FATAL_ERROR "AIVERSE_ASSIMP=ON: assimp not found (install it or allow AIVERSE_FETCH_ASSIMP)")
        endif()
        target_compile_definitions(aiverse_modelconv PRIVATE AIVERSE_HAVE_ASSIMP=1)
    endif()
    add_executable(modelconv tools/models/modelconv.cpp)
    target_link_libraries(modelconv PRIVATE aiverse_modelconv aiverse_models)
    target_link_libraries(aiversebuild PRIVATE aiverse_modelconv)
endif()

# Model viewer (models agent): models, materials, clips and quality tiers as the game renders them.
if(AIVERSE_BUILD_TOOLS AND AIVERSE_DESKTOP)
    add_executable(aiverse_modelview
        apps/modelview/main.cpp apps/modelview/Gallery.cpp
        apps/modelview/NativeScenePreview.cpp
        $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_modelview PRIVATE aiverse_runtime aiverse_game_view AIVERSE::View AIVERSE::libraries)
endif()

# Catalog preview (docs/packages.md § Web viewer): materials / models of catalog packages through
# Renderer3D. Web: the website's catalog/viewer page (own shell, files fetched by the page into /view);
# desktop: the same viewer for checks and screenshots (--src <view folder>).
option(AIVERSE_BUILD_CATALOGVIEW "Build the package catalog preview (web page / desktop)" OFF)
if(AIVERSE_BUILD_CATALOGVIEW AND (AIVERSE_DESKTOP OR EMSCRIPTEN))
    add_executable(aiverse_catalogview apps/catalogview/main.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_catalogview PRIVATE aiverse_runtime AIVERSE::View)
    if(EMSCRIPTEN)
        set_target_properties(aiverse_catalogview PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
        target_link_options(aiverse_catalogview PRIVATE
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/apps/catalogview/shell.html"
            "-sFORCE_FILESYSTEM=1"
            "-sEXPORTED_FUNCTIONS=['_main','_cv_select','_cv_shape','_cv_lod','_cv_wire','_cv_clusters','_cv_zoom']"
            "-sEXPORTED_RUNTIME_METHODS=['FS','ccall','UTF8ToString']")
        set_property(TARGET aiverse_catalogview APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/apps/catalogview/shell.html")
    endif()
endif()

if(AIVERSE_BUILD_TOOLS)
    add_executable(aiverse_i18n_tool tools/i18n/main.cpp)
    set_target_properties(aiverse_i18n_tool PROPERTIES OUTPUT_NAME aiverse_i18n)
    target_link_libraries(aiverse_i18n_tool PRIVATE aiverse_i18n)
    target_compile_definitions(aiverse_i18n_tool PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(AIVERSE_BUILD_HEADLESS)
    add_executable(aiverse_headless apps/headless/main.cpp)
    target_link_libraries(aiverse_headless PRIVATE aiverse_game)
endif()

if(AIVERSE_BUILD_SERVER)
    add_executable(aiverse_server apps/server/main.cpp)
    target_link_libraries(aiverse_server PRIVATE aiverse_net_game)
    add_executable(aiverse_netbot apps/netbot/main.cpp)
    target_link_libraries(aiverse_netbot PRIVATE aiverse_net_game aiverse_i18n)
endif()
# Hosted battles (agent-net-mobile): battle tool (the load harness library is defined next to aiverse_net_server).
# The persistent city server (docs/city-server.md): one authoritative simulation, clients get what is near them.
add_library(aiverse_net_city STATIC engine/net/city/CityNet.cpp engine/net/city/CityMirror.cpp)
target_link_libraries(aiverse_net_city PUBLIC aiverse_net aiverse_game)
target_include_directories(aiverse_net_city PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(aiverse_net_city_meta STATIC engine/net/city/CityMeta.cpp)   # accounts in the meta service (module "world")
target_link_libraries(aiverse_net_city_meta PUBLIC aiverse_net_city aiverse::meta AIVERSE::meta_world)
if(AIVERSE_BUILD_SERVER)
    add_executable(aiverse_city apps/city/main.cpp)
    target_link_libraries(aiverse_city PRIVATE aiverse_net_city aiverse_net_city_meta)
endif()

if(AIVERSE_BUILD_SERVER AND TARGET aiverse_battle_load)
    add_executable(aiverse_battle apps/battle/main.cpp)
    target_link_libraries(aiverse_battle PRIVATE aiverse_battle_load aiverse_battle_rts)
endif()

if(AIVERSE_BUILD_TESTS)
    enable_testing()
    add_executable(aiverse_net_tests tests/net_tests.cpp)
    target_link_libraries(aiverse_net_tests PRIVATE aiverse_net_game aiverse_i18n)
    target_compile_definitions(aiverse_net_tests PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME aiverse_net_tests COMMAND aiverse_net_tests)
    if(TARGET aiverse_battle_load)
        add_executable(aiverse_battle_tests tests/net_mobile/battle_tests.cpp)
        target_link_libraries(aiverse_battle_tests PRIVATE aiverse_battle_load aiverse_battle_rts)
        target_compile_definitions(aiverse_battle_tests PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME aiverse_battle_tests COMMAND aiverse_battle_tests)
    endif()
    # Remote backend hooks: MessagePack / encodings, request-response HTTP, plain HTTP server, external
    # matches (Server::reserve, tickets, spectators, ExternalMatchEnd), server extensions.
    add_executable(aiverse_remote_tests tests/remote_tests.cpp)
    target_link_libraries(aiverse_remote_tests PRIVATE aiverse_meta_codec aiverse_net_game aiverse_pack)
    target_compile_definitions(aiverse_remote_tests PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME aiverse_remote_tests COMMAND aiverse_remote_tests)
    add_executable(aiverse_tests tests/tests.cpp)
    target_link_libraries(aiverse_tests PRIVATE aiverse_game aiverse_i18n aiverse_pack_writer aiverse_models)
    target_link_libraries(aiverse_tests PRIVATE aiverse_postfx_data)   # postfx agent
    target_link_libraries(aiverse_tests PRIVATE aiverse_game aiverse_game_audio aiverse_audiopack)
    target_link_libraries(aiverse_tests PRIVATE aiverse_game_fx)
    target_link_libraries(aiverse_tests PRIVATE aiverse_net_city aiverse_net_city_meta)   # the city server (agent-gta2)
    target_link_libraries(aiverse_tests PRIVATE aiverse_terrain_detail)   # terrain detail surface tests (agent-terrain)
    target_link_libraries(aiverse_tests PRIVATE aiverse_game_view)   # fog of war look parsing (view, no GPU)
    # view.entity_fx draw tests: the 3D renderer against a recording gfx::Device (no GPU; platform stubs in tests.cpp)
    target_sources(aiverse_tests PRIVATE engine/render/Renderer3D.cpp engine/render/Renderer3DModels.cpp engine/render/Lights.cpp
        engine/render/Renderer3DReflect.cpp engine/render/Renderer3DTrace.cpp engine/render/Prisma.cpp engine/render/PhysicalSky.cpp engine/render/LocalFog.cpp engine/render/GeometryScene.cpp)
    target_sources(aiverse_tests PRIVATE engine/render/PrismaScreenLighting.cpp)   # LENS per-camera PostFx pool test
    target_sources(aiverse_tests PRIVATE engine/render/BakedLighting.cpp engine/render/Renderer3DBaked.cpp)   # agent-lightbake
    target_sources(aiverse_tests PRIVATE engine/ui/UiLua.cpp)    # UI Lua sandbox (no RmlUi)
    target_link_libraries(aiverse_tests PRIVATE aiverse_lua)
    target_link_libraries(aiverse_tests PRIVATE aiverse_plugins)     # native plugins bridge, store/ads services
    target_sources(aiverse_tests PRIVATE tools/aiversebuild/Toolchains.cpp)   # toolchain detection (fake HOME)
    target_sources(aiverse_tests PRIVATE tools/aiversebuild/Plugins.cpp)   # plugin step: Info.plist / SKAdNetwork / Podfile (agent-sdk-plugins)
    if(TARGET aiverse_modelconv)   # model conversion step of packs (models agent)
        target_link_libraries(aiverse_tests PRIVATE aiverse_modelconv)
        target_compile_definitions(aiverse_tests PRIVATE AIVERSE_TEST_MODELCONV=1)
    endif()
    target_link_libraries(aiverse_tests PRIVATE aiverse_plugins)     # native plugins bridge, store/ads services
    if(TARGET aiverse_editor_core)   # planet map documents (MapDoc world block)
        target_link_libraries(aiverse_tests PRIVATE aiverse_editor_core)
        target_compile_definitions(aiverse_tests PRIVATE AIVERSE_TESTS_EDITOR_CORE=1)
    endif()
    target_compile_definitions(aiverse_tests PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    # aiverse_models compiles out FBX privately when AIVERSE_FBX=OFF.
    # Expose the same selection to its test runner, so a disabled loader
    # checks its rejection path instead of trying to parse cube.fbx.
    if(NOT AIVERSE_FBX)
        target_compile_definitions(aiverse_tests PRIVATE AIVERSE_NO_FBX=1)
    endif()
    add_test(NAME aiverse_tests COMMAND aiverse_tests)
    # AIVERSE LENS: ECS camera entities, authoring state and read-only render snapshots.
    add_executable(aiverse_ecs_camera_tests tests/render/ecs_camera.cpp)
    target_include_directories(aiverse_ecs_camera_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ecs_camera COMMAND aiverse_ecs_camera_tests)
    add_executable(aiverse_camera_scheduler_tests tests/render/camera_scheduler.cpp)
    target_include_directories(aiverse_camera_scheduler_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_scheduler COMMAND aiverse_camera_scheduler_tests)
    add_executable(aiverse_camera_rt_pool_tests tests/render/camera_rt_pool.cpp)
    target_include_directories(aiverse_camera_rt_pool_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_rt_pool COMMAND aiverse_camera_rt_pool_tests)
    add_executable(aiverse_camera_rig_tests tests/render/camera_rig.cpp)
    target_include_directories(aiverse_camera_rig_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_rig COMMAND aiverse_camera_rig_tests)
    add_executable(aiverse_camera_behavior_tests tests/render/camera_behavior.cpp)
    target_include_directories(aiverse_camera_behavior_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_camera_behavior_tests PRIVATE aiverse_engine)
    add_test(NAME camera_behavior COMMAND aiverse_camera_behavior_tests)
    add_executable(aiverse_camera_rail_path_tests tests/render/camera_rail_path.cpp)
    target_include_directories(aiverse_camera_rail_path_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_rail_path COMMAND aiverse_camera_rail_path_tests)
    add_executable(aiverse_camera_exr_tests tests/render/camera_exr.cpp)
    target_include_directories(aiverse_camera_exr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_exr COMMAND aiverse_camera_exr_tests)
    add_executable(aiverse_camera_temporal_tests tests/render/camera_temporal.cpp)
    target_include_directories(aiverse_camera_temporal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_temporal COMMAND aiverse_camera_temporal_tests)
    add_executable(aiverse_camera_layout_tests tests/render/camera_layout.cpp)
    target_include_directories(aiverse_camera_layout_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_layout COMMAND aiverse_camera_layout_tests)
    add_executable(aiverse_camera_capture_views_tests tests/render/camera_capture_views.cpp)
    target_include_directories(aiverse_camera_capture_views_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_capture_views COMMAND aiverse_camera_capture_views_tests)
    add_executable(aiverse_camera_scene_asset_tests tests/render/camera_scene_asset.cpp)
    target_include_directories(aiverse_camera_scene_asset_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_camera_scene_asset_tests PRIVATE aiverse_engine)
    add_test(NAME camera_scene_asset COMMAND aiverse_camera_scene_asset_tests)
    add_executable(aiverse_camera_lens_physics_tests tests/render/camera_lens_physics.cpp)
    target_include_directories(aiverse_camera_lens_physics_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_camera_lens_physics_tests PRIVATE aiverse_engine)
    add_test(NAME camera_lens_physics COMMAND aiverse_camera_lens_physics_tests)
    add_executable(aiverse_camera_capture_graph_tests tests/render/camera_capture_graph.cpp)
    target_include_directories(aiverse_camera_capture_graph_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_capture_graph COMMAND aiverse_camera_capture_graph_tests)
    add_executable(aiverse_camera_oblique_tests tests/render/camera_oblique.cpp)
    target_include_directories(aiverse_camera_oblique_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_oblique COMMAND aiverse_camera_oblique_tests)
    add_executable(aiverse_camera_png_tests tests/render/camera_png.cpp)
    target_include_directories(aiverse_camera_png_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_png COMMAND aiverse_camera_png_tests)
    add_executable(aiverse_camera_frustum_tests tests/render/camera_frustum.cpp)
    target_include_directories(aiverse_camera_frustum_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_frustum COMMAND aiverse_camera_frustum_tests)
    add_executable(aiverse_camera_hdr_resolve_tests tests/render/camera_hdr_resolve.cpp)
    target_include_directories(aiverse_camera_hdr_resolve_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME camera_hdr_resolve COMMAND aiverse_camera_hdr_resolve_tests)
    add_executable(aiverse_camera_client_adapter_tests tests/render/camera_client_adapter.cpp)
    target_include_directories(aiverse_camera_client_adapter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_camera_client_adapter_tests PRIVATE aiverse_runtime aiverse_content_reader)
    add_test(NAME camera_client_adapter COMMAND aiverse_camera_client_adapter_tests)
    add_executable(aiverse_camera_model_snapshot_tests tests/render/camera_model_snapshot.cpp)
    aiverse_keep_test_asserts(aiverse_camera_model_snapshot_tests)
    target_include_directories(aiverse_camera_model_snapshot_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_camera_model_snapshot_tests PRIVATE aiverse_engine AIVERSE::view aiverse_zstd_dec)
    add_test(NAME camera_model_snapshot COMMAND aiverse_camera_model_snapshot_tests)
    add_executable(aiverse_camera_terrain_prepared_api_tests tests/render/camera_terrain_prepared_api.cpp)
    target_include_directories(aiverse_camera_terrain_prepared_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_camera_terrain_prepared_api_tests PRIVATE aiverse_engine AIVERSE::view)
    add_test(NAME camera_terrain_prepared_api COMMAND aiverse_camera_terrain_prepared_api_tests)
    add_executable(aiverse_camera_sprite_vfx_prepared_api_tests tests/render/camera_sprite_vfx_prepared_api.cpp)
    target_include_directories(aiverse_camera_sprite_vfx_prepared_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_camera_sprite_vfx_prepared_api_tests PRIVATE aiverse_engine AIVERSE::view)
    add_test(NAME camera_sprite_vfx_prepared_api COMMAND aiverse_camera_sprite_vfx_prepared_api_tests)
    if(TARGET aiverse_editor_core)
        add_executable(aiverse_editor_scene_adapter_tests tests/editor/scene_entity_adapter_tests.cpp)
        target_link_libraries(aiverse_editor_scene_adapter_tests PRIVATE aiverse_editor_core)
        add_test(NAME editor_scene_adapter COMMAND aiverse_editor_scene_adapter_tests)
        add_executable(aiverse_editor_scene_picking_tests tests/editor/scene_picking_tests.cpp)
        target_link_libraries(aiverse_editor_scene_picking_tests PRIVATE aiverse_editor_core)
        add_test(NAME editor_scene_picking COMMAND aiverse_editor_scene_picking_tests)
        add_executable(aiverse_editor_scene_gizmo_tests tests/editor/scene_gizmo_tests.cpp)
        target_link_libraries(aiverse_editor_scene_gizmo_tests PRIVATE aiverse_editor_core)
        add_test(NAME editor_scene_gizmo COMMAND aiverse_editor_scene_gizmo_tests)
        add_executable(aiverse_editor_oriented_collider_tests tests/editor/scene_oriented_collider_tests.cpp)
        target_link_libraries(aiverse_editor_oriented_collider_tests PRIVATE aiverse_editor_core)
        add_test(NAME editor_oriented_collider COMMAND aiverse_editor_oriented_collider_tests)
        add_executable(aiverse_editor_scene_rotation_tests tests/editor/scene_rotation_tests.cpp)
        target_link_libraries(aiverse_editor_scene_rotation_tests PRIVATE aiverse_editor_core)
        add_test(NAME editor_scene_rotation COMMAND aiverse_editor_scene_rotation_tests)
        # Review fixes 2026-11-21: agent-link handshake/token/line-cap and Lua inspector sandbox.
        add_executable(aiverse_editor_agent_link_tests tests/editor/agent_link_tests.cpp)
        target_link_libraries(aiverse_editor_agent_link_tests PRIVATE aiverse_editor_core)
        add_test(NAME editor_agent_link COMMAND aiverse_editor_agent_link_tests)
        add_executable(aiverse_editor_lua_inspector_sandbox_tests tests/editor/lua_inspector_sandbox_tests.cpp)
        target_link_libraries(aiverse_editor_lua_inspector_sandbox_tests PRIVATE aiverse_editor_core aiverse_lua aiverse_imgui)
        add_test(NAME editor_lua_inspector_sandbox COMMAND aiverse_editor_lua_inspector_sandbox_tests)
    endif()
    add_executable(aiverse_build_tool_tests tests/editor/build_tool.cpp)
    target_include_directories(aiverse_build_tool_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME editor_build_tool COMMAND aiverse_build_tool_tests "${CMAKE_CURRENT_BINARY_DIR}/build-tool-fixture")
endif()

# ---------------------------------------------------------------------------
# In-game UI (UI agent): RmlUi (HTML/CSS) rendered through the RHI, fonts via stb_truetype.
# Documents live in engine/assets/ui; a game overrides any of them with <game>/ui/<same name>.
# ---------------------------------------------------------------------------
file(GLOB AIVERSE_RMLUI_SRC CONFIGURE_DEPENDS
    third_party/rmlui/Source/Core/*.cpp third_party/rmlui/Source/Core/Elements/*.cpp
    third_party/rmlui/Source/Core/Layout/*.cpp third_party/rmlui/Source/Debugger/*.cpp)
add_library(aiverse_rmlui STATIC ${AIVERSE_RMLUI_SRC})
target_include_directories(aiverse_rmlui PUBLIC third_party/rmlui/Include)
target_compile_definitions(aiverse_rmlui PUBLIC RMLUI_STATIC_LIB PRIVATE RMLUI_VERSION="6.1")
if(NOT MSVC)
    target_compile_options(aiverse_rmlui PRIVATE -w)
endif()

# Lua 5.4: target aiverse_lua is defined by the scripting block below (deterministic build, no os/io libs).
# RmlUi's Lua plugin (<script>, onclick="...", rmlui.* in documents)
file(GLOB AIVERSE_RMLUI_LUA_SRC CONFIGURE_DEPENDS third_party/rmlui/Source/Lua/*.cpp third_party/rmlui/Source/Lua/Elements/*.cpp)
add_library(aiverse_rmlui_lua STATIC ${AIVERSE_RMLUI_LUA_SRC})
target_link_libraries(aiverse_rmlui_lua PUBLIC aiverse_rmlui aiverse_lua)
if(NOT MSVC)
    target_compile_options(aiverse_rmlui_lua PRIVATE -w)
endif()

add_library(aiverse_ui STATIC engine/ui/UiLayer.cpp engine/ui/UiRender.cpp engine/ui/UiFont.cpp engine/ui/UiStb.cpp engine/ui/UiKeyboard.cpp engine/ui/UiLua.cpp
    engine/ui/UiLinear.cpp engine/ui/UiModelView.cpp)   # linear UI blending (ui-linear)
target_include_directories(aiverse_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE third_party/stb)
target_link_libraries(aiverse_ui PUBLIC aiverse_rmlui aiverse_rmlui_lua aiverse_lua aiverse_engine)
option(AIVERSE_CLIENT_WITH_FX "Include SpectraFX particles in packaged clients" ON)
foreach(t ${AIVERSE_GAME_CLIENT_TARGETS})
    if(TARGET ${t})
        target_sources(${t} PRIVATE apps/client/ClientUi.cpp apps/client/ClientActions.cpp apps/client/ClientScript.cpp apps/client/ClientScriptCmd.cpp apps/client/ClientScriptUi.cpp apps/client/Minimap.cpp apps/client/Lobby.cpp apps/client/ClientPlanet.cpp apps/client/MinimapPlanet.cpp)
        target_link_libraries(${t} PRIVATE aiverse_ui aiverse_net)
        if(AIVERSE_CLIENT_WITH_FX)
            target_sources(${t} PRIVATE apps/client/ClientFx.cpp)
            target_link_libraries(${t} PRIVATE aiverse_game_fx)
        else()
            target_compile_definitions(${t} PRIVATE AIVERSE_CLIENT_NO_FX=1)
        endif()
    endif()
endforeach()

# ---------------------------------------------------------------------------
# 2D games (2D-mode agent): tile maps + autotiling (deterministic, part of the game library),
# sprite batch + textures (RHI only), sprite/tile presentation in the RTS view.
# ---------------------------------------------------------------------------
target_sources(aiverse_game PRIVATE engine/game/Tiles.cpp)
# terrain / map generator (agent-terrain-gen): integer noise, erosion, biomes, fair starts, presets, planets
aiverse_game_module(AIVERSE_GAME_WITH_MAPGEN "Include the map generator (map \"generate\" blocks)"
    engine/game/disabled/MapGen.cpp
    engine/game/gen/GenNoise.cpp engine/game/gen/GenField.cpp engine/game/gen/GenTerrain.cpp
    engine/game/gen/GenContent.cpp engine/game/gen/GenCity.cpp engine/game/gen/GenPlanet.cpp engine/game/gen/GenWorld.cpp engine/game/gen/MapGen.cpp)
add_library(aiverse_render2d STATIC engine/render/SpriteBatch.cpp engine/render/TextureCache.cpp)
target_include_directories(aiverse_render2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE third_party/stb)
target_sources(aiverse_game_view PRIVATE engine/view/game/GameSprites.cpp engine/view/game/GameTileView.cpp engine/view/game/GameSpriteShadow.cpp)
target_link_libraries(aiverse_game_view PUBLIC aiverse_render2d)
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/TileOps.cpp)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/TerrainOps.cpp apps/editor/core/TerrainDetailOps.cpp)   # terrain agent: sculpt, materials, heightmaps
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/GenOps.cpp)       # terrain-gen agent: map_generate, gen_presets
endif()

# ---------------------------------------------------------------------------
# Texture atlases (atlas agent). aiverse_atlas: runtime manifest/page reader, CPU image source and
# the GPU texture lookup (TextureLibrary, through gfx::Device only). aiverse_atlas_builder: host-only
# packer used by aiversebuild/assetpipe at build time.
# ---------------------------------------------------------------------------
add_library(aiverse_atlas STATIC engine/atlas/AtlasFormat.cpp engine/atlas/ImageDecode.cpp engine/atlas/TextureLibrary.cpp)
target_include_directories(aiverse_atlas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE third_party/stb)
target_link_libraries(aiverse_atlas PUBLIC aiverse_engine)
foreach(t ${AIVERSE_GAME_CLIENT_TARGETS})
    if(TARGET ${t})
        target_link_libraries(${t} PRIVATE aiverse_atlas)
    endif()
endforeach()
if(TARGET aiverse_render2d)
    target_link_libraries(aiverse_render2d PUBLIC aiverse_atlas)  # TextureCache resolves atlased sprites
endif()
if(AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS)
    add_library(aiverse_atlas_builder STATIC tools/atlas/AtlasPacker.cpp tools/atlas/AtlasMesh.cpp tools/atlas/AtlasBuild.cpp)
    target_include_directories(aiverse_atlas_builder PRIVATE third_party/stb_image_write)
    target_link_libraries(aiverse_atlas_builder PUBLIC aiverse_atlas aiverse_pack_writer)
    foreach(t aiversebuild assetpipe aiverse_tests)
        if(TARGET ${t})
            target_link_libraries(${t} PRIVATE aiverse_atlas_builder)
        endif()
    endforeach()
endif()
if(TARGET aiverse_editor_core) # lighting agent: light_* / lighting_set operations
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/LightOps.cpp apps/editor/core/PhysisOps.cpp)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/PlanetOps.cpp apps/editor/core/PlanetDetailOps.cpp)   # planet-render agent: planet sculpt / paint
endif()
if(TARGET aiverse_tests) # lighting agent: GPU-free lighting logic under test
    target_sources(aiverse_tests PRIVATE engine/view/game/GameLightsData.cpp engine/render/QualityGovernor.cpp)
endif()

# ---------------------------------------------------------------------------
# Overlays (overlays agent): data-driven helper elements (build grids, ranges, rally lines, damage
# numbers, bars). aiverse_overlay_data: GPU-free schema / validation / templates, shared by the view and
# the editor core (MCP overlay_* operations); the evaluator + renderer live in aiverse_game_view.
# ---------------------------------------------------------------------------
add_library(aiverse_overlay_data STATIC engine/view/game/GameOverlayData.cpp)
target_link_libraries(aiverse_overlay_data PUBLIC aiverse_game)
target_include_directories(aiverse_overlay_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(aiverse_game_view PRIVATE engine/view/game/GameOverlays.cpp engine/view/game/GameOverlaysDraw.cpp)
target_link_libraries(aiverse_game_view PUBLIC aiverse_overlay_data)
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/OverlayOps.cpp)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_overlay_data)
endif()
# grid fields in the editor (field_* MCP ops, Scene preview)
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/FieldOps.cpp)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/RaidOps.cpp)   # extraction raids (agent-extraction)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/RouteOps.cpp)   # Bunker Wars routes / screen
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/RoadOps.cpp)    # spline roads (docs/roads.md)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/WaterOps.cpp)   # water from splines (docs/water.md)
endif()

# VFX tools (vfx tools agent): Unity particle importer tests (Python, stdlib only) and the fx_* editor
# operations (list / get / set / delete / templates / import_unity / bind; fx_preview in the editor GUI).
# ---------------------------------------------------------------------------
if(AIVERSE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME unity_vfx_importer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/unity_vfx/test_import.py)
        if(TARGET aiverse_clipinfo)   # host tool: not built for Android / iOS / web
            add_test(NAME unity_anim_importer COMMAND ${CMAKE_COMMAND} -E env AIVERSE_CLIPINFO=$<TARGET_FILE:aiverse_clipinfo>
                     ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/unity_anim/test_unity_anim.py)
        endif()
    endif()
endif()
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/FxOps.cpp apps/editor/core/FxTemplates.cpp)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_game_fx)
endif()
if(TARGET aiverse_editor)
    target_sources(aiverse_editor PRIVATE apps/editor/EditorFxOps.cpp)   # fx_preview (GUI)
endif()

# Splash & icons (splash agent). aiverse_splash: game.json "splash" timeline + the in-engine loading
# screen (RHI quads, GLSL + MSL) driven by apps/client/ClientSplash.cpp. aiverse_branding: host-only
# icon / launch screen generation (resize, .ico, .icns, Android/iOS/web/Linux sets, previews) used
# by aiversebuild, the editor core (branding_* / icons_preview / splash_preview) and the tests.
# ---------------------------------------------------------------------------
add_library(aiverse_splash STATIC engine/splash/SplashData.cpp engine/splash/SplashScreen.cpp engine/splash/SplashBoot.cpp)
target_include_directories(aiverse_splash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aiverse_splash PUBLIC aiverse_engine aiverse_atlas)
if(AIVERSE_BUILD_TESTS)   # game.json "splash" choices: engine screen, changed, own, none, map loading
    add_executable(aiverse_splash_choice_tests tests/splash/choice.cpp)
    target_link_libraries(aiverse_splash_choice_tests PRIVATE aiverse_splash)
    add_test(NAME splash_choice COMMAND aiverse_splash_choice_tests)
endif()
# aiversebuild: window colour before the first frame (0xRRGGBB from game.json splash.background)
set(AIVERSE_SPLASH_BACKGROUND "" CACHE STRING "Window background before the first frame, 0xRRGGBB (set by aiversebuild)")
foreach(t ${AIVERSE_GAME_CLIENT_TARGETS})
    if(TARGET ${t})
        target_sources(${t} PRIVATE apps/client/ClientSplash.cpp apps/client/ClientLoading.cpp)
        target_link_libraries(${t} PRIVATE aiverse_splash)
        if(AIVERSE_SPLASH_BACKGROUND)
            target_compile_definitions(${t} PRIVATE AIVERSE_SPLASH_BACKGROUND=${AIVERSE_SPLASH_BACKGROUND})
        endif()
    endif()
endforeach()
# Generated packaging assets (aiversebuild --target ...): Windows icon resource, iOS launch storyboard.
if(TARGET aiverse_client AND AIVERSE_PLATFORM STREQUAL "windows" AND AIVERSE_GEN_DIR AND EXISTS "${AIVERSE_GEN_DIR}/app.rc")
    enable_language(RC)
    target_sources(aiverse_client PRIVATE "${AIVERSE_GEN_DIR}/app.rc")
endif()
set(AIVERSE_IOS_LAUNCH_STORYBOARD "")   # Info.plist: launch storyboard key when aiversebuild generated one
if(TARGET aiverse_client AND AIVERSE_PLATFORM STREQUAL "ios" AND AIVERSE_GEN_DIR AND EXISTS "${AIVERSE_GEN_DIR}/LaunchScreen.storyboard")
    target_sources(aiverse_client PRIVATE "${AIVERSE_GEN_DIR}/LaunchScreen.storyboard")
    set_property(TARGET aiverse_client APPEND PROPERTY RESOURCE "${AIVERSE_GEN_DIR}/LaunchScreen.storyboard")
    set(AIVERSE_IOS_LAUNCH_STORYBOARD "<key>UILaunchStoryboardName</key><string>LaunchScreen</string>")
endif()
if(AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS)
    add_library(aiverse_branding STATIC tools/branding/BrandImage.cpp tools/branding/Icons.cpp tools/branding/Splash.cpp)
    target_include_directories(aiverse_branding PRIVATE third_party/stb_image_write)
    target_link_libraries(aiverse_branding PUBLIC aiverse_splash)
    foreach(t aiversebuild aiverse_tests)
        if(TARGET ${t})
            target_link_libraries(${t} PRIVATE aiverse_branding)
        endif()
    endforeach()
    if(TARGET aiversebuild)
        target_sources(aiversebuild PRIVATE tools/aiversebuild/Branding.cpp)
    endif()
endif()
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/BrandingOps.cpp)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_branding)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/UiOps.cpp)   # UI agent: game screens + ui_preview
    # game wizard ("Сделать игру"): a whole project from art + idea (docs/game-wizard.md)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/GameWizard.cpp apps/editor/core/GameWizardContent.cpp
        apps/editor/core/GameWizardTutorial.cpp apps/editor/core/GameWizardMeta.cpp apps/editor/core/WizardOps.cpp
        apps/editor/core/WizardAgent.cpp)
    if(TARGET aiverse_editor)   # dictation for the wizard's text fields (EditorSpeech.h)
        if(APPLE)
            target_sources(aiverse_editor PRIVATE apps/editor/EditorSpeech_mac.mm)
            set_source_files_properties(apps/editor/EditorSpeech_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
            target_link_libraries(aiverse_editor PRIVATE "-framework Speech" "-framework AVFoundation")
        else()
            target_sources(aiverse_editor PRIVATE apps/editor/EditorSpeech.cpp)
        endif()
    endif()
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_lua)
endif()

# Material library and procedural textures (models agent): material_* / texture_* editor operations
# (materials.json, rules view "materials" / "assign"); the Materials window previews them.
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/AnimationOps.cpp apps/editor/core/AnimationDocument.cpp apps/editor/core/MaterialOps.cpp apps/editor/core/ShaderGraphOps.cpp apps/editor/core/GeometryOps.cpp)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_models)
endif()
if(TARGET aiversemcp)   # KTX2 zstd in aiverse_models: aiversemcp links no pack writer, so it takes the decoder
    target_link_libraries(aiversemcp PRIVATE aiverse_models aiverse_zstd_dec)
endif()

# ---------------------------------------------------------------------------
# Scripting (scripting agent): Lua 5.4 (third_party/lua, MIT) compiled as C without the os / io /
# package / debug libraries (same build on web, iOS, Android, desktop). Simulation scripts are
# deterministic (fx numbers, float guard in aiverse_luaconf.h); docs/scripting.md.
# ---------------------------------------------------------------------------
set(AIVERSE_LUA_SRC lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c
    llex.c lmathlib.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c lstrlib.c ltable.c ltablib.c
    ltm.c lundump.c lutf8lib.c lvm.c lzio.c aiverse_lua.c)
list(TRANSFORM AIVERSE_LUA_SRC PREPEND third_party/lua/)
add_library(aiverse_lua STATIC ${AIVERSE_LUA_SRC})
target_include_directories(aiverse_lua PUBLIC third_party/lua)
set_target_properties(aiverse_lua PROPERTIES LINKER_LANGUAGE C)
if(MSVC)
    target_compile_options(aiverse_lua PRIVATE /W0)
else()
    target_compile_options(aiverse_lua PRIVATE -w)
endif()
# os library for the UI / meta-screen Lua (engine/ui/UiLua.cpp: clocks and dates only, the dangerous
# functions are removed there); simulation states never open it
target_sources(aiverse_lua PRIVATE third_party/lua/loslib.c)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(aiverse_lua PRIVATE LUA_USE_IOS)   # no system() on iOS
endif()
if(UNIX)
    target_compile_definitions(aiverse_lua PRIVATE LUA_USE_POSIX)  # os.tmpname via mkstemp (the linker warns about tmpnam)
endif()
target_sources(aiverse_game PRIVATE engine/game/ScriptTerritory.cpp)
target_sources(aiverse_game PRIVATE engine/game/ScriptGrid.cpp)
target_sources(aiverse_game PRIVATE engine/game/Script.cpp engine/game/ScriptEngine.cpp engine/game/ScriptApi.cpp engine/game/ScriptCommands.cpp
    engine/game/ScriptStd.cpp engine/game/ScriptIeee.cpp)
if(NOT MSVC)
    # one IEEE operation per binding: never contract into FMA (ScriptIeee.cpp)
    set_source_files_properties(engine/game/ScriptIeee.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
# grid fields (G5): data, kernels, Lua `field` table (docs/fields.md)
target_sources(aiverse_game PRIVATE engine/game/GridFields.cpp engine/game/GridFieldData.cpp engine/game/GridFieldsApi.cpp)
# extraction raids (agent-extraction): match loop, extraction points, raid inventory, interactables, receipts, bot, Lua `raid`
target_sources(aiverse_game PRIVATE engine/game/Raid.cpp engine/game/RaidData.cpp engine/game/RaidBot.cpp engine/game/RaidApi.cpp)
target_sources(aiverse_game PRIVATE engine/game/DrivingApi.cpp engine/game/ServicesApi.cpp engine/game/Prompts.cpp engine/game/PromptApi.cpp)
aiverse_game_module(AIVERSE_GAME_WITH_COMMERCE "Include city commerce (rules \"commerce\")"
    engine/game/disabled/Commerce.cpp engine/game/Commerce.cpp engine/game/CommerceApi.cpp)
aiverse_game_module(AIVERSE_GAME_WITH_CAREERS "Include career ladders (rules \"careers\")"
    engine/game/disabled/Career.cpp engine/game/Career.cpp engine/game/CareerApi.cpp)
target_sources(aiverse_game PRIVATE engine/game/Sensors.cpp engine/game/FieldOps.cpp)   # radar / sonar / jammers, field operations
# top-down heroes (agent-topdown-control): rules "topDown", stick input commands, aim / auto-fire (docs/topdown.md)
target_sources(aiverse_game PRIVATE engine/game/TopDown.cpp)
# in-match progression (agent-df-progression-glue): match-progression package on real units, rules "progression" (docs/progression.md)
target_sources(aiverse_game PRIVATE engine/game/Progression.cpp engine/game/ProgressionData.cpp engine/game/ProgressionApi.cpp)
# boss encounters (agent-df-encounters-glue): rules "encounters" over the boss-encounters library package (docs/encounters.md)
aiverse_game_module(AIVERSE_GAME_WITH_ENCOUNTERS "Include boss encounters (rules \"encounters\")"
    engine/game/disabled/Encounters.cpp engine/game/Encounters.cpp engine/game/EncountersApi.cpp)
# per-map overrides of rules blocks (map "rules": topDown tuning, raid, progression, encounters; docs/game-format.md)
target_sources(aiverse_game PRIVATE engine/game/MapRules.cpp)
# boss encounters (engine/mechanics) and planet seas (engine/world/nereid) are engine headers: no library link.
# map environment (weather, wind, zones, trample fields, auto skills on splines, fog options)
target_sources(aiverse_game PRIVATE engine/game/Environment.cpp engine/game/EnvironmentData.cpp)
# driving (agent-gta2): the top-down hero drives vehicles of the vehicle-controllers library (docs/driving.md)
target_sources(aiverse_game PRIVATE engine/game/Driving.cpp)
# Bunker Wars routes, groups, multiscreen (docs/game-format.md § Routes)
target_sources(aiverse_game PRIVATE engine/game/Routes.cpp engine/game/RoutesData.cpp engine/game/ScriptRoutes.cpp)
aiverse_game_module(AIVERSE_GAME_WITH_ARSENAL "Include hero weapons (rules \"arsenal\")"
    engine/game/disabled/Arsenal.cpp engine/game/Arsenal.cpp)
# traffic (agent-traffic): AI cars on lanes, signalised junctions, pedestrians (docs/traffic.md)
aiverse_game_module(AIVERSE_GAME_WITH_TRAFFIC "Include city traffic (rules \"traffic\") in the simulation"
    engine/game/disabled/Traffic.cpp
    engine/game/Traffic.cpp engine/game/TrafficCity.cpp engine/game/TrafficPopulation.cpp engine/game/TrafficCommute.cpp engine/game/TrafficGangs.cpp)
# spline roads (agent-roads): road types + library, map roadNetwork, shared geometry (docs/roads.md)
target_sources(aiverse_game PRIVATE engine/game/TrafficRoads.cpp)
target_link_libraries(aiverse_game PUBLIC aiverse_roads)
# water from splines: editor-time / presentation data (never read by the simulation; the editor bakes it into cells)
target_sources(aiverse_game PRIVATE engine/view/game/GameWater.cpp)
# the engine's standard Lua library (rule-level mechanics) is compiled in: engine/game/lua/*.lua
file(GLOB AIVERSE_STD_LUA CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/game/lua/*.lua)
set(AIVERSE_STD_LUA_H ${CMAKE_CURRENT_BINARY_DIR}/gen/StdLua.h)
add_custom_command(OUTPUT ${AIVERSE_STD_LUA_H}
    COMMAND ${CMAKE_COMMAND} -DOUT=${AIVERSE_STD_LUA_H} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/engine/game/lua -P ${CMAKE_CURRENT_SOURCE_DIR}/engine/game/lua/embed.cmake
    DEPENDS ${AIVERSE_STD_LUA} ${CMAKE_CURRENT_SOURCE_DIR}/engine/game/lua/embed.cmake VERBATIM)
target_sources(aiverse_game PRIVATE ${AIVERSE_STD_LUA_H})
target_include_directories(aiverse_game PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_link_libraries(aiverse_game PUBLIC aiverse_lua)
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/ScriptOps.cpp)
endif()
target_sources(aiverse_game_view PRIVATE engine/view/game/GameViewScript.cpp engine/view/game/ScriptSound.cpp)

# Generic GPU canvases and Lua world-space paint layers.
target_sources(aiverse_game_view PRIVATE engine/render/RenderTexture.cpp engine/view/game/GamePaintLayers.cpp engine/view/game/GameGroundCanvas.cpp)

# Shared region gesture input; gameplay remains in project Lua ability effects.
option(AIVERSE_CLIENT_WITH_CITY "Include persistent city server client in packaged clients" ON)
foreach(t ${AIVERSE_GAME_CLIENT_TARGETS})
    if(TARGET ${t})
        target_sources(${t} PRIVATE apps/client/ClientRegionInput.cpp)
        target_sources(${t} PRIVATE apps/client/ClientTopDown.cpp)   # top-down heroes (agent-topdown-control)
        target_sources(${t} PRIVATE apps/client/ClientCommerce.cpp)   # city economy (agent-gta2)
        if(AIVERSE_CLIENT_WITH_CITY)
            target_sources(${t} PRIVATE apps/client/ClientCity.cpp)   # a seat in a city on a server
            target_link_libraries(${t} PRIVATE aiverse_net_city)
        else()
            target_sources(${t} PRIVATE apps/client/ClientCityStub.cpp)
        endif()
    endif()
endforeach()

if(TARGET aiverse_editor)
    target_link_libraries(aiverse_editor PRIVATE aiverse_i18n)
endif()

# Game viewport uses the same RmlUi renderer and documents as the client.
if(TARGET aiverse_editor)
    target_link_libraries(aiverse_editor PRIVATE aiverse_ui)
    target_sources(aiverse_editor PRIVATE apps/editor/GameHud.cpp)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_ui_viewport_tests tests/ui_viewport.cpp tests/ui_font_effects.cpp)
    target_link_libraries(aiverse_ui_viewport_tests PRIVATE aiverse_ui)
    add_test(NAME aiverse_ui_viewport_tests COMMAND aiverse_ui_viewport_tests)
endif()

# ---------------------------------------------------------------------------
# Project files `<Title>.aiverse` (legacy `.aiverse`) and opening them from the OS (project-file agent): ProjectFile /
# FileAssoc in the editor core (Linux mime + .desktop, Windows HKCU registry, macOS LaunchServices),
# the editor's own Info.plist (document type com.aiverse.project) and the document icon drawn by
# aiverse_projicon (tools/branding/ProjectIcon.*).
# ---------------------------------------------------------------------------
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/ProjectFile.cpp apps/editor/core/FileAssoc.cpp)
    set_property(SOURCE apps/editor/core/ProjectFile.cpp APPEND PROPERTY COMPILE_DEFINITIONS "AIVERSE_ENGINE_VERSION=\"${PROJECT_VERSION}\"")
    if(WIN32)
        target_link_libraries(aiverse_editor_core PUBLIC advapi32 shell32)
    elseif(APPLE)
        target_link_libraries(aiverse_editor_core PUBLIC "-framework CoreServices")
    endif()
endif()
if(TARGET aiverse_branding)
    target_sources(aiverse_branding PRIVATE tools/branding/ProjectIcon.cpp)
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(aiverse_projicon tools/branding/projicon_main.cpp)
        target_link_libraries(aiverse_projicon PRIVATE aiverse_branding)
    endif()
endif()
if(TARGET aiverse_editor)
    target_sources(aiverse_editor PRIVATE apps/editor/EditorOpenFile.cpp)
    if(APPLE)
        set_target_properties(aiverse_editor PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/EditorInfo.plist.in"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}" MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
        if(TARGET aiverse_projicon)
            add_dependencies(aiverse_editor aiverse_projicon)
            # Resources/rtsproj.icns: the document icon in Finder, drawn from the editor logo
            add_custom_command(TARGET aiverse_editor POST_BUILD
                COMMAND $<TARGET_FILE:aiverse_projicon> "$<TARGET_BUNDLE_CONTENT_DIR:aiverse_editor>/Resources"
                        "${CMAKE_CURRENT_SOURCE_DIR}/apps/editor/branding/icon_1024.png"
                COMMENT ".aiverse document icon")
        endif()
    endif()
endif()

# Project-local C++ module build selection (no graphics or game execution).
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME native_project_build COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_project_build.py ${CMAKE_COMMAND})
endif()

# builds-branch tooling: index.json, and a packaged aiversemcp run from an unpacked tools-only Linux zip
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    if(TARGET aiversemcp AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME release_tools COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_release.py $<TARGET_FILE:aiversemcp>)
    else()
        add_test(NAME release_tools COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/test_release.py)
    endif()
endif()

# Project import (shared Python pipeline + editor/MCP bridge).
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/PortOps.cpp apps/editor/core/PackageOps.cpp
        apps/editor/core/PackageTool.cpp)
endif()
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME project_importer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/port/test_port.py)
    add_test(NAME unity_ui_importer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/unity_ui/test_import.py)
    if(TARGET aiverse_headless)
        set_tests_properties(project_importer PROPERTIES ENVIRONMENT "AIVERSE_PORT_HEADLESS=$<TARGET_FILE:aiverse_headless>")
    endif()
    add_test(NAME world_importer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/port/test_world.py)
    if(TARGET aiverse_headless)
        set_tests_properties(world_importer PROPERTIES ENVIRONMENT "AIVERSE_PORT_HEADLESS=$<TARGET_FILE:aiverse_headless>")
    endif()
    # Package catalog client (tools/pkg): local index, no network
    add_test(NAME aiverse_pkg COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/pkg/test_aiverse_pkg.py)
    add_test(NAME branding_loader COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/branding/logo/loader.py --check)
    # Total Annihilation importer + HPI unpacker on made-up data (tools/importers/ta)
    add_test(NAME ta_importer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/ta/test_ta.py)
    if(TARGET aiverse_headless)
        set_tests_properties(ta_importer PROPERTIES ENVIRONMENT "AIVERSE_PORT_HEADLESS=$<TARGET_FILE:aiverse_headless>")
    endif()
endif()

# Unity URP materials, Shader Graph conversion and texture importer settings (agent-unity-materials):
# Python importer tests, aiverse_graphcheck (engine Shader Graph compiler on converted graphs), KTX2/ASTC writer tests.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_graphcheck tools/importers/unity_materials/graphcheck.cpp)
    target_link_libraries(aiverse_graphcheck PRIVATE aiverse_shader_graph)
    target_sources(aiverse_tests PRIVATE tools/aiversebuild/TextureCompress.cpp)
    if(Python3_Interpreter_FOUND)
        add_test(NAME unity_materials COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/unity_materials/test_materials.py)
        set_tests_properties(unity_materials PROPERTIES ENVIRONMENT "AIVERSE_GRAPHCHECK=$<TARGET_FILE:aiverse_graphcheck>")
    endif()
endif()

# The game client's core (GameClient: menus, script UI, view scripts, input, audio): AiverseGame's
# main.cpp adds only the entry point; the editor's Game window hosts the same code (apps/editor/ClientHost.cpp).
set(AIVERSE_CLIENT_CORE_SOURCES apps/client/ClientLoading.cpp apps/client/GameClient.cpp apps/client/ClientUi.cpp apps/client/ClientActions.cpp apps/client/ClientScript.cpp
    apps/client/ClientScriptCmd.cpp apps/client/ClientScriptUi.cpp apps/client/Minimap.cpp apps/client/Lobby.cpp apps/client/ClientFx.cpp
    apps/client/ClientPlanet.cpp apps/client/MinimapPlanet.cpp apps/client/ClientRegionInput.cpp
    apps/client/ClientBaked.cpp   # ClientBaked: the map's baked lighting (agent-lightbake)
    apps/client/ClientTopDown.cpp apps/client/ClientCommerce.cpp apps/client/ClientCity.cpp
    apps/client/ClientGameActionsLua.cpp)   # shared sendPercent/useAbility UI Lua; independent of gamepad
foreach(t ${AIVERSE_GAME_CLIENT_TARGETS})
    if(TARGET ${t})
        target_link_libraries(${t} PRIVATE aiverse_content_reader)   # streamed world: worker-thread project reader
        target_sources(${t} PRIVATE apps/client/GameClient.cpp)
        target_sources(${t} PRIVATE apps/client/ClientBaked.cpp)   # agent-lightbake
    endif()
endforeach()
# Editor: play the project as in the game client, hosted in the Game window or in AiverseGame (apps/editor/EditorClientPlay.cpp)
if(TARGET aiverse_editor OR AIVERSE_BUILD_STUDIO_HTML5)
    # Shared game runtime host; editor shells own controls and native surfaces.
    # Existing in-game overlay rendering remains inside GameClient, not Studio UI.
    add_library(aiverse_hosted_client STATIC apps/client/HostedClient.cpp ${AIVERSE_CLIENT_CORE_SOURCES})
    target_include_directories(aiverse_hosted_client PUBLIC "${PROJECT_SOURCE_DIR}")
    target_link_libraries(aiverse_hosted_client PUBLIC aiverse_runtime aiverse_game_view
        aiverse_net_city aiverse_splash aiverse_net aiverse_game_audio aiverse_atlas
        aiverse_ui aiverse_game_fx aiverse_i18n aiverse_content_reader AIVERSE::InputFlow)
endif()
if(TARGET aiverse_editor)
    target_sources(aiverse_editor PRIVATE apps/editor/EditorClientPlay.cpp)
    target_link_libraries(aiverse_editor PRIVATE aiverse_hosted_client)
endif()
if(AIVERSE_BUILD_TESTS AND (AIVERSE_PLATFORM STREQUAL "linux" OR AIVERSE_PLATFORM STREQUAL "macos"))
    add_executable(aiverse_hosted_view_tests tests/hosted_view.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_hosted_view_tests PRIVATE aiverse_runtime)
    if(TARGET aiverse_hosted_client)
        target_link_libraries(aiverse_hosted_view_tests PRIVATE aiverse_hosted_client)
        target_compile_definitions(aiverse_hosted_view_tests PRIVATE AIVERSE_TEST_HOSTED_CLIENT=1)
    endif()
    add_test(NAME aiverse_hosted_view_tests COMMAND aiverse_hosted_view_tests)
endif()

# Reusable algorithms are public to external native game modules as well as engine code.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_tests tests/libraries/tests.cpp)
    target_link_libraries(aiverse_library_tests PRIVATE AIVERSE::libraries)
    add_test(NAME aiverse_library_tests COMMAND aiverse_library_tests)
endif()

# Meta-game policies: CPU-only tests, usable independently of a game or GPU.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_meta_tests tests/meta/tests.cpp)
    target_link_libraries(aiverse_meta_tests PRIVATE aiverse::meta)
    add_test(NAME aiverse_meta_tests COMMAND aiverse_meta_tests)
endif()

if(AIVERSE_BUILD_TOOLS)
    add_executable(aiverse_meta_validate tools/meta/validate.cpp)
    target_link_libraries(aiverse_meta_validate PRIVATE aiverse::meta)
endif()

if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME meta_native_link COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/meta/native_link.py ${CMAKE_COMMAND})
endif()


if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_gameplay_tests tests/libraries/gameplay_tests.cpp)
    target_link_libraries(aiverse_gameplay_tests PRIVATE AIVERSE::gameplay_native aiverse_engine)
    add_test(NAME aiverse_gameplay_tests COMMAND aiverse_gameplay_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME aiverse_gameplay_generated COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/libraries/generate_gameplay.py --check)
        add_test(NAME aiverse_gameplay_tools COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries/gameplay_tools.py)
        # Python tempfile on some macOS runners chooses /var/folders/...,
        # where nested CMake configure_file() has failed with ENOTTY.
        # Use the build filesystem for temporary project checks instead.
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aiverse-test-tmp")
        set_tests_properties(aiverse_gameplay_tools PROPERTIES
            ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/aiverse-test-tmp")
        # Test the headless executable built from THIS worktree/config,
        # not a stale build/.temp binary or an inherited shell setting.
        if(TARGET aiverse_headless)
            set_tests_properties(aiverse_gameplay_tools PROPERTIES
                ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/aiverse-test-tmp;AIVERSE_HEADLESS=$<TARGET_FILE:aiverse_headless>")
        endif()
    endif()
endif()

# Reusable application packages: host policies remain independent of the simulation.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_app_tests tests/libraries/app_tests.cpp)
    target_link_libraries(aiverse_app_tests PRIVATE AIVERSE::libraries)
    add_test(NAME aiverse_app_tests COMMAND aiverse_app_tests)
    if(TARGET aiverse::meta)
        add_executable(aiverse_app_meta_tests tests/libraries/app_meta_tests.cpp)
        target_link_libraries(aiverse_app_meta_tests PRIVATE AIVERSE::libraries aiverse::meta)
        add_test(NAME aiverse_app_meta_tests COMMAND aiverse_app_meta_tests)
    endif()
endif()
if(AIVERSE_BUILD_TOOLS)
    add_executable(aiverse_app_tools tools/libraries/app_main.cpp)
    target_link_libraries(aiverse_app_tools PRIVATE AIVERSE::gameplay_native aiverse_engine)
endif()

# Extended meta service remains host-side, outside deterministic match callbacks.
if(AIVERSE_BUILD_TOOLS)
    add_executable(aiverse_meta_service tools/meta/service_main.cpp)
    target_link_libraries(aiverse_meta_service PRIVATE aiverse::meta)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_meta_service_tests tests/meta/service_tests.cpp)
    target_link_libraries(aiverse_meta_service_tests PRIVATE aiverse::meta AIVERSE::meta_world)
    add_test(NAME meta_service COMMAND aiverse_meta_service_tests ${CMAKE_CURRENT_SOURCE_DIR}/examples/meta/service.json)
endif()

# FPS is an independent GameModule; RTS lockstep remains available unchanged.
add_subdirectory(engine/actor)

# Host-only offline TerraWeave atlas baker: not linked into any game module.
# Uses the same optional WorldScene Horizon manifest and pure C++20 bake API.
if(AIVERSE_BUILD_TOOLS AND NOT EMSCRIPTEN)
    add_executable(aiverse_horizon_bake tools/horizon/main.cpp)
    target_link_libraries(aiverse_horizon_bake PRIVATE aiverse_world aiverse_models aiverse_zstd_dec)
endif()
# Opt-in additive scene collision namespaces use the existing
# aiverse_world / CollisionStream library. Define these after engine/actor
# has created aiverse_world, not in the early aiverse_engine-only test group.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_ecs_scene_collision_namespaces_test
        tests/ecs_scene_collision_namespaces_test.cpp)
    target_link_libraries(aiverse_ecs_scene_collision_namespaces_test PRIVATE
        aiverse_world aiverse_engine)
    add_test(NAME ecs_scene_collision_namespaces_test
        COMMAND aiverse_ecs_scene_collision_namespaces_test)

    add_executable(aiverse_ecs_scene_collision_bridge_test
        tests/ecs_scene_collision_bridge_test.cpp)
    target_link_libraries(aiverse_ecs_scene_collision_bridge_test PRIVATE
        aiverse_world aiverse_engine)
    add_test(NAME ecs_scene_collision_bridge_test
        COMMAND aiverse_ecs_scene_collision_bridge_test)
endif()

# Real genre-adapter validation: both production GameModules, v9 disk states,
# canonical command order, COW snapshots, replay and FPS ECS authority.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_ecs_real_genre_replay_test
        tests/ecs_real_genre_replay_test.cpp)
    target_link_libraries(aiverse_ecs_real_genre_replay_test PRIVATE
        aiverse_game aiverse_fps)
    target_compile_definitions(aiverse_ecs_real_genre_replay_test PRIVATE
        AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ecs_real_genre_replay_test COMMAND aiverse_ecs_real_genre_replay_test)
endif()
# Demonstrate that one ECS projectile/impact mechanic can serve unrelated
# shooter and strategy hosts. Public libraries + shared collision only;
# neither aiverse_game nor aiverse_fps is linked to the test.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_ecs_projectile_composition_test
        tests/ecs_projectile_composition_test.cpp)
    target_link_libraries(aiverse_ecs_projectile_composition_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::libraries)
    add_test(NAME ecs_projectile_composition_test
        COMMAND aiverse_ecs_projectile_composition_test)
    add_executable(aiverse_ecs_cross_genre_projectile_test
        tests/ecs_cross_genre_projectile_test.cpp)
    target_link_libraries(aiverse_ecs_cross_genre_projectile_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::libraries)
    add_test(NAME ecs_cross_genre_projectile_test
        COMMAND aiverse_ecs_cross_genre_projectile_test)
    add_executable(aiverse_ecs_cross_genre_weapon_pipeline_test
        tests/ecs_cross_genre_weapon_pipeline_test.cpp)
    target_link_libraries(aiverse_ecs_cross_genre_weapon_pipeline_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::libraries)
    add_test(NAME ecs_cross_genre_weapon_pipeline_test
        COMMAND aiverse_ecs_cross_genre_weapon_pipeline_test)
    add_executable(aiverse_ecs_kinematic_projectile_test
        tests/ecs_kinematic_projectile_test.cpp)
    target_link_libraries(aiverse_ecs_kinematic_projectile_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::libraries)
    add_test(NAME ecs_kinematic_projectile_test
        COMMAND aiverse_ecs_kinematic_projectile_test)
    add_executable(aiverse_ecs_cross_genre_impact_queue_test
        tests/ecs_cross_genre_impact_queue_test.cpp)
    target_link_libraries(aiverse_ecs_cross_genre_impact_queue_test PRIVATE
        aiverse_engine AIVERSE::lib_ecs_impact_queue)
    add_test(NAME ecs_cross_genre_impact_queue_test
        COMMAND aiverse_ecs_cross_genre_impact_queue_test)
    add_executable(aiverse_ecs_integer_health_commit_test
        tests/ecs_integer_health_commit_test.cpp)
    target_link_libraries(aiverse_ecs_integer_health_commit_test PRIVATE
        aiverse_engine AIVERSE::lib_ecs_impact_damage)
    add_test(NAME ecs_integer_health_commit_test
        COMMAND aiverse_ecs_integer_health_commit_test)
    add_executable(aiverse_ecs_hitscan_selection_test
        tests/ecs_hitscan_selection_test.cpp)
    target_link_libraries(aiverse_ecs_hitscan_selection_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::libraries)
    add_test(NAME ecs_hitscan_selection_test
        COMMAND aiverse_ecs_hitscan_selection_test)
    add_executable(aiverse_ecs_hitscan_resolution_test
        tests/ecs_hitscan_resolution_test.cpp)
    target_link_libraries(aiverse_ecs_hitscan_resolution_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::libraries)
    add_test(NAME ecs_hitscan_resolution_test
        COMMAND aiverse_ecs_hitscan_resolution_test)
    # NEREID Water stage 1: native shared ECS sampling in unrelated host modules.
    add_executable(aiverse_ecs_nereid_water_test tests/ecs_nereid_water_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_water_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_water_test COMMAND aiverse_ecs_nereid_water_test)
    # NEREID stage 2: shared deterministic buoyancy/drag policy.
    add_executable(aiverse_ecs_nereid_buoyancy_test tests/ecs_nereid_buoyancy_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_buoyancy_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_buoyancy)
    add_test(NAME ecs_nereid_buoyancy_test COMMAND aiverse_ecs_nereid_buoyancy_test)
    # NEREID on spheres: radial sampling, currents and buoyancy for planet seas.
    add_executable(aiverse_ecs_nereid_sphere_water_test tests/ecs_nereid_sphere_water_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_sphere_water_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_buoyancy)
    add_test(NAME ecs_nereid_sphere_water_test COMMAND aiverse_ecs_nereid_sphere_water_test)
    # NEREID stage 3: deterministic polygon/spline shapes and native persistence.
    add_executable(aiverse_ecs_nereid_geometry_test tests/ecs_nereid_geometry_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_geometry_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_geometry_test COMMAND aiverse_ecs_nereid_geometry_test)
    add_executable(aiverse_ecs_nereid_river_grade_test tests/ecs_nereid_river_grade_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_river_grade_test PRIVATE aiverse_models aiverse_world aiverse_zstd_dec AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_river_grade_test COMMAND aiverse_ecs_nereid_river_grade_test)
    # NEREID stage 4: transient spatial query index, parity and rollback.
    add_executable(aiverse_ecs_nereid_query_index_test tests/ecs_nereid_query_index_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_query_index_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_query_index_test COMMAND aiverse_ecs_nereid_query_index_test)
    # NEREID stage 5: read-only legacy waterNetwork raster import parity.
    # Link only legacy water's data source rather than all RTS/editor/renderer.
    add_executable(aiverse_ecs_nereid_legacy_raster_test
        tests/ecs_nereid_legacy_raster_test.cpp
        engine/view/game/GameWater.cpp)
    target_link_libraries(aiverse_ecs_nereid_legacy_raster_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_legacy_raster_test
        COMMAND aiverse_ecs_nereid_legacy_raster_test)
    # NEREID stage 6: adaptive/indexed sampling parity and native-game setup.
    add_executable(aiverse_ecs_nereid_native_pipeline_test
        tests/ecs_nereid_native_pipeline_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_native_pipeline_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_buoyancy)
    add_test(NAME ecs_nereid_native_pipeline_test
        COMMAND aiverse_ecs_nereid_native_pipeline_test)
    # Diagnostic only: performance timings must not be a CI correctness gate.
    add_executable(aiverse_ecs_nereid_sampling_bench
        tests/ecs_nereid_sampling_bench.cpp)
    target_link_libraries(aiverse_ecs_nereid_sampling_bench PRIVATE
        aiverse_engine AIVERSE::lib_nereid_water)
    # NEREID stage 7B: presentation-only surface mesh, no gfx dependency.
    add_executable(aiverse_ecs_nereid_surface_mesh_test
        tests/ecs_nereid_surface_mesh_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_surface_mesh_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_surface_mesh_test COMMAND aiverse_ecs_nereid_surface_mesh_test)
    # NEREID stage 7C: Renderer3D source/GLSL/MSL bridge contract.
    add_executable(aiverse_ecs_nereid_renderer_contract_test
        tests/ecs_nereid_renderer_contract_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_renderer_contract_test PRIVATE
        aiverse_engine aiverse_world aiverse_models AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_renderer_contract_test
        COMMAND aiverse_ecs_nereid_renderer_contract_test)
    # Legacy cell water -> NEREID conversion (CPU-only).
    add_executable(aiverse_game_water_nereid_test tests/game_water_nereid_test.cpp)
    target_link_libraries(aiverse_game_water_nereid_test PRIVATE
        aiverse_engine aiverse_world aiverse_models AIVERSE::lib_nereid_water)
    add_test(NAME game_water_nereid_test COMMAND aiverse_game_water_nereid_test)
    # NEREID large-world ocean LOD: legacy coverage, stitched rings (CPU-only).
    add_executable(aiverse_ecs_nereid_ocean_lod_test
        tests/ecs_nereid_ocean_lod_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_ocean_lod_test PRIVATE
        aiverse_engine aiverse_world aiverse_models AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_ocean_lod_test
        COMMAND aiverse_ecs_nereid_ocean_lod_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/nereid/ocean-large-world.json)
    # NEREID stages 41-44: JSON links, reference/indexed/scene flow parity
    # and shader-only water mesh upload identity (CPU-only).
    add_executable(aiverse_ecs_nereid_confluence_contract_test
        tests/ecs_nereid_confluence_contract_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_confluence_contract_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_confluence_contract_test
        COMMAND aiverse_ecs_nereid_confluence_contract_test)
    # Review fixes 2026-11-21: rock erosion, flood carry/flow/ports, wake cull, cascades.
    add_executable(aiverse_ecs_nereid_review_fixes_test
        tests/ecs_nereid_review_fixes_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_review_fixes_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_review_fixes_test
        COMMAND aiverse_ecs_nereid_review_fixes_test)
    # Review fixes 2026-11-21: TerraWeave resident tile rebuilds, dirty records, wind patch uploads.
    add_executable(aiverse_terraweave_review_fixes_test tests/terraweave_review_fixes_test.cpp)
    target_link_libraries(aiverse_terraweave_review_fixes_test PRIVATE
        aiverse_world aiverse_models aiverse_zstd_dec AIVERSE::lib_nereid_water)
    add_test(NAME terraweave_review_fixes COMMAND aiverse_terraweave_review_fixes_test)
    # Real NativeModule instantiated by GameCore, exercising GameSave and COW.
    add_executable(aiverse_ecs_native_rts_integration_test tests/ecs_native_rts_integration_test.cpp)
    target_link_libraries(aiverse_ecs_native_rts_integration_test PRIVATE aiverse_game)
    target_compile_definitions(aiverse_ecs_native_rts_integration_test PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ecs_native_rts_integration_test COMMAND aiverse_ecs_native_rts_integration_test)
    add_executable(aiverse_rts_authored_pose_test tests/rts_authored_pose_test.cpp)
    target_link_libraries(aiverse_rts_authored_pose_test PRIVATE aiverse_runtime aiverse_game aiverse_game_view)
    target_compile_definitions(aiverse_rts_authored_pose_test PRIVATE AIVERSE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rts_authored_pose_test COMMAND aiverse_rts_authored_pose_test)
    add_executable(aiverse_ecs_native_persistence_test tests/ecs_native_persistence_test.cpp)
    target_link_libraries(aiverse_ecs_native_persistence_test PRIVATE aiverse_engine)
    add_test(NAME ecs_native_persistence_test COMMAND aiverse_ecs_native_persistence_test)
    add_executable(aiverse_ecs_cross_genre_timed_mechanics_test
        tests/ecs_cross_genre_timed_mechanics_test.cpp)
    target_link_libraries(aiverse_ecs_cross_genre_timed_mechanics_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::lib_ecs_timed_mechanics)
    add_test(NAME ecs_cross_genre_timed_mechanics_test
        COMMAND aiverse_ecs_cross_genre_timed_mechanics_test)
    add_executable(aiverse_ecs_cross_genre_locomotion_test
        tests/ecs_cross_genre_locomotion_test.cpp)
    target_link_libraries(aiverse_ecs_cross_genre_locomotion_test PRIVATE
        aiverse_engine AIVERSE::lib_ecs_locomotion)
    add_test(NAME ecs_cross_genre_locomotion_test
        COMMAND aiverse_ecs_cross_genre_locomotion_test)
    add_executable(aiverse_ecs_cross_genre_inventory_test
        tests/ecs_cross_genre_inventory_test.cpp)
    target_link_libraries(aiverse_ecs_cross_genre_inventory_test PRIVATE
        aiverse_engine AIVERSE::lib_ecs_inventory)
    add_test(NAME ecs_cross_genre_inventory_test
        COMMAND aiverse_ecs_cross_genre_inventory_test)
    add_executable(aiverse_ecs_fixed_regeneration_policy_test
        tests/ecs_fixed_regeneration_policy_test.cpp)
    target_link_libraries(aiverse_ecs_fixed_regeneration_policy_test PRIVATE
        aiverse_engine AIVERSE::lib_ecs_timed_mechanics)
    add_test(NAME ecs_fixed_regeneration_policy_test
        COMMAND aiverse_ecs_fixed_regeneration_policy_test)
endif()
add_subdirectory(engine/voxel)

# Opt-in end-to-end reusable library host, excluded from normal builds.
option(AIVERSE_BUILD_LIBRARY_SHOWCASE "Build native library integration showcase" OFF)
if(AIVERSE_BUILD_LIBRARY_SHOWCASE)
    if(NOT TARGET aiverse::meta)
        add_subdirectory(engine/meta)
    endif()
    add_executable(aiverse_library_showcase examples/libraries/showcase/main.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_compile_definitions(aiverse_library_showcase PRIVATE AIVERSE_LIBRARY_ENGINE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(aiverse_library_showcase PRIVATE aiverse_runtime aiverse_game_view aiverse_ui aiverse_i18n aiverse::meta AIVERSE::gameplay_native)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_runtime_library_tests tests/libraries/runtime_tests.cpp)
    target_link_libraries(aiverse_runtime_library_tests PRIVATE AIVERSE::gameplay_native aiverse_engine)
    add_test(NAME aiverse_runtime_library_tests COMMAND aiverse_runtime_library_tests)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_systems_tests tests/libraries/systems_tests.cpp)
    target_link_libraries(aiverse_systems_tests PRIVATE AIVERSE::libraries)
    add_test(NAME aiverse_systems_tests COMMAND aiverse_systems_tests)
    add_executable(aiverse_systems_engine_tests tests/libraries/systems_engine_tests.cpp)
    target_link_libraries(aiverse_systems_engine_tests PRIVATE AIVERSE::gameplay_native aiverse_game)
    add_test(NAME aiverse_systems_engine_tests COMMAND aiverse_systems_engine_tests)
endif()

# LiveOps shares the meta aggregate; independently testable without GPU or network.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_meta_liveops_tests tests/meta/liveops_tests.cpp)
    target_link_libraries(aiverse_meta_liveops_tests PRIVATE aiverse::meta)
    add_test(NAME meta_liveops COMMAND aiverse_meta_liveops_tests ${CMAKE_CURRENT_SOURCE_DIR}/examples/meta/service.json ${CMAKE_CURRENT_SOURCE_DIR}/examples/meta/liveops.json)
    if(Python3_Interpreter_FOUND)
        add_test(NAME meta_liveops_schema COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/meta/generate_liveops.py --check)
    endif()
endif()

# Reusable bot adapters: real engine integration, isolated from the reference FPS bot.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_aiverse_bot_tests tests/libraries/bots/aiverse_tests.cpp)
    target_link_libraries(aiverse_library_aiverse_bot_tests PRIVATE AIVERSE::aiverse_bots)
    add_test(NAME library_aiverse_bots COMMAND aiverse_library_aiverse_bot_tests ${AIVERSE_TEMPLATES_DIR}/skirmish)
endif()
if(TARGET aiverse_fps AND (AIVERSE_BUILD_TESTS OR AIVERSE_BUILD_TOOLS))
    add_executable(aiverse_bot_match examples/libraries/bots/main.cpp)
    target_link_libraries(aiverse_bot_match PRIVATE AIVERSE::aiverse_bots AIVERSE::fps_bots)
endif()
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_fps)
    add_executable(aiverse_library_fps_bot_tests tests/libraries/bots/fps_tests.cpp)
    target_link_libraries(aiverse_library_fps_bot_tests PRIVATE AIVERSE::fps_bots)
    add_test(NAME library_fps_bots COMMAND aiverse_library_fps_bot_tests)
endif()

if(TARGET aiverse_fps AND (AIVERSE_BUILD_TESTS OR AIVERSE_BUILD_TOOLS))
    add_executable(aiverse_navmesh tools/libraries/navmesh_main.cpp)
    target_link_libraries(aiverse_navmesh PRIVATE AIVERSE::fps_bots)
endif()
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_fps)
    add_executable(aiverse_library_navmesh_tests tests/libraries/bots/navmesh_tests.cpp)
    target_link_libraries(aiverse_library_navmesh_tests PRIVATE AIVERSE::fps_bots)
    add_test(NAME library_navmesh COMMAND aiverse_library_navmesh_tests)
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiverse_world)
    add_executable(aiverse_library_genre_tests tests/libraries/genre_tests.cpp)
    target_link_libraries(aiverse_library_genre_tests PRIVATE AIVERSE::mechanics)
    add_test(NAME library_genre COMMAND aiverse_library_genre_tests ${PROJECT_SOURCE_DIR})
endif()

# Policy packages added 2026-11-22 (nav-links, volume-nav, structure-destruction, portals-rewind,
# item-affixes, quest-journal, wounds-disease, herds-taming, colonist-relations, utility-networks, railways, ...).
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_world)
    add_executable(aiverse_library_policy_mechanics_tests tests/libraries/policy_mechanics_tests.cpp)
    target_include_directories(aiverse_library_policy_mechanics_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/libraries)
    target_link_libraries(aiverse_library_policy_mechanics_tests PRIVATE AIVERSE::mechanics)
    add_test(NAME library_policy_mechanics COMMAND aiverse_library_policy_mechanics_tests)
endif()

# boss-encounters (telegraphs, statuses, boss brain, schedule, mob behaviour): tests + headless reference fight.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_encounter_tests tests/libraries/encounter_tests.cpp)
    target_link_libraries(aiverse_library_encounter_tests PRIVATE AIVERSE::lib_boss_encounters)
    add_test(NAME library_encounters COMMAND aiverse_library_encounter_tests ${PROJECT_SOURCE_DIR})
endif()
# building-gen (procedural enterable buildings from boxes).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_building_tests tests/libraries/building_tests.cpp)
    target_link_libraries(aiverse_library_building_tests PRIVATE AIVERSE::lib_building_gen)
    add_test(NAME library_building_gen COMMAND aiverse_library_building_tests)
endif()
# Universal functional building properties (doors, large/small gates, retractable roof)
# do not depend on any genre module, Studio, GPU or legacy factory/barracks preset.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_building_feature_tests tests/libraries/building_feature_tests.cpp)
    target_link_libraries(aiverse_library_building_feature_tests PRIVATE AIVERSE::lib_building_style)
    add_test(NAME library_building_features COMMAND aiverse_library_building_feature_tests ${PROJECT_SOURCE_DIR})
endif()
# Pure StructureGraph: functional layout without engine, renderer, material
# catalog or Studio dependencies; standalone C++20 compilation contract.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_building_structure_graph_tests tests/libraries/building_structure_graph_tests.cpp)
    target_compile_features(aiverse_building_structure_graph_tests PRIVATE cxx_std_20)
    target_include_directories(aiverse_building_structure_graph_tests PRIVATE libraries/include)
    add_test(NAME library_building_structure_graph COMMAND aiverse_building_structure_graph_tests)
endif()
# Genre-neutral native integration executable: actual Simulation, world
# collision, native navigation and admitted generic Replica loopback.
if((AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS) AND NOT EMSCRIPTEN AND
   NOT ANDROID AND NOT IOS AND TARGET aiverse_world AND TARGET aiverse_net_replica)
    add_executable(aiverse_building_world_example examples/world/runtime-building/main.cpp)
    target_link_libraries(aiverse_building_world_example PRIVATE
        AIVERSE::lib_building_style aiverse_engine aiverse_world
        aiverse_models aiverse_zstd_dec aiverse_net_replica)
    if(AIVERSE_BUILD_TESTS)
        add_test(NAME building_native_world_replica
            COMMAND aiverse_building_world_example ${PROJECT_SOURCE_DIR})
        set_tests_properties(building_native_world_replica PROPERTIES LABELS "ECS;NETWORK")
        add_test(NAME building_native_world_lockstep
            COMMAND aiverse_building_world_example ${PROJECT_SOURCE_DIR} --lockstep)
        set_tests_properties(building_native_world_lockstep PROPERTIES LABELS "ECS;NETWORK;PERSISTENCE")
    endif()
    if(TARGET aiverse_physics)
        target_link_libraries(aiverse_building_world_example PRIVATE AIVERSE::physics)
        target_compile_definitions(aiverse_building_world_example PRIVATE AIVERSE_BUILDING_PHYSIS_EXAMPLE=1)
        if(AIVERSE_BUILD_TESTS)
            add_test(NAME building_native_world_physis
                COMMAND aiverse_building_world_example ${PROJECT_SOURCE_DIR} --physis)
            set_tests_properties(building_native_world_physis PROPERTIES LABELS "PHYSIS")
        endif()
    endif()
endif()
# building-style (settings of generated buildings as style files) and its .glb exporter.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_building_style_tests tests/libraries/building_style_tests.cpp)
    target_link_libraries(aiverse_library_building_style_tests PRIVATE AIVERSE::lib_building_style)
    add_test(NAME library_building_style COMMAND aiverse_library_building_style_tests ${PROJECT_SOURCE_DIR})
endif()
# Runtime generation uses the SAME header-only CPU library in packaged games.
# This targeted test has no Studio, renderer, modelviewer or filesystem runtime dependency.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_building_runtime_tests tests/libraries/building_runtime_tests.cpp)
    target_link_libraries(aiverse_library_building_runtime_tests PRIVATE AIVERSE::lib_building_style)
    add_test(NAME library_building_runtime COMMAND aiverse_library_building_runtime_tests ${PROJECT_SOURCE_DIR})
endif()
if((AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS) AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS)
    add_executable(aiverse_building_export tools/buildings/building_style_export.cpp)
    target_link_libraries(aiverse_building_export PRIVATE AIVERSE::lib_building_style)
endif()
# gear-tiers (protective gear slots, absorption, wear, capacity).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_gear_tests tests/libraries/gear_tests.cpp)
    target_link_libraries(aiverse_library_gear_tests PRIVATE AIVERSE::lib_gear_tiers)
    add_test(NAME library_gear COMMAND aiverse_library_gear_tests)
endif()
# snapshot-delta (record diffs for authoritative snapshots).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_delta_tests tests/libraries/delta_tests.cpp)
    target_link_libraries(aiverse_library_delta_tests PRIVATE AIVERSE::lib_snapshot_delta)
    add_test(NAME library_snapshot_delta COMMAND aiverse_library_delta_tests)
endif()
# battle-royale (safe zone phases, bombardment, supply drops, air insertion, standings, lobby).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_royale_tests tests/libraries/royale_tests.cpp)
    target_link_libraries(aiverse_library_royale_tests PRIVATE AIVERSE::lib_battle_royale)
    add_test(NAME library_royale COMMAND aiverse_library_royale_tests ${PROJECT_SOURCE_DIR})
endif()
# Genre library suites that used to run only in CI through tests/libraries/run.py. They check the acceptance
# criteria of docs/capability-status.json (D1, P3, Q1, Q2, Q5, I1, T1, C2, ...), so ./run-tests.sh must run them.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_strategy_library_tests tests/libraries/strategy_tests.cpp)
    target_link_libraries(aiverse_strategy_library_tests PRIVATE aiverse_engine AIVERSE::libraries)
    add_test(NAME strategy_libraries COMMAND aiverse_strategy_library_tests)
    add_executable(aiverse_library_genre_puzzles_tests tests/libraries/genre_puzzles_tests.cpp)
    add_executable(aiverse_puzzles_example examples/libraries/mechanics/puzzles/PuzzlesExample.cpp)
    add_executable(aiverse_library_cards_turns_tests tests/libraries/genre_cards_turns_tests.cpp)
    add_executable(aiverse_cards_turns_example examples/libraries/mechanics/cards-turns/main.cpp)
    add_executable(aiverse_library_vehicles_sports_tests tests/libraries/genre_vehicles_sports_tests.cpp)
    add_executable(aiverse_vehicles_sports_example examples/libraries/mechanics/vehicles-sports/VehiclesSportsExample.cpp)
    add_executable(aiverse_library_genre_production_survival_tests tests/libraries/genre_production_survival_tests.cpp)
    add_executable(aiverse_library_production_survival_example examples/libraries/mechanics/production-survival/production_survival.cpp)
    foreach(_aiverse_gt aiverse_library_genre_puzzles_tests aiverse_puzzles_example aiverse_library_cards_turns_tests
            aiverse_cards_turns_example aiverse_library_vehicles_sports_tests aiverse_vehicles_sports_example
            aiverse_library_genre_production_survival_tests aiverse_library_production_survival_example)
        target_link_libraries(${_aiverse_gt} PRIVATE aiverse_engine AIVERSE::gameplay_native)
    endforeach()
    add_test(NAME library_genre_puzzles COMMAND aiverse_library_genre_puzzles_tests ${PROJECT_SOURCE_DIR})
    add_test(NAME library_genre_puzzles_example COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:aiverse_puzzles_example>
        -DLEVELS=${PROJECT_SOURCE_DIR}/examples/libraries/mechanics/puzzles/levels.json
        -P ${PROJECT_SOURCE_DIR}/examples/libraries/mechanics/puzzles/run_twice.cmake)
    add_test(NAME library_cards_turns COMMAND aiverse_library_cards_turns_tests ${PROJECT_SOURCE_DIR})
    add_test(NAME library_cards_turns_example COMMAND aiverse_cards_turns_example ${PROJECT_SOURCE_DIR}/examples/libraries/mechanics/cards-turns 7)
    set_tests_properties(library_cards_turns_example PROPERTIES PASS_REGULAR_EXPRESSION "deterministic replay: identical, winners: yes")
    add_test(NAME library_vehicles_sports COMMAND aiverse_library_vehicles_sports_tests ${PROJECT_SOURCE_DIR})
    add_test(NAME library_vehicles_sports_example COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:aiverse_vehicles_sports_example>
        -DDATA=${PROJECT_SOURCE_DIR}/examples/libraries/mechanics/vehicles-sports
        -P ${PROJECT_SOURCE_DIR}/tests/libraries/vehicles_sports_example.cmake)
    add_test(NAME library_genre_production_survival COMMAND aiverse_library_genre_production_survival_tests ${PROJECT_SOURCE_DIR})
    add_test(NAME library_production_survival_example COMMAND aiverse_library_production_survival_example
        ${PROJECT_SOURCE_DIR}/examples/libraries/mechanics/production-survival --quiet)
endif()
# docs/capability-status.json stays honest: libraries exist, listed tests run here, "core" areas name their tests.
if(AIVERSE_BUILD_TESTS AND Python3_EXECUTABLE)
    add_test(NAME genre_registry COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/genre/check_registry.py ${PROJECT_SOURCE_DIR})
endif()
if(AIVERSE_BUILD_TESTS OR AIVERSE_BUILD_TOOLS)
    add_executable(aiverse_encounter tools/libraries/encounter_main.cpp)
    target_link_libraries(aiverse_encounter PRIVATE AIVERSE::lib_boss_encounters)
endif()
# voxel packages (world, light, mesh, gen, ticks, nav, replication): tests + headless check/benchmark (docs/voxel.md).
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_library_voxel_tests tests/libraries/voxel_tests.cpp)
    target_link_libraries(aiverse_library_voxel_tests PRIVATE AIVERSE::lib_voxel_world AIVERSE::lib_voxel_light AIVERSE::lib_voxel_mesh AIVERSE::lib_voxel_gen AIVERSE::lib_voxel_ticks AIVERSE::lib_voxel_nav AIVERSE::lib_voxel_replication)
    add_test(NAME library_voxel COMMAND aiverse_library_voxel_tests)
endif()
if(AIVERSE_BUILD_TESTS OR AIVERSE_BUILD_TOOLS)
    add_executable(aiverse_voxel tools/libraries/voxel_main.cpp)
    target_link_libraries(aiverse_voxel PRIVATE AIVERSE::lib_voxel_world AIVERSE::lib_voxel_light AIVERSE::lib_voxel_mesh AIVERSE::lib_voxel_gen AIVERSE::lib_voxel_ticks AIVERSE::lib_voxel_nav AIVERSE::lib_voxel_replication)
    if(AIVERSE_BUILD_TESTS)
        add_test(NAME voxel_headless COMMAND aiverse_voxel --content ${PROJECT_SOURCE_DIR}/examples/libraries/voxel --radius 3 --edits 500 --verify)
    endif()
endif()
# match-progression (perks, xp, offers, pickups, summons, battle deck): tests + headless run.
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_world)
    add_executable(aiverse_library_progression_tests tests/libraries/perks_tests.cpp)
    target_link_libraries(aiverse_library_progression_tests PRIVATE AIVERSE::mechanics)
    add_test(NAME library_progression COMMAND aiverse_library_progression_tests ${PROJECT_SOURCE_DIR})
endif()
if(TARGET aiverse_world AND (AIVERSE_BUILD_TESTS OR AIVERSE_BUILD_TOOLS))
    add_executable(aiverse_progression tools/libraries/progression_main.cpp)
    target_link_libraries(aiverse_progression PRIVATE AIVERSE::mechanics)
    if(AIVERSE_BUILD_TESTS)
        add_test(NAME progression_headless COMMAND aiverse_progression --root ${PROJECT_SOURCE_DIR} --verify)
    endif()
endif()

# GPU-free PRISMA graph planner tests; included by default in the unified CTest runner.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_prisma_graph_tests tests/render/prisma_graph.cpp)
    target_include_directories(aiverse_prisma_graph_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME prisma_graph COMMAND aiverse_prisma_graph_tests)
    add_executable(aiverse_prisma_lens_water_tests tests/render/prisma_lens_water.cpp)
    target_link_libraries(aiverse_prisma_lens_water_tests PRIVATE aiverse_postfx_data)
    add_test(NAME prisma_lens_water COMMAND aiverse_prisma_lens_water_tests)
    add_executable(aiverse_prisma_volume_tests tests/render/prisma_volume.cpp)
    target_link_libraries(aiverse_prisma_volume_tests PRIVATE aiverse_postfx_data)
    add_test(NAME prisma_volume COMMAND aiverse_prisma_volume_tests)
    # 2026-11-22 effects (AO, SSR, lens flare, SMAA, distortion, streaks, halation, haze,
    # FSR 1, DLSS route, histogram metering, stylization graphs): GPU-free contract test.
    add_executable(aiverse_prisma_new_effects_tests tests/render/prisma_new_effects.cpp)
    target_link_libraries(aiverse_prisma_new_effects_tests PRIVATE aiverse_postfx_data)
    add_test(NAME prisma_new_effects COMMAND aiverse_prisma_new_effects_tests)
    set_tests_properties(prisma_new_effects PROPERTIES LABELS "Prisma")
    if(Python3_EXECUTABLE)
        add_test(NAME prisma_style_presets_fresh COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/prisma/make_style_presets.py --check)
        set_tests_properties(prisma_style_presets_fresh PROPERTIES LABELS "Prisma")
    endif()
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_alpha_coverage_mips_test tests/render/alpha_coverage_mips.cpp)
    target_include_directories(aiverse_alpha_coverage_mips_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME alpha_coverage_mips COMMAND aiverse_alpha_coverage_mips_test)
    add_executable(aiverse_shader_graph_tests tests/shader_graph/compiler.cpp)
    target_link_libraries(aiverse_shader_graph_tests PRIVATE aiverse_shader_graph aiverse_postfx_data aiverse_fx)
    add_test(NAME shader_graph COMMAND aiverse_shader_graph_tests)
endif()

# Asset-free HDR visual regression fixture (opt-in, uses the actual backends).
option(AIVERSE_BUILD_HDR_TESTS "Build HDR GPU validation scene" OFF)
if(AIVERSE_BUILD_HDR_TESTS AND AIVERSE_DESKTOP)
    add_executable(aiverse_hdr_policy tests/render/hdr_policy.cpp)
    target_link_libraries(aiverse_hdr_policy PRIVATE aiverse_postfx_data)
    add_test(NAME hdr_policy COMMAND aiverse_hdr_policy)
    add_executable(aiverse_hdr_scene tests/render/hdr_scene.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_hdr_scene PRIVATE aiverse_runtime)
    if(AIVERSE_PLATFORM STREQUAL "macos")
        add_executable(aiverse_hdr_gl tests/render/hdr_gl.mm engine/gfx/gl/GfxGL.cpp $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_hdr_gl PRIVATE aiverse_runtime "-framework OpenGL")
        set_source_files_properties(tests/render/hdr_gl.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc -Wno-deprecated-declarations")
    endif()
endif()

# Headless Vulkan host of the same HDR / GI scene (Linux, Android device push).
if(AIVERSE_BUILD_HDR_TESTS AND AIVERSE_VULKAN AND AIVERSE_PLATFORM MATCHES "^(linux|android)$")
    add_executable(aiverse_hdr_vulkan tests/render/hdr_vulkan.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_hdr_vulkan PRIVATE aiverse_runtime)
    if(AIVERSE_BUILD_TESTS)
        foreach(_case dynamic_gi compute_gi compute_gi_cascades compute_gi_scroll)
            add_test(NAME lighting_${_case}_vulkan COMMAND aiverse_hdr_vulkan)
        endforeach()
        set_tests_properties(lighting_dynamic_gi_vulkan PROPERTIES ENVIRONMENT "AIVERSE_DYNAMIC_GI_VERIFY=1")
        set_tests_properties(lighting_compute_gi_vulkan PROPERTIES ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1")
        set_tests_properties(lighting_compute_gi_cascades_vulkan PROPERTIES
            ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1;AIVERSE_COMPUTE_GI_CASCADES=3")
        set_tests_properties(lighting_compute_gi_scroll_vulkan PROPERTIES
            ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1;AIVERSE_COMPUTE_GI_CASCADES=2;AIVERSE_COMPUTE_GI_SCROLL=1")
        foreach(_fx dof motion ao ssr flare smaa distort streaks halation haze fsr histogram)
            add_test(NAME prisma_${_fx}_vulkan COMMAND aiverse_hdr_vulkan)
            set_tests_properties(prisma_${_fx}_vulkan PROPERTIES ENVIRONMENT "AIVERSE_PRISMA_EFFECT_TEST=${_fx}"
                LABELS "Lighting;GPU;Vulkan;Prisma" TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
                PASS_REGULAR_EXPRESSION "PRISMA ${_fx}: (diff [0-9.]+ )?PASS|HDR Vulkan: skipped" FAIL_REGULAR_EXPRESSION "FAIL")
        endforeach()
        add_test(NAME lighting_baked_vulkan COMMAND aiverse_hdr_vulkan)
        set_tests_properties(lighting_baked_vulkan PROPERTIES ENVIRONMENT "AIVERSE_BAKED_LIGHTING_TEST=1"
            LABELS "Lighting;GPU;Vulkan" TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
            PASS_REGULAR_EXPRESSION "Baked lighting: .*PASS|HDR Vulkan: skipped" FAIL_REGULAR_EXPRESSION "FAIL")
        add_test(NAME lighting_luma_vulkan COMMAND aiverse_hdr_vulkan)
        set_tests_properties(lighting_luma_vulkan PROPERTIES ENVIRONMENT "AIVERSE_LUMA_VERIFY=1"
            LABELS "Lighting;GPU;Vulkan" TIMEOUT 180 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
            PASS_REGULAR_EXPRESSION "Luma GPU 11:.*PASS|HDR Vulkan: skipped" FAIL_REGULAR_EXPRESSION "FAIL")
        set_tests_properties(lighting_dynamic_gi_vulkan lighting_compute_gi_vulkan lighting_compute_gi_cascades_vulkan lighting_compute_gi_scroll_vulkan
            PROPERTIES LABELS "Lighting;GPU;Vulkan" TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
            PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS|HDR Vulkan: skipped" FAIL_REGULAR_EXPRESSION "FAIL")
    endif()
endif()

# Independently selectable CPU cable geometry; no renderer, physics or GPU link.
add_library(aiverse_cables STATIC EXCLUDE_FROM_ALL engine/cables/Cables.cpp)
add_library(AIVERSE::cables ALIAS aiverse_cables)
target_include_directories(aiverse_cables PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
    target_compile_options(aiverse_cables PRIVATE -Wall -Wextra)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_cables_tests tests/cables/cables.cpp)
    target_link_libraries(aiverse_cables_tests PRIVATE AIVERSE::cables)
    add_test(NAME cables_geometry COMMAND aiverse_cables_tests)
endif()

# Host authoring: shared by the editor, MCP and CPU tests.
if(AIVERSE_DESKTOP AND (TARGET aiverse_editor_core OR AIVERSE_BUILD_TESTS))
    add_library(aiverse_modeling STATIC tools/modeling/Cable.cpp tools/modeling/Geometry.cpp tools/modeling/Boolean.cpp tools/modeling/Advanced.cpp
        tools/modeling/Document.cpp tools/modeling/Export.cpp tools/modeling/Clips.cpp tools/modeling/Recipes.cpp)
    target_link_libraries(aiverse_modeling PUBLIC aiverse_models AIVERSE::cables)
    target_include_directories(aiverse_modeling PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/ModelingOps.cpp)
    target_link_libraries(aiverse_editor_core PUBLIC aiverse_modeling)
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiverse_editor_core)
    add_executable(aiverse_modeling_tests tests/modeling/modeling.cpp)
    target_link_libraries(aiverse_modeling_tests PRIVATE aiverse_editor_core aiverse_modeling aiverse_models aiverse_zstd_dec)
    add_test(NAME modeling_authoring COMMAND aiverse_modeling_tests)
endif()

if(TARGET aiverse_editor_core AND AIVERSE_BUILD_TOOLS)
    add_executable(aiverse_modeling_cli tools/modeling/main.cpp)
    set_target_properties(aiverse_modeling_cli PROPERTIES OUTPUT_NAME aiverse_modeling)
    target_link_libraries(aiverse_modeling_cli PRIVATE aiverse_editor_core aiverse_modeling aiverse_models aiverse_zstd_dec)
endif()
if(TARGET aiverse_modeling AND NOT MSVC)
    target_compile_options(aiverse_modeling PRIVATE -Wall -Wextra)
endif()

# Optional standalone physics example; normal RTS/FPS clients do not create a physics world.
if(AIVERSE_ENABLE_PHYSICS)
    option(AIVERSE_BUILD_PHYSICS_DEMO "Build the interactive Box3D sandbox" ${AIVERSE_DESKTOP})
    if(AIVERSE_BUILD_TESTS)
        add_executable(aiverse_physics_tests tests/physics/tests.cpp)
        target_link_libraries(aiverse_physics_tests PRIVATE AIVERSE::physics)
        add_test(NAME physics_runtime COMMAND aiverse_physics_tests ${PROJECT_SOURCE_DIR}/examples/physics/scene.json)
        add_executable(aiverse_physics_scene_tests tests/physics/scene_tests.cpp)
        target_link_libraries(aiverse_physics_scene_tests PRIVATE AIVERSE::physics)
        if(NOT MSVC)
            target_compile_options(aiverse_physics_scene_tests PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME physics_scene COMMAND aiverse_physics_scene_tests ${PROJECT_SOURCE_DIR}/examples/physics/scene.json)
    endif()
    if(AIVERSE_BUILD_PHYSICS_DEMO AND AIVERSE_DESKTOP)
        add_executable(aiverse_physics_demo examples/physics/main.cpp $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_physics_demo PRIVATE AIVERSE::physics aiverse_runtime aiverse_i18n)
        target_compile_definitions(aiverse_physics_demo PRIVATE AIVERSE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    endif()
endif()

# Multiplayer physics sandbox place on replica + physics::Scene (agent-place, docs/replica.md "First milestone").
# Library aiverse_place (server, client logic, bots, SimNet load harness); executable aiverse_place
# (target aiverse_place_app): serve / bots / load everywhere, the graphical client on desktop builds.
if(AIVERSE_ENABLE_PHYSICS AND NOT EMSCRIPTEN)
    add_library(aiverse_place STATIC apps/place/PlaceProtocol.cpp apps/place/PlaceServer.cpp apps/place/PlaceClient.cpp apps/place/PlacePredict.cpp apps/place/PlaceSim.cpp
                apps/place/PlaceAdmission.cpp apps/place/PlaceLink.cpp)
    target_link_libraries(aiverse_place PUBLIC aiverse_net_replica AIVERSE::physics)
    target_include_directories(aiverse_place PUBLIC ${PROJECT_SOURCE_DIR})

    # Explicit native performance-acceptance mode for Debug test builds:
    # retain Debug symbols, asserts, Box3D validation, scene/substeps, 4G
    # network traffic and the strict <5ms tick budget. Optimizing only
    # the production hot-path dependencies avoids measuring a 523-body
    # load against -O0 libraries while leaving ordinary Debug builds alone.
    # OFF by default; the one-pass performance gate enables it explicitly.
    option(AIVERSE_PLACE_FAST_DEBUG
        "Enable -O2 for Place/Box3D/replica hot paths in Debug, retaining assertions"
        OFF)
    if(AIVERSE_PLACE_FAST_DEBUG)
        if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
            foreach(_place_hot_target IN ITEMS
                    aiverse_place aiverse_net_replica aiverse_net
                    aiverse_physics box3d)
                if(TARGET ${_place_hot_target})
                    target_compile_options(${_place_hot_target} PRIVATE
                        "$<$<CONFIG:Debug>:-O2>")
                endif()
            endforeach()
        else()
            message(WARNING "AIVERSE_PLACE_FAST_DEBUG is only implemented for AppleClang/Clang/GNU")
        endif()
    endif()
    option(AIVERSE_BUILD_PLACE_CLIENT "Build the graphical client into aiverse_place" ${AIVERSE_BUILD_CLIENT})
    if(AIVERSE_BUILD_PLACE_CLIENT AND AIVERSE_DESKTOP)
        add_executable(aiverse_place_app apps/place/main.cpp apps/place/PlaceView.cpp $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_place_app PRIVATE aiverse_place aiverse_runtime aiverse_i18n)
        target_compile_definitions(aiverse_place_app PRIVATE AIVERSE_PLACE_CLIENT=1 AIVERSE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    else()
        add_executable(aiverse_place_app apps/place/main.cpp)
        target_link_libraries(aiverse_place_app PRIVATE aiverse_place)
    endif()
    set_target_properties(aiverse_place_app PROPERTIES OUTPUT_NAME aiverse_place)
    if(NOT MSVC)
        target_compile_options(aiverse_place PRIVATE -Wall -Wextra)
        target_compile_options(aiverse_place_app PRIVATE -Wall -Wextra)
    endif()
    if(AIVERSE_BUILD_TESTS)
        add_executable(aiverse_place_tests tests/place/place_tests.cpp)
        target_link_libraries(aiverse_place_tests PRIVATE aiverse_place)
        if(NOT MSVC)
            target_compile_options(aiverse_place_tests PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME place COMMAND aiverse_place_tests ${PROJECT_SOURCE_DIR}/examples/place/scene.json)
        # The place test includes a strict measured server-tick (<5 ms) performance
        # budget. Never co-schedule it with heavyweight CTests: competing
        # workers distort wall-clock time without changing simulation work.
        # Preserve the original performance threshold and all functional checks.
        set_tests_properties(place PROPERTIES RUN_SERIAL TRUE)
        # Place hosting: tickets, gate, reconnect, suspend, WebSocket (agent-place-secure, docs/replica.md § Place hosting)
        add_executable(aiverse_place_secure_tests tests/place/place_secure_tests.cpp)
        target_link_libraries(aiverse_place_secure_tests PRIVATE aiverse_place)
        if(NOT MSVC)
            target_compile_options(aiverse_place_secure_tests PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME place_secure COMMAND aiverse_place_secure_tests)
    endif()
endif()

# VIVID stage 5D: real macOS scene/renderer visual acceptance preview.
# GUI-driven, NOT a CTest (requires drawable, snapshots and actual shadows).
# The runtime object library owns the platform entry point (aiverseMain).
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_vivid_renderer_preview tests/vivid/renderer_preview.cpp)
    target_link_libraries(aiverse_vivid_renderer_preview PRIVATE
        aiverse_runtime AIVERSE::vivid_actor AIVERSE::MotionWeave aiverse_zstd_dec)
    target_compile_features(aiverse_vivid_renderer_preview PRIVATE cxx_std_20)
endif()

# VIVID Creator: interactive DNA + rig/eye/skin/hair authoring with
# MotionWeave and native Model/ShaderGraph preview. Separate from the
# general editor and optional for non-desktop builds.
if(AIVERSE_DESKTOP AND (AIVERSE_BUILD_TOOLS OR AIVERSE_BUILD_TESTS))
    add_executable(aiverse_vivid_creator tools/vivid-creator/main.cpp)
    target_link_libraries(aiverse_vivid_creator PRIVATE
        aiverse_runtime AIVERSE::vivid_human AIVERSE::vivid_actor
        AIVERSE::MotionWeave aiverse_zstd_dec)
    target_compile_features(aiverse_vivid_creator PRIVATE cxx_std_20)
    target_compile_definitions(aiverse_vivid_creator PRIVATE AIVERSE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()

# Geometry visibility verification uses the same renderer on GL and Metal.
if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP)
    add_executable(aiverse_geometry_preview tests/geometry/preview.cpp)
    target_link_libraries(aiverse_geometry_preview PRIVATE aiverse_runtime)
endif()

option(AIVERSE_ENABLE_CLOTH "Build optional presentation XPBD cloth" OFF)
if(AIVERSE_ENABLE_CLOTH)
    add_subdirectory(engine/cloth)
endif()

# Offline surface proxies reuse the model format, PNG encoder and ordinary rendering paths.
if(TARGET aiverse_modeling)
    target_sources(aiverse_modeling PRIVATE tools/modeling/SurfaceProxy.cpp)
    target_link_libraries(aiverse_modeling PRIVATE aiverse_branding)
endif()
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/SurfaceProxyOps.cpp)
endif()
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_modeling)
    add_executable(aiverse_surface_proxy_tests tests/geometry/surface_proxy.cpp)
    target_link_libraries(aiverse_surface_proxy_tests PRIVATE aiverse_editor_core aiverse_modeling aiverse_zstd_dec)
    add_test(NAME surface_proxy COMMAND aiverse_surface_proxy_tests)
endif()
if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP AND TARGET aiverse_modeling)
    add_executable(aiverse_surface_proxy_preview tests/geometry/surface_preview.cpp)
    target_link_libraries(aiverse_surface_proxy_preview PRIVATE aiverse_runtime aiverse_modeling)
endif()
if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP AND TARGET aiverse_modeling)
    add_executable(aiverse_geometry_bench tools/geometrybench/main.cpp)
    target_link_libraries(aiverse_geometry_bench PRIVATE aiverse_runtime aiverse_modeling)
endif()

option(AIVERSE_BUILD_AUTOLOD "Build offline meshoptimizer LOD tools" OFF)
option(AIVERSE_BUILD_STRATA "Build opt-in Strata import-time virtual geometry builder" ON)
option(AIVERSE_RUNTIME_AUTOLOD "Generate missing rigid-model LODs during upload preparation" ON)
# meshoptimizer is a portable import-time dependency: not used during a render
# frame. Share its exact implementation with the optional offline worker.
if(AIVERSE_RUNTIME_AUTOLOD OR AIVERSE_BUILD_STRATA OR (AIVERSE_BUILD_AUTOLOD AND AIVERSE_DESKTOP))
    if(NOT TARGET aiverse_meshoptimizer_lod)
        add_library(aiverse_meshoptimizer_lod STATIC
            third_party/meshoptimizer/src/allocator.cpp
            third_party/meshoptimizer/src/indexgenerator.cpp
            third_party/meshoptimizer/src/simplifier.cpp
            third_party/meshoptimizer/src/vcacheoptimizer.cpp)
        target_include_directories(aiverse_meshoptimizer_lod PUBLIC third_party/meshoptimizer/src)
    endif()
endif()
if(AIVERSE_BUILD_STRATA)
    add_library(aiverse_strata_builder STATIC
        engine/render/StrataBuilder.cpp
        engine/render/StrataSeamTopology.cpp)
    target_link_libraries(aiverse_strata_builder PUBLIC aiverse_meshoptimizer_lod)
    target_include_directories(aiverse_strata_builder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    add_library(aiverse_strata_model_adapter STATIC
        engine/render/StrataModelAdapter.cpp
        engine/render/StrataCooked.cpp
        engine/render/StrataSourceSeams.cpp)
    target_link_libraries(aiverse_strata_model_adapter PUBLIC aiverse_strata_builder aiverse_models)
    target_include_directories(aiverse_strata_model_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # Per-pass CPU reference selection, using existing Geometry.cpp projection.
    # Output is non-submitting source-local index ranges, not GPU commands.
    add_library(aiverse_strata_view STATIC engine/render/StrataView.cpp)
    target_link_libraries(aiverse_strata_view PUBLIC aiverse_strata_model_adapter aiverse_models)
    target_include_directories(aiverse_strata_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # Offline archive IO is independently linkable by native import/editor
    # tools without pulling the complete renderer or a gfx::Device.
    add_library(aiverse_strata_disk STATIC
        engine/render/StrataDiskPages.cpp
        engine/render/StrataStreamingCoordinator.cpp)
    target_link_libraries(aiverse_strata_disk PUBLIC aiverse_strata_model_adapter)
    target_include_directories(aiverse_strata_disk PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT EMSCRIPTEN)
        find_package(Threads REQUIRED)
        target_link_libraries(aiverse_strata_disk PUBLIC Threads::Threads)
    endif()
    # Portable Vulkan/GLES3/WebGL2 fallback: CPU-selected LOD frontiers and
    # immutable auxiliary INDEX-only data. Original Model VB stays owned by
    # Renderer3D/GlobalGeometryPool; no second Strata vertex allocator.
    add_library(aiverse_strata_portable STATIC
        engine/render/StrataPortableRender.cpp
        engine/render/StrataDebugView.cpp
        engine/render/StrataIndexResidency.cpp
        engine/render/StrataIndexPages.cpp)
    target_link_libraries(aiverse_strata_portable PUBLIC
        aiverse_strata_view aiverse_models aiverse_strata_disk)
    target_include_directories(aiverse_strata_portable PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})
    # GPU metadata is a non-drawing, source-revision-checked storage buffer.
    # Original GPU geometry MUST remain owned by GlobalGeometryPool.
    add_library(aiverse_strata_gpu_scene STATIC engine/render/StrataGpuScene.cpp)
    target_link_libraries(aiverse_strata_gpu_scene PUBLIC aiverse_strata_model_adapter)
    target_include_directories(aiverse_strata_gpu_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # Metal/Vulkan diagnostic GPU cluster selection. Vulkan also writes
    # guarded indexed indirect candidates, but Renderer3D is still the
    # exclusive production draw owner (no automatic GPU raster switch).
    add_library(aiverse_strata_gpu_culling STATIC engine/render/StrataGpuCulling.cpp)
    target_link_libraries(aiverse_strata_gpu_culling PUBLIC
        aiverse_strata_gpu_scene aiverse_strata_view)
    target_include_directories(aiverse_strata_gpu_culling PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # Shared-gfx indirect argument preflight. No live draw path is selected:
    # renderer ownership/parity gates are explicit and default Legacy.
    add_library(aiverse_strata_render_routing STATIC
        engine/render/StrataRenderRouting.cpp
        engine/render/StrataRendererPreview.cpp
        engine/render/StrataVulkanIndirectContract.cpp)
    target_link_libraries(aiverse_strata_render_routing PUBLIC
        aiverse_strata_gpu_scene aiverse_strata_view)
    target_include_directories(aiverse_strata_render_routing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    if(TARGET aiverse_editor_core)
        target_link_libraries(aiverse_editor_core PUBLIC aiverse_strata_model_adapter)
        target_compile_definitions(aiverse_editor_core PRIVATE AIVERSE_STRATA_EDITOR_ENABLED=1)
    endif()
    # GeometryScene is still the only instance owner. This option exposes
    # opt-in CPU staging at scene load, not a second draw submission path.
    target_link_libraries(aiverse_runtime PUBLIC
        aiverse_strata_view aiverse_strata_gpu_scene
        aiverse_strata_gpu_culling aiverse_strata_render_routing
        aiverse_strata_portable)
    target_compile_definitions(aiverse_runtime PRIVATE AIVERSE_STRATA_STAGING_ENABLED=1)
    if(AIVERSE_BUILD_TESTS)
        add_executable(aiverse_strata_builder_tests tests/render/strata_builder.cpp)
        target_link_libraries(aiverse_strata_builder_tests PRIVATE aiverse_strata_builder)
        add_test(NAME strata_builder COMMAND aiverse_strata_builder_tests)
        add_executable(aiverse_strata_seams_tests tests/render/strata_seams.cpp)
        target_link_libraries(aiverse_strata_seams_tests PRIVATE
            aiverse_strata_builder)
        add_test(NAME strata_seams COMMAND aiverse_strata_seams_tests)
        add_executable(aiverse_strata_model_adapter_tests tests/render/strata_model_adapter.cpp)
        target_link_libraries(aiverse_strata_model_adapter_tests PRIVATE
            aiverse_strata_model_adapter aiverse_zstd_dec)
        add_test(NAME strata_model_adapter COMMAND aiverse_strata_model_adapter_tests)
        # Manual load-time benchmark on a real asset (no CTest: needs an external model).
        add_executable(aiverse_strata_stage_bench tests/render/strata_stage_bench.cpp)
        target_link_libraries(aiverse_strata_stage_bench PRIVATE
            aiverse_strata_model_adapter aiverse_zstd_dec)
        add_executable(aiverse_strata_stage_probe tools/strata/stage_probe.cpp)
        target_link_libraries(aiverse_strata_stage_probe PRIVATE
            aiverse_strata_model_adapter aiverse_zstd_dec)
        # Cook: writes cooked STRATA hierarchies + index-page archives for a scene.
        add_executable(aiverse_strata_cook tools/strata/cook.cpp)
        target_link_libraries(aiverse_strata_cook PRIVATE
            aiverse_strata_disk aiverse_strata_model_adapter aiverse_zstd_dec)
        add_executable(aiverse_strata_source_seams_tests
            tests/render/strata_source_seams.cpp)
        target_link_libraries(aiverse_strata_source_seams_tests PRIVATE
            aiverse_strata_model_adapter aiverse_zstd_dec)
        add_test(NAME strata_source_seams
            COMMAND aiverse_strata_source_seams_tests)
        add_executable(aiverse_strata_view_tests tests/render/strata_view.cpp)
        target_link_libraries(aiverse_strata_view_tests PRIVATE aiverse_strata_view aiverse_zstd_dec)
        add_test(NAME strata_view COMMAND aiverse_strata_view_tests)
        # Stage18.4: opt-in CPU-only deterministic trace; not GPU timing/FPS.
        add_executable(aiverse_strata_lod_benchmark
            tests/render/strata_lod_benchmark.cpp)
        target_include_directories(aiverse_strata_lod_benchmark PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME strata_lod_benchmark COMMAND aiverse_strata_lod_benchmark)
        add_executable(aiverse_strata_gpu_scene_tests tests/render/strata_gpu_scene.cpp)
        target_link_libraries(aiverse_strata_gpu_scene_tests PRIVATE
            aiverse_strata_gpu_scene aiverse_zstd_dec)
        add_test(NAME strata_gpu_scene COMMAND aiverse_strata_gpu_scene_tests)
        add_executable(aiverse_strata_gpu_culling_tests tests/render/strata_gpu_culling.cpp)
        target_link_libraries(aiverse_strata_gpu_culling_tests PRIVATE
            aiverse_strata_gpu_culling aiverse_zstd_dec)
        add_test(NAME strata_gpu_culling COMMAND aiverse_strata_gpu_culling_tests)
        if(AIVERSE_VULKAN AND NOT EMSCRIPTEN)
            add_executable(aiverse_strata_vulkan_kernel_tests
                tests/render/strata_vulkan_kernel.cpp)
            target_link_libraries(aiverse_strata_vulkan_kernel_tests PRIVATE
                aiverse_strata_gpu_culling aiverse_gfx_vulkan aiverse_zstd_dec)
            add_test(NAME strata_vulkan_compile
                COMMAND aiverse_strata_vulkan_kernel_tests --compile-only)
            add_test(NAME strata_vulkan_kernel
                COMMAND aiverse_strata_vulkan_kernel_tests)
            set_tests_properties(strata_vulkan_kernel PROPERTIES SKIP_RETURN_CODE 77)
        endif()
        add_executable(aiverse_strata_render_routing_tests
            tests/render/strata_render_routing.cpp)
        target_link_libraries(aiverse_strata_render_routing_tests PRIVATE
            aiverse_strata_render_routing aiverse_zstd_dec)
        add_test(NAME strata_render_routing COMMAND aiverse_strata_render_routing_tests)
        add_executable(aiverse_strata_portable_tests tests/render/strata_portable.cpp)
        target_link_libraries(aiverse_strata_portable_tests PRIVATE
            aiverse_strata_portable aiverse_zstd_dec)
        add_test(NAME strata_portable COMMAND aiverse_strata_portable_tests)
        add_executable(aiverse_strata_debug_view_tests
            tests/render/strata_debug_view.cpp)
        target_link_libraries(aiverse_strata_debug_view_tests PRIVATE
            aiverse_strata_portable aiverse_zstd_dec)
        add_test(NAME strata_debug_view COMMAND aiverse_strata_debug_view_tests)
        add_executable(aiverse_strata_index_residency_tests
            tests/render/strata_index_residency.cpp)
        target_link_libraries(aiverse_strata_index_residency_tests PRIVATE
            aiverse_strata_portable aiverse_zstd_dec)
        add_test(NAME strata_index_residency COMMAND aiverse_strata_index_residency_tests)
        add_executable(aiverse_strata_index_pages_tests
            tests/render/strata_index_pages.cpp)
        target_link_libraries(aiverse_strata_index_pages_tests PRIVATE
            aiverse_strata_portable aiverse_zstd_dec)
        add_test(NAME strata_index_pages COMMAND aiverse_strata_index_pages_tests)
        if(NOT EMSCRIPTEN)
            add_executable(aiverse_strata_disk_pages_tests
                tests/render/strata_disk_pages.cpp)
            target_link_libraries(aiverse_strata_disk_pages_tests PRIVATE
                aiverse_strata_portable aiverse_zstd_dec)
            add_test(NAME strata_disk_pages COMMAND aiverse_strata_disk_pages_tests)
            add_executable(aiverse_strata_streaming_coordinator_tests
                tests/render/strata_streaming_coordinator.cpp)
            target_link_libraries(aiverse_strata_streaming_coordinator_tests PRIVATE
                aiverse_strata_disk aiverse_zstd_dec)
            add_test(NAME strata_streaming_coordinator
                COMMAND aiverse_strata_streaming_coordinator_tests)
        endif()
    endif()
    # The GPU parity runner opens a real macOS CAMetalLayer and compiles MSL at
    # runtime. Keep it manual/opt-in: focused CPU/mocked CTests have no window.
    option(AIVERSE_STRATA_METAL_GPU_TESTS "Build actual Strata Metal GPU parity runner" OFF)
    if(AIVERSE_STRATA_METAL_GPU_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
        add_executable(aiverse_strata_metal_parity
            tests/render/strata_metal_parity.cpp
            $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_strata_metal_parity PRIVATE
            aiverse_runtime aiverse_strata_gpu_culling aiverse_strata_gpu_scene)
        # Stage 17.4: opt-in native Metal GPU per-node temporal mask parity.
        # Three consecutive frames, asynchronously checked against CPU LOD.
        add_executable(aiverse_strata_metal_temporal
            tests/render/strata_metal_temporal.cpp
            $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_strata_metal_temporal PRIVATE
            aiverse_runtime aiverse_strata_gpu_culling aiverse_strata_gpu_scene)
        # Stage 11: separate real Metal raster A/B; shares original source
        # VB/IB and Strata pooled index sidecar, no production draw switch.
        add_executable(aiverse_strata_metal_raster_parity
            tests/render/strata_metal_raster_parity.cpp
            $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_strata_metal_raster_parity PRIVATE
            aiverse_runtime aiverse_strata_render_routing
            aiverse_strata_gpu_scene)
        # Stage 12: actual Renderer3D 2-view pixel and shadow Depth32Float
        # parity, using the SAME GlobalGeometryPool and no extra draw owner.
        add_executable(aiverse_strata_metal_depth_multiview_parity
            tests/render/strata_metal_depth_multiview_parity.cpp
            $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_strata_metal_depth_multiview_parity PRIVATE
            aiverse_runtime aiverse_strata_render_routing
            aiverse_strata_gpu_scene)
        # Stage 13: actual Metal planar-reflection RTT + textured material
        # and UV-boundary parity, still no production Strata raster route.
        add_executable(aiverse_strata_metal_reflection_material_parity
            tests/render/strata_metal_reflection_material_parity.cpp
            $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_strata_metal_reflection_material_parity PRIVATE
            aiverse_runtime aiverse_strata_render_routing
            aiverse_strata_gpu_scene)
        # Stage 14: new portable CPU-selected LOD path, selected clusters
        # through the SAME production Renderer3D on actual Metal hardware.
        # Vulkan/GLES3 compile contract is in strata_portable_tests.
        add_executable(aiverse_strata_metal_portable_parity
            tests/render/strata_metal_portable_parity.cpp
            $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_strata_metal_portable_parity PRIVATE
            aiverse_runtime aiverse_strata_portable)
    endif()
endif()
if(AIVERSE_RUNTIME_AUTOLOD)
    target_sources(aiverse_models PRIVATE engine/render/AutoLodRuntime.cpp)
    target_link_libraries(aiverse_models PUBLIC aiverse_meshoptimizer_lod)
    target_compile_definitions(aiverse_models PRIVATE AIVERSE_RUNTIME_AUTOLOD=1)
    if(AIVERSE_BUILD_TESTS)
        add_executable(aiverse_auto_lod_tests tests/render/auto_lod_runtime.cpp)
        target_link_libraries(aiverse_auto_lod_tests PRIVATE aiverse_models aiverse_zstd_dec)
        add_test(NAME auto_lod_runtime COMMAND aiverse_auto_lod_tests)
    endif()
endif()

if(AIVERSE_BUILD_AUTOLOD AND AIVERSE_DESKTOP)
    add_executable(aiverse_model_lod_transition_test tests/render/ModelLodTransition.cpp)
    target_include_directories(aiverse_model_lod_transition_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    aiverse_keep_test_asserts(aiverse_model_lod_transition_test)
    add_test(NAME model_lod_transition COMMAND aiverse_model_lod_transition_test)
    add_executable(aiverse_model_visibility_test tests/render/ModelVisibility.cpp)
    target_include_directories(aiverse_model_visibility_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    aiverse_keep_test_asserts(aiverse_model_visibility_test)
    add_executable(aiverse_lod_simplify tools/models/lod_simplify.cpp)
    target_link_libraries(aiverse_lod_simplify PRIVATE aiverse_meshoptimizer_lod)
endif()

option(AIVERSE_BUILD_RENDERBENCH "Build the Sponza rendering benchmark" OFF)
if(AIVERSE_BUILD_RENDERBENCH AND AIVERSE_DESKTOP)
    add_executable(aiverse_static_light_grid_test tests/render/StaticLightGrid.cpp)
    target_include_directories(aiverse_static_light_grid_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    aiverse_keep_test_asserts(aiverse_static_light_grid_test)
    add_test(NAME static_light_grid COMMAND aiverse_static_light_grid_test)
    add_executable(aiverse_camera_tour_test tests/render/CameraTour.cpp)
    target_link_libraries(aiverse_camera_tour_test PRIVATE aiverse_engine)
    aiverse_keep_test_asserts(aiverse_camera_tour_test)
    add_test(NAME camera_tour COMMAND aiverse_camera_tour_test ${CMAKE_CURRENT_SOURCE_DIR}/tools/renderbench/sponza-tour-draft.json)
    add_executable(aiverse_reinstance tools/renderbench/reinstance.cpp)
    target_link_libraries(aiverse_reinstance PRIVATE aiverse_engine)
    add_executable(aiverse_probe_visibility tools/renderbench/probe_visibility.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_probe_visibility PRIVATE aiverse_runtime)
    execute_process(COMMAND git describe --always --dirty WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE BENCH_REV OUTPUT_STRIP_TRAILING_WHITESPACE)
    add_executable(aiverse_renderbench apps/renderbench/main.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_renderbench PRIVATE aiverse_runtime)
    target_compile_definitions(aiverse_renderbench PRIVATE AIVERSE_BENCH_REVISION="${BENCH_REV}" AIVERSE_BENCH_BUILD="${CMAKE_BUILD_TYPE}")
    if(AIVERSE_PLATFORM STREQUAL "macos")
        add_executable(aiverse_renderbench_gl apps/renderbench/gl_mac.mm engine/gfx/gl/GfxGL.cpp $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_renderbench_gl PRIVATE aiverse_runtime "-framework OpenGL")
        target_compile_definitions(aiverse_renderbench_gl PRIVATE AIVERSE_BENCH_REVISION="${BENCH_REV}" AIVERSE_BENCH_BUILD="${CMAKE_BUILD_TYPE}")
        set_source_files_properties(apps/renderbench/gl_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc -Wno-deprecated-declarations")
    endif()
endif()

if(TARGET aiverse_geometry_bench AND TARGET aiverse_meshoptimizer_lod)
    target_link_libraries(aiverse_geometry_bench PRIVATE aiverse_meshoptimizer_lod)
    target_compile_definitions(aiverse_geometry_bench PRIVATE AIVERSE_BENCH_AUTOLOD=1)
endif()

if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP AND TARGET aiverse_modeling)
    add_executable(aiverse_instance_preview tests/geometry/instance_preview.cpp)
    target_link_libraries(aiverse_instance_preview PRIVATE aiverse_runtime aiverse_modeling)
endif()

# Skill metadata is checked alongside native integration; isolation is a host-only build check.
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME skill_catalog COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/skills/catalog.py check)
    add_test(NAME skill_routing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/skills/catalog_test.py)
    add_test(NAME skill_audit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/skills/audit.py --strict)
    add_test(NAME genre_free_engine COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/genre/check_engine_genre_free.py ${CMAKE_CURRENT_SOURCE_DIR})   # NO GENRE SPLIT: no Rts/Fps names in engine code
    # tools behind workflow skills (skills/ai/aiverse-bots-tune, verification/aiverse-determinism-triage, aiverse-visual-qa)
    add_test(NAME tool_bot_sweep COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/bots/test_sweep.py)
    add_test(NAME tool_determinism_diff COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/determinism/test_diff.py)
    add_test(NAME tool_screenshot_compare COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/visual/test_compare.py)
    # Terminal build monitor: warnings/errors parser, pinned diagnostic UI and logs.
    add_test(NAME tool_aiverse_progress COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/test_aiverse_progress.py)
    set_tests_properties(tool_aiverse_progress PROPERTIES LABELS "TOOLS")
endif()

if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND AND UNIX AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME skill_library_isolation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/skills/isolation.py)
    # Nested CMake probes require a working writable temporary volume.
    # CTest-created directories inherit the verified build filesystem.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aiverse-test-tmp")
    set_tests_properties(skill_library_isolation PROPERTIES
        TIMEOUT 600
        ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/aiverse-test-tmp")
endif()

# ---------------------------------------------------------------------------
# Spine 2D skeletal animation (engine/spine). aiverse_spine is GPU-free (data, pose,
# animation state, draw lists: tests and tools link it); the RHI renderer and the client's UI views
# (apps/client/ClientSpine.cpp, <img src="#spine:NAME"/>) go into every target that hosts GameClient.
# ---------------------------------------------------------------------------
add_library(aiverse_spine STATIC engine/spine/SpineData.cpp engine/spine/SpineSkeleton.cpp)
target_include_directories(aiverse_spine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aiverse_spine PUBLIC aiverse_engine)
option(AIVERSE_CLIENT_WITH_SPINE "Include Spine UI animation in packaged clients" ON)
option(AIVERSE_CLIENT_WITH_GAMEPAD "Include gamepad controls in packaged clients" ON)
foreach(t ${AIVERSE_GAME_CLIENT_TARGETS} aiverse_hosted_client)
    if(TARGET ${t})
        if(t STREQUAL "aiverse_hosted_client" OR AIVERSE_CLIENT_WITH_SPINE)
            target_sources(${t} PRIVATE engine/spine/SpineRender.cpp apps/client/ClientSpine.cpp)
            target_link_libraries(${t} PRIVATE aiverse_spine)
        else()
            target_sources(${t} PRIVATE apps/client/ClientSpineDisabled.cpp)
            target_compile_definitions(${t} PRIVATE AIVERSE_CLIENT_NO_SPINE=1)
        endif()
        target_sources(${t} PRIVATE apps/client/ClientServices.cpp)
        if(NOT t STREQUAL "aiverse_hosted_client")
            target_sources(${t} PRIVATE apps/client/ClientGameActionsLua.cpp)
        endif()
        if(t STREQUAL "aiverse_hosted_client" OR AIVERSE_CLIENT_WITH_GAMEPAD)
            target_sources(${t} PRIVATE apps/client/ClientGamepad.cpp apps/client/ClientGamepadHost.cpp)
        else()
            target_sources(${t} PRIVATE apps/client/ClientGamepadDisabled.cpp)
        endif()
        target_link_libraries(${t} PRIVATE aiverse_render2d)
    endif()
endforeach()
if(TARGET aiverse_tests)
    target_link_libraries(aiverse_tests PRIVATE aiverse_spine)
    target_sources(aiverse_tests PRIVATE apps/client/ClientServices.cpp)   # UI Lua services tables (plugins.on)
endif()
# Gamepads (engine/platform/Gamepad.h): common state + one backend per platform.
if(TARGET aiverse_runtime)
    target_sources(aiverse_runtime PRIVATE engine/platform/gamepad/GamepadCommon.cpp)
    if(AIVERSE_PLATFORM STREQUAL "macos" OR AIVERSE_PLATFORM STREQUAL "ios")
        target_sources(aiverse_runtime PRIVATE engine/platform/gamepad/GamepadApple.mm)
        target_link_libraries(aiverse_runtime PUBLIC "-framework GameController")
    elseif(AIVERSE_PLATFORM STREQUAL "android")
        target_sources(aiverse_runtime PRIVATE engine/platform/gamepad/GamepadAndroid.cpp)
    elseif(AIVERSE_PLATFORM STREQUAL "web")
        target_sources(aiverse_runtime PRIVATE engine/platform/gamepad/GamepadWeb.cpp)
    elseif(AIVERSE_PLATFORM STREQUAL "windows")
        target_sources(aiverse_runtime PRIVATE engine/platform/gamepad/GamepadWin.cpp)
    elseif(AIVERSE_PLATFORM STREQUAL "linux")
        target_sources(aiverse_runtime PRIVATE engine/platform/gamepad/GamepadLinux.cpp)
    else()
        target_sources(aiverse_runtime PRIVATE engine/platform/gamepad/GamepadNone.cpp)
    endif()
endif()
if(TARGET aiverse_tests)
    target_sources(aiverse_tests PRIVATE engine/platform/gamepad/GamepadCommon.cpp engine/platform/gamepad/GamepadNone.cpp apps/client/ClientGamepad.cpp)
endif()
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)   # UI.Windows audio bank importer (synthetic data, no game files)
    add_test(NAME uiwindows_audio_importer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/importers/fmod/test_import_audio.py)
endif()
# The project's native modules (<content>/native, engine/game/native/Build.cmake) after every engine target, so a
# module's CMake may link aiverse_net / aiverse_grpc / aiverse_plugins / server libraries and call
# aiverse_protobuf_generate (docs/native-modules.md § Engine libraries); before the Windows manifest pass below.
aiverse_native_configure()

# Windows: every executable gets platform/windows/utf8.manifest (activeCodePage UTF-8). Paths in the
# engine are UTF-8 std::strings; without it the narrow CRT/STL file APIs use the legacy ANSI code page
# and a project in a folder like "Моя стратегия" cannot be opened. MSVC merges .manifest sources into
# its own manifest; MinGW links it as resource 1 of type RT_MANIFEST (24) via a generated .rc.
if(WIN32)
    set(AIVERSE_UTF8_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/utf8.manifest")
    if(MSVC)
        set(AIVERSE_UTF8_MANIFEST_SOURCE "${AIVERSE_UTF8_MANIFEST}")
    else()
        enable_language(RC)
        set(AIVERSE_UTF8_MANIFEST_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/aiverse_utf8_manifest.rc")
        file(CONFIGURE OUTPUT "${AIVERSE_UTF8_MANIFEST_SOURCE}" @ONLY
             CONTENT "// Generated by CMakeLists.txt: UTF-8 code page manifest\n1 24 \"@AIVERSE_UTF8_MANIFEST@\"\n")
    endif()
    function(aiverse_utf8_manifest dir)
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(t IN LISTS targets)
            get_target_property(type ${t} TYPE)
            if(type STREQUAL "EXECUTABLE")
                target_sources(${t} PRIVATE "${AIVERSE_UTF8_MANIFEST_SOURCE}")
            endif()
        endforeach()
        get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(d IN LISTS subdirs)
            aiverse_utf8_manifest("${d}")
        endforeach()
    endfunction()
    aiverse_utf8_manifest("${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(AIVERSE_BUILD_HDR_TESTS AND AIVERSE_DESKTOP)
    add_executable(aiverse_shadow_batches tests/render/shadow_batches.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_shadow_batches PRIVATE aiverse_runtime)
endif()

if(AIVERSE_BUILD_HDR_TESTS AND AIVERSE_DESKTOP)
    add_executable(aiverse_material_batches tests/render/material_batches.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_material_batches PRIVATE aiverse_runtime)
endif()

# Split rules files from Python tools: rules_io (read / write back) and split_rules.
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME rules_files COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/rules/test_rules.py)
    add_test(NAME virtual_texture_pack
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/virtual_texture_pack.py)
endif()

# Opt-in real Linux EGL/GLES3 texture-array GPU acceptance. No mock driver
# and no silent SKIP: failure to create ES3 context exits nonzero.
option(AIVERSE_TEXTURE_GLES_GPU_TESTS
    "Enable native Linux EGL/GLES3 Texture2DArray GPU readback acceptance" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "linux"
   AND AIVERSE_TEXTURE_GLES_GPU_TESTS)
    add_executable(aiverse_gles_texture_array_gpu_test
        tests/render/gles_texture_array_gpu.cpp)
    target_include_directories(aiverse_gles_texture_array_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_gles_texture_array_gpu_test
        PRIVATE aiverse_runtime EGL GLESv2)
    add_test(NAME gles_texture_array_gpu COMMAND aiverse_gles_texture_array_gpu_test)
    set_tests_properties(gles_texture_array_gpu PROPERTIES
        LABELS "TextureRHI;GLES;GPU" TIMEOUT 45)
endif()

# Opt-in standalone WebGL2 acceptance app using the production Device,
# VirtualTextureStreamer, canvas lifecycle and standard web shell.
# The normal web client is not modified by the test.
if(AIVERSE_PLATFORM STREQUAL "web" AND AIVERSE_WEBGL_CONTEXT_ACCEPTANCE)
    add_executable(aiverse_webgl_vt_acceptance
        tests/render/webgl2_vt_integration.cpp
        $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_webgl_vt_acceptance PRIVATE aiverse_runtime)
    target_include_directories(aiverse_webgl_vt_acceptance
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(aiverse_webgl_vt_acceptance
        PROPERTIES OUTPUT_NAME "webgl-vt-acceptance" SUFFIX ".html")
    target_link_options(aiverse_webgl_vt_acceptance PRIVATE
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/platform/web/shell.html")
endif()

# GPU-independent descriptor validation; a foundation for subsequent texture backends.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(aiverse_texture_contract_test tests/render/texture_contract.cpp)
    target_include_directories(aiverse_texture_contract_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME texture_contract COMMAND aiverse_texture_contract_test)
    add_executable(aiverse_render_target_set_test tests/render/render_target_set.cpp)
    target_include_directories(aiverse_render_target_set_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME render_target_set COMMAND aiverse_render_target_set_test)
    add_executable(aiverse_ktx2_layers_test tests/render/ktx2_layers.cpp)
    target_link_libraries(aiverse_ktx2_layers_test PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME ktx2_layers COMMAND aiverse_ktx2_layers_test)
    add_executable(aiverse_ktx2_transcode_test tests/render/ktx2_transcode.cpp)
    target_link_libraries(aiverse_ktx2_transcode_test PRIVATE aiverse_models aiverse_zstd_dec)
    if(AIVERSE_TEXTURE_WITH_LIBKTX)
        target_link_libraries(aiverse_ktx2_transcode_test PRIVATE aiverse_texture_libktx)
    endif()
    add_test(NAME ktx2_transcode COMMAND aiverse_ktx2_transcode_test)
    add_executable(aiverse_texture_asset_test tests/render/texture_asset.cpp)
    target_link_libraries(aiverse_texture_asset_test PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME texture_asset COMMAND aiverse_texture_asset_test)
    add_executable(aiverse_hdr_texture_test tests/render/hdr_texture.cpp)
    target_include_directories(aiverse_hdr_texture_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME hdr_texture COMMAND aiverse_hdr_texture_test)
    add_executable(aiverse_exr_texture_test tests/render/exr_texture.cpp)
    target_include_directories(aiverse_exr_texture_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(AIVERSE_TEXTURE_WITH_OPENEXR)
        target_link_libraries(aiverse_exr_texture_test PRIVATE OpenEXR::OpenEXR)
        target_compile_definitions(aiverse_exr_texture_test PRIVATE AIVERSE_TEXTURE_WITH_OPENEXR=1)
    endif()
    add_test(NAME exr_texture COMMAND aiverse_exr_texture_test)
    add_executable(aiverse_dds_layers_test tests/render/dds_layers.cpp)
    target_link_libraries(aiverse_dds_layers_test PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME dds_layers COMMAND aiverse_dds_layers_test)
    add_executable(aiverse_texture_streaming_test tests/render/texture_streaming.cpp)
    target_include_directories(aiverse_texture_streaming_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME texture_streaming COMMAND aiverse_texture_streaming_test)
    add_executable(aiverse_texture_mip_residency_test tests/render/texture_mip_residency.cpp)
    target_include_directories(aiverse_texture_mip_residency_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME texture_mip_residency COMMAND aiverse_texture_mip_residency_test)
    add_executable(aiverse_texture_resident_streamer_test tests/render/texture_resident_streamer.cpp)
    target_include_directories(aiverse_texture_resident_streamer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME texture_resident_streamer COMMAND aiverse_texture_resident_streamer_test)
    add_executable(aiverse_virtual_texture_pages_test tests/render/virtual_texture_pages.cpp)
    target_include_directories(aiverse_virtual_texture_pages_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME virtual_texture_pages COMMAND aiverse_virtual_texture_pages_test)
    add_executable(aiverse_virtual_texture_page_table_test tests/render/virtual_texture_page_table.cpp)
    target_include_directories(aiverse_virtual_texture_page_table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME virtual_texture_page_table COMMAND aiverse_virtual_texture_page_table_test)
    add_executable(aiverse_virtual_texture_streamer_test tests/render/virtual_texture_streamer.cpp)
    target_include_directories(aiverse_virtual_texture_streamer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME virtual_texture_streamer COMMAND aiverse_virtual_texture_streamer_test)
    add_executable(aiverse_virtual_texture_mip_pipeline_test tests/render/virtual_texture_mip_pipeline.cpp)
    target_include_directories(aiverse_virtual_texture_mip_pipeline_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME virtual_texture_mip_pipeline COMMAND aiverse_virtual_texture_mip_pipeline_test)
    add_executable(aiverse_virtual_texture_disk_queue_test tests/render/virtual_texture_disk_queue.cpp)
    target_include_directories(aiverse_virtual_texture_disk_queue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_virtual_texture_disk_queue_test PRIVATE Threads::Threads)
    add_test(NAME virtual_texture_disk_queue COMMAND aiverse_virtual_texture_disk_queue_test)
    add_executable(aiverse_virtual_texture_file_streamer_test tests/render/virtual_texture_file_streamer.cpp)
    target_include_directories(aiverse_virtual_texture_file_streamer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_virtual_texture_file_streamer_test PRIVATE Threads::Threads)
    add_test(NAME virtual_texture_file_streamer COMMAND aiverse_virtual_texture_file_streamer_test)
    add_executable(aiverse_virtual_texture_prefetch_test tests/render/virtual_texture_prefetch.cpp)
    target_include_directories(aiverse_virtual_texture_prefetch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME virtual_texture_prefetch COMMAND aiverse_virtual_texture_prefetch_test)
    add_executable(aiverse_virtual_texture_feedback_test tests/render/virtual_texture_feedback.cpp)
    target_include_directories(aiverse_virtual_texture_feedback_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME virtual_texture_feedback COMMAND aiverse_virtual_texture_feedback_test)
    add_executable(aiverse_virtual_texture_feedback_pass_test tests/render/virtual_texture_feedback_pass.cpp)
    target_include_directories(aiverse_virtual_texture_feedback_pass_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_virtual_texture_feedback_pass_test PRIVATE Threads::Threads)
    add_test(NAME virtual_texture_feedback_pass COMMAND aiverse_virtual_texture_feedback_pass_test)
    # Portable C++ contract checks for mixed Metal ICB material snapshots
    # and fragment-only Shader Graph pipeline identity (no native GPU needed).
    add_executable(aiverse_gpu_icb_material_contract_test tests/render/gpu_icb_material_contract.cpp)
    target_include_directories(aiverse_gpu_icb_material_contract_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME gpu_icb_material_contract COMMAND aiverse_gpu_icb_material_contract_test)
    # Optional shader compiler: do not fail platforms that do not ship
    # glslangValidator, but register a real failing test when it is present.
    find_package(Python3 QUIET COMPONENTS Interpreter)
    find_program(AIVERSE_GLSLANG_VALIDATOR NAMES glslangValidator)
    if(Python3_Interpreter_FOUND AND AIVERSE_GLSLANG_VALIDATOR)
        add_test(NAME virtual_texture_feedback_shaders
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/check_virtual_texture_feedback_shaders.py")
        add_test(NAME virtual_texture_sampling_glsl
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/check_virtual_texture_sampling_shaders.py"
                --backend glsl)
        add_test(NAME virtual_texture_sampling_vulkan_spirv
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/check_virtual_texture_sampling_shaders.py"
                --backend vulkan)
    endif()
    if(APPLE AND Python3_Interpreter_FOUND)
        find_program(AIVERSE_XCRUN NAMES xcrun)
        if(AIVERSE_XCRUN)
            add_test(NAME virtual_texture_feedback_msl
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/check_virtual_texture_feedback_msl.py")
            add_test(NAME virtual_texture_sampling_msl
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/check_virtual_texture_sampling_shaders.py"
                    --backend metal)
        endif()
    endif()
endif()

# Explicitly opt-in REAL GPU acceptance; never run on CPU-only Linux or
# silently skip when a Mac lacks a working Metal GPU / drawable.
# The binary compiles the production Metal RHI directly, without the
# platform application main() or any game/genre dependency.
option(AIVERSE_TEXTURE_METAL_GPU_TESTS
    "Opt-in real Metal GPU texture acceptance (macOS hardware required)" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos"
   AND AIVERSE_TEXTURE_METAL_GPU_TESTS)
    add_executable(aiverse_metal_texture_gpu_test
        tests/render/metal_texture_gpu.mm
        engine/gfx/metal/GfxMetal.mm)
    target_include_directories(aiverse_metal_texture_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_source_files_properties(tests/render/metal_texture_gpu.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(aiverse_metal_texture_gpu_test PRIVATE
        Threads::Threads
        "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    add_test(NAME metal_texture_gpu COMMAND aiverse_metal_texture_gpu_test)
    set_tests_properties(metal_texture_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI" TIMEOUT 60)
endif()

# High-risk experimental GPU-authored Metal ICB must NEVER run in the
# default 23 source CTests or the ordinary --metal GPU acceptance.
# Explicit opt-in runs in an isolated process to contain AGX driver faults.
option(AIVERSE_TEXTURE_METAL_ICB_GPU_TESTS
    "Opt-in GPU-encoded Metal ICB graph-material ABI acceptance on a native Mac" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos"
   AND AIVERSE_TEXTURE_METAL_ICB_GPU_TESTS)
    add_executable(aiverse_metal_texture_icb_graph_gpu_test
        tests/render/metal_icb_graph_gpu.mm
        engine/gfx/metal/GfxMetal.mm)
    target_include_directories(aiverse_metal_texture_icb_graph_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_source_files_properties(tests/render/metal_icb_graph_gpu.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(aiverse_metal_texture_icb_graph_gpu_test PRIVATE
        "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    add_test(NAME metal_texture_icb_graph_gpu
        COMMAND aiverse_metal_texture_icb_graph_gpu_test)
    set_tests_properties(metal_texture_icb_graph_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI;EXPERIMENTAL_ICB" TIMEOUT 90
        ENVIRONMENT "AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_ENCODED_ICB=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1;AIVERSE_ICB_TRACE=1")
endif()



# Full production Renderer3D/Shader Graph GPU-encoded ICB scene comparison.
# Opt-in is independent of the low-level Metal material ABI acceptance:
# this target links the *actual* renderer/graph compiler and captures output
# of a synthetic two-material scene (direct vs GPU-authored render path).
# May exercise unsafe driver ICB code; NEVER register by default.
option(AIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS
    "Opt-in production Renderer3D Shader Graph mixed Metal GPU ICB pixel comparison" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos"
   AND AIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS)
    add_executable(aiverse_metal_renderer_graph_icb_gpu_test
        tests/render/metal_renderer_graph_icb_gpu.cpp
        $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_metal_renderer_graph_icb_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_metal_renderer_graph_icb_gpu_test PRIVATE
        aiverse_runtime aiverse_shader_graph)
    add_test(NAME metal_renderer_graph_icb_gpu
        COMMAND aiverse_metal_renderer_graph_icb_gpu_test)
    set_tests_properties(metal_renderer_graph_icb_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI;EXPERIMENTAL_ICB;RENDERER3D;SHADER_GRAPH"
        TIMEOUT 120
        ENVIRONMENT "AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_ENCODED_ICB=1;AIVERSE_GPU_AUTHORED_DRAWS=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1;AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1;AIVERSE_GPU_ICB_ADAPTIVE=0;AIVERSE_ADAPTIVE_DRAW_GROUPS=0;AIVERSE_GPU_ICB_MIN_COMMANDS=2;AIVERSE_ICB_TRACE=1")
endif()


# Risky, independently gated production Shader Graph + disk-backed VT
# *mixed per-command atlas* Metal GPU-encoded ICB regression. Never add this
# test to the safe --metal or default CTest matrix; run from a separate build.
option(AIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS
    "Opt-in production Shader Graph, disk VT and heterogeneous Metal GPU ICB pixel acceptance" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos"
   AND AIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS)
    add_executable(aiverse_metal_renderer_graph_vt_icb_gpu_test
        tests/render/metal_renderer_graph_vt_icb_gpu.cpp
        $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_metal_renderer_graph_vt_icb_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_metal_renderer_graph_vt_icb_gpu_test PRIVATE
        aiverse_runtime aiverse_shader_graph Threads::Threads)
    add_test(NAME metal_renderer_graph_vt_icb_gpu
        COMMAND aiverse_metal_renderer_graph_vt_icb_gpu_test)
    set_tests_properties(metal_renderer_graph_vt_icb_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI;EXPERIMENTAL_ICB;RENDERER3D;SHADER_GRAPH;VIRTUAL_TEXTURE"
        TIMEOUT 120
        ENVIRONMENT "AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_ENCODED_ICB=1;AIVERSE_GPU_AUTHORED_DRAWS=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1;AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1;AIVERSE_GPU_ICB_ADAPTIVE=0;AIVERSE_ADAPTIVE_DRAW_GROUPS=0;AIVERSE_GPU_ICB_MIN_COMMANDS=2;AIVERSE_ICB_TRACE=1")
endif()


# Isolated, high-risk production Shader Graph + VT missing-page and UV
# Repeat/MirrorRepeat seam pixel oracle on real Metal GPU-encoded ICB.
# Remains OFF for ordinary CTest and --metal acceptance; configure in a
# dedicated build cache because GPU ICB driver faults are not recoverable.
option(AIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS
    "Opt-in real Renderer3D graph VT missing-page and Repeat/MirrorRepeat Metal GPU ICB pixel oracle" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos"
   AND AIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS)
    add_executable(aiverse_metal_renderer_graph_vt_seam_gpu_test
        tests/render/metal_renderer_graph_vt_seam_gpu.cpp
        $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_metal_renderer_graph_vt_seam_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_metal_renderer_graph_vt_seam_gpu_test PRIVATE
        aiverse_runtime aiverse_shader_graph Threads::Threads)
    add_test(NAME metal_renderer_graph_vt_seam_gpu
        COMMAND aiverse_metal_renderer_graph_vt_seam_gpu_test)
    set_tests_properties(metal_renderer_graph_vt_seam_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI;EXPERIMENTAL_ICB;RENDERER3D;SHADER_GRAPH;VIRTUAL_TEXTURE"
        TIMEOUT 120
        ENVIRONMENT "AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_ENCODED_ICB=1;AIVERSE_GPU_AUTHORED_DRAWS=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1;AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1;AIVERSE_GPU_ICB_ADAPTIVE=0;AIVERSE_ADAPTIVE_DRAW_GROUPS=0;AIVERSE_GPU_ICB_MIN_COMMANDS=2;AIVERSE_ICB_TRACE=1")
endif()


# Canonical Metal VT bilinear/gutter correctness against an independent
# original-logical-texel CPU oracle. Separate from GPU-authored ICB fixtures:
# this gate checks shader sampling math before high-level renderer parity.
# No default GPU/driver test should silently run this additional workload.
option(AIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS
    "Opt-in native Metal canonical VT nonuniform bilinear/gutter CPU pixel oracle" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos"
   AND AIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS)
    add_executable(aiverse_metal_vt_bilinear_gpu_test
        tests/render/metal_vt_bilinear_gpu.mm
        engine/gfx/metal/GfxMetal.mm)
    target_include_directories(aiverse_metal_vt_bilinear_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_source_files_properties(tests/render/metal_vt_bilinear_gpu.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(aiverse_metal_vt_bilinear_gpu_test PRIVATE
        Threads::Threads
        "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    add_test(NAME metal_vt_bilinear_gpu COMMAND aiverse_metal_vt_bilinear_gpu_test)
    set_tests_properties(metal_vt_bilinear_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI;VIRTUAL_TEXTURE;BILINEAR_ORACLE" TIMEOUT 120)
endif()


# Full production Renderer3D compiled graph + disk-backed nonuniform VT
# gutters through heterogeneous Metal GPU-authored ICB, including a
# separate original-source CPU seam oracle. Isolated opt-in test process.
option(AIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS
    "Opt-in production graph plus nonuniform VT gutters and mixed Metal GPU ICB scene pixel oracle" OFF)
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos"
   AND AIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS)
    add_executable(aiverse_metal_renderer_graph_vt_gutter_gpu_test
        tests/render/metal_renderer_graph_vt_gutter_gpu.cpp
        $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_metal_renderer_graph_vt_gutter_gpu_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_metal_renderer_graph_vt_gutter_gpu_test PRIVATE
        aiverse_runtime aiverse_shader_graph Threads::Threads)
    add_test(NAME metal_renderer_graph_vt_gutter_gpu
        COMMAND aiverse_metal_renderer_graph_vt_gutter_gpu_test)
    set_tests_properties(metal_renderer_graph_vt_gutter_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI;EXPERIMENTAL_ICB;RENDERER3D;SHADER_GRAPH;VIRTUAL_TEXTURE;BILINEAR_ORACLE"
        PASS_REGULAR_EXPRESSION "PASS: production Renderer3D Shader Graph nonuniform guttered disk VT mixed Metal GPU ICB seam parity"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        TIMEOUT 120
        ENVIRONMENT "AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_ENCODED_ICB=1;AIVERSE_GPU_AUTHORED_DRAWS=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1;AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1;AIVERSE_GPU_ICB_ADAPTIVE=0;AIVERSE_ADAPTIVE_DRAW_GROUPS=0;AIVERSE_GPU_ICB_MIN_COMMANDS=2;AIVERSE_ICB_TRACE=1")
    # Indexed VT currently requires the safe direct fallback even with ICB enabled.
    add_test(NAME metal_renderer_indexed_vt_gpu
        COMMAND aiverse_metal_renderer_graph_vt_gutter_gpu_test)
    set_tests_properties(metal_renderer_indexed_vt_gpu PROPERTIES
        LABELS "METAL;GPU;TEXTURE_RHI;EXPERIMENTAL_ICB;RENDERER3D;SHADER_GRAPH;VIRTUAL_TEXTURE;BILINEAR_ORACLE"
        PASS_REGULAR_EXPRESSION "PASS: production Renderer3D indexed VT nonuniform direct and ICB fallback parity"
        FAIL_REGULAR_EXPRESSION "FAIL:"
        TIMEOUT 120
        ENVIRONMENT "AIVERSE_TEST_INDEXED_VT=1;AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_ENCODED_ICB=1;AIVERSE_GPU_AUTHORED_DRAWS=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1;AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1;AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1;AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1;AIVERSE_GPU_ICB_ADAPTIVE=0;AIVERSE_ADAPTIVE_DRAW_GROUPS=0;AIVERSE_GPU_ICB_MIN_COMMANDS=2;AIVERSE_ICB_TRACE=1")
endif()


# The macOS application delegate can exit the process without returning
# the test fixture's aiverseMain() status. Never mark a real native GPU
# acceptance PASS when its log contains a failed runtime assertion.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    foreach(_texture_gpu_ctest IN ITEMS
        metal_texture_gpu
        metal_texture_icb_graph_gpu
        metal_renderer_graph_icb_gpu
        metal_renderer_graph_vt_icb_gpu
        metal_renderer_graph_vt_seam_gpu
        metal_vt_bilinear_gpu
        metal_renderer_graph_vt_gutter_gpu
        metal_renderer_indexed_vt_gpu)
        if(TEST ${_texture_gpu_ctest})
            set_tests_properties(${_texture_gpu_ctest} PROPERTIES
                FAIL_REGULAR_EXPRESSION "FAIL:" RUN_SERIAL TRUE)
        endif()
    endforeach()
endif()

# Enforce the SpectraFX core/consumer direction independently of FPS/RTS/Editor.
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME spectrafx_architecture
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectrafx/check_boundaries.py)
    set_tests_properties(spectrafx_architecture PROPERTIES LABELS "SpectraFX;Architecture")
endif()

# Genre-neutral Niagara-style graph compiler: headless CPU fallback contract.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_graph_tests tests/vfx_graph_compiler_test.cpp)
    target_include_directories(aiverse_vfx_graph_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_vfx_graph_tests PRIVATE aiverse_fx)
    add_test(NAME vfx_graph_compiler COMMAND aiverse_vfx_graph_tests)
    set_tests_properties(vfx_graph_compiler PROPERTIES LABELS "SpectraFX")
endif()

# Universal VFX ECS presenter is read-only and independent of genre GameModules.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_ecs_tests tests/vfx_ecs_presentation_test.cpp)
    target_include_directories(aiverse_vfx_ecs_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/libraries/include)
    target_link_libraries(aiverse_vfx_ecs_tests PRIVATE aiverse_fx aiverse_engine)
    add_test(NAME vfx_ecs_presentation COMMAND aiverse_vfx_ecs_tests)
    set_tests_properties(vfx_ecs_presentation PROPERTIES LABELS "SpectraFX")
endif()

# Standalone SpectraFX API compile smoke and GPU-free authored contract checks.
# Keep these separate from the large monolithic runtime tests: missing C++
# symbols/API mismatches in new inline headers must fail immediately.
if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP)
    add_library(aiverse_spectrafx_header_smoke OBJECT tests/spectrafx_compile_smoke.cpp)
    target_include_directories(aiverse_spectrafx_header_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_spectrafx_header_smoke PRIVATE aiverse_runtime aiverse_fx)
    add_executable(aiverse_spectrafx_contract_tests tests/spectrafx_contract_test.cpp)
    target_include_directories(aiverse_spectrafx_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_spectrafx_contract_tests PRIVATE aiverse_fx)
    add_test(NAME spectrafx_contract COMMAND aiverse_spectrafx_contract_tests)
    set_tests_properties(spectrafx_contract PROPERTIES LABELS "SpectraFX")
endif()

# Actual Apple Metal GPU compute acceptance. Unlike CPU-only contract tests,
# this compiles MSL at runtime and dispatches the same evolution, depth-sort
# and compaction shaders used by HybridPlayback, then reads the GPU results.
# A runner without an available Metal device reports SKIPPED (exit 77).
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_spectrafx_metal_gpu_test tests/spectrafx_metal_gpu.mm)
    target_include_directories(aiverse_spectrafx_metal_gpu_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_spectrafx_metal_gpu_test PRIVATE
        "-framework Metal" "-framework Foundation")
    add_test(NAME spectrafx_metal_gpu COMMAND aiverse_spectrafx_metal_gpu_test)
    set_tests_properties(spectrafx_metal_gpu PROPERTIES
        LABELS "SpectraFX;MetalGPU"
        SKIP_RETURN_CODE 77)
endif()

# Metal rasterization acceptance for the exact SpectraFX billboard MSL:
# validates source-instance lookup and alpha/additive pixel output offscreen.
# Runs independently of the previously passing compute-only MSL test.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_spectrafx_metal_billboard_test
        tests/spectrafx_metal_billboard.mm)
    target_include_directories(aiverse_spectrafx_metal_billboard_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_spectrafx_metal_billboard_test PRIVATE
        "-framework Metal" "-framework Foundation")
    add_test(NAME spectrafx_metal_billboard
        COMMAND aiverse_spectrafx_metal_billboard_test)
    set_tests_properties(spectrafx_metal_billboard PROPERTIES
        LABELS "SpectraFX;MetalGPU"
        SKIP_RETURN_CODE 77)
endif()

# Metal evidence parser guards against false-green suites with skipped
# hardware tests. Standard-library-only; runs on any host with Python.
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME spectrafx_metal_evidence_parser
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectrafx/test_verify_metal_ctest.py)
    set_tests_properties(spectrafx_metal_evidence_parser PROPERTIES
        LABELS "SpectraFX;Architecture")
endif()

# Portable visual evidence review of seven real P6 screenshots.
# Runs hermetic synthetic fixtures; actual screenshots are analyzed separately.
if(AIVERSE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME spectrafx_visual_review_parser
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spectrafx/test_review_frames.py)
    set_tests_properties(spectrafx_visual_review_parser PROPERTIES
        LABELS "SpectraFX;TOOLS")
endif()

# Production Metal RHI acceptance: two independent offscreen camera views,
# real compute->gfx::Device draw, RGBA pixel readback and 3-in-flight resource churn.
# Explicitly compiles the production Metal backend without linking PlatformMac's main().
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_spectrafx_metal_rhi_test
        tests/spectrafx_metal_rhi.mm engine/gfx/metal/GfxMetal.mm)
    target_include_directories(aiverse_spectrafx_metal_rhi_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(aiverse_spectrafx_metal_rhi_test PRIVATE "-fobjc-arc")
    target_link_libraries(aiverse_spectrafx_metal_rhi_test PRIVATE
        "-framework Metal" "-framework Foundation"
        "-framework QuartzCore")
    add_test(NAME spectrafx_metal_rhi COMMAND aiverse_spectrafx_metal_rhi_test)
    set_tests_properties(spectrafx_metal_rhi PROPERTIES
        LABELS "SpectraFX;MetalGPU;RHI"
        SKIP_RETURN_CODE 77)
endif()

# Pure native Editor Preview controls: pause/step, invalid JSON and failed
# revision retries. No ImGui, platform or Metal backend needed for this gate.
if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP)
    add_executable(aiverse_spectrafx_preview_policy_tests
        tests/spectrafx_preview_policy.cpp)
    target_include_directories(aiverse_spectrafx_preview_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME spectrafx_preview_policy
        COMMAND aiverse_spectrafx_preview_policy_tests)
    set_tests_properties(spectrafx_preview_policy PROPERTIES
        LABELS "SpectraFX;EDITOR;Architecture")
endif()

# Real HybridPlayback retirement policy with a controlled Metal-capable RHI.
# This checks slot/fence ownership and reset semantics, not hardware shaders.
if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP)
    add_executable(aiverse_spectrafx_retirement_tests
        tests/spectrafx_retirement_test.cpp)
    target_include_directories(aiverse_spectrafx_retirement_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_spectrafx_retirement_tests PRIVATE aiverse_fx)
    add_test(NAME spectrafx_retirement COMMAND aiverse_spectrafx_retirement_tests)
    set_tests_properties(spectrafx_retirement PROPERTIES
        LABELS "SpectraFX;Architecture")
endif()

# Real Renderer3D + SpectraFxSceneHost offscreen scene acceptance. The
# platform OBJECT target supplies the production Metal backend and main();
# the fixture provides aiverseMain and creates its own tiny CAMetalLayer.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_spectrafx_metal_scene_test
        tests/spectrafx_metal_scene.mm $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_spectrafx_metal_scene_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(aiverse_spectrafx_metal_scene_test PRIVATE "-fobjc-arc")
    target_link_libraries(aiverse_spectrafx_metal_scene_test PRIVATE
        aiverse_runtime aiverse_fx aiverse_render2d)
    add_test(NAME spectrafx_metal_scene
        COMMAND aiverse_spectrafx_metal_scene_test)
    set_tests_properties(spectrafx_metal_scene PROPERTIES
        LABELS "SpectraFX;MetalGPU;RHI;Renderer3D"
        SKIP_RETURN_CODE 77)
endif()

# Data-driven entity effects shared by every host (table + presenter).
if(AIVERSE_BUILD_TESTS AND AIVERSE_DESKTOP)
    add_executable(aiverse_spectrafx_entity_effects_tests
        tests/spectrafx_entity_effects_test.cpp)
    target_include_directories(aiverse_spectrafx_entity_effects_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_spectrafx_entity_effects_tests PRIVATE aiverse_fx)
    add_test(NAME spectrafx_entity_effects COMMAND aiverse_spectrafx_entity_effects_tests)
    set_tests_properties(spectrafx_entity_effects PROPERTIES LABELS "SpectraFX")
endif()

# CPU/GPU linkage: one asset on the Metal compute kernel and on its CPU twin
# (kernel state parity, emitter image parity, particles/ms perf print).
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_spectrafx_cpu_gpu_parity_test
        tests/spectrafx_cpu_gpu_parity.mm $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_spectrafx_cpu_gpu_parity_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(aiverse_spectrafx_cpu_gpu_parity_test PRIVATE "-fobjc-arc")
    target_link_libraries(aiverse_spectrafx_cpu_gpu_parity_test PRIVATE
        aiverse_runtime aiverse_fx aiverse_render2d)
    add_test(NAME spectrafx_cpu_gpu_parity
        COMMAND aiverse_spectrafx_cpu_gpu_parity_test)
    set_tests_properties(spectrafx_cpu_gpu_parity PROPERTIES
        LABELS "SpectraFX;MetalGPU;RHI"
        SKIP_RETURN_CODE 77)
endif()

# Real TerraWeave forest render acceptance on production Apple Metal RHI.
# Unlike CPU Arbor/Flora tests, this exercises Renderer3D with actual uploaded
# BranchLOD and alpha-masked eight-view impostor models and GPU readback.
# A missing Metal device is a failure; never silently green via CTest SKIP.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_terraweave_metal_forest_test
        tests/terraweave_metal_forest.mm $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_terraweave_metal_forest_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(aiverse_terraweave_metal_forest_test PRIVATE "-fobjc-arc")
    target_link_libraries(aiverse_terraweave_metal_forest_test PRIVATE
        aiverse_runtime aiverse_models aiverse_world aiverse_zstd_dec
        aiverse_render2d)
    add_test(NAME terraweave_metal_forest
        COMMAND aiverse_terraweave_metal_forest_test)
    set_tests_properties(terraweave_metal_forest PROPERTIES
        LABELS "TERRAWEAVE;MetalGPU;RHI;Renderer3D")
endif()

# Linux forest parity gate: exercise the REAL Vulkan offscreen and GLES3
# EGL-pbuffer RHI, not just Vulkan shader translation or CMake compilation.
# These use the same production Renderer3D and ArborForestAdapter as Metal.
# Each test refuses unavailable backend/driver instead of reporting SKIPPED.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "linux" AND AIVERSE_VULKAN)
    add_executable(aiverse_terraweave_linux_forest_backends
        tests/terraweave_linux_forest_backends.cpp
        $<TARGET_OBJECTS:aiverse_runtime>)
    target_include_directories(aiverse_terraweave_linux_forest_backends
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_terraweave_linux_forest_backends PRIVATE
        aiverse_runtime aiverse_models aiverse_world aiverse_zstd_dec
        aiverse_render2d EGL GLESv2)
    add_test(NAME terraweave_vulkan_forest
        COMMAND aiverse_terraweave_linux_forest_backends vulkan)
    add_test(NAME terraweave_gles3_forest
        COMMAND aiverse_terraweave_linux_forest_backends gles3)
    set_tests_properties(terraweave_vulkan_forest terraweave_gles3_forest
        PROPERTIES LABELS "TERRAWEAVE;Renderer3D;RHI;LinuxGPU")
endif()

# Versioned VFX graph assets, CPU fallback, exposed parameters and live reload.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_assets_tests tests/vfx_graph_assets_test.cpp)
    target_include_directories(aiverse_vfx_assets_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_vfx_assets_tests PRIVATE aiverse_fx)
    add_test(NAME vfx_graph_assets COMMAND aiverse_vfx_assets_tests)
    set_tests_properties(vfx_graph_assets PROPERTIES LABELS "SpectraFX")
endif()

# LENS Stage 20: exercise SpriteBatch partial GPU initialization and
# failed/retried idle barriers with a fault-injecting CPU-only gfx::Device.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_lens_sprite_gpu_faults tests/render/lens_sprite_gpu_fault_injection.cpp)
    target_include_directories(aiverse_lens_sprite_gpu_faults PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_lens_sprite_gpu_faults PRIVATE aiverse_runtime aiverse_game_view aiverse_content_reader)
    add_test(NAME lens_sprite_gpu_faults COMMAND aiverse_lens_sprite_gpu_faults)
    set_tests_properties(lens_sprite_gpu_faults PROPERTIES LABELS "LENS;RHI")
    if(AIVERSE_PLATFORM STREQUAL "macos")
        add_executable(aiverse_lens_metal_frame tests/render/lens_metal_frame.cpp)
        target_include_directories(aiverse_lens_metal_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(aiverse_lens_metal_frame PRIVATE aiverse_runtime aiverse_game_view aiverse_content_reader)
        add_test(NAME lens_metal_frame COMMAND aiverse_lens_metal_frame)
        set_tests_properties(lens_metal_frame PROPERTIES LABELS "LENS;GPU;Metal" SKIP_RETURN_CODE 77 TIMEOUT 90)
    endif()
endif()

# SpectraFX graph CRUD is shared by the GUI Workspace and standalone MCP server.
# It is deliberately independent of RTS/FPS and reuses the CPU EffectDef compiler.
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/SpectraGraphOps.cpp)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/SpectraFunctionOps.cpp)
endif()
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_editor_core)
    add_executable(aiverse_vfx_ops_tests tests/vfx_graph_ops_test.cpp)
    target_include_directories(aiverse_vfx_ops_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_vfx_ops_tests PRIVATE aiverse_editor_core aiverse_fx)
    add_test(NAME vfx_graph_ops COMMAND aiverse_vfx_ops_tests)
    set_tests_properties(vfx_graph_ops PROPERTIES LABELS "SpectraFX")
endif()

# Stage 44 — pure C++20 SPFX-V2 checkpoint/ZIP/privacy diagnostics, no GPU.
# Keep this test runnable on headless Linux and macOS without Metal hardware.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_spfx_diagnostics_test
        tests/vfx_spfx_diagnostics_test.cpp)
    target_include_directories(aiverse_vfx_spfx_diagnostics_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    aiverse_keep_test_asserts(aiverse_vfx_spfx_diagnostics_test)
    add_test(NAME vfx_spfx_diagnostics
        COMMAND aiverse_vfx_spfx_diagnostics_test)
    set_tests_properties(vfx_spfx_diagnostics
        PROPERTIES LABELS "SpectraFX")
endif()

# SpectraFX GPU backend policy and legacy CPU fallback must be testable headlessly.
# A separate Metal device smoke test is still required for actual compute/draw QA.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_gpu_tests tests/vfx_gpu_backend_test.cpp)
    target_include_directories(aiverse_vfx_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_vfx_gpu_tests PRIVATE aiverse_fx)
    add_test(NAME vfx_gpu_backend COMMAND aiverse_vfx_gpu_tests)
    set_tests_properties(vfx_gpu_backend PROPERTIES LABELS "SpectraFX")
endif()

# PHYSIS authoritative environment simulation is independent of all genre modules.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_core_test tests/physis_core_test.cpp)
    target_link_libraries(aiverse_physis_core_test PRIVATE aiverse_engine)
    add_test(NAME physis_core COMMAND aiverse_physis_core_test)
    set_tests_properties(physis_core PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 2: independent climate zones and weather fronts.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_climate_test tests/physis_climate_test.cpp)
    target_link_libraries(aiverse_physis_climate_test PRIVATE aiverse_engine)
    add_test(NAME physis_climate COMMAND aiverse_physis_climate_test)
    set_tests_properties(physis_climate PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 3: material-specific accumulation / read-only environmental responses.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_environment_test tests/physis_environment_test.cpp)
    target_link_libraries(aiverse_physis_environment_test PRIVATE aiverse_engine)
    add_test(NAME physis_environment COMMAND aiverse_physis_environment_test)
    set_tests_properties(physis_environment PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 4: deterministic disaster overlays, no game genre dependencies.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_disasters_test tests/physis_disasters_test.cpp)
    target_link_libraries(aiverse_physis_disasters_test PRIVATE aiverse_engine)
    add_test(NAME physis_disasters COMMAND aiverse_physis_disasters_test)
    set_tests_properties(physis_disasters PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 5: generic Simulation tick, Archive persistence, headless view snapshots.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_integration_test tests/physis_integration_test.cpp)
    target_link_libraries(aiverse_physis_integration_test PRIVATE aiverse_engine)
    add_test(NAME physis_integration COMMAND aiverse_physis_integration_test)
    set_tests_properties(physis_integration PROPERTIES LABELS "PHYSIS")
    # Stage 78: PHYSIS per-entity nature contacts and NEREID canonical
    # water samples coexist on one genre-independent authoritative World.
    add_executable(aiverse_physis_nereid_contacts_test
        tests/physis_nereid_nature_contacts_test.cpp)
    target_link_libraries(aiverse_physis_nereid_contacts_test PRIVATE
        aiverse_engine AIVERSE::lib_nereid_water)
    add_test(NAME physis_nereid_contacts COMMAND
        aiverse_physis_nereid_contacts_test)
    set_tests_properties(physis_nereid_contacts PROPERTIES LABELS
        "PHYSIS;NEREID;ECS")
    # Stage 79: structural load/fracture exposure reuses PHYSIS force output
    # and nature contacts, without runtime Box3D/TerraWeave dependency.
    add_executable(aiverse_physis_nature_stress_test
        tests/physis_nature_stress_test.cpp)
    target_link_libraries(aiverse_physis_nature_stress_test PRIVATE aiverse_engine)
    add_test(NAME physis_nature_stress COMMAND aiverse_physis_nature_stress_test)
    set_tests_properties(physis_nature_stress PROPERTIES LABELS "PHYSIS;ECS")
    # Stage 82: durable, deduplicated host fracture event handoff.
    # PHYSIS never owns TerraWeave/Box3D physics bodies.
    add_executable(aiverse_physis_fracture_outbox_test
        tests/physis_fracture_outbox_test.cpp)
    target_link_libraries(aiverse_physis_fracture_outbox_test PRIVATE aiverse_engine)
    add_test(NAME physis_fracture_outbox COMMAND aiverse_physis_fracture_outbox_test)
    set_tests_properties(physis_fracture_outbox PROPERTIES LABELS "PHYSIS;ECS;PERSISTENCE")
    # Stage 87: host-owned root binding and fall ledger persist independently
    # of the TerraWeave branch. The REAL Box3D bridge is optional until Terra
    # headers have been integrated by the respective owning agent.
    add_executable(aiverse_physis_nature_fall_host_test
        tests/physis_nature_fall_host_test.cpp)
    target_link_libraries(aiverse_physis_nature_fall_host_test PRIVATE aiverse_engine)
    add_test(NAME physis_nature_fall_host COMMAND aiverse_physis_nature_fall_host_test)
    set_tests_properties(physis_nature_fall_host PROPERTIES
        LABELS "PHYSIS;ECS;PERSISTENCE")
endif()

# Stage 87 real cross-system test is deliberately opt-in only once the
# separate TerraWeave branch has been integrated into the SAME checkout.
# PHYSIS-only branches must neither copy TerraWeave's source nor build a stub
# claiming to test the actual Box3D falling plant implementation.
if(AIVERSE_BUILD_TESTS AND AIVERSE_ENABLE_PHYSICS AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engine/world/TerraWeaveRootEvents.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engine/world/TerraWeaveFallenPhysics.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engine/world/TerraWeaveEcsAuthority.h" AND
   TARGET aiverse_physics)
    add_executable(aiverse_physis_terraweave_fall_bridge_test
        tests/physis_terraweave_fall_bridge_test.cpp)
    # TerraWeaveEcsAuthority.h transitively reads the NEREID shoreline
    # policy through TerraWeaveNereidSoilBridge.h. AIVERSE::lib_nereid_water
    # exports libraries/include for "engine/world/nereid/NereidErosion.h".
    # Do not compensate with a global compiler include path.
    target_link_libraries(aiverse_physis_terraweave_fall_bridge_test PRIVATE
        aiverse_engine aiverse_physics aiverse_models aiverse_zstd_dec
        AIVERSE::lib_nereid_water)
    add_test(NAME physis_terraweave_fall_bridge
        COMMAND aiverse_physis_terraweave_fall_bridge_test)
    set_tests_properties(physis_terraweave_fall_bridge PROPERTIES
        LABELS "PHYSIS;TERRAWEAVE;PHYSICS;ECS")
endif()

# PHYSIS Stage 8: deterministic spatial wind and seeded gust volumes.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_wind_test tests/physis_wind_test.cpp)
    target_link_libraries(aiverse_physis_wind_test PRIVATE aiverse_engine)
    add_test(NAME physis_wind COMMAND aiverse_physis_wind_test)
    set_tests_properties(physis_wind PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 9: composable fixed-point gravity policy, independent of Box3D.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_gravity_test tests/physis_gravity_test.cpp)
    target_link_libraries(aiverse_physis_gravity_test PRIVATE aiverse_engine)
    add_test(NAME physis_gravity COMMAND aiverse_physis_gravity_test)
    set_tests_properties(physis_gravity PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 10: ECS hurricane/tornado forcing, moving tracks and weather coupling.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_storm_test tests/physis_storm_test.cpp)
    target_link_libraries(aiverse_physis_storm_test PRIVATE aiverse_engine)
    add_test(NAME physis_storm COMMAND aiverse_physis_storm_test)
    set_tests_properties(physis_storm PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 11: CPU ECS force outputs and optional Box3D Scene integration.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_forces_test tests/physis_forces_test.cpp)
    target_link_libraries(aiverse_physis_forces_test PRIVATE aiverse_engine)
    add_test(NAME physis_forces COMMAND aiverse_physis_forces_test)
    set_tests_properties(physis_forces PROPERTIES LABELS "PHYSIS")
    if(AIVERSE_ENABLE_PHYSICS)
        add_executable(aiverse_physis_box3d_test tests/physis_box3d_test.cpp)
        target_link_libraries(aiverse_physis_box3d_test PRIVATE AIVERSE::physics)
        add_test(NAME physis_box3d COMMAND aiverse_physis_box3d_test)
        set_tests_properties(physis_box3d PROPERTIES LABELS "PHYSIS;PHYSICS")
    endif()
endif()

# PHYSIS Stage 13: unified immutable NatureFieldSample for native consumers.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_presentation_test tests/physis_presentation_test.cpp)
    target_link_libraries(aiverse_physis_presentation_test PRIVATE aiverse_engine)
    add_test(NAME physis_presentation COMMAND aiverse_physis_presentation_test)
    set_tests_properties(physis_presentation PROPERTIES LABELS "PHYSIS")
    add_executable(aiverse_physis_nature_presentation_test tests/physis_nature_presentation_test.cpp)
    target_link_libraries(aiverse_physis_nature_presentation_test PRIVATE aiverse_engine)
    add_test(NAME physis_nature_presentation COMMAND aiverse_physis_nature_presentation_test)
    set_tests_properties(physis_nature_presentation PROPERTIES LABELS "PHYSIS")
    # Parallel grid passes: bit-identical to serial for every lane count;
    # `aiverse_physis_parallel_test bench 1024 10` prints the speedup.
    add_executable(aiverse_physis_parallel_test tests/physis_parallel_test.cpp)
    target_link_libraries(aiverse_physis_parallel_test PRIVATE aiverse_engine)
    add_test(NAME physis_parallel COMMAND aiverse_physis_parallel_test)
    set_tests_properties(physis_parallel PROPERTIES LABELS "PHYSIS")
    add_executable(aiverse_physis_streaming_test tests/physis_streaming_test.cpp)
    target_link_libraries(aiverse_physis_streaming_test PRIVATE aiverse_engine)
    add_test(NAME physis_streaming COMMAND aiverse_physis_streaming_test)
    set_tests_properties(physis_streaming PROPERTIES LABELS "PHYSIS;PERSISTENCE")
    if(AIVERSE_SIM_PHYSIS)
        # Map-data install for any GameModule + presentation config/wind adapters.
        add_executable(aiverse_physis_nature_runtime_test tests/physis_nature_runtime_test.cpp)
        target_link_libraries(aiverse_physis_nature_runtime_test PRIVATE aiverse_engine)
        add_test(NAME physis_nature_runtime COMMAND aiverse_physis_nature_runtime_test)
        set_tests_properties(physis_nature_runtime PROPERTIES LABELS "PHYSIS;TERRAWEAVE;NEREID")
        # Tornadoes: path, life, wind, fire, debris, carried objects, host damage,
        # map install, save/load and checksums (engine/physis/PhysisTornado.h).
        add_executable(aiverse_physis_tornado_test tests/physis_tornado_test.cpp)
        target_link_libraries(aiverse_physis_tornado_test PRIVATE aiverse_engine)
        add_test(NAME physis_tornado COMMAND aiverse_physis_tornado_test)
        set_tests_properties(physis_tornado PROPERTIES LABELS "PHYSIS;PERSISTENCE")
        # Fire whirl, water / waterspouts, ground scour (PhysisTornadoNature.h).
        add_executable(aiverse_physis_firestorm_test tests/physis_firestorm_test.cpp)
        target_link_libraries(aiverse_physis_firestorm_test PRIVATE aiverse_engine)
        add_test(NAME physis_firestorm COMMAND aiverse_physis_firestorm_test)
        set_tests_properties(physis_firestorm PROPERTIES LABELS "PHYSIS;PERSISTENCE")
    endif()
    if(TARGET aiverse_fx AND TARGET aiverse_audio AND TARGET aiverse_models)
        # PHYSIS -> SpectraFX / audio planners and TerraWeave grass overlay
        # (CPU only; no device, no audio hardware).
        add_executable(aiverse_physis_nature_cues_test tests/physis_nature_cues_test.cpp)
        target_link_libraries(aiverse_physis_nature_cues_test PRIVATE
            aiverse_engine aiverse_fx aiverse_audio aiverse_models aiverse_zstd_dec)
        add_test(NAME physis_nature_cues COMMAND aiverse_physis_nature_cues_test)
        set_tests_properties(physis_nature_cues PROPERTIES LABELS "PHYSIS;TERRAWEAVE;VFX;AUDIO")
        add_executable(aiverse_physis_tornado_cues_test tests/physis_tornado_cues_test.cpp)
        target_link_libraries(aiverse_physis_tornado_cues_test PRIVATE
            aiverse_engine aiverse_fx aiverse_audio aiverse_models aiverse_zstd_dec)
        add_test(NAME physis_tornado_cues COMMAND aiverse_physis_tornado_cues_test)
        set_tests_properties(physis_tornado_cues PROPERTIES LABELS "PHYSIS;VFX;AUDIO")
    endif()
endif()

# PHYSIS Stage 14: genre-neutral read-only diagnostics for editor/host overlays.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_diagnostics_test tests/physis_diagnostics_test.cpp)
    target_link_libraries(aiverse_physis_diagnostics_test PRIVATE aiverse_engine)
    add_test(NAME physis_diagnostics COMMAND aiverse_physis_diagnostics_test)
    set_tests_properties(physis_diagnostics PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 16: transient snapshot-backed spatial broadphase, bit-exact parity.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_spatial_index_test tests/physis_spatial_index_test.cpp)
    target_link_libraries(aiverse_physis_spatial_index_test PRIVATE aiverse_engine)
    add_test(NAME physis_spatial_index COMMAND aiverse_physis_spatial_index_test)
    set_tests_properties(physis_spatial_index PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 17: renderer-independent heatmap palette and editor preview fixture.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_editor_view_test tests/physis_editor_view_test.cpp)
    target_link_libraries(aiverse_physis_editor_view_test PRIVATE aiverse_engine)
    add_test(NAME physis_editor_view COMMAND aiverse_physis_editor_view_test)
    set_tests_properties(physis_editor_view PROPERTIES LABELS "PHYSIS")
endif()

# PHYSIS Stage 18: projected debug geometry and atomic map-document Inspector
# authoring. The authoring test needs the existing editor_core/MapDoc library;
# geometry remains engine-only and is built in all headless PHYSIS test jobs.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_scene_geometry_test tests/physis_scene_geometry_test.cpp)
    target_link_libraries(aiverse_physis_scene_geometry_test PRIVATE aiverse_engine)
    add_test(NAME physis_scene_geometry COMMAND aiverse_physis_scene_geometry_test)
    set_tests_properties(physis_scene_geometry PROPERTIES LABELS "PHYSIS")
    add_executable(aiverse_physis_project_config_test tests/physis_project_config_test.cpp)
    target_link_libraries(aiverse_physis_project_config_test PRIVATE aiverse_engine)
    add_test(NAME physis_project_config COMMAND aiverse_physis_project_config_test)
    set_tests_properties(physis_project_config PROPERTIES LABELS "PHYSIS")

    if(TARGET aiverse_editor_core)
        add_executable(aiverse_physis_authoring_test tests/physis_authoring_test.cpp)
        target_link_libraries(aiverse_physis_authoring_test PRIVATE aiverse_editor_core)
        add_test(NAME physis_authoring COMMAND aiverse_physis_authoring_test)
        set_tests_properties(physis_authoring PROPERTIES LABELS "PHYSIS;EDITOR")
        if(TARGET aiverse_fx AND TARGET aiverse_audio AND TARGET aiverse_models)
            # Editor «Живая природа»: private preview World, ignition, live read-only.
            add_executable(aiverse_physis_nature_preview_test tests/physis_nature_preview_test.cpp)
            target_link_libraries(aiverse_physis_nature_preview_test PRIVATE
                aiverse_editor_core aiverse_fx aiverse_audio aiverse_models aiverse_zstd_dec)
            add_test(NAME physis_nature_preview COMMAND aiverse_physis_nature_preview_test)
            set_tests_properties(physis_nature_preview PROPERTIES LABELS "PHYSIS;EDITOR;VFX")
        endif()
        if(TARGET aiversemcp AND Python3_Interpreter_FOUND)
            add_test(NAME physis_mcp COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/physis_mcp_smoke.py
                $<TARGET_FILE:aiversemcp> ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(physis_mcp PROPERTIES LABELS "PHYSIS;EDITOR;MCP" TIMEOUT 30)
        endif()
    endif()
endif()

# PHYSIS Stage 49: independent POSIX journal path/fd regression.
# Deliberately compiles without ECS/net/Box3D to prove the low-level I/O ABI.
if(AIVERSE_BUILD_TESTS AND UNIX AND NOT EMSCRIPTEN)
    add_executable(aiverse_physis_journal_posix_io_test
        tests/physis_journal_posix_io_test.cpp)
    target_include_directories(aiverse_physis_journal_posix_io_test
        PRIVATE "${CMAKE_SOURCE_DIR}")
    add_test(NAME physis_journal_posix_io
        COMMAND aiverse_physis_journal_posix_io_test)
    set_tests_properties(physis_journal_posix_io
        PROPERTIES LABELS "PHYSIS;PERSISTENCE;SECURITY")
    # Stage 54: exercise the real lock + pinned I/O headers together,
    # without linking ECS, networking or the editor.
    add_executable(aiverse_physis_journal_locked_io_test
        tests/physis_journal_locked_io_test.cpp)
    target_include_directories(aiverse_physis_journal_locked_io_test
        PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_features(aiverse_physis_journal_locked_io_test
        PRIVATE cxx_std_20)
    add_test(NAME physis_journal_locked_io
        COMMAND aiverse_physis_journal_locked_io_test)
    set_tests_properties(physis_journal_locked_io
        PROPERTIES LABELS "PHYSIS;PERSISTENCE;SECURITY")
    # Stage 59: real fork/SIGKILL crash recovery with the pinned journal.
    add_executable(aiverse_physis_journal_crash_recovery_test
        tests/physis_journal_crash_recovery_test.cpp)
    target_include_directories(aiverse_physis_journal_crash_recovery_test
        PRIVATE "${CMAKE_SOURCE_DIR}")
    target_compile_features(aiverse_physis_journal_crash_recovery_test
        PRIVATE cxx_std_20)
    add_test(NAME physis_journal_crash_recovery
        COMMAND aiverse_physis_journal_crash_recovery_test)
    set_tests_properties(physis_journal_crash_recovery
        PROPERTIES LABELS "PHYSIS;PERSISTENCE;SECURITY")
endif()

# PHYSIS Stage 19: deterministic authored field assets and editor Move gizmo
# transaction contracts. No genre module dependency; authoring uses MapDoc.
if(AIVERSE_BUILD_TESTS)
    # Stage 23: native field registry and tick-boundary command reconciliation.
    # Stage 24: editor command gateway and authenticated server ingress.
    # Stage 25: remote edit protocol over existing reliable net::Conn.
    # Stage 26: native loopback admin endpoint and durable journal checks.
    add_executable(aiverse_physis_admin_journal_test tests/physis_admin_journal_test.cpp)
    target_link_libraries(aiverse_physis_admin_journal_test PRIVATE aiverse_net)
    add_test(NAME physis_admin_journal COMMAND aiverse_physis_admin_journal_test)
    set_tests_properties(physis_admin_journal PROPERTIES LABELS "PHYSIS;NETWORK;PERSISTENCE")
    add_executable(aiverse_physis_remote_edit_test tests/physis_remote_edit_test.cpp)
    target_link_libraries(aiverse_physis_remote_edit_test PRIVATE aiverse_net)
    add_test(NAME physis_remote_edit COMMAND aiverse_physis_remote_edit_test)
    set_tests_properties(physis_remote_edit PROPERTIES LABELS "PHYSIS;NETWORK")
    add_executable(aiverse_physis_live_edit_test tests/physis_live_edit_test.cpp)
    target_link_libraries(aiverse_physis_live_edit_test PRIVATE aiverse_engine)
    add_test(NAME physis_live_edit COMMAND aiverse_physis_live_edit_test)
    set_tests_properties(physis_live_edit PROPERTIES LABELS "PHYSIS;EDITOR")
    add_executable(aiverse_physis_field_runtime_test tests/physis_field_runtime_test.cpp)
    target_link_libraries(aiverse_physis_field_runtime_test PRIVATE aiverse_engine)
    add_test(NAME physis_field_runtime COMMAND aiverse_physis_field_runtime_test)
    set_tests_properties(physis_field_runtime PROPERTIES LABELS "PHYSIS")
    add_executable(aiverse_physis_field_assets_test tests/physis_field_assets_test.cpp)
    target_link_libraries(aiverse_physis_field_assets_test PRIVATE aiverse_engine)
    add_test(NAME physis_field_assets COMMAND aiverse_physis_field_assets_test)
    set_tests_properties(physis_field_assets PROPERTIES LABELS "PHYSIS")
    if(TARGET aiverse_editor_core)
        add_executable(aiverse_physis_field_authoring_test tests/physis_field_authoring_test.cpp)
        target_link_libraries(aiverse_physis_field_authoring_test PRIVATE aiverse_editor_core)
        add_test(NAME physis_field_authoring COMMAND aiverse_physis_field_authoring_test)
        set_tests_properties(physis_field_authoring PROPERTIES LABELS "PHYSIS;EDITOR")
    endif()
endif()

# TerraWeave genre-independent deterministic terrain/biome/foliage core.
# Header-only engine/world API: also usable from native project modules.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    # Shared GPU timing service: hardware-free deterministic fan-out test.
    add_executable(aiverse_gpu_timing_router_test tests/gpu_timing_router_test.cpp)
    target_include_directories(aiverse_gpu_timing_router_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    aiverse_keep_test_asserts(aiverse_gpu_timing_router_test)
    add_test(NAME gpu_timing_router COMMAND aiverse_gpu_timing_router_test)
    # Sparse editor terrain is genre independent: click-to-create, shared
    # height borders, camera-dependent visual LOD and flat 4-vertex quad.
    # Colliders remain based on the normal fixed-resolution TerrainPatch.
    if(TARGET aiverse_editor_core)
        add_executable(aiverse_terraweave_sparse_editor_test
            tests/terraweave_sparse_editor_test.cpp)
        target_link_libraries(aiverse_terraweave_sparse_editor_test PRIVATE
            aiverse_editor_core)
        add_test(NAME terraweave_sparse_editor
            COMMAND aiverse_terraweave_sparse_editor_test)
        set_tests_properties(terraweave_sparse_editor PROPERTIES
            LABELS "TERRAWEAVE;EDITOR")
    endif()
    add_executable(aiverse_terraweave_sparse_authoring_lod_test
        tests/terraweave_sparse_authoring_lod_test.cpp)
    target_link_libraries(aiverse_terraweave_sparse_authoring_lod_test PRIVATE
        aiverse_engine aiverse_models aiverse_world aiverse_zstd_dec Threads::Threads)
    target_compile_options(aiverse_terraweave_sparse_authoring_lod_test PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME terraweave_sparse_authoring_lod
        COMMAND aiverse_terraweave_sparse_authoring_lod_test)
    set_tests_properties(terraweave_sparse_authoring_lod PROPERTIES
        LABELS "TERRAWEAVE;EDITOR;Renderer3D")

    add_executable(aiverse_terraweave_test tests/terraweave_test.cpp)
    target_link_libraries(aiverse_terraweave_test PRIVATE aiverse_engine Threads::Threads)
    aiverse_keep_test_asserts(aiverse_terraweave_test)
    add_test(NAME terraweave COMMAND aiverse_terraweave_test)
    # Stage70 sparse 4m per-NEREID-body nutrient and v2 archive migration.
    # Source fixtures authored; first real C++ compile/CTest owned by QA.
    add_executable(aiverse_terraweave_micro_nutrient_test
        tests/terraweave_micro_nutrient_test.cpp)
    target_link_libraries(aiverse_terraweave_micro_nutrient_test PRIVATE
        aiverse_engine Threads::Threads)
    target_compile_options(aiverse_terraweave_micro_nutrient_test PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME terraweave_micro_nutrient
        COMMAND aiverse_terraweave_micro_nutrient_test)
    # Stage70 targeted sustained nutrient conservation / v3 replay. Distinct
    # from the full real PHYSIS+NEREID+Box3D ECS bridge integration test.
    add_executable(aiverse_terraweave_stage70_sustained_test
        tests/terraweave_stage70_sustained_test.cpp)
    target_link_libraries(aiverse_terraweave_stage70_sustained_test PRIVATE
        aiverse_engine Threads::Threads)
    target_compile_options(aiverse_terraweave_stage70_sustained_test PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME terraweave_stage70_sustained
        COMMAND aiverse_terraweave_stage70_sustained_test)
    set_tests_properties(terraweave_stage70_sustained PROPERTIES
        LABELS "TERRAWEAVE;PERSISTENCE")
    # Native headers must compile together: ECS, NEREID, 4m nutrient
    # adapters and optional PHYSIS weather without that feature branch.
    add_executable(aiverse_terraweave_native_micro_compile_test
        tests/terraweave_native_micro_compile_test.cpp)
    target_include_directories(aiverse_terraweave_native_micro_compile_test
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/libraries/include)
    target_link_libraries(aiverse_terraweave_native_micro_compile_test PRIVATE
        aiverse_client_runtime aiverse_models aiverse_world aiverse_zstd_dec)   # WorldScene + renderer: runtime archive (only needed objects)
    target_compile_options(aiverse_terraweave_native_micro_compile_test PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME terraweave_native_micro_compile
        COMMAND aiverse_terraweave_native_micro_compile_test)
    # The physics adapter is optional: Box3D include paths are provided
    # transitively ONLY by the public AIVERSE::physics target, not by
    # aiverse_world or aiverse_models. Test it separately when enabled.
    if(TARGET aiverse_physics)
        add_executable(aiverse_terraweave_native_micro_physics_compile_test
            tests/terraweave_native_micro_compile_test.cpp)
        target_include_directories(aiverse_terraweave_native_micro_physics_compile_test
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/include)
        target_compile_definitions(aiverse_terraweave_native_micro_physics_compile_test
            PRIVATE AIVERSE_TERRAWEAVE_SMOKE_WITH_PHYSICS=1)
        target_link_libraries(aiverse_terraweave_native_micro_physics_compile_test
            PRIVATE aiverse_models aiverse_world aiverse_zstd_dec AIVERSE::physics)
        target_compile_options(aiverse_terraweave_native_micro_physics_compile_test
            PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
        add_test(NAME terraweave_native_micro_physics_compile
            COMMAND aiverse_terraweave_native_micro_physics_compile_test)
    endif()
    add_executable(aiverse_terraweave_arbor_test tests/terraweave_arbor_test.cpp)
    target_link_libraries(aiverse_terraweave_arbor_test PRIVATE aiverse_engine Threads::Threads)
    aiverse_keep_test_asserts(aiverse_terraweave_arbor_test)
    add_test(NAME terraweave_arbor COMMAND aiverse_terraweave_arbor_test)
    add_executable(aiverse_terraweave_flora_test tests/terraweave_flora_test.cpp)
    target_link_libraries(aiverse_terraweave_flora_test PRIVATE aiverse_engine Threads::Threads)
    aiverse_keep_test_asserts(aiverse_terraweave_flora_test)
    add_test(NAME terraweave_flora COMMAND aiverse_terraweave_flora_test)
    # Hierarchical wind masks: library reuse, rig packing, reference evaluation,
    # node-tree rigs, sidecars and TerraWeave Arbor rigs (CPU, no device).
    add_executable(aiverse_wind_rig_test tests/wind_rig_test.cpp)
    target_link_libraries(aiverse_wind_rig_test PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME wind_rig COMMAND aiverse_wind_rig_test)
    set_tests_properties(wind_rig PROPERTIES LABELS "RENDER;TERRAWEAVE;WIND")
    if(TARGET aiverse_glslang)
        add_executable(aiverse_wind_rig_glsl_test tests/wind_rig_glsl_test.cpp)
        target_include_directories(aiverse_wind_rig_glsl_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(aiverse_wind_rig_glsl_test PRIVATE aiverse_glslang aiverse_engine)
        add_test(NAME wind_rig_glsl COMMAND aiverse_wind_rig_glsl_test)
        set_tests_properties(wind_rig_glsl PROPERTIES LABELS "RENDER;WIND;SHADERS")
    endif()
    add_executable(aiverse_terraweave_model_test tests/terraweave_model_test.cpp)
    target_link_libraries(aiverse_terraweave_model_test PRIVATE aiverse_models aiverse_zstd_dec)
    aiverse_keep_test_asserts(aiverse_terraweave_model_test)
    add_test(NAME terraweave_model COMMAND aiverse_terraweave_model_test)
    add_executable(aiverse_terraweave_heightmap_test tests/terraweave_heightmap_test.cpp)
    target_link_libraries(aiverse_terraweave_heightmap_test PRIVATE aiverse_engine)
    aiverse_keep_test_asserts(aiverse_terraweave_heightmap_test)
    add_test(NAME terraweave_heightmap COMMAND aiverse_terraweave_heightmap_test)
    add_executable(aiverse_terraweave_ops_test tests/terraweave_ops_test.cpp)
    target_link_libraries(aiverse_terraweave_ops_test PRIVATE aiverse_engine)
    aiverse_keep_test_asserts(aiverse_terraweave_ops_test)
    add_test(NAME terraweave_ops COMMAND aiverse_terraweave_ops_test)
    add_executable(aiverse_terraweave_preview_test tests/terraweave_preview_test.cpp)
    # TerraWeaveHorizonScene owns a world::AsyncChunkStream (aiverse_world).
    target_link_libraries(aiverse_terraweave_preview_test PRIVATE aiverse_models aiverse_world aiverse_zstd_dec)
    aiverse_keep_test_asserts(aiverse_terraweave_preview_test)
    add_test(NAME terraweave_preview COMMAND aiverse_terraweave_preview_test)
    add_executable(aiverse_terraweave_erosion_zones_test tests/terraweave_erosion_zones_test.cpp)
    target_link_libraries(aiverse_terraweave_erosion_zones_test PRIVATE aiverse_models aiverse_zstd_dec AIVERSE::lib_nereid_water)
    target_compile_options(aiverse_terraweave_erosion_zones_test PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME terraweave_erosion_zones COMMAND aiverse_terraweave_erosion_zones_test)
    add_executable(aiverse_terraweave_forest_scene_test tests/terraweave_forest_scene_test.cpp)
    target_link_libraries(aiverse_terraweave_forest_scene_test PRIVATE aiverse_models aiverse_world aiverse_zstd_dec)
    aiverse_keep_test_asserts(aiverse_terraweave_forest_scene_test)
    add_test(NAME terraweave_forest_scene COMMAND aiverse_terraweave_forest_scene_test)
    # Automatic observer-driven residency planner + tree trunk colliders.
    add_executable(aiverse_terraweave_streaming_test tests/terraweave_streaming_test.cpp)
    target_link_libraries(aiverse_terraweave_streaming_test PRIVATE aiverse_models aiverse_world aiverse_zstd_dec)
    aiverse_keep_test_asserts(aiverse_terraweave_streaming_test)
    add_test(NAME terraweave_streaming COMMAND aiverse_terraweave_streaming_test)
    if(TARGET aiverse_physics)
        add_executable(aiverse_terraweave_streaming_physics_test tests/terraweave_streaming_test.cpp)
        target_compile_definitions(aiverse_terraweave_streaming_physics_test PRIVATE AIVERSE_TERRAWEAVE_TEST_WITH_PHYSICS=1)
        target_link_libraries(aiverse_terraweave_streaming_physics_test PRIVATE
            aiverse_models aiverse_world aiverse_zstd_dec AIVERSE::physics)
        aiverse_keep_test_asserts(aiverse_terraweave_streaming_physics_test)
        add_test(NAME terraweave_streaming_physics COMMAND aiverse_terraweave_streaming_physics_test)
    endif()
    if(TARGET aiverse_world)
        add_executable(aiverse_terraweave_horizon_test tests/terraweave_horizon_test.cpp)
        target_link_libraries(aiverse_terraweave_horizon_test PRIVATE aiverse_world aiverse_models aiverse_zstd_dec)
        aiverse_keep_test_asserts(aiverse_terraweave_horizon_test)
        add_test(NAME terraweave_horizon COMMAND aiverse_terraweave_horizon_test)
    endif()
    # Desktop editor integration test is optional; isolated CPU CI disables tools/editor.
    if(TARGET aiverse_editor_core)
        add_executable(aiverse_terraweave_editor_test tests/terraweave_editor_test.cpp)
        target_link_libraries(aiverse_terraweave_editor_test PRIVATE aiverse_editor_core aiverse_zstd_dec)
        aiverse_keep_test_asserts(aiverse_terraweave_editor_test)
        add_test(NAME terraweave_editor COMMAND aiverse_terraweave_editor_test)
    endif()
endif() # TerraWeave standalone tests

# SpectraFX graph-v1 capability dispatch: lossless CPU fallback for unsupported modules.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_graph_runtime_tests tests/vfx_graph_runtime_test.cpp)
    target_include_directories(aiverse_vfx_graph_runtime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_vfx_graph_runtime_tests PRIVATE aiverse_fx)
    add_test(NAME vfx_graph_runtime COMMAND aiverse_vfx_graph_runtime_tests)
    set_tests_properties(vfx_graph_runtime PROPERTIES LABELS "SpectraFX")
endif()


if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physical_sky_tests tests/render/physical_sky.cpp engine/render/PhysicalSky.cpp)
    target_link_libraries(aiverse_physical_sky_tests PRIVATE aiverse_engine)
    add_test(NAME physical_sky COMMAND aiverse_physical_sky_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_physis_sky_tests tests/render/physis_sky.cpp)
    target_link_libraries(aiverse_physis_sky_tests PRIVATE aiverse_engine)
    add_test(NAME physis_sky COMMAND aiverse_physis_sky_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_local_fog_tests tests/render/local_fog.cpp engine/render/LocalFog.cpp)
    target_link_libraries(aiverse_local_fog_tests PRIVATE aiverse_engine)
    add_test(NAME local_fog COMMAND aiverse_local_fog_tests)
endif()

if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_nereid_flow_map_tests tests/render/nereid_flow_map.cpp)
    target_link_libraries(aiverse_nereid_flow_map_tests PRIVATE aiverse_models AIVERSE::lib_nereid_water)
    add_test(NAME nereid_flow_map COMMAND aiverse_nereid_flow_map_tests)
    add_executable(aiverse_terraweave_rocks_test tests/terraweave_rocks_test.cpp)
    target_link_libraries(aiverse_terraweave_rocks_test PRIVATE aiverse_models aiverse_zstd_dec)
    target_compile_options(aiverse_terraweave_rocks_test PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME terraweave_rocks COMMAND aiverse_terraweave_rocks_test)
endif()
if(AIVERSE_BUILD_TOOLS AND TARGET aiverse_modeling)
    add_executable(aiverse_terraweave_rocks tools/terrain/rocks.cpp)
    target_link_libraries(aiverse_terraweave_rocks PRIVATE aiverse_modeling aiverse_zstd_dec)
endif()

if(AIVERSE_BUILD_TOOLS AND TARGET aiverse_modeling)
    add_executable(aiverse_terraweave_vegetation tools/terrain/vegetation.cpp)
    target_link_libraries(aiverse_terraweave_vegetation PRIVATE aiverse_modeling aiverse_zstd_dec)
endif()
if(AIVERSE_BUILD_TOOLS AND TARGET aiverse_modeling)
    add_executable(aiverse_terraweave_landscape tools/terrain/landscape.cpp)
    target_link_libraries(aiverse_terraweave_landscape PRIVATE aiverse_modeling aiverse_world aiverse_zstd_dec AIVERSE::lib_nereid_water)
endif()
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_terraweave_landscape_test tests/terraweave_landscape_test.cpp)
    target_link_libraries(aiverse_terraweave_landscape_test PRIVATE aiverse_models aiverse_world aiverse_zstd_dec AIVERSE::lib_nereid_water)
    target_compile_options(aiverse_terraweave_landscape_test PRIVATE $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-UNDEBUG>)
    add_test(NAME terraweave_landscape COMMAND aiverse_terraweave_landscape_test)
endif()

# LENS CPU-only project authoring shared with MCP.
if(TARGET aiverse_editor_core)
    target_sources(aiverse_editor_core PRIVATE apps/editor/core/LensOps.cpp)
    if(AIVERSE_BUILD_TESTS)
        add_executable(aiverse_lens_ops_tests tests/lens_ops_test.cpp)
        target_link_libraries(aiverse_lens_ops_tests PRIVATE aiverse_editor_core)
        add_test(NAME lens_ops COMMAND aiverse_lens_ops_tests)
    endif()
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiversemcp AND Python3_EXECUTABLE)
    add_test(NAME lens_mcp_stdio COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/lens_mcp_test.py
        $<TARGET_FILE:aiversemcp> ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(lens_mcp_stdio PROPERTIES TIMEOUT 60)
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiverse_editor_core AND AIVERSE_BUILD_STRATA)
    add_executable(aiverse_strata_editor_test tests/strata_editor_test.cpp)
    target_link_libraries(aiverse_strata_editor_test PRIVATE aiverse_editor_core)
    add_test(NAME strata_editor COMMAND aiverse_strata_editor_test)
endif()

# Stage 14: genuine Apple Metal compute/argument smoke (not a software mock).
# When the macOS runner has no Metal device it exits 77 => CTest SKIP, not PASS.
if(AIVERSE_BUILD_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_executable(aiverse_vfx_metal_smoke tests/vfx_metal_compaction_smoke.mm)
    target_include_directories(aiverse_vfx_metal_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_source_files_properties(tests/vfx_metal_compaction_smoke.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(aiverse_vfx_metal_smoke PRIVATE "-framework Metal" "-framework Foundation")
    add_test(NAME vfx_metal_compaction_smoke COMMAND aiverse_vfx_metal_smoke)
    set_tests_properties(vfx_metal_compaction_smoke PROPERTIES
        LABELS "SpectraFX;Metal" SKIP_RETURN_CODE 77)
endif()

# Independent graph instances must share compatible RHI draw submissions.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_shared_batch_tests tests/vfx_shared_batch_world_test.cpp)
    target_include_directories(aiverse_vfx_shared_batch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_vfx_shared_batch_tests PRIVATE AIVERSE::spectrafx)
    add_test(NAME vfx_shared_batch_world COMMAND aiverse_vfx_shared_batch_tests)
    set_tests_properties(vfx_shared_batch_world PROPERTIES LABELS "SpectraFX")
endif()

# Stage 15 — generic bounded tick cues, ECS attachments, generation and rollback.
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_vfx_cue_scheduler_tests tests/vfx_cue_scheduler_test.cpp)
    target_include_directories(aiverse_vfx_cue_scheduler_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/libraries/include)
    target_link_libraries(aiverse_vfx_cue_scheduler_tests PRIVATE AIVERSE::spectrafx)
    add_test(NAME vfx_cue_scheduler COMMAND aiverse_vfx_cue_scheduler_tests)
    set_tests_properties(vfx_cue_scheduler PROPERTIES LABELS "SpectraFX")
endif()

# Lighting main integration: CPU-only source/cluster/indirect composition and
# backend admission contracts. No rendering feature is activated by this.
if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    foreach(lighting_fixture IN ITEMS
            lumaweave_clusters
            lumaweave_gpu_packet
            gi_lighting_composition
            lighting_gles_admission
            gi_chunked_geometry
            gi_gpu_bvh_validator
            gi_gpu_bvh_validation
            gi_gpu_bvh_sparse_upload
            gi_gpu_chunk_bvh_cache
            gi_gpu_chunk_bvh_lifetime
            gi_gpu_paged_bvh
            gi_gpu_residency_planner
            gi_world_dirty_accumulator
            model_vt_shader_injection
            gi_geometry_scene
            gi_world_geometry_bridge
            gi_terraweave_resident_bridge
            lumaweave_direct_reference
            gi_dynamic_probe_reference
            gi_cpu_trace_budget
            lighting_gi_cpu_sampling
            gi_dynamic_probe_texture
            lighting_cpu_gi_bridge
            lighting_gpu_bridge
            lighting_gi_cascades
            lighting_gi_scrolling
            lighting_luma_mobile_fallback
            lighting_model_gi_shader_contract
            lighting_terrain_gi_shader_contract)
        add_executable(aiverse_lighting_${lighting_fixture}_test
                       tests/render/${lighting_fixture}.cpp)
        target_include_directories(aiverse_lighting_${lighting_fixture}_test
                                   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME lighting_${lighting_fixture}
                 COMMAND aiverse_lighting_${lighting_fixture}_test)
        set_tests_properties(lighting_${lighting_fixture} PROPERTIES LABELS "Lighting")
    endforeach()
endif()

# Exercise the real CPU GI atlas -> PBR receiver, beyond source-string checks.
if(AIVERSE_BUILD_TESTS AND AIVERSE_BUILD_HDR_TESTS AND AIVERSE_DESKTOP)
    add_test(NAME lighting_dynamic_gi_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_dynamic_gi_gpu PROPERTIES
        ENVIRONMENT "AIVERSE_DYNAMIC_GI_VERIFY=1" LABELS "Lighting;GPU"
        TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS"
        FAIL_REGULAR_EXPRESSION "FAIL")
    if(TARGET aiverse_hdr_gl)
        add_test(NAME lighting_dynamic_gi_gl COMMAND aiverse_hdr_gl)
        set_tests_properties(lighting_dynamic_gi_gl PROPERTIES
            ENVIRONMENT "AIVERSE_DYNAMIC_GI_VERIFY=1" LABELS "Lighting;GPU"
            TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS"
        FAIL_REGULAR_EXPRESSION "FAIL")
    endif()
endif()

if(AIVERSE_BUILD_TESTS AND AIVERSE_BUILD_HDR_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    add_test(NAME lighting_icb_gi_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_icb_gi_gpu PROPERTIES
        ENVIRONMENT "AIVERSE_DYNAMIC_GI_VERIFY=1;AIVERSE_ICB_GI_VERIFY=1;AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_AUTHORED_DRAWS=1;AIVERSE_GPU_ENCODED_ICB=1;AIVERSE_ICB_TRACE=1"
        LABELS "Lighting;GPU;Experimental" TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    add_test(NAME lighting_icb_compute_gi_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_icb_compute_gi_gpu PROPERTIES
        ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1;AIVERSE_ICB_GI_VERIFY=1;AIVERSE_EXPERIMENTAL_METAL_ICB=1;AIVERSE_GPU_AUTHORED_DRAWS=1;AIVERSE_GPU_ENCODED_ICB=1"
        LABELS "Lighting;GPU;Experimental" TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    add_test(NAME lighting_compute_gi_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_compute_gi_gpu PROPERTIES
        ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1" LABELS "Lighting;GPU"
        TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS"
        FAIL_REGULAR_EXPRESSION "FAIL")
    # Same scene plus a 9000-triangle chunk out of GI reach: residency keeps
    # the GPU path (no CPU fallback) and the pixels still match the reference.
    # Three GPU cascades in one atlas; the checked pixel lies in cascade 0.
    add_test(NAME lighting_compute_gi_gpu_cascades COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_compute_gi_gpu_cascades PROPERTIES
        ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1;AIVERSE_COMPUTE_GI_CASCADES=3" LABELS "Lighting;GPU"
        TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS"
        FAIL_REGULAR_EXPRESSION "FAIL")
    add_test(NAME lighting_compute_gi_gpu_far_world COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_compute_gi_gpu_far_world PROPERTIES
        ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1;AIVERSE_COMPUTE_GI_FAR_WORLD=1" LABELS "Lighting;GPU"
        TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS"
        FAIL_REGULAR_EXPRESSION "FAIL")
    # Scrolling cascades: the focus walks away (all probes recycled and zeroed)
    # and back; the re-traced probes must match the CPU reference again.
    add_test(NAME lighting_compute_gi_gpu_scroll COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_compute_gi_gpu_scroll PROPERTIES
        ENVIRONMENT "AIVERSE_COMPUTE_GI_VERIFY=1;AIVERSE_COMPUTE_GI_CASCADES=2;AIVERSE_COMPUTE_GI_SCROLL=1" LABELS "Lighting;GPU"
        TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS"
        FAIL_REGULAR_EXPRESSION "FAIL")
endif()

if(AIVERSE_BUILD_TESTS AND AIVERSE_BUILD_HDR_TESTS AND AIVERSE_DESKTOP)
    add_test(NAME lighting_terrain_gi_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_terrain_gi_gpu PROPERTIES
        ENVIRONMENT "AIVERSE_TERRAIN_GI_VERIFY=1" LABELS "Lighting;GPU"
        TIMEOUT 120 RUN_SERIAL TRUE PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    if(TARGET aiverse_hdr_gl)
        add_test(NAME lighting_terrain_gi_gl COMMAND aiverse_hdr_gl)
        set_tests_properties(lighting_terrain_gi_gl PROPERTIES
            ENVIRONMENT "AIVERSE_TERRAIN_GI_VERIFY=1" LABELS "Lighting;GPU"
            TIMEOUT 120 RUN_SERIAL TRUE PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    endif()
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiverse_renderbench)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(_lighting_auto_backend any)
    if(AIVERSE_PLATFORM STREQUAL "macos")
        set(_lighting_auto_backend gpu)
    endif()
    add_test(NAME lighting_native_host COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/lighting_native_host.py
        $<TARGET_FILE:aiverse_renderbench> ${CMAKE_BINARY_DIR}/lighting-native-host ${_lighting_auto_backend})
    set_tests_properties(lighting_native_host PROPERTIES LABELS "Lighting;GPU" RUN_SERIAL TRUE TIMEOUT 330)
    if(TARGET aiverse_renderbench_gl)
        add_test(NAME lighting_native_host_gl COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/lighting_native_host.py
            $<TARGET_FILE:aiverse_renderbench_gl> ${CMAKE_BINARY_DIR}/lighting-native-host-gl cpu)
        set_tests_properties(lighting_native_host_gl PROPERTIES LABELS "Lighting;GPU" RUN_SERIAL TRUE TIMEOUT 330)
    endif()
endif()

if(AIVERSE_BUILD_TESTS AND AIVERSE_BUILD_HDR_TESTS AND AIVERSE_DESKTOP)
    add_test(NAME lighting_graph_gi_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_graph_gi_gpu PROPERTIES ENVIRONMENT "AIVERSE_DYNAMIC_GI_VERIFY=1;AIVERSE_GRAPH_GI_VERIFY=1"
        LABELS "Lighting;GPU" TIMEOUT 120 RUN_SERIAL TRUE PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    if(TARGET aiverse_hdr_gl)
        add_test(NAME lighting_graph_gi_gl COMMAND aiverse_hdr_gl)
        set_tests_properties(lighting_graph_gi_gl PROPERTIES ENVIRONMENT "AIVERSE_DYNAMIC_GI_VERIFY=1;AIVERSE_GRAPH_GI_VERIFY=1"
            LABELS "Lighting;GPU" TIMEOUT 120 RUN_SERIAL TRUE PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    endif()
endif()

if(AIVERSE_BUILD_TESTS AND AIVERSE_BUILD_HDR_TESTS AND AIVERSE_PLATFORM STREQUAL "macos")
    # PRISMA Depth of Field and Motion Blur: regular effects (no opt-in flag).
    # 2026-11-22: AO, SSR, lens flare, SMAA, lens distortion, streaks, halation, heat haze,
    # FSR 1 and histogram metering (tests/render/PrismaEffectsScene.h, prismaNewEffect).
    foreach(_fx dof motion ao ssr flare smaa distort streaks halation haze fsr histogram)
        add_test(NAME prisma_${_fx}_gpu COMMAND aiverse_hdr_scene)
        set_tests_properties(prisma_${_fx}_gpu PROPERTIES ENVIRONMENT "AIVERSE_PRISMA_EFFECT_TEST=${_fx}"
            LABELS "Lighting;GPU;Prisma" TIMEOUT 120 RUN_SERIAL TRUE
            PASS_REGULAR_EXPRESSION "PRISMA ${_fx}: (diff [0-9.]+ )?PASS" FAIL_REGULAR_EXPRESSION "FAIL")
        if(TARGET aiverse_hdr_gl)
            add_test(NAME prisma_${_fx}_gl COMMAND aiverse_hdr_gl)
            set_tests_properties(prisma_${_fx}_gl PROPERTIES ENVIRONMENT "AIVERSE_PRISMA_EFFECT_TEST=${_fx}"
                LABELS "Lighting;GPU;Prisma" TIMEOUT 120 RUN_SERIAL TRUE
                PASS_REGULAR_EXPRESSION "PRISMA ${_fx}: (diff [0-9.]+ )?PASS" FAIL_REGULAR_EXPRESSION "FAIL")
        endif()
    endforeach()
    # Baked lightmaps and probes drawn by the real renderer (sun off, pixel check).
    add_test(NAME lighting_baked_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_baked_gpu PROPERTIES ENVIRONMENT "AIVERSE_BAKED_LIGHTING_TEST=1"
        LABELS "Lighting;GPU" TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Baked lighting: .*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    if(TARGET aiverse_hdr_gl)
        add_test(NAME lighting_baked_gl COMMAND aiverse_hdr_gl)
        set_tests_properties(lighting_baked_gl PROPERTIES ENVIRONMENT "AIVERSE_BAKED_LIGHTING_TEST=1"
            LABELS "Lighting;GPU" TIMEOUT 120 RUN_SERIAL TRUE
            PASS_REGULAR_EXPRESSION "Baked lighting: .*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
    endif()
    add_test(NAME lighting_luma_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_luma_gpu PROPERTIES ENVIRONMENT "AIVERSE_LUMA_VERIFY=1"
        LABELS "Lighting;GPU" TIMEOUT 180 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Luma GPU 11:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiverse_renderbench AND AIVERSE_PLATFORM STREQUAL "macos")
    add_test(NAME lighting_luma_host COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/lighting_luma_host.py
        $<TARGET_FILE:aiverse_renderbench> ${CMAKE_BINARY_DIR}/lighting-luma-host gpu)
    set_tests_properties(lighting_luma_host PROPERTIES LABELS "Lighting;GPU" RUN_SERIAL TRUE TIMEOUT 240)
    if(TARGET aiverse_renderbench_gl)
        add_test(NAME lighting_luma_host_gl COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/lighting_luma_host.py
            $<TARGET_FILE:aiverse_renderbench_gl> ${CMAKE_BINARY_DIR}/lighting-luma-host-gl legacy)
        set_tests_properties(lighting_luma_host_gl PROPERTIES LABELS "Lighting;GPU" RUN_SERIAL TRUE TIMEOUT 240)
    endif()
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiverse_models)
    add_executable(aiverse_lighting_model_geometry_test tests/render/lighting_model_geometry.cpp)
    target_include_directories(aiverse_lighting_model_geometry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(aiverse_lighting_model_geometry_test PRIVATE aiverse_models aiverse_zstd_dec)
    add_test(NAME lighting_model_geometry COMMAND aiverse_lighting_model_geometry_test)
    set_tests_properties(lighting_model_geometry PROPERTIES LABELS "Lighting")
endif()

if(AIVERSE_BUILD_TESTS AND TARGET aiverse_renderbench)
    add_test(NAME lighting_animated_host COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/lighting_animated_host.py
        $<TARGET_FILE:aiverse_renderbench> ${CMAKE_BINARY_DIR}/lighting-animated-host ${_lighting_auto_backend})
    set_tests_properties(lighting_animated_host PROPERTIES LABELS "Lighting;GPU" RUN_SERIAL TRUE TIMEOUT 420)
    if(TARGET aiverse_renderbench_gl)
        add_test(NAME lighting_animated_host_gl COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/lighting_animated_host.py
            $<TARGET_FILE:aiverse_renderbench_gl> ${CMAKE_BINARY_DIR}/lighting-animated-host-gl cpu)
        set_tests_properties(lighting_animated_host_gl PROPERTIES LABELS "Lighting;GPU" RUN_SERIAL TRUE TIMEOUT 420)
    endif()
endif()

# ---------------------------------------------------------------------------
# Experimental HTML5 Studio host. Production editor still uses its existing UI.
# ---------------------------------------------------------------------------
if(AIVERSE_BUILD_STUDIO_HTML5)
    if((APPLE OR WIN32) AND TARGET aiverse_editor_core)
        add_subdirectory(apps/studio-native)
    else()
        message(WARNING "AIVERSE Studio HTML5 requires macOS or Windows and aiverse_editor_core (AIVERSE_BUILD_TOOLS or AIVERSE_BUILD_EDITOR)")
    endif()
endif()

# Main integration: exercise virtual material sampling and probe GI together.
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_hdr_scene AND AIVERSE_PLATFORM STREQUAL "macos")
    add_test(NAME lighting_virtual_material_gi_gpu COMMAND aiverse_hdr_scene)
    set_tests_properties(lighting_virtual_material_gi_gpu PROPERTIES
        ENVIRONMENT "AIVERSE_DYNAMIC_GI_VERIFY=1;AIVERSE_VT_GI_VERIFY=1"
        LABELS "Lighting;GPU" TIMEOUT 120 RUN_SERIAL TRUE
        PASS_REGULAR_EXPRESSION "Dynamic GI GPU 5:.*PASS" FAIL_REGULAR_EXPRESSION "FAIL")
endif()
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_modeling AND TARGET aiverse_vivid_human)
    add_executable(aiverse_vivid_human_wardrobe_tests tests/vivid/human_wardrobe.cpp)
    target_link_libraries(aiverse_vivid_human_wardrobe_tests PRIVATE aiverse_vivid_human aiverse_modeling aiverse_zstd_dec)
    add_test(NAME vivid_human_wardrobe COMMAND aiverse_vivid_human_wardrobe_tests)
endif()
if(AIVERSE_BUILD_TESTS AND TARGET aiverse_modeling AND TARGET aiverse_vivid_human)
    add_executable(aiverse_vivid_face_shape_tests tests/vivid/human_face_shape.cpp)
    target_link_libraries(aiverse_vivid_face_shape_tests PRIVATE aiverse_vivid_human aiverse_modeling aiverse_zstd_dec)
    add_test(NAME vivid_face_shape COMMAND aiverse_vivid_face_shape_tests)
endif()

# Website capture host for LENS (tools/site/lens_showcase.cpp). Off by default; no engine behaviour.
option(AIVERSE_BUILD_SITE_SHOWCASE "Build tools/site capture hosts" OFF)
if(AIVERSE_BUILD_SITE_SHOWCASE AND AIVERSE_DESKTOP)
    add_executable(aiverse_lens_showcase tools/site/lens_showcase.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_lens_showcase PRIVATE aiverse_runtime)
    # STRATA debug view, SpectraFX and MotionWeave website frames
    add_executable(aiverse_fx_showcase tools/site/fx_showcase.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_fx_showcase PRIVATE aiverse_runtime aiverse_fx)
    add_executable(aiverse_anim_showcase tools/site/anim_showcase.cpp $<TARGET_OBJECTS:aiverse_runtime>)
    target_link_libraries(aiverse_anim_showcase PRIVATE aiverse_runtime)
    if(AIVERSE_BUILD_STRATA)
        add_executable(aiverse_strata_showcase tools/site/strata_showcase.cpp $<TARGET_OBJECTS:aiverse_runtime>)
        target_link_libraries(aiverse_strata_showcase PRIVATE aiverse_runtime aiverse_strata_portable)
    endif()
endif()

# DLSS runtime library next to the client (the SDK ships it as lib/Windows_x86_64/rel/nvngx_dlss.dll).
if(AIVERSE_WITH_DLSS AND TARGET aiverse_client)
    add_custom_command(TARGET aiverse_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AIVERSE_DLSS_SDK_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll" "$<TARGET_FILE_DIR:aiverse_client>"
        COMMENT "Copying nvngx_dlss.dll")
endif()

# One target that builds every PHYSIS test executable defined above, so CI and
# agents never keep a hand-written list that silently misses new tests (a
# registered CTest without its executable fails as "Not Run").
if(AIVERSE_BUILD_TESTS)
    get_property(_aiverse_all_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(_aiverse_physis_test_targets)
    foreach(_aiverse_target IN LISTS _aiverse_all_targets)
        if(_aiverse_target MATCHES "^aiverse_physis_.*_tests?$")
            list(APPEND _aiverse_physis_test_targets ${_aiverse_target})
        endif()
    endforeach()
    add_custom_target(aiverse_physis_tests)
    if(_aiverse_physis_test_targets)
        add_dependencies(aiverse_physis_tests ${_aiverse_physis_test_targets})
    endif()
    # physis_mcp drives the MCP server binary.
    if(TARGET aiversemcp)
        add_dependencies(aiverse_physis_tests aiversemcp)
    endif()
endif()

# Warcraft III source importer: no game data or StormLib is required for these
# synthetic format/parser/map-writer acceptance cases. Native integration is a
# separate acceptance gate when a licensed Warcraft III map is available.
if(AIVERSE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME warcraft3_importer_python
            COMMAND ${Python3_EXECUTABLE} -m unittest discover
                -s ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools
                -p "test_warcraft3*.py" -v)
        set_tests_properties(warcraft3_importer_python PROPERTIES LABELS "TOOLS;IMPORT")
    endif()
endif()

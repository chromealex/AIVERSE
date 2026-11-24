# Voxel sandbox game (docs/voxel.md, skills/workflow/aiverse-voxel-game): module + session, dedicated server,
# desktop client, tests. The voxel library packages come from libraries/catalog.json (AIVERSE::lib_voxel_*).
set(AIVERSE_VOXEL_LIBS AIVERSE::lib_voxel_world AIVERSE::lib_voxel_light AIVERSE::lib_voxel_mesh AIVERSE::lib_voxel_gen
    AIVERSE::lib_voxel_ticks AIVERSE::lib_voxel_nav AIVERSE::lib_voxel_replication)
add_library(aiverse_voxelgame STATIC VoxelGame.cpp VoxelSession.cpp)
target_link_libraries(aiverse_voxelgame PUBLIC aiverse_engine aiverse_net ${AIVERSE_VOXEL_LIBS})
# Sessions build their Simulation on game::GameCore (the one host); aiverse_game links this library back.
target_link_libraries(aiverse_voxelgame PUBLIC aiverse_game)
target_include_directories(aiverse_voxelgame PUBLIC ${PROJECT_SOURCE_DIR})
add_library(AIVERSE::voxelgame ALIAS aiverse_voxelgame)
if(AIVERSE_BUILD_TESTS)
    add_executable(aiverse_voxel_game_tests ../../tests/voxel/game_tests.cpp)
    target_link_libraries(aiverse_voxel_game_tests PRIVATE aiverse_voxelgame)
    add_test(NAME voxel_game COMMAND aiverse_voxel_game_tests ${PROJECT_SOURCE_DIR})
endif()
if(AIVERSE_BUILD_SERVER AND NOT EMSCRIPTEN)
    add_executable(aiverse_voxel_server ../../apps/voxel/server.cpp)
    target_link_libraries(aiverse_voxel_server PRIVATE aiverse_voxelgame)
    if(AIVERSE_BUILD_TESTS)
        add_test(NAME voxel_server COMMAND aiverse_voxel_server --project ${AIVERSE_TEMPLATES_DIR}/voxel_sandbox --ticks 100 --loopback --port 27290)
    endif()
endif()
# Voxel projects run in the one game client (apps/client/main.cpp: game.json "module" "voxel").
if(TARGET aiverse_client AND AIVERSE_DESKTOP)
    target_sources(aiverse_client PRIVATE ../../apps/voxel/client.cpp view/VoxelView.cpp)
    target_compile_definitions(aiverse_client PRIVATE AIVERSE_CLIENT_VOXEL=1)
    target_link_libraries(aiverse_client PRIVATE aiverse_voxelgame)
endif()

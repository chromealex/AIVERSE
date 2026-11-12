# Public header-only algorithms: usable by native project modules and standalone hosts.
add_library(aiverse_libraries INTERFACE)
add_library(AIVERSE::libraries ALIAS aiverse_libraries)

# Pure policies use libraries; persistence additionally needs engine public headers only.
add_library(aiverse_strategy_library INTERFACE)
add_library(AIVERSE::strategy ALIAS aiverse_strategy_library)
target_link_libraries(aiverse_strategy_library INTERFACE aiverse_libraries)
target_include_directories(aiverse_strategy_library INTERFACE "${CMAKE_CURRENT_LIST_DIR}/..")
target_compile_features(aiverse_libraries INTERFACE cxx_std_20)
target_include_directories(aiverse_libraries INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
# Optional engine adapter includes native Command types. Pure gameplay headers remain standalone.
add_library(aiverse_gameplay_native INTERFACE)
add_library(AIVERSE::gameplay_native ALIAS aiverse_gameplay_native)
target_link_libraries(aiverse_gameplay_native INTERFACE AIVERSE::libraries)
target_include_directories(aiverse_gameplay_native INTERFACE "${CMAKE_CURRENT_LIST_DIR}/..")

# Genre bot adapters are available before an external game's native/CMakeLists.txt executes.
add_library(aiverse_aiverse_bots INTERFACE)
add_library(AIVERSE::aiverse_bots ALIAS aiverse_aiverse_bots)
target_link_libraries(aiverse_aiverse_bots INTERFACE AIVERSE::gameplay_native aiverse_game)
add_library(aiverse_fps_bots INTERFACE)
add_library(AIVERSE::fps_bots ALIAS aiverse_fps_bots)
target_link_libraries(aiverse_fps_bots INTERFACE AIVERSE::gameplay_native aiverse_fps)

add_library(aiverse_navmesh_library INTERFACE)
add_library(AIVERSE::navmesh ALIAS aiverse_navmesh_library)
target_link_libraries(aiverse_navmesh_library INTERFACE AIVERSE::gameplay_native aiverse_world)

# OBJECT native modules are inserted with TARGET_OBJECTS: their PRIVATE link requirements do not
# propagate. Put backend libraries on the owning game target, avoiding a native -> aiverse_game cycle.
function(aiverse_native_use_bots target)
    if(NOT TARGET ${target} OR NOT TARGET aiverse_game)
        message(FATAL_ERROR "aiverse_native_use_bots requires a registered native target")
    endif()
    target_link_libraries(${target} PRIVATE AIVERSE::gameplay_native)
    foreach(genre IN LISTS ARGN)
        if(genre STREQUAL "FPS")
            target_link_libraries(aiverse_game PUBLIC aiverse_fps)
        elseif(genre STREQUAL "NAVMESH")
            target_link_libraries(aiverse_game PUBLIC aiverse_world)
        elseif(NOT genre STREQUAL "RTS")
            message(FATAL_ERROR "aiverse_native_use_bots: expected RTS, FPS or NAVMESH")
        endif()
    endforeach()
endfunction()

add_library(aiverse_genre_library INTERFACE)
add_library(AIVERSE::genre ALIAS aiverse_genre_library)
target_link_libraries(aiverse_genre_library INTERFACE AIVERSE::gameplay_native aiverse_world)
function(aiverse_native_use_genre target)
    if(NOT TARGET ${target} OR NOT TARGET aiverse_game)
        message(FATAL_ERROR "aiverse_native_use_genre requires a registered native target")
    endif()
    target_link_libraries(${target} PRIVATE AIVERSE::gameplay_native)
    target_link_libraries(aiverse_game PUBLIC aiverse_world)
endfunction()

# Host-only session admission; never instantiate transport in a simulation callback.
add_library(aiverse_session_library INTERFACE)
add_library(AIVERSE::session ALIAS aiverse_session_library)
target_link_libraries(aiverse_session_library INTERFACE AIVERSE::gameplay_native)
function(aiverse_native_use_session target)
    if(NOT TARGET ${target} OR NOT TARGET aiverse_game)
        message(FATAL_ERROR "aiverse_native_use_session requires a registered native target")
    endif()
    target_link_libraries(${target} PRIVATE AIVERSE::session)
    target_link_libraries(aiverse_game PUBLIC aiverse_net)
endfunction()

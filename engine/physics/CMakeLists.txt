# No dependency on the simulation module: authoritative hosts opt in explicitly.
set(BOX3D_PROFILE OFF CACHE BOOL "No implicit profiler dependency" FORCE)
set(BOX3D_DOUBLE_PRECISION OFF CACHE BOOL "Pinned physics ABI" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/box3d ${CMAKE_CURRENT_BINARY_DIR}/box3d EXCLUDE_FROM_ALL)
add_library(aiverse_physics STATIC World.cpp)
target_link_libraries(aiverse_physics PUBLIC aiverse_engine box3d::box3d)
target_include_directories(aiverse_physics PUBLIC ${PROJECT_SOURCE_DIR})
if(NOT MSVC)
    target_compile_options(aiverse_physics PRIVATE -ffp-contract=off)
endif()
add_library(AIVERSE::physics ALIAS aiverse_physics)

target_compile_definitions(aiverse_physics PRIVATE AIVERSE_PHYSICS_ABI="${CMAKE_SYSTEM_NAME}:${CMAKE_SYSTEM_PROCESSOR}:${CMAKE_CXX_COMPILER_ID}:${CMAKE_CXX_COMPILER_VERSION}:$<CONFIG>")

cmake_minimum_required(VERSION 3.20)
project(aiverse_building_caverns_demo LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure this browser example with emcmake.")
endif()
get_filename_component(engine_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web")
add_executable(caverns cave.cpp)
add_executable(buildings building.cpp
  "${engine_root}/apps/studio-native/StudioAssetCatalog.cpp"
  "${engine_root}/engine/data/Json.cpp"
  "${engine_root}/engine/core/Fixed.cpp"
  "${engine_root}/engine/render/Model.cpp"
  "${engine_root}/engine/render/TextureCodec.cpp"
  "${engine_root}/engine/render/ImageDds.cpp"
  "${engine_root}/third_party/zstd/zstddeclib.c")
foreach(target caverns buildings)
  target_include_directories(${target} PRIVATE "${engine_root}" "${engine_root}/libraries/include")
  target_compile_options(${target} PRIVATE -O2)
  target_link_options(${target} PRIVATE -O2 --no-entry -sMODULARIZE=1 -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1)
endforeach()
target_link_options(caverns PRIVATE -sEXPORT_NAME=createCaverns -sEXPORTED_RUNTIME_METHODS=HEAPF32)
target_compile_definitions(buildings PRIVATE AIVERSE_NO_BLP)
target_compile_options(buildings PRIVATE -fexceptions --use-port=sqlite3)
file(GLOB styles CONFIGURE_DEPENDS "${engine_root}/engine/assets/building-styles/*.json")
set_property(TARGET buildings APPEND PROPERTY LINK_DEPENDS "${styles}")
target_link_options(buildings PRIVATE -fexceptions --use-port=sqlite3 -sEXPORT_NAME=createBuildings
  -sEXPORTED_RUNTIME_METHODS=ccall,FS -sSTACK_SIZE=4194304
  --embed-file "${engine_root}/engine/assets/building-styles@/engine/assets/building-styles")

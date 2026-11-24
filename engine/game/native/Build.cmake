# Native modules are project code: their objects, the generated registry and every library they use belong to
# AIVERSE::native_modules, never to an engine target (the engine does not depend on libraries; docs/architecture-layers.md).
# Executables that host projects link it (aiverse_native_link_hosts at the end of configuration); a module registers
# itself through aiverse::game::NativeRegistrar, so a binary without modules simply has none.
if(NOT TARGET aiverse_native_modules)
    add_library(aiverse_native_modules INTERFACE)
    add_library(AIVERSE::native_modules ALIAS aiverse_native_modules)
endif()
# Native module authors call this from <project>/native/CMakeLists.txt.
function(aiverse_native_module)
    cmake_parse_arguments(M "" "ID;VERSION;ENTRY" "SOURCES" ${ARGN})
    if(M_UNPARSED_ARGUMENTS OR NOT M_ID MATCHES "^[a-z][a-z0-9_]*$" OR
       NOT M_ENTRY MATCHES "^[A-Za-z_][A-Za-z0-9_]*$" OR NOT M_SOURCES OR
       NOT M_VERSION MATCHES "^[1-9][0-9]*$" OR M_VERSION GREATER 4294967295)
        message(FATAL_ERROR "aiverse_native_module requires ID (lowercase identifier), VERSION (uint32), ENTRY (global factory), SOURCES")
    endif()
    get_property(ids GLOBAL PROPERTY AIVERSE_NATIVE_IDS)
    get_property(entries GLOBAL PROPERTY AIVERSE_NATIVE_ENTRIES)
    if(M_ID IN_LIST ids OR M_ENTRY IN_LIST entries)
        message(FATAL_ERROR "Duplicate native module ID or ENTRY: ${M_ID} / ${M_ENTRY}")
    endif()
    set_property(GLOBAL APPEND PROPERTY AIVERSE_NATIVE_IDS "${M_ID}")
    set_property(GLOBAL APPEND PROPERTY AIVERSE_NATIVE_ENTRIES "${M_ENTRY}")
    add_library(aiverse_native_${M_ID} OBJECT ${M_SOURCES})
    target_include_directories(aiverse_native_${M_ID} PRIVATE "${AIVERSE_NATIVE_ENGINE_ROOT}")
    target_compile_features(aiverse_native_${M_ID} PRIVATE cxx_std_20)
    set_target_properties(aiverse_native_${M_ID} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_sources(aiverse_native_modules INTERFACE $<TARGET_OBJECTS:aiverse_native_${M_ID}>)
    set_property(GLOBAL APPEND_STRING PROPERTY AIVERSE_NATIVE_DECLS
        "std::unique_ptr<aiverse::game::NativeModule> ${M_ENTRY}();\n")
    set_property(GLOBAL APPEND_STRING PROPERTY AIVERSE_NATIVE_DEFS
        "    {\"${M_ID}\", ${M_VERSION}u, &::${M_ENTRY}},\n")
endfunction()
# Links libraries into a native module: its objects get their usage requirements (include dirs, defines) and
# AIVERSE::native_modules, which carries the module's objects into every host executable, links them for the final link.
#   aiverse_native_link(ID <id> LIBRARIES aiverse_net aiverse_plugins ...)
function(aiverse_native_link)
    cmake_parse_arguments(L "" "ID" "LIBRARIES" ${ARGN})
    if(NOT L_ID OR NOT TARGET aiverse_native_${L_ID})
        message(FATAL_ERROR "aiverse_native_link: ID of a module declared with aiverse_native_module")
    endif()
    target_link_libraries(aiverse_native_${L_ID} PRIVATE ${L_LIBRARIES})
    target_link_libraries(aiverse_native_modules INTERFACE ${L_LIBRARIES})
endfunction()
# protobuf messages + gRPC client stubs of the module's own .proto files (docs/grpc.md), linked with aiverse_grpc:
#   aiverse_native_use_grpc(ID <id> IMPORT_ROOT <dir> PROTOS a.proto b.proto [NO_STUBS])
function(aiverse_native_use_grpc)
    cmake_parse_arguments(G "NO_STUBS" "ID;IMPORT_ROOT" "PROTOS" ${ARGN})
    if(NOT G_ID OR NOT TARGET aiverse_native_${G_ID})
        message(FATAL_ERROR "aiverse_native_use_grpc: ID of a module declared with aiverse_native_module")
    endif()
    if(NOT TARGET aiverse_grpc OR NOT COMMAND aiverse_protobuf_generate)
        message(FATAL_ERROR "aiverse_native_use_grpc: this engine build has no gRPC client (engine/grpc)")
    endif()
    if(G_PROTOS)
        set(stubs GRPC)
        if(G_NO_STUBS)
            set(stubs)
        endif()
        aiverse_protobuf_generate(TARGET aiverse_native_${G_ID} ${stubs} IMPORT_ROOT "${G_IMPORT_ROOT}" PROTOS ${G_PROTOS})
    endif()
    aiverse_native_link(ID ${G_ID} LIBRARIES aiverse_grpc)
endfunction()
set(AIVERSE_NATIVE_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(AIVERSE_NATIVE_DIR "" CACHE PATH "Native module source directory; empty uses <content>/native")
set(_native_dir "${AIVERSE_NATIVE_DIR}")
if(NOT _native_dir)
    set(_native_dir "${AIVERSE_CONTENT_DIR}/native")
    # Exported builds keep sources beside the engine; Android content is in assets/.
    if(NOT EXISTS "${_native_dir}/CMakeLists.txt" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/CMakeLists.txt")
        set(_native_dir "${CMAKE_CURRENT_SOURCE_DIR}/native")
    endif()
elseif(NOT EXISTS "${_native_dir}/CMakeLists.txt")
    message(FATAL_ERROR "AIVERSE_NATIVE_DIR has no CMakeLists.txt: ${_native_dir}")
endif()
# The project's native/CMakeLists.txt is loaded by aiverse_native_configure(), which the engine's top-level
# CMakeLists calls at its end: every engine target (aiverse_net, aiverse_grpc, aiverse_plugins, the meta client,
# server libraries ...) and aiverse_protobuf_generate exist by then, so a module may use them directly.
macro(aiverse_native_configure)
if(EXISTS "${_native_dir}/CMakeLists.txt")
    add_subdirectory("${_native_dir}" "${CMAKE_CURRENT_BINARY_DIR}/native")
endif()
get_property(_decls GLOBAL PROPERTY AIVERSE_NATIVE_DECLS)
get_property(_defs GLOBAL PROPERTY AIVERSE_NATIVE_DEFS)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/NativeRegistry.cpp" CONTENT
"#include \"engine/game/Native.h\"\n${_decls}
namespace {
// Explicit references prevent dead stripping of the module objects.
const aiverse::game::NativeRegistrar aiverseNativeRegistrar({
${_defs}});
}
")
target_sources(aiverse_game PRIVATE "${AIVERSE_NATIVE_ENGINE_ROOT}/engine/game/Native.cpp")
target_sources(aiverse_native_modules INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/gen/NativeRegistry.cpp")
target_include_directories(aiverse_native_modules INTERFACE "${AIVERSE_NATIVE_ENGINE_ROOT}")
# Every executable of this build that links a game host gets the project's modules (deferred to the end of the
# top-level CMakeLists, so executables declared later are included too).
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL aiverse_native_link_hosts "${CMAKE_SOURCE_DIR}")
endmacro()

set(AIVERSE_NATIVE_HOSTS aiverse_game aiverse_runtime aiverse_client_runtime aiverse_authority aiverse_editor_core aiverse_hosted_client
    CACHE INTERNAL "Targets whose direct users are project hosts (they receive AIVERSE::native_modules)")
function(aiverse_native_link_hosts dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        get_target_property(type ${t} TYPE)
        if(type STREQUAL "EXECUTABLE")
            get_target_property(links ${t} LINK_LIBRARIES)
            foreach(h IN LISTS AIVERSE_NATIVE_HOSTS)
                if(links AND h IN_LIST links)
                    target_link_libraries(${t} PRIVATE aiverse_native_modules)
                    break()
                endif()
            endforeach()
        endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(d IN LISTS subdirs)
        aiverse_native_link_hosts("${d}")
    endforeach()
endfunction()

# aiverse_protobuf_generate(TARGET <tgt> IMPORT_ROOT <dir> PROTOS <a.proto>... [OUT_DIR <dir>] [GRPC])
# Runs tools/protoc/aiverse_protoc.py (Python 3 stdlib only; no protoc) at build time and adds the
# generated .pb.cpp files to <tgt>. Headers: #include "<path relative to IMPORT_ROOT>.pb.h" /
# ".grpc.h". GRPC also generates typed client stubs (link aiverse_grpc).
set(AIVERSE_PROTOC "${CMAKE_CURRENT_LIST_DIR}/../../tools/protoc/aiverse_protoc.py" CACHE INTERNAL "")

function(aiverse_protobuf_generate)
    cmake_parse_arguments(PB "GRPC" "TARGET;IMPORT_ROOT;OUT_DIR" "PROTOS" ${ARGN})
    if(NOT PB_TARGET OR NOT PB_IMPORT_ROOT OR NOT PB_PROTOS)
        message(FATAL_ERROR "aiverse_protobuf_generate: TARGET, IMPORT_ROOT and PROTOS are required")
    endif()
    if(NOT PB_OUT_DIR)
        set(PB_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${PB_TARGET}_pb")
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "aiverse_protobuf_generate(${PB_TARGET}): Python 3 is required to generate protobuf code")
    endif()
    get_filename_component(root "${PB_IMPORT_ROOT}" ABSOLUTE)
    set(flags)
    if(PB_GRPC)
        set(flags --grpc)
    endif()
    set(sources)
    foreach(proto IN LISTS PB_PROTOS)
        get_filename_component(abs "${proto}" ABSOLUTE BASE_DIR "${root}")
        file(RELATIVE_PATH rel "${root}" "${abs}")
        string(REGEX REPLACE "\\.proto$" "" base "${rel}")
        set(outs "${PB_OUT_DIR}/${base}.pb.h" "${PB_OUT_DIR}/${base}.pb.cpp")
        if(PB_GRPC)
            list(APPEND outs "${PB_OUT_DIR}/${base}.grpc.h")
        endif()
        add_custom_command(
            OUTPUT ${outs}
            COMMAND ${Python3_EXECUTABLE} "${AIVERSE_PROTOC}" --out "${PB_OUT_DIR}" -I "${root}" ${flags} "${abs}"
            DEPENDS "${abs}" "${AIVERSE_PROTOC}"
            COMMENT "aiverse_protoc ${rel}"
            VERBATIM)
        list(APPEND sources "${PB_OUT_DIR}/${base}.pb.cpp")
        set_source_files_properties(${outs} PROPERTIES GENERATED TRUE)
    endforeach()
    target_sources(${PB_TARGET} PRIVATE ${sources})
    target_include_directories(${PB_TARGET} PUBLIC "${PB_OUT_DIR}")
    target_link_libraries(${PB_TARGET} PUBLIC aiverse_protobuf)
endfunction()

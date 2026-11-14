# gRPC client + protobuf runtime/codegen (docs/grpc.md). No third-party code: protobuf wire format,
# HTTP/2 + HPACK, gRPC-Web and the GrpcWebSocketBridge protocol are implemented here; TLS uses the
# platform's own library (OpenSSL loaded at runtime on Linux, Secure Transport on Apple, SChannel on
# Windows, SSLEngine via JNI on Android, the browser on the web).
# Included from the top-level CMakeLists after aiverse_net.
include(${CMAKE_CURRENT_LIST_DIR}/Protobuf.cmake)

add_library(aiverse_protobuf INTERFACE)
target_include_directories(aiverse_protobuf INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../..")
target_compile_features(aiverse_protobuf INTERFACE cxx_std_20)
add_library(AIVERSE::protobuf ALIAS aiverse_protobuf)

set(AIVERSE_GRPC_SOURCES GrpcUtil.cpp Channel.cpp Hpack.cpp Http2.cpp Http1.cpp)
if(EMSCRIPTEN)
    list(APPEND AIVERSE_GRPC_SOURCES WebTransport.cpp)
else()
    # Each TLS file compiles to nothing on the other platforms.
    list(APPEND AIVERSE_GRPC_SOURCES ByteStream.cpp Http2Transport.cpp GrpcWebTransport.cpp
         TlsOpenSsl.cpp TlsApple.cpp TlsWindows.cpp TlsAndroid.cpp)
endif()
add_library(aiverse_grpc STATIC EXCLUDE_FROM_ALL ${AIVERSE_GRPC_SOURCES})
target_link_libraries(aiverse_grpc PUBLIC aiverse_protobuf aiverse_net)
if(NOT MSVC)
    target_compile_options(aiverse_grpc PRIVATE -Wall -Wextra)
endif()
if(APPLE)
    target_link_libraries(aiverse_grpc PRIVATE "-framework Security" "-framework CoreFoundation")
elseif(WIN32)
    target_link_libraries(aiverse_grpc PRIVATE secur32 crypt32 ws2_32)
elseif(ANDROID)
    target_link_libraries(aiverse_grpc PRIVATE log)
elseif(EMSCRIPTEN)
    target_link_options(aiverse_grpc PUBLIC -sFETCH=1)
else()
    target_link_libraries(aiverse_grpc PRIVATE ${CMAKE_DL_LIBS})
endif()
add_library(AIVERSE::grpc ALIAS aiverse_grpc)

if(AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing() # this directory is added before the top level enables testing
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_executable(aiverse_grpc_tests ${CMAKE_CURRENT_LIST_DIR}/../../tests/grpc/grpc_tests.cpp
                   ${CMAKE_CURRENT_LIST_DIR}/../../tests/grpc/MockServer.cpp)
    aiverse_protobuf_generate(TARGET aiverse_grpc_tests GRPC IMPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../tests/grpc"
        PROTOS test.proto)
    target_link_libraries(aiverse_grpc_tests PRIVATE aiverse_grpc)
    if(NOT MSVC)
        target_compile_options(aiverse_grpc_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME aiverse_grpc_tests COMMAND aiverse_grpc_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME aiverse_protoc COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../tests/grpc/protoc_test.py)
    endif()
endif()

"""Check native module selection across projects in one reused CMake cache."""
import pathlib
import subprocess
import sys
import tempfile

engine = pathlib.Path(__file__).resolve().parents[1]
cmake = sys.argv[1] if len(sys.argv) > 1 else "cmake"
with tempfile.TemporaryDirectory(prefix="rts-native-projects-") as tmp:
    root = pathlib.Path(tmp)
    src, build = root / "engine", root / "build"
    (src / "engine/game").mkdir(parents=True)
    # The real selection/registration code is exercised; no renderer/toolchain is needed.
    (src / "engine/game/Native.cpp").write_text("")
    (src / "stub.cpp").write_text("")
    module_cmake = (engine / "engine/game/native/Build.cmake").as_posix()
    protobuf_cmake = (engine / "engine/grpc/Protobuf.cmake").as_posix()
    (src / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.22)\nproject(NativeIsolation LANGUAGES CXX)\n'
        'add_library(aiverse_game STATIC stub.cpp)\ninclude("' + module_cmake + '")\n'
        # engine targets defined after the include are there for the project's native CMake (configured last)
        'add_library(aiverse_grpc STATIC stub.cpp)\nadd_library(aiverse_protobuf INTERFACE)\n'
        'include("' + protobuf_cmake + '")\naiverse_native_configure()\n')
    for name in ("a", "b"):
        native = root / name / "native"
        native.mkdir(parents=True)
        (native / "Module.cpp").write_text("")
        (native / "CMakeLists.txt").write_text(
            'aiverse_native_module(ID local VERSION 1 ENTRY create_' + name + ' SOURCES Module.cpp)\n')
    # a module with its own protos + gRPC stubs (aiverse_native_use_grpc) and an engine library (aiverse_native_link)
    grpc = root / "g" / "native"
    (grpc / "proto").mkdir(parents=True)
    (grpc / "Module.cpp").write_text("")
    (grpc / "proto" / "svc.proto").write_text('syntax = "proto3";\npackage t;\nmessage Ping { int32 n = 1; }\n'
                                               'service S { rpc Call (Ping) returns (Ping); }\n')
    (grpc / "CMakeLists.txt").write_text(
        'if(NOT TARGET aiverse_grpc)\n  message(FATAL_ERROR "engine targets are not defined yet")\nendif()\n'
        'aiverse_native_module(ID rpc VERSION 1 ENTRY create_rpc SOURCES Module.cpp)\n'
        'aiverse_native_use_grpc(ID rpc IMPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/proto" PROTOS svc.proto)\n'
        'get_target_property(s aiverse_native_rpc SOURCES)\nget_target_property(l aiverse_native_modules INTERFACE_LINK_LIBRARIES)\n'
        'get_target_property(g aiverse_game INTERFACE_LINK_LIBRARIES)\n'
        'message(STATUS "rpc sources: ${s}; native links: ${l}; game links: ${g}")\n')
    (root / "empty").mkdir()

    log = {}

    def configure(project, *args):
        r = subprocess.run([cmake, "-S", str(src), "-B", str(build),
                            "-DAIVERSE_CONTENT_DIR=" + str(root / project), *args],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode != 0:
            sys.exit(r.stdout)
        log["out"] = r.stdout
        return (build / "gen/NativeRegistry.cpp").read_text()

    a = configure("a")
    assert "create_a" in a and "create_b" not in a
    b = configure("b")
    assert "create_b" in b and "create_a" not in b
    empty = configure("empty")
    assert "create_a" not in empty and "create_b" not in empty
    override = configure("b", "-DAIVERSE_NATIVE_DIR=" + str(root / "a/native"))
    assert "create_a" in override and "create_b" not in override
    # This is the cache reset supplied by aiversebuild for the current game project.
    reset = configure("b", "-DAIVERSE_NATIVE_DIR=")
    assert "create_b" in reset and "create_a" not in reset
    g = configure("g", "-DAIVERSE_NATIVE_DIR=")
    assert "create_rpc" in g, g
    line = next(l for l in log["out"].splitlines() if "rpc sources:" in l)
    native_links, game_links = line.split("native links:")[1].split("; game links:")
    assert "svc.pb.cpp" in line and "aiverse_grpc" in native_links and "aiverse_grpc" not in game_links, line   # project code, not the engine
    print("Native modules: project isolation and cache reset passed; engine targets + gRPC helper passed")

"""Check native module selection across projects in one reused CMake cache."""
import pathlib
import shutil
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
    # Build.cmake now separates the registrar from the gameplay implementation.
    (src / "engine/game/NativeRegistry.cpp").write_bytes((engine / "engine/game/NativeRegistry.cpp").read_bytes())
    (src / "stub.cpp").write_text("")
    # Studio is declared in a subdirectory and packages the top-level game executable.
    (src / "studio").mkdir()
    (src / "studio/CMakeLists.txt").write_text(
        'add_executable(game_client ../stub.cpp)\n'
        'target_link_libraries(game_client PRIVATE aiverse_client_runtime aiverse_game_view)\n'
        'add_dependencies(game_client game_host)\n')
    module_cmake = (engine / "engine/game/native/Build.cmake").as_posix()
    protobuf_cmake = (engine / "engine/grpc/Protobuf.cmake").as_posix()
    (src / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.22)\nproject(NativeIsolation LANGUAGES CXX)\n'
        'add_library(aiverse_engine INTERFACE)\nadd_library(aiverse_game STATIC stub.cpp)\ninclude("' + module_cmake + '")\n'
        # engine targets defined after the include are there for the project's native CMake (configured last)
        'add_library(aiverse_grpc STATIC stub.cpp)\nadd_library(aiverse_protobuf INTERFACE)\n'
        'include("' + protobuf_cmake + '")\n'
        'add_library(aiverse_runtime INTERFACE)\n'
        'add_library(aiverse_client_runtime INTERFACE)\n'
        'add_library(aiverse_game_view INTERFACE)\n'
        'target_link_libraries(aiverse_game_view INTERFACE aiverse_game)\n'
        'add_executable(camera_only stub.cpp)\n'
        'target_link_libraries(camera_only PRIVATE aiverse_runtime)\n'
        'add_executable(scene_only stub.cpp)\n'
        'target_link_libraries(scene_only PRIVATE aiverse_client_runtime)\n'
        'add_executable(game_host stub.cpp)\n'
        'target_link_libraries(game_host PRIVATE aiverse_game)\n'
        'add_subdirectory(studio)\n'
        'aiverse_native_configure()\n'
        'foreach(t camera_only scene_only game_client game_host)\n'
        '  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${t}-links.txt" CONTENT "$<TARGET_PROPERTY:${t},LINK_LIBRARIES>")\n'
        'endforeach()\n')
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
        # Deferred registration must not inject project objects into render-only
        # or scene-only executables, even when real project modules are configured.
        for target, expected in (("camera_only", False), ("scene_only", False),
                                 ("game_client", True), ("game_host", True)):
            links = (build / (target + "-links.txt")).read_text().split(";")
            assert ("aiverse_native_modules" in links) == expected, (target, links)
        registry = build / "gen/NativeRegistry.cpp"
        return registry.read_text() if registry.exists() else None

    a = configure("a")
    assert "create_a" in a and "create_b" not in a
    # Both editor and player consume the registry, and the editor packages the player.
    # Ninja must resolve the whole dependency chain before compilation begins.
    if shutil.which("ninja"):
        ninja_build = root / "ninja-build"
        subprocess.run([cmake, "-S", str(src), "-B", str(ninja_build), "-G", "Ninja",
                        "-DAIVERSE_CONTENT_DIR=" + str(root / "a")], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([cmake, "--build", str(ninja_build), "--target", "game_client", "--", "-n"],
                       check=True, stdout=subprocess.DEVNULL)
    b = configure("b")
    assert "create_b" in b and "create_a" not in b
    empty = configure("empty")
    assert empty is None, "stale registry survived empty-project reconfigure"
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

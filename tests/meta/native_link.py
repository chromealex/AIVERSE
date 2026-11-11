"""Compile/link real native registration + meta helper from an external project."""
import pathlib
import subprocess
import sys
import tempfile

engine = pathlib.Path(__file__).resolve().parents[2]
cmake = sys.argv[1] if len(sys.argv) > 1 else 'cmake'
with tempfile.TemporaryDirectory(prefix='rts-meta-native-') as tmp:
    root = pathlib.Path(tmp)
    src, native, build = root / 'host', root / 'project' / 'native', root / 'build'
    (src / 'modules/rts').mkdir(parents=True)
    native.mkdir(parents=True)
    # Isolate the link contract: real Build.cmake and meta, minimal sim host types.
    (src / 'modules/rts/RtsNative.cpp').write_text('')
    (src / 'modules/rts/RtsNative.h').write_text('''#pragma once
#include <memory>
#include <string>
#include <vector>
namespace aiverse::game {
struct NativeModule { virtual ~NativeModule() = default; };
struct NativeDefinition { std::string id; unsigned version; std::unique_ptr<NativeModule> (*factory)(); };
std::vector<NativeDefinition> compiledNativeModules();
}
''')
    (src / 'stub.cpp').write_text('')
    (native / 'Module.cpp').write_text('''#include "modules/rts/RtsNative.h"
#include "engine/meta/Meta.h"
#include "engine/meta/Rating.h"
#include "engine/meta/LiveOps.h"
int metaProbe() {
    if(aiverse::meta::elo(1500,1500,1,32)!=1516) return -2;
    aiverse::meta::validateLiveOps(aiverse::Json::object(),aiverse::meta::Config{});
    aiverse::meta::Config c; c.assets["gold"].cap=10;
    aiverse::meta::Economy e(c);
    return e.transact("gift",{},{{"gold",7}}) ? int(e.profile().balances.at("gold")) : -1;
}
std::unique_ptr<aiverse::game::NativeModule> createMetaProbe() { return std::make_unique<aiverse::game::NativeModule>(); }
''')
    (native / 'CMakeLists.txt').write_text('''aiverse_native_module(ID meta_probe VERSION 1 ENTRY createMetaProbe SOURCES Module.cpp)
aiverse_native_use_meta(aiverse_native_meta_probe)
''')
    (src / 'main.cpp').write_text('''#include "modules/rts/RtsNative.h"
int metaProbe();
int main() { return metaProbe()==7 && aiverse::game::compiledNativeModules().size()==1 ? 0 : 1; }
''')
    (src / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.22)
project(NativeMeta LANGUAGES CXX)
add_subdirectory("{engine.as_posix()}/engine/meta" meta)
add_library(aiverse_game STATIC stub.cpp)
target_include_directories(aiverse_game PUBLIC "${{CMAKE_CURRENT_SOURCE_DIR}}")
include("{engine.as_posix()}/modules/rts/native/Build.cmake")
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE aiverse_game)
''')
    for cmd in ([cmake, '-S', str(src), '-B', str(build), '-DAIVERSE_CONTENT_DIR='+str(native.parent)],
                [cmake, '--build', str(build), '--config', 'Release', '-j', '4']):
        subprocess.run(cmd, check=True)
    binary = build / 'consumer'
    if not binary.exists():
        binary = build / 'Release' / 'consumer.exe'
    subprocess.run([str(binary)], check=True)
print('Native meta consumer passed')

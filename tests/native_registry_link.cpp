#include "../engine/game/Native.h"
#include <string>

namespace {
const aiverse::game::NativeRegistrar first({{"registry_first", 1u, nullptr}});
const aiverse::game::NativeRegistrar second({{"registry_second", 2u, nullptr}});
}

int main() {
    const auto definitions = aiverse::game::compiledNativeModules();
    if (definitions.size() != 2u) return 1;
    if (definitions[0].id != "registry_first" || definitions[0].version != 1u) return 2;
    if (definitions[1].id != "registry_second" || definitions[1].version != 2u) return 3;
    return 0;
}

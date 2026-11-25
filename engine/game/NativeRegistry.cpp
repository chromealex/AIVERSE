#include "Native.h"

namespace aiverse::game {
namespace {
std::vector<NativeDefinition>& registeredNativeModules() {
    static std::vector<NativeDefinition> modules;
    return modules;
}
}

NativeRegistrar::NativeRegistrar(std::vector<NativeDefinition> definitions) {
    auto& modules = registeredNativeModules();
    for (auto& definition : definitions) modules.push_back(std::move(definition));
}

std::vector<NativeDefinition> compiledNativeModules() {
    return registeredNativeModules();
}
} // namespace aiverse::game

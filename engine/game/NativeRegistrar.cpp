#include "Native.h"
#include <utility>

namespace aiverse::game {
namespace {
std::vector<NativeDefinition>& registeredNativeModules() { static std::vector<NativeDefinition> modules; return modules; }
}
NativeRegistrar::NativeRegistrar(std::vector<NativeDefinition> definitions) {
    auto& modules = registeredNativeModules();
    for (auto& d : definitions) modules.push_back(std::move(d));
}
std::vector<NativeDefinition> compiledNativeModules() { return registeredNativeModules(); }
} // namespace aiverse::game

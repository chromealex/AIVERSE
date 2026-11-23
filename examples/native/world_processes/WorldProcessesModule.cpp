// Copy this native/ module into any AIVERSE project and enable
// {"id":"world_processes"} in rules.json/nativeModules.
#include "aiverselib/world/WorldProcessesNativeModule.h"
std::unique_ptr<aiverse::game::NativeModule> createWorldProcessesModule() {
    return aiverse::lib::worldprocess::createWorldProcessesNativeModule();
}

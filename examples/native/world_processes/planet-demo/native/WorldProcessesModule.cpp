#include "PlanetDemoModule.h"
std::unique_ptr<aiverse::game::NativeModule> createWorldProcessesModule() {
    return aiverse::examples::planet_processes::create();
}

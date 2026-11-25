#include "apps/studio-native/StudioBuildingGeneration.h"
#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE const char* demo_build(const char* request) {
    static std::string result;
    try {
        const std::filesystem::path project="/demo-project";
        std::filesystem::remove_all(project);
        std::filesystem::create_directories(project/"content");
        result=aiverse::studio::studioBuildingGenerate("/",project,aiverse::Json::parse(request)).dump();
    } catch (const std::exception& error) {
        aiverse::Json response=aiverse::Json::object();
        response.set("error",aiverse::Json::string(error.what()));
        result=response.dump();
    }
    return result.c_str();
}

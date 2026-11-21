// Map generator adapter for packaged clients built without the "mapgen" engine module
// (packaging.engineModules, CMake AIVERSE_GAME_WITH_MAPGEN=OFF). Maps without a "generate" block (top
// level or on a world body) load unchanged, as with the full module; generated maps fail with a
// readable error before the match starts.
#include "../gen/MapGen.h"

namespace aiverse::gen {
namespace {
bool wantsGenerator(const Json& map) {
    if (map["generate"].isObject()) return true;
    for (const auto& b : map["world"]["bodies"].items())
        if (b["generate"].isObject()) return true;
    return false;
}
bool missingModule(std::string* err) {
    if (err) *err = "map generate: this client was built without the mapgen module; "
                    "add \"mapgen\" (or \"auto\") to packaging.engineModules in game.json";
    return false;
}
} // namespace

bool prepareGenerate(Json& map, const std::function<bool(const std::string&, std::string&)>&, std::string* err) {
    return wantsGenerator(map) ? missingModule(err) : true;
}
bool applyGenerate(Json& map, const game::Rules&, const Json&, uint64_t, int, std::string* err) {
    return wantsGenerator(map) ? missingModule(err) : true;
}

} // namespace aiverse::gen

#include "apps/editor/core/TerraWeaveOps.h"
#include <cassert>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::editor;

int main() {
    Json p = Json::object();
    p.set("seed", Json::number(int64_t(42)));
    p.set("reliefCm", Json::number(int64_t(1600)));
    p.set("basePeriodCm", Json::number(int64_t(2800)));
    p.set("octaves", Json::number(int64_t(4)));
    const auto terrain = terraweave_ops_detail::terrain(p);
    assert(terrain.seed == 42 && terrain.reliefCm == 1600);
    assert(terrain.basePeriodCm == 2800 && terrain.octaves == 4);

    const auto origin = terraweave_ops_detail::origin(p);
    const Json preview1 = terraweave_ops_detail::preview(terrain, origin, 64, 48);
    const Json preview2 = terraweave_ops_detail::preview(terrain, origin, 64, 48);
    assert(preview1.getBool("ok", false));
    assert(preview1["samplesCm"].isArray());
    assert(preview1["samplesCm"].size() == 9);
    for (const Json& row : preview1["samplesCm"].items()) {
        assert(row.isArray());
        assert(row.size() == 9);
    }
    assert(preview1.dump() == preview2.dump());
    const auto config = terraweave_ops_detail::sanitizedConfig(terrain, origin);
    assert(config.getInt("version", 0) == 1);
    assert(config.getInt("seed", 0) == 42);

    bool threw = false;
    p.set("octaves", Json::number(int64_t(13)));
    try { (void)terraweave_ops_detail::terrain(p); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    p.set("octaves", Json::number(int64_t(4)));
    threw = false;
    p.set("reliefCm", Json::number(int64_t(-5)));
    try { (void)terraweave_ops_detail::terrain(p); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}

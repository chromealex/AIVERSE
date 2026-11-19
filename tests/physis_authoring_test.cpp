#include "apps/editor/PhysisAuthoring.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::editor;
using namespace aiverse::editor::physis_authoring;
namespace {
int failed = 0;
#define CHECK(c) do { if(!(c)) { std::printf("PHYSIS AUTHORING FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failed; } } while(0)

void plainMapAndUndo() {
    MapDoc map; map.reset(8,8);
    Json config = defaultConfig();
    std::string error;
    CHECK(validate(config, &error));
    CHECK(apply(map, config, &error));
    CHECK(map.dirty);
    CHECK(map.canUndo());
    CHECK(read(map) == config);
    CHECK(map.toJson()["physis"] == config);
    const auto revision = map.revision;
    CHECK(apply(map, config, &error)); // no-op must not allocate undo
    CHECK(map.revision == revision);
    Json changed = config;
    Json grid = changed["grid"];
    grid.set("width", Json::number(int64_t(64)));
    changed.set("grid",grid);
    changed.set("customUserData", Json::parse(R"({"future":[1,2,3]})"));
    CHECK(apply(map, changed, &error));
    CHECK(read(map)["customUserData"].isObject());
    map.undo(); CHECK(read(map) == config);
    map.redo(); CHECK(read(map) == changed);
    CHECK(apply(map, Json{}, &error));
    CHECK(read(map).isNull());
    map.undo(); CHECK(read(map) == changed);
}

void rejectionIsTransactional() {
    MapDoc map;map.reset(8,8);
    Json valid = defaultConfig();
    CHECK(apply(map, valid));
    const auto rev = map.revision;
    Json invalid = valid;
    auto grid = invalid["grid"];
    grid.set("cellSizeMeters", Json::number(int64_t(-5)));
    invalid.set("grid",grid);
    std::string error;
    CHECK(!apply(map, invalid, &error));
    CHECK(!error.empty());
    CHECK(map.revision == rev);
    CHECK(read(map) == valid);
    invalid = valid;
    invalid.set("floodMode", Json::string("Both"));
    CHECK(!apply(map, invalid, &error));
    CHECK(map.revision == rev);
    CHECK(read(map) == valid);
    invalid = valid;
    invalid.set("storms", Json::object());
    CHECK(!apply(map, invalid, &error));
    CHECK(map.revision == rev);
}

void fpsDocumentRoundtrip() {
    MapDoc map;
    std::string error;
    const Json source = Json::parse(
        R"({"name":"Example","colliders":[],"spawns":[],"custom":"keep"})");
    CHECK(map.loadJson(source, &error));
    Json cfg = defaultConfig();
    cfg.set("floodMode", Json::string("ExternalAuthority"));
    CHECK(apply(map,cfg,&error));
    CHECK(map.toJson()["custom"].str()=="keep");
    CHECK(map.toJson()["physis"] == cfg);
    MapDoc copy;
    CHECK(copy.loadJson(map.toJson(),&error));
    CHECK(read(copy)==cfg);
    map.undo(); CHECK(map.toJson()["physis"].isNull());
    map.redo(); CHECK(map.toJson()["physis"] == cfg);
}
}
int main() {
    plainMapAndUndo();
    rejectionIsTransactional();
    fpsDocumentRoundtrip();
    std::printf("PHYSIS authoring: %s\n",failed?"FAIL":"PASS");
    return failed?1:0;
}

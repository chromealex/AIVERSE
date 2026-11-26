#include "apps/editor/PhysisAuthoring.h"
#include <cstdio>
#include "apps/editor/core/Workspace.h"
using namespace aiverse;
using namespace aiverse::editor;
using namespace aiverse::editor::physis_authoring;
namespace {
int failed = 0;
#define CHECK(c) do { if(!(c)) { std::printf("PHYSIS AUTHORING FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failed; } } while(0)

void mcpAuthoring() {
    Workspace w; w.map.reset(8,8);
    const auto initial=w.map.revision;
    auto get=w.call("physis_get",Json::object());
    CHECK(get["config"].isNull()); CHECK(get["defaultConfig"].isObject());
    Json p=Json::object(); p.set("config",get["defaultConfig"]);
    CHECK(w.call("physis_validate",p).getBool("ok"));
    CHECK(w.map.revision==initial);
    CHECK(w.call("physis_set",p).has("error"));
    p.set("expectedRevision",get["revision"]);
    CHECK(w.call("physis_set",p).getBool("changed"));
    CHECK(!w.call("physis_get",Json::object()).getBool("liveApplied",true));
    CHECK(w.call("physis_set",p).has("error")); // stale replacement
    p.set("expectedRevision",Json::number(int64_t(w.map.revision)));
    CHECK(!w.call("physis_set",p).getBool("changed",true));
    const auto valid=read(w.map); const auto rev=w.map.revision;
    for (auto bad : {Json::fromBool(true),Json::number(int64_t(2)),Json::array(),Json::object()}) {
        Json cfg=valid; cfg.set("floodMode",bad); p.set("config",cfg);
        CHECK(w.call("physis_validate",p).has("error"));
        CHECK(w.call("physis_set",p).has("error"));
        CHECK(w.map.revision==rev); CHECK(read(w.map)==valid);
    }
    w.map.undo(); CHECK(read(w.map).isNull());
    w.map.redo(); CHECK(read(w.map)==valid);
    p.set("config",Json{}); p.set("expectedRevision",Json::number(int64_t(w.map.revision)));
    CHECK(w.call("physis_set",p).getBool("changed")); CHECK(read(w.map).isNull());
    w.map.undo(); CHECK(read(w.map)==valid);
    CHECK(w.map.loadJson(Json::parse(R"({"name":"FPS","colliders":[],"spawns":[]})"),nullptr));
    p.set("config",valid);p.set("expectedRevision",Json::number(int64_t(w.map.revision)));
    CHECK(w.call("physis_set",p).getBool("ok")); CHECK(w.map.toJson()["physis"]==valid);
    int count=0;for(const auto& op:w.ops())if(op.name.rfind("physis_",0)==0)++count;
    CHECK(count==4);
    const auto mapBefore=w.map.toJson();
    const auto revisionBefore=w.map.revision;
    auto demo=[&](const char* args){return w.call("physis_demo",Json::parse(args));};
    CHECK(!demo(R"({"action":"view"})").getBool("active"));
    const auto first=demo(R"({"action":"reset"})");
    CHECK(first["tick"].integer()==25 && first["available"].integer()==1024);
    CHECK(demo(R"({"action":"step60","size":0})").has("error"));
    CHECK(demo(R"({"action":"step"})")["tick"].integer()==26);
    CHECK(demo(R"({"action":"view","centerX":1000})")["available"].integer()==0);
    CHECK(demo(R"({"action":"reset"})")["values"]==first["values"]);
    Workspace other;
    CHECK(!other.call("physis_demo",Json::parse(R"({"action":"view"})")).getBool("active"));
    CHECK(w.map.toJson()==mapBefore && w.map.revision==revisionBefore);
}
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

void malformedDocumentRejected() {
    for (bool worldSpace : {false, true}) {
        MapDoc map; map.reset(8,8);
        map.data.worldSpace = worldSpace;
        map.data.extra = Json::array();
        const auto before = map.data.extra;
        const auto revision = map.revision;
        const auto undo = map.canUndo();
        std::string error;
        CHECK(!apply(map, defaultConfig(), &error));
        CHECK(!error.empty());
        CHECK(map.data.extra == before && map.revision == revision);
        CHECK(map.canUndo() == undo);
    }
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
    mcpAuthoring();
    plainMapAndUndo();
    rejectionIsTransactional();
    malformedDocumentRejected();
    fpsDocumentRoundtrip();
    std::printf("PHYSIS authoring: %s\n",failed?"FAIL":"PASS");
    return failed?1:0;
}

TEST(animation_editor_asset_roundtrip_and_conflicts) {
    auto dir=testDir("animation_editor");editor::Workspace ws;ws.project.dir=dir.string();
    Json graph=Json::parse(R"({"parameters":{},"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})");
    Json p=Json::object();p.set("name",Json::string("character"));p.set("graph",graph);
    auto r=ws.call("animation_graph_set",p);CHECK(!r.has("error"));CHECK(tfs::exists(dir/"animations/character.json"));
    r=ws.call("animation_graph_get",p);CHECK(r["graph"]==graph);
    auto changed=graph;changed.set("editorNote",Json::string("preserved"));p.set("graph",changed);CHECK(ws.call("animation_graph_set",p).has("error"));
    p.set("expected",graph);CHECK(!ws.call("animation_graph_set",p).has("error"));CHECK(ws.call("animation_graph_get",p)["graph"]==changed);
    p.set("graph",graph);CHECK(ws.call("animation_graph_set",p).has("error"));
    p.set("name",Json::string("../escape"));CHECK(ws.call("animation_graph_set",p).has("error"));
    p.set("name",Json::string("cycle"));p.set("graph",Json::parse(R"({"nodes":[{"id":"a","kind":"rig","input":"a","units":[]}],"output":"a"})"));CHECK(ws.call("animation_graph_set",p).has("error"));CHECK(!tfs::exists(dir/"animations/cycle.json"));
    CHECK(ws.call("animation_graph_list",Json::object())["graphs"].size()==1);
    std::error_code ec;tfs::remove_all(dir,ec);
}

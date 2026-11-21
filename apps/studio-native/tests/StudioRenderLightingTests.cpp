#include "apps/studio-native/StudioSession.h"
#include "engine/render/RenderLightingSettings.h"
#include "apps/editor/EditorSceneNodes.h"
#include "apps/studio-native/StudioDocumentIds.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    check(argc==2,"external fixture directory required");
    const std::filesystem::path root=argv[1];
    for(const char* genre:{"rts","fps"}) {
        const auto dir=root/"templates"/genre;
        std::filesystem::create_directories(dir);
        std::ofstream(dir/"game.json")<<"{\"name\":\"Lighting fixture\",\"module\":\""<<genre<<"\",\"maps\":[{\"file\":\"map.json\"}]}";
        std::ofstream(dir/"rules.json")<<R"({"fps":{},"types":{"unit":{"health":{"max":100}}}})";
        std::ofstream(dir/"map.json")<<(std::string(genre)=="fps"?R"({"name":"Fixture","colliders":[],"spawns":[[0,1,0]],"players":[{"name":"P1","team":1}]})":R"({"name":"Fixture","size":[8,8]})");
        studio::StudioSession session(root.string(),dir.string());
        auto call=[&](const char* method,Json params=Json::object()){
            Json req=Json::object();req.set("id",Json::number(int64_t(1)));req.set("method",Json::string(method));req.set("params",params);
            return session.dispatch(req);
        };
        auto get=[&](){auto r=call("studio.renderLighting");check(!r.has("error"),"get rejected");return r["result"];};
        const auto before=get();check(before.getStr("indirect")=="off","legacy default");
        const auto changed=call("studio.renderLightingSet",Json::parse(R"({"patch":{"indirect":"auto","direct":"luma","probes":6}})"));
        if(changed.has("error"))throw std::runtime_error(changed.dump());
        const auto after=get();check(after.getStr("direct")=="luma","set missing");
        const auto revision=session.workspace().map.revision;
        for(const char* patch:{R"({"patch":{"probes":3}})",R"({"patch":{"indirect":"bad"}})",R"({"patch":{"unknown":1}})",R"({"patch":{"samples":8.5}})",R"({"patch":{"version":2}})"}) {
            check(call("studio.renderLightingSet",Json::parse(patch)).has("error"),"invalid patch accepted");
            check(get()==after && session.workspace().map.revision==revision,"invalid mutation");
        }
        check(!call("studio.undo").has("error") && get()==before,"undo");
        check(!call("studio.redo").has("error") && get()==after,"redo");
        check(!call("studio.save").has("error"),"save");
        studio::StudioSession reopened(root.string(),dir.string());
        Json req=Json::parse(R"({"id":1,"method":"studio.renderLighting","params":{}})");
        check(reopened.dispatch(req)["result"]==after,"reopen");
        check(Json::parseFile((dir/"map.json").string())["renderLighting"]==after,"disk serialization");
        // One canonical light payload, stable entity identity across index shifts.
        check(!call("studio.lightAdd").has("error"),"add first light");
        const auto added=call("studio.lightAdd");check(!added.has("error"),"add second light");
        const auto id0=added["result"]["lights"][0].getStr("entityId");
        const auto id1=added["result"]["lights"][1].getStr("entityId");
        check(!id0.empty() && !id1.empty() && id0!=id1,"unique light entity IDs");
        Json selection=Json::object();selection.set("id",Json::string("light:"+id1));
        check(!call("studio.select",selection).has("error"),"select light entity");
        check(!call("studio.lightDelete",Json::parse(R"({"index":0})")).has("error"),"delete earlier light");
        auto snap=call("studio.snapshot")["result"];
        check(snap.getStr("selectedId")=="light:"+id1,"selection changed after index shift");
        bool inHierarchy=false;
        for(const auto& row:snap["scene"]["entities"].items())
            if(row.getStr("id")=="light:"+id1)inHierarchy=row.getStr("kind")=="Light";
        check(inHierarchy && snap["components"].size()==2 && snap["properties"].size()==0,"light hierarchy and inspector");
        Json stale=Json::parse(R"({"index":1,"key":"intensity","value":2})");
        stale.set("entityId",Json::string("light:"+id1));
        check(!call("studio.lightUpdate",stale).has("error"),"stable ID resolves stale ordinal");
        check(!call("studio.undo").has("error"),"restore intensity after identity test");
        stale.set("entityId",Json::string("light:"+id0));
        check(call("studio.lightUpdate",stale).has("error"),"deleted identity must not alias surviving light");
        Json edit=Json::object();edit.set("entityId",Json::string("light:"+id1));
        edit.set("componentId",Json::string("Light"));edit.set("key",Json::string("intensity"));edit.set("value",Json::number(7.0));
        edit.set("componentId",Json::string("Transform"));
        check(call("studio.setProperty",edit).has("error"),"component ownership enforced");
        edit.set("componentId",Json::string("Light"));
        check(!call("studio.setProperty",edit).has("error"),"light inspector write");
        check(call("studio.lighting")["result"]["lights"][0]["intensity"].toDouble()==7,"shared canonical write");
        check(!call("studio.undo").has("error"),"light undo");
        check(call("studio.lighting")["result"]["lights"][0]["intensity"].toDouble()==1.5,"light undo value");
        check(!call("studio.redo").has("error"),"light redo");
        edit.set("value",Json::number(-1.0));check(call("studio.setProperty",edit).has("error"),"invalid light intensity");
        check(!call("studio.save").has("error"),"save entity light");
        studio::StudioSession lightReload(root.string(),dir.string());
        Json snapshotRequest=Json::parse(R"({"id":2,"method":"studio.snapshot","params":{}})");
        bool retained=false;
        const auto loadedSnapshot=lightReload.dispatch(snapshotRequest);
        for(const auto& row:loadedSnapshot["result"]["scene"]["entities"].items())
            if(row.getStr("id")=="light:"+id1)retained=true;
        check(retained,"entity ID persists on reopen");
        auto& map=session.workspace().map;
        const auto ref=editor::DocumentEntityRef::light(0,std::string(genre)=="fps");
        const auto components=editor::documentComponents(map,ref);
        check(components.size()==2 && components[1].kind==editor::DocumentComponentKind::Light,"light components");
        editor::MapDoc draft;draft.data=map.data;
        check(editor::setDocumentPosition(draft,ref,{2,3,4}),"common transform write");
        editor::DocumentTransform position;check(editor::readDocumentTransform(draft,ref,position) && position.y==3,"common transform read");
        check(editor::writeDocumentComponent(draft,ref,editor::DocumentComponentKind::Light,Json::parse(R"({"radius":9})")),"common Light write");
        check(editor::documentLight(draft,ref)->getStr("entityId")==id1,"component write preserves ID");

    }
    std::cout<<"Studio render lighting persistence PASS\n";
}

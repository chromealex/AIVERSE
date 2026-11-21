// Regression checks for the RTS/FPS authored-object source adapter.
#include "apps/editor/EditorSceneNodes.h"
#include "apps/editor/AuthoringWater.h"
#include "apps/editor/EditorWorldEdit.h"
#include <cstdlib>
#include <cmath>
#include <vector>

using namespace aiverse;
using namespace aiverse::editor;

static void require(bool condition) {
    // Active in Release builds too; assertions are compiled out by NDEBUG.
    if (!condition) std::abort();
}

int main() {
    MapDoc map;
    map.reset(16,16);
    require(map.data.extra.getStr("terrainPresentation")=="entities");
    require(map.data.entities.empty());
    {
        MapDoc authored;authored.reset(16,16);
        DocEntity tree;tree.id="tree-1";tree.type="tree";tree.x=2;tree.z=3;
        tree.extra=Json::parse(R"({"components":{"Transform":{"y":4,"yaw":90,"scale":2},"StaticModel":{"asset":"content/tree.gltf"},"TerraTree":{"seed":42}}})");
        authored.data.entities.push_back(tree);
        const auto ref=DocumentEntityRef::mapEntity(0);
        DocumentTransform p;require(readDocumentTransform(authored,ref,p)&&p.y==4);
        authored.beginEdit();require(translateDocumentEntity(authored,ref,{1,2,-1}));
        require(readDocumentTransform(authored,ref,p)&&p.x==3&&p.y==6&&p.z==2);
        authored.undo();require(readDocumentTransform(authored,ref,p)&&p.y==4&&p.x==2);
        authored.redo();require(readDocumentTransform(authored,ref,p)&&p.y==6);
        auto transform=Json::parse(R"({"rotationY":45,"uniformScale":1.5})");
        require(writeDocumentComponent(authored,ref,DocumentComponentKind::Transform,transform));
        const auto before=authored.toJson();transform.set("uniformScale",Json::number(-1.0));
        require(!writeDocumentComponent(authored,ref,DocumentComponentKind::Transform,transform));require(authored.toJson()==before);
        const auto copies=worldedit::duplicate(authored,{0});require(copies.size()==1&&authored.data.entities[1].id!="tree-1");
        require(authored.data.entities[1].extra==authored.data.entities[0].extra);
        DocEntity river;river.id="river-1";river.x=10;river.z=20;
        river.extra=Json::parse(R"({"components":{"Transform":{"y":3,"yaw":0,"scale":1},"NereidWater":{"id":7,"kind":"river","shape":"river","surfaceY":2,"bottomY":0,"points":[[0,0,3],[0,16,3]],"grade":{"origin":[0,0],"slopeDegrees":1}}}})");
        authored.data.entities.push_back(river);
        auto water=world::parseNereidWaterManifest(authoredWaterDocument(authored));require(water.surfaces.size()==1);
        require(water.surfaces[0].body.surfaceY==5_fx && water.surfaces[0].geometry.points[0].x==10_fx && water.surfaces[0].geometry.points[0].z==20_fx);
        editAuthoredWaterFlow(authored,Json::parse(R"({"id":7,"mode":"manual","flow":[1,0,0]})"));
        require(!authored.data.extra.has("nereidWater"));require(authored.data.entities[2].extra["components"]["NereidWater"]["flow"][0].integer()==1);
        authored.undo();require(!authored.data.entities[2].extra["components"]["NereidWater"].has("flow"));
    }
    DocEntity sedan;
    sedan.id = "sedan_01";
    sedan.type = "sedan";
    sedan.x = 32.0f;
    sedan.z = 64.0f;
    map.data.entities.push_back(sedan);

    const auto rts = enumerateDocumentSceneNodes(map, false);
    require(rts.size() == 1);
    require(rts[0].ref.source == DocumentEntityRef::Source::MapEntity);
    require(rts[0].label == "sedan #sedan_01");
    require(rts[0].hasPosition);
    require(rts[0].position.x == 32.0 && rts[0].position.z == 64.0);
    require(translateDocumentEntity(map, rts[0].ref, {1.0, 0.0, -2.0}));
    require(map.data.entities[0].x == 33.0f && map.data.entities[0].z == 62.0f);
    // RTS map coordinates are planar; do not silently drop a Y movement.
    require(!translateDocumentEntity(map, rts[0].ref, {0.0, 1.0, 0.0}));

    Json lower = Json::array();
    lower.push(Json::number(0.0)); lower.push(Json::number(1.0)); lower.push(Json::number(2.0));
    Json upper = Json::array();
    upper.push(Json::number(4.0)); upper.push(Json::number(5.0)); upper.push(Json::number(6.0));
    Json collider = Json::object();
    collider.set("min", lower);
    collider.set("max", upper);
    Json colliders = Json::array();
    colliders.push(collider);
    Json spawn = Json::array();
    spawn.push(Json::number(7.0)); spawn.push(Json::number(8.0)); spawn.push(Json::number(9.0));
    Json spawns = Json::array();
    spawns.push(spawn);
    map.data.fps = Json::object();
    map.data.fps.set("colliders", colliders);
    map.data.fps.set("spawns", spawns);

    const auto fps = enumerateDocumentSceneNodes(map, true);
    require(fps.size() == 2);
    require(fps[0].group == "Colliders" && fps[0].label == "Box Collider #1");
    require(fps[1].group == "Spawn Points" && fps[1].label == "Spawn #1");
    require(fps[0].hasPosition && fps[1].hasPosition);
    require(fps[0].position.x == 2 && fps[0].position.y == 3 && fps[0].position.z == 4);
    require(fps[1].position.x == 7 && fps[1].position.y == 8 && fps[1].position.z == 9);

    DocumentEntityRef decoded;
    for (const auto& node : fps) {
        const uint64_t key = node.ref.fpsKey();
        require(key != 0);
        require(DocumentEntityRef::fromFpsKey(key, decoded));
        require(decoded.source == node.ref.source && decoded.index == node.ref.index);
    }
    // Batch movement must deduplicate sources, and must be atomic on invalid input.
    const auto before = map.revision;
    require(translateDocumentEntities(map, {fps[0].ref, fps[0].ref, fps[1].ref}, {1, 0, 0}));
    require(map.revision == before + 1);
    DocumentTransform moved;
    require(readDocumentTransform(map, fps[0].ref, moved));
    require(moved.x == 3.0);
    require(readDocumentTransform(map, fps[1].ref, moved));
    require(moved.x == 8.0);

    const auto stable = map.revision;
    const auto invalid = DocumentEntityRef::fpsSpawn(1000);
    require(!translateDocumentEntities(map, {fps[0].ref, invalid}, {1, 0, 0}));
    require(map.revision == stable);
    require(readDocumentTransform(map, fps[0].ref, moved));
    require(moved.x == 3.0);
}

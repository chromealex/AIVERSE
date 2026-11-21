#include "apps/editor/EditorEntityAdapter.h"
#include "apps/editor/EditorTransformGizmo.h"
#include <cstdlib>
#include <cmath>

using namespace aiverse;
using namespace aiverse::editor;

static void require(bool ok) { if (!ok) std::abort(); }

int main() {
    MapDoc map;
    DocEntity sedan;
    sedan.id = "sedan";
    sedan.type = "sedan";
    sedan.x = 10.0f;
    sedan.z = 12.0f;
    sedan.extra.set("facing", Json::number(90.0));
    map.data.entities.push_back(sedan);
    const auto ref = DocumentEntityRef::mapEntity(0);

    Json component;
    require(readDocumentComponent(map, ref, DocumentComponentKind::Transform,
                                  component));
    require(component["rotationY"].isNumber());
    require(component["rotationY"].toDouble() == 90.0);
    require(!component["scale"].isArray()); // RTS lacks per-entity Scale

    Json change = Json::object();
    change.set("rotationY", Json::number(180.0));
    const uint64_t before = map.revision;
    require(writeDocumentComponent(map, ref, DocumentComponentKind::Transform,
                                   change));
    require(map.revision == before + 1);
    require(map.data.entities[0].extra["facing"].toDouble() == 180.0);
    require(map.data.entities[0].x == 10.0f && map.data.entities[0].z == 12.0f);

    // Objects lacking authored orientation must not invent it.
    map.data.entities[0].extra.erase("facing");
    require(!writeDocumentComponent(map, ref, DocumentComponentKind::Transform,
                                    change));

    // Review fix 2026-11-21: rotationY + position in one patch applies both.
    map.data.entities[0].extra.set("facing", Json::number(90.0));
    Json both = Json::object();
    both.set("rotationY", Json::number(45.0));
    Json where = Json::array();
    where.push(Json::number(20.0)); where.push(Json::number(0.0)); where.push(Json::number(30.0));
    both.set("position", where);
    require(writeDocumentComponent(map, ref, DocumentComponentKind::Transform, both));
    require(map.data.entities[0].extra["facing"].toDouble() == 45.0);
    require(map.data.entities[0].x == 20.0f && map.data.entities[0].z == 30.0f);
    // Without authored facing the position part is still written (no facing invented).
    map.data.entities[0].extra.erase("facing");
    where = Json::array();
    where.push(Json::number(21.0)); where.push(Json::number(0.0)); where.push(Json::number(31.0));
    both.set("position", where);
    require(writeDocumentComponent(map, ref, DocumentComponentKind::Transform, both));
    require(!map.data.entities[0].extra["facing"].isNumber());
    require(map.data.entities[0].x == 21.0f && map.data.entities[0].z == 31.0f);

    // Legacy map light "pos":[x,z] is read like the runtime reads it: a move
    // starts from pos, not from x = z = 0.
    {
        MapDoc lights;
        Json light = Json::object();
        Json pos = Json::array(); pos.push(Json::number(5.0)); pos.push(Json::number(7.0));
        light.set("pos", pos);
        light.set("radius", Json::number(3.0));
        Json list = Json::array(); list.push(light);
        lights.data.extra = Json::object();
        lights.data.extra.set("lights", list);
        const auto lightRef = DocumentEntityRef::light(0, false);
        DocumentTransform t;
        require(readDocumentTransform(lights, lightRef, t));
        require(t.x == 5.0 && t.z == 7.0);
        require(translateDocumentEntity(lights, lightRef, {1.0, 0.0, 1.0}));
        const Json& moved = lights.data.extra["lights"][size_t(0)];
        require(moved["x"].toDouble() == 6.0 && moved["z"].toDouble() == 8.0);
        require(!moved["pos"].isArray());
    }

    render::RTSCamera camera;
    camera.target = {0, 0, 0};
    camera.distance = 18;
    camera.pitch = 0.9f;
    render::float2 cursor;
    constexpr float radius = 2.0f;
    require(camera.worldToScreen({radius, 0, 0}, 800, 600, cursor));
    require(pickRotateYawGizmoRing(camera, 800, 600, 0, 0,
                                   cursor.x, cursor.y,
                                   {0, 0, 0}, radius));
    require(!pickRotateYawGizmoRing(camera, 800, 600, 0, 0,
                                    -500, -500, {0, 0, 0}, radius));
}

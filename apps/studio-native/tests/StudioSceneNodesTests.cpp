#include "apps/editor/EditorSceneNodes.h"
#include "tests/TestCheck.h"
#include <cmath>
#include <iostream>

int main() {
    using namespace aiverse;
    editor::MapDoc map;
    editor::DocEntity placed;
    placed.id = "alpha";
    placed.type = "tower";
    placed.x = 4.25f;
    placed.z = -2.5f;
    map.data.entities.push_back(placed);
    auto nodes = editor::enumerateDocumentSceneNodes(map, false);
    AIVERSE_TEST_CHECK(nodes.size() == 1);
    AIVERSE_TEST_CHECK(nodes[0].ref.source == editor::DocumentEntityRef::Source::MapEntity);
    AIVERSE_TEST_CHECK(nodes[0].label == "tower #alpha");
    AIVERSE_TEST_CHECK(nodes[0].hasPosition && std::abs(nodes[0].position.x - 4.25) < .0001);

    map.data.fps = Json::parse(R"({
        "colliders":[{"min":[0,1,2],"max":[10,11,12]}],
        "spawns":[[2,3,4]]
    })");
    nodes = editor::enumerateDocumentSceneNodes(map, true);
    AIVERSE_TEST_CHECK(nodes.size() == 2);
    AIVERSE_TEST_CHECK(nodes[0].ref.source == editor::DocumentEntityRef::Source::FpsCollider);
    AIVERSE_TEST_CHECK(nodes[0].group == "Colliders" && nodes[0].hasPosition);
    AIVERSE_TEST_CHECK(std::abs(nodes[0].position.x - 5.0) < .0001);
    AIVERSE_TEST_CHECK(nodes[1].ref.source == editor::DocumentEntityRef::Source::FpsSpawn);
    AIVERSE_TEST_CHECK(nodes[1].hasPosition && nodes[1].position.y == 3);
    editor::DocumentTransform target = nodes[1].position;
    target.x = 7; target.y = 9;
    AIVERSE_TEST_CHECK(editor::setDocumentPosition(map,nodes[1].ref,target));
    editor::DocumentTransform read{};
    AIVERSE_TEST_CHECK(editor::readDocumentTransform(map,nodes[1].ref,read));
    AIVERSE_TEST_CHECK(read.x == 7 && read.y == 9 && read.z == 4);

    // AABB collider transforms preserve half-extents through the shared adapter.
    const auto collider=nodes[0].ref;
    AIVERSE_TEST_CHECK(editor::setDocumentPosition(map,collider,{8,7,6}));
    AIVERSE_TEST_CHECK(editor::readDocumentTransform(map,collider,read));
    AIVERSE_TEST_CHECK(read.x == 8 && read.y == 7 && read.z == 6);
    AIVERSE_TEST_CHECK(map.data.fps["colliders"][0]["min"][0].toDouble() == 3);
    AIVERSE_TEST_CHECK(map.data.fps["colliders"][0]["max"][0].toDouble() == 13);
    std::cout << "Studio scene node contract PASS\n";
    return 0;
}

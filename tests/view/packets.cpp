#include "engine/view/View.h"
#include <cassert>
#include <cstddef>
#include <variant>
#include <vector>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::view;
    ScenePacketBatch batch;
    batch.reserve(6);
    Object object;object.id=42;object.generation=7;
    MeshObject mesh;mesh.flags=128;
    LineObject line{{0,0,0},{0,1,0},0xff};
    TriangleObject triangle{{0,0,0},{1,0,0},{0,1,0},0xffffffff};
    CircleXZObject circle{{3,0,4},.7f,0xff00ff00u,24,.05f};
    QuadXZObject ribbon{{0,0,0},{4,0,5},.5f,0xffaabbccu,.04f};
    batch.add(object);
    batch.add(mesh);
    batch.add(line);
    batch.add(triangle);
    batch.add(circle);
    batch.add(ribbon);
    assert(batch.size()==6);
    const auto& packets=batch.packets();
    assert(std::holds_alternative<Object>(packets[0]));
    assert(std::get<Object>(packets[0]).id==42);
    assert(std::holds_alternative<MeshObject>(packets[1]));
    assert(std::get<MeshObject>(packets[1]).flags==128);
    assert(std::holds_alternative<LineObject>(packets[2]));
    assert(std::get<LineObject>(packets[2]).end.y==1.f);
    assert(std::holds_alternative<TriangleObject>(packets[3]));
    assert(std::get<TriangleObject>(packets[3]).c.y==1.f);
    assert(std::holds_alternative<CircleXZObject>(packets[4]));
    assert(std::get<CircleXZObject>(packets[4]).segments==24);
    assert(std::holds_alternative<QuadXZObject>(packets[5]));
    assert(std::get<QuadXZObject>(packets[5]).start.x==0.f);
    assert(std::get<QuadXZObject>(packets[5]).end.z==5.f);
    assert(std::get<QuadXZObject>(packets[5]).width==.5f);
    assert(std::get<QuadXZObject>(packets[5]).y==.04f);
    // Updating the same mixed stream must count only Object packets;
    // empty/model-less objects must not allocate animation sources.
    View scene(0);
    const auto first=scene.update(batch,1.f/60.f);
    assert(first.visible==1);
    assert(first.drawable==0);
    assert(first.animated==0);
    assert(scene.presentationEntity(42)==kNullEntity);
    const auto second=scene.update(batch,1.f/60.f);
    assert(second.visible==first.visible);
    assert(second.animated==first.animated);
    batch.clear();assert(batch.size()==0);
    const auto empty=scene.update(batch,1.f/60.f);
    assert(empty.visible==0 && empty.animated==0);
    // A host with a separate presentation World (e.g. montage/crowd
    // controller) can stage precomputed model packets without supplying
    // their graph to View. The mixed order is not sorted or deduplicated.
    ScenePacketBatch precomputed;
    Object alive;alive.id=5;alive.generation=2;
    MeshObject fallback;fallback.flags=73;
    Object dead;dead.id=9;dead.generation=3;
    precomputed.add(alive);
    precomputed.add(fallback);
    precomputed.add(dead);
    assert(precomputed.size()==3);
    assert(std::get<Object>(precomputed.packets()[0]).id==5);
    assert(std::get<MeshObject>(precomputed.packets()[1]).flags==73);
    assert(std::get<Object>(precomputed.packets()[2]).id==9);
    // Without an attached graph, View does not create a second player.
    const auto staged=scene.update(precomputed,1.f/60.f);
    assert(staged.animated==0);
    assert(scene.presentationEntity(5)==kNullEntity);
    assert(scene.presentationEntity(9)==kNullEntity);
}

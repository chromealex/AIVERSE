// LENS final-ModelDraw snapshot: borrowed transient data must be owned.
#include "engine/render/CameraModelSnapshot.h"
#include <cassert>
using namespace aiverse::render;
int main(){
    CameraModelSnapshot snapshot(1);
    Model model;
    std::vector<mat4> nodes{mat4{}};
    std::vector<NodePose> pose(1);
    std::vector<Material> materials(1);
    std::vector<int> nodeMaterials{3};
    BakedSample baked{};
    ModelDraw draw;
    draw.model=&model;
    draw.persistentId=73;
    draw.nodeWorld=&nodes;
    draw.pose=&pose;
    draw.materials=&materials;
    draw.nodeMaterial=&nodeMaterials;
    draw.baked=&baked;
    snapshot.record(draw);
    // Source containers can be destroyed immediately after primary draw.
    nodes.clear();pose.clear();materials.clear();nodeMaterials.clear();
    draw.persistentId=999;
    size_t visits=0;
    snapshot.forEach([&](const ModelDraw& captured){
        ++visits;
        assert(captured.persistentId==73);
        assert(captured.model==&model);
        assert(captured.nodeWorld && captured.nodeWorld->size()==1);
        assert(captured.pose && captured.pose->size()==1);
        assert(captured.materials && captured.materials->size()==1);
        assert(captured.nodeMaterial && captured.nodeMaterial->at(0)==3);
        assert(captured.baked);
        assert(!captured.staticCache);
    });
    snapshot.forEach([&](const ModelDraw& captured){
        ++visits;
        assert(captured.persistentId==73);
    });
    assert(visits==2 && snapshot.size()==1);
    snapshot.record(draw);
    assert(snapshot.dropped()==1 && snapshot.size()==1);
    snapshot.clear();
    assert(snapshot.size()==0 && snapshot.dropped()==0);

    // A pre-LOD packet replaces only its matching finalized base draw;
    // unrelated attachments and legacy draws still replay exactly once.
    CameraModelSnapshot mixed(8);
    aiverse::view::View visual(0);
    aiverse::view::Object prepared;
    prepared.id=43;prepared.generation=2;
    prepared.draw.model=&model;
    prepared.draw.persistentId=73;
    std::vector<Material> preparedMaterial(1);
    prepared.draw.materials=&preparedMaterial;
    mixed.recordPrepared(visual,prepared);
    mixed.record(prepared.draw);
    ModelDraw attachment;
    attachment.model=&model;
    attachment.persistentId=0;
    mixed.record(attachment);
    preparedMaterial.clear();
    assert(mixed.size()==2 && mixed.preparedCount()==1);
    assert(mixed.isPreparedDraw(73) && !mixed.isPreparedDraw(74));
    int fallbacks=0,preparedCount=0;
    mixed.forEachFallback([&](const ModelDraw& retained){
        ++fallbacks;
        assert(retained.persistentId==0);
    });
    mixed.forEachPrepared([&](const aiverse::view::View& source,
                              const aiverse::view::Object& retained){
        ++preparedCount;
        assert(&source==&visual);
        assert(retained.id==43 && retained.draw.persistentId==73);
        assert(retained.draw.materials && retained.draw.materials->size()==1);
    });
    assert(fallbacks==1 && preparedCount==1);
    mixed.clear();
    assert(mixed.size()==0 && mixed.preparedCount()==0);

    // Geometry packets are value-owned, except for the host-owned Mesh.
    // All primitive coordinates and draw parameters survive source mutation.
    CameraGeometryCommand triangle;
    triangle.kind=CameraGeometryCommand::Kind::Triangle;
    triangle.a={1,2,3};triangle.b={4,5,6};triangle.c={7,8,9};
    triangle.rgba=0xff113355u;
    mixed.recordGeometry(triangle);
    triangle.a={100,200,300};
    CameraGeometryCommand line;
    line.kind=CameraGeometryCommand::Kind::Line;
    line.a={0,1,2};line.b={3,4,5};
    mixed.recordGeometry(line);
    int geometryVisits=0;
    mixed.forEachGeometry([&](const CameraGeometryCommand& packet) {
        if(geometryVisits==0) {
            assert(packet.kind==CameraGeometryCommand::Kind::Triangle);
            assert(packet.a.x==1.f && packet.b.y==5.f);
            assert(packet.rgba==0xff113355u);
        } else {
            assert(packet.kind==CameraGeometryCommand::Kind::Line);
            assert(packet.a.y==1.f && packet.b.z==5.f);
        }
        ++geometryVisits;
    });
    assert(mixed.geometryCount()==2 && geometryVisits==2);
    mixed.clear();
    assert(mixed.geometryCount()==0);
}

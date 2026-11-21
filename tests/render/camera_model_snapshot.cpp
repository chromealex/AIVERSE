// LENS final-ModelDraw snapshot: borrowed transient data must be owned.
#include "engine/render/CameraModelSnapshot.h"
#include "tests/AlwaysCheck.h"
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
        AIVERSE_TEST_CHECK(captured.persistentId==73);
        AIVERSE_TEST_CHECK(captured.model==&model);
        AIVERSE_TEST_CHECK(captured.nodeWorld && captured.nodeWorld->size()==1);
        AIVERSE_TEST_CHECK(captured.pose && captured.pose->size()==1);
        AIVERSE_TEST_CHECK(captured.materials && captured.materials->size()==1);
        AIVERSE_TEST_CHECK(captured.nodeMaterial && captured.nodeMaterial->at(0)==3);
        AIVERSE_TEST_CHECK(captured.baked);
        AIVERSE_TEST_CHECK(!captured.staticCache);
    });
    snapshot.forEach([&](const ModelDraw& captured){
        ++visits;
        AIVERSE_TEST_CHECK(captured.persistentId==73);
    });
    AIVERSE_TEST_CHECK(visits==2 && snapshot.size()==1);
    snapshot.record(draw);
    AIVERSE_TEST_CHECK(snapshot.dropped()==1 && snapshot.size()==1);
    snapshot.clear();
    AIVERSE_TEST_CHECK(snapshot.size()==0 && snapshot.dropped()==0);

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
    AIVERSE_TEST_CHECK(mixed.size()==2 && mixed.preparedCount()==1);
    AIVERSE_TEST_CHECK(mixed.isPreparedDraw(73) && !mixed.isPreparedDraw(74));
    int fallbacks=0,preparedCount=0;
    mixed.forEachFallback([&](const ModelDraw& retained){
        ++fallbacks;
        AIVERSE_TEST_CHECK(retained.persistentId==0);
    });
    mixed.forEachPrepared([&](const aiverse::view::View& source,
                              const aiverse::view::Object& retained){
        ++preparedCount;
        AIVERSE_TEST_CHECK(&source==&visual);
        AIVERSE_TEST_CHECK(retained.id==43 && retained.draw.persistentId==73);
        AIVERSE_TEST_CHECK(retained.draw.materials && retained.draw.materials->size()==1);
    });
    AIVERSE_TEST_CHECK(fallbacks==1 && preparedCount==1);
    mixed.clear();
    AIVERSE_TEST_CHECK(mixed.size()==0 && mixed.preparedCount()==0);

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
            AIVERSE_TEST_CHECK(packet.kind==CameraGeometryCommand::Kind::Triangle);
            AIVERSE_TEST_CHECK(packet.a.x==1.f && packet.b.y==5.f);
            AIVERSE_TEST_CHECK(packet.rgba==0xff113355u);
        } else {
            AIVERSE_TEST_CHECK(packet.kind==CameraGeometryCommand::Kind::Line);
            AIVERSE_TEST_CHECK(packet.a.y==1.f && packet.b.z==5.f);
        }
        ++geometryVisits;
    });
    AIVERSE_TEST_CHECK(mixed.geometryCount()==2 && geometryVisits==2);
    mixed.clear();
    AIVERSE_TEST_CHECK(mixed.geometryCount()==0);

    // Specialized water and selection packets retain shader and outline
    // parameters, rather than becoming incomplete ordinary Mesh commands.
    CameraGeometryCommand water;
    water.kind=CameraGeometryCommand::Kind::Water;
    water.mesh=nullptr;
    water.ripples[0]={1.f,2.f,3.f,4.f};
    water.wakes[2]={5.f,6.f,7.f,8.f};
    water.wind={9.f,10.f,11.f,12.f};
    water.firstIndex=10;water.indexCount=22;
    mixed.recordGeometry(water);
    CameraGeometryCommand selection;
    selection.kind=CameraGeometryCommand::Kind::Selection;
    selection.selection={.3f,.5f,.8f,1.f};
    mixed.recordGeometry(selection);
    int specials=0;
    mixed.forEachGeometry([&](const CameraGeometryCommand& packet) {
        if(specials==0) {
            AIVERSE_TEST_CHECK(packet.kind==CameraGeometryCommand::Kind::Water);
            AIVERSE_TEST_CHECK(packet.ripples[0].w==4.f);
            AIVERSE_TEST_CHECK(packet.wakes[2].z==7.f);
            AIVERSE_TEST_CHECK(packet.wind.x==9.f);
            AIVERSE_TEST_CHECK(packet.firstIndex==10&&packet.indexCount==22);
        } else {
            AIVERSE_TEST_CHECK(packet.kind==CameraGeometryCommand::Kind::Selection);
            AIVERSE_TEST_CHECK(packet.selection.y==.5f);
        }
        ++specials;
    });
    AIVERSE_TEST_CHECK(specials==2);
    mixed.clear();
    {
        Mesh temporary;temporary.vb={71};temporary.ib={72};temporary.indexCount=9;
        CameraGeometryCommand mesh;mesh.mesh=&temporary;
        mixed.recordGeometry(mesh);
        temporary.indexCount=100;
    }
    auto retained=mixed;
    mixed.clear();
    retained.forEachGeometry([](const CameraGeometryCommand& packet){
        AIVERSE_TEST_CHECK(packet.mesh && packet.mesh->vb.id==71 && packet.mesh->indexCount==9);
    });
}

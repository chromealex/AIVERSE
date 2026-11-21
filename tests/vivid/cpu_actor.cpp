#include "engine/character/vivid/CpuActorStream.h"
#include "tests/cloth/FakeDevice.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::vivid;
namespace {
struct TestDevice:FakeDevice {
    bool waitForResourceIdle()override{return !inFrame;}
};
void expect(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
void run() {
    TestDevice device;
    device.info_.backend=gfx::Backend::GLES3;
    // GLES3/WebGL2 has no compute and can report zero frame serial.
    expect(!device.supportsCompute()&&!device.frameSerial(),
           "fake GLES capability contract mismatch");
    render::Model model;
    model.vertices.resize(3);
    model.vertices[0].pos={0,0,0};
    model.vertices[1].pos={1,0,0};
    model.vertices[2].pos={0,1,0};
    model.vertices[0].nrm=model.vertices[1].nrm=model.vertices[2].nrm={0,0,1};
    model.indices={0,1,2};
    model.vb={42};model.ib={43};
    render::MeshDef mesh;
    render::Primitive primitive;
    primitive.firstVertex=0;primitive.vertexCount=3;
    primitive.firstIndex=0;primitive.indexCount=3;
    mesh.primitives.push_back(primitive);
    model.meshes.push_back(mesh);
    MorphTargetInput target;
    target.name="jawOpen";
    MorphDelta delta;
    delta.vertex=1;
    delta.position={0,0.25f,0};
    target.deltas.push_back(delta);
    auto morphs=SparseMorphLibrary::compile(3,{target});
    const std::vector<ShapeRoute> routes{{Channel::JawOpen,"jawOpen",1.0f}};
    const auto binding=FaceShapeBinding::compile(morphs,routes);
    CpuActorStream actor;
    expect(actor.initialize(device,model,morphs,model.vertices),
           "GLES CPU actor rejected valid full model vertex stream");
    // Publishing a HumanPreviewAsset moves and destroys the staged library.
    // The actor must keep valid immutable data across that owner transition.
    morphs=SparseMorphLibrary{};
    FaceFrame frame{};
    frame.values[static_cast<std::size_t>(Channel::JawOpen)]=1.0f;
    device.beginFrame(64,64);
    expect(actor.dispatch(frame,binding),
           "GLES CPU morph actor dispatch failed");
    render::ModelDraw draw;
    draw.model=&model;
    expect(actor.attach(draw)&&draw.vertexOverride.valid()&&
           draw.vertexOverrideCount==3,
           "GLES CPU actor did not attach full ModelVertex stream");
    const auto& bytes=device.buffers.at(draw.vertexOverride.id);
    expect(bytes.size()==3*sizeof(render::ModelVertex),
           "GLES morph buffer wrong size");
    render::ModelVertex moved{};
    std::memcpy(&moved,bytes.data()+sizeof(render::ModelVertex),sizeof(moved));
    expect(std::fabs(moved.pos.y-0.25f)<1.e-4f &&
           std::fabs(moved.pos.x-1.0f)<1.e-4f,
           "GLES fallback failed to deform mapped source vertex");
    device.endFrame();
    expect(actor.shutdownAfterIdle()&&!actor.ready(),
           "GLES CPU actor did not retire vertex buffer at idle");
}
}
int main(){try{run();std::cout<<"VIVID CPU actor GLES fallback PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID CPU actor FAIL: "<<e.what()<<"\n";return 1;}
return 0;}

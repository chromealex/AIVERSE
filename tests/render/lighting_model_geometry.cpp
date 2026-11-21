#include "engine/render/GiModelGeometry.h"
#include "engine/render/GiChunkedGeometry.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse::render;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void near(float a,float b){require(std::abs(a-b)<1e-5f,"position mismatch");}
int main() {
    Model model;model.materials.resize(1);model.meshes.resize(1);model.nodes.resize(2);
    model.nodes[0].mesh=0;model.nodes[0].t={10,0,0};
    model.vertices.resize(3);model.vertices[0].pos={-1,-1,0};model.vertices[1].pos={1,-1,0};model.vertices[2].pos={0,1,0};
    model.indices={0,1,2};Primitive primitive;primitive.indexCount=3;primitive.vertexCount=3;
    model.meshes[0].primitives.push_back(primitive);model.finalize();
    Pose pose;bindPose(model,pose);pose[0].t={0,0,2};
    auto rigid=giModelGeometry(model,mat4::translate({0,0,1}),&pose);
    rigid.build();near(rigid.distance({0,0,0},{0,0,1},10),3);
    auto original=giModelGeometry(model);near(original.triangles[0].a.x,9);
    // Joint matrices are model-space: ignore the mesh node transform and
    // normalize byte weights, exactly as the render shader does.
    model.skins.resize(1);model.skins[0].joints={1};model.skins[0].inverseBind={mat4::translate({0,0,-1})};
    model.nodes[0].skin=0;model.meshes[0].primitives[0].jointPalette={0};
    for(auto& v:model.vertices)v.weights[0]=127;
    pose[1].t={0,0,5};auto skinned=giModelGeometry(model,{},&pose);skinned.build();
    near(skinned.distance({0,0,0},{0,0,1},10),4);near(skinned.triangles[0].a.x,-1);
    // Morph is applied BEFORE joint inverse-bind/pose/world, not afterward.
    auto& p=model.meshes[0].primitives[0];p.morphTargets=1;p.morphDeltas.resize(18);
    for(int i=0;i<3;++i)p.morphDeltas[size_t(i)*6+2]=2;
    pose[0].w[0]=.5f;pose[1].s={1,1,2};
    auto morphed=giModelGeometry(model,{},&pose);morphed.build();near(morphed.distance({0,0,0},{0,0,1},10),5);
    // Immutable snapshot keeps old geometry while a later animated pose moves.
    GiChunkedGeometryBuilder builder;builder.replace(1,skinned.triangles,skinned.albedos);
    auto previous=builder.publish();builder.replace(1,morphed.triangles,morphed.albedos);
    near(previous.traceView().distance({0,0,0},{0,0,1},10),4);
    auto current=builder.publish();near(current.traceView().distance({0,0,0},{0,0,1},10),5);
    near(rigid.triangles[0].a.z,3);
    p.morphDeltas.clear();bool rejected=false;try{(void)giModelGeometry(model,{},&pose);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"missing CPU morph data must reject");
    model.materials[0].alpha=AlphaMode::Blend;require(giModelGeometry(model,{},&pose).triangles.empty(),"transparent caster included");
    std::puts("Lighting animated model geometry PASS");
}

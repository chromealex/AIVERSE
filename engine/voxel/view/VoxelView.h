#pragma once
// Presentation of the voxel game: chunk meshes on the GPU through the engine renderer (no custom shaders), vertex
// colours from block colours, face shade, ambient occlusion and voxel light (sky light scaled by daylight, warm block
// light), and the first-person look. Meshes are rebuilt for chunks the world marks dirty, nearest first, within a
// per-frame budget; a change of daylight recolours every chunk gradually.
#include "engine/render/Renderer3D.h"
#include <aiverselib/systems/voxelmesh/VoxelMesh.h>
#include <cmath>
#include <map>
#include <set>
namespace aiverse::voxelgame::view {
struct Look{
    float yaw=0,pitch=0; // turns, the same convention as the simulation input
    void motion(float dx,float dy,float sensitivity=0.0004f){
        if(!std::isfinite(dx)||!std::isfinite(dy))return;
        yaw=std::fmod(yaw-dx*sensitivity,1.0f);if(yaw<0)yaw+=1.0f;
        pitch=std::clamp(pitch-dy*sensitivity,-0.248f,0.248f);}
    render::float3 forward()const{constexpr float tau=6.28318530718f;auto cp=std::cos(pitch*tau);return {std::sin(yaw*tau)*cp,std::sin(pitch*tau),std::cos(yaw*tau)*cp};}
    render::Camera camera(render::float3 eye,float fov=1.22f)const{constexpr float tau=6.28318530718f;
        render::Camera c;c.distance=1;c.pitch=-pitch*tau;c.yaw=yaw*tau+tau*0.5f;c.target=eye+forward();c.fovY=fov;c.zNear=0.05f;c.zFar=600;return c;}
};
uint32_t vertexColor(const lib::systems::BlockDef& def,const lib::systems::VoxelVertex& v,float daylight);
class ChunkRenderer{
public:
    struct Stats{size_t meshes=0,pending=0,rebuilt=0,triangles=0;};
    // Rebuilds up to `budget` chunks (nearest to `eye` first) and drops meshes of chunks that left the world.
    void sync(gfx::Device&,lib::systems::VoxelWorld& world,const lib::systems::BlockRegistry& blocks,lib::systems::BlockPos eye,float daylight,int budget);
    // Draws chunks within `radius` chunks of `origin` (the camera's render origin, in blocks).
    void draw(render::Renderer3D&,lib::systems::BlockPos origin,int radius)const;
    void shutdown(gfx::Device&);
    const Stats& stats()const{return stats_;}
private:
    struct Gpu{render::Mesh layer[2];bool has[2]{false,false};};
    void release(gfx::Device&,Gpu&);
    std::map<lib::systems::ChunkPos,Gpu> chunks_;std::set<lib::systems::ChunkPos> queue_;int epoch_=-1;Stats stats_;
};
}

#include "VoxelView.h"
#include <algorithm>
#include <vector>
namespace aiverse::voxelgame::view {
using namespace lib::systems;
uint32_t vertexColor(const BlockDef& def,const VoxelVertex& v,float day){
    int face=v.ny<0?2:v.ny>0?3:(v.nx&&v.nz)?0:v.nx<0?0:v.nx>0?1:v.nz<0?4:5;
    auto rgb=def.colors[static_cast<size_t>(face)];
    float shade=v.ny>0?1.0f:v.ny<0?0.55f:(v.nx&&v.nz)?0.9f:v.nx?0.8f:0.68f;
    static const float ao[4]={0.52f,0.68f,0.84f,1.0f};
    float sky=float(v.sky)/15.0f*day,blk=float(v.block)/15.0f,level=std::max(sky,blk);
    float bright=0.05f+0.95f*std::pow(level,1.5f);
    float warm=blk>sky?std::min(1.0f,(blk-sky)*1.5f):0.0f;
    float tint[3]={1.0f,1.0f-0.08f*warm,1.0f-0.25f*warm};
    float k=shade*ao[std::min<int>(v.ao,3)]*bright;
    auto ch=[&](int shift,int i){return std::clamp(float((rgb>>shift)&255)/255.0f*k*tint[i],0.0f,1.0f);};
    return render::rgbaf({ch(16,0),ch(8,1),ch(0,2),def.layer==RenderLayer::Translucent?float(def.alpha)/255.0f:1.0f});
}
void ChunkRenderer::release(gfx::Device& dev,Gpu& g){for(int i=0;i<2;++i)if(g.has[i]){dev.destroyBuffer(g.layer[i].vb);dev.destroyBuffer(g.layer[i].ib);g.has[i]=false;}}
void ChunkRenderer::sync(gfx::Device& dev,VoxelWorld& world,const BlockRegistry& blocks,BlockPos eye,float day,int budget){
    stats_.rebuilt=0;
    int epoch=int(std::lround(day*16.0f));
    if(epoch!=epoch_){epoch_=epoch;for(const auto& [c,g]:chunks_){(void)g;queue_.insert(c);}}
    for(auto c:world.renderDirty)queue_.insert(c);world.renderDirty.clear();
    for(auto it=chunks_.begin();it!=chunks_.end();){if(world.loaded(it->first)){++it;continue;}release(dev,it->second);queue_.erase(it->first);it=chunks_.erase(it);}
    for(auto it=queue_.begin();it!=queue_.end();)if(!world.loaded(*it))it=queue_.erase(it);else ++it;
    auto ec=chunkOf(eye);
    std::vector<std::pair<int64_t,ChunkPos>> order;order.reserve(queue_.size());
    for(auto c:queue_){int64_t dx=c.x-ec.x,dy=c.y-ec.y,dz=c.z-ec.z;order.push_back({dx*dx+dy*dy+dz*dz,c});}
    std::sort(order.begin(),order.end());
    VoxelMeshOptions options;options.unloadedOpaque=true;
    for(const auto& [d,c]:order){(void)d;if(budget--<=0)break;
        queue_.erase(c);auto mesh=meshChunk(world,blocks,c,options);auto& g=chunks_[c];release(dev,g);
        for(int target=0;target<2;++target){
            std::vector<render::MeshVertex> verts;std::vector<uint32_t> idx;
            auto add=[&](const VoxelMeshLayer& L){auto base=uint32_t(verts.size());
                for(const auto& v:L.vertices){render::float3 n{float(v.nx),float(v.ny),float(v.nz)};float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);if(len>0)n={n.x/len,n.y/len,n.z/len};
                    verts.push_back({{v.x,v.y,v.z},n,vertexColor(blocks[v.blockId],v,day)});}
                for(auto i:L.indices)idx.push_back(base+i);};
            if(target==0){add(mesh.layers[kVoxelOpaque]);add(mesh.layers[kVoxelCutout]);}else add(mesh.layers[kVoxelTranslucent]);
            if(idx.empty())continue;
            render::Mesh m;m.boundsMin={0,0,0};m.boundsMax={16,16,16};
            gfx::BufferDesc vb;vb.type=gfx::BufferType::Vertex;vb.size=verts.size()*sizeof(render::MeshVertex);vb.data=verts.data();vb.label="voxel chunk";m.vb=dev.createBuffer(vb);
            gfx::BufferDesc ib;ib.type=gfx::BufferType::Index;ib.size=idx.size()*sizeof(uint32_t);ib.data=idx.data();ib.label="voxel chunk";m.ib=dev.createBuffer(ib);
            m.indexCount=uint32_t(idx.size());g.layer[target]=m;g.has[target]=true;
        }
        ++stats_.rebuilt;
    }
    stats_.meshes=chunks_.size();stats_.pending=queue_.size();
    stats_.triangles=0;for(const auto& [c,g]:chunks_){(void)c;for(int i=0;i<2;++i)if(g.has[i])stats_.triangles+=g.layer[i].indexCount/3;}
}
void ChunkRenderer::draw(render::Renderer3D& r,BlockPos origin,int radius)const{
    auto oc=chunkOf(origin);
    for(const auto& [c,g]:chunks_){
        int64_t dx=c.x-oc.x,dz=c.z-oc.z;if(dx*dx+dz*dz>int64_t(radius)*radius)continue;
        auto o=chunkOrigin(c);auto model=render::mat4::translate({float(o.x-origin.x),float(o.y-origin.y),float(o.z-origin.z)});
        const uint32_t flags=render::kDrawUnlit|render::kDrawNoCastShadow|render::kDrawNoReceiveShadow;
        if(g.has[0])r.drawMesh(g.layer[0],model,{1,1,1,1},flags,0.0f);
        if(g.has[1])r.drawMesh(g.layer[1],model,{1,1,1,1},flags|render::kDrawTransparent,0.0f);
    }
}
void ChunkRenderer::shutdown(gfx::Device& dev){for(auto& [c,g]:chunks_){(void)c;release(dev,g);}chunks_.clear();queue_.clear();}
}

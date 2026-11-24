// Reuse the model loader / mesh extraction already used by SpectraFX.
#pragma once
#include "engine/bake/MeshDistanceGrid.h"
#include "engine/fx/FxModelMesh.h"
#include "engine/fx/SpectraStaticSdfVolume.h"
#include "engine/data/Json.h"
namespace aiverse::fx {
static_assert(bake::kMeshDistanceMaxDimension==kStaticSdfMaxDimension,
              "Baker and runtime lattice limits must agree");
inline bool bakeStaticSdf(const FxMesh& mesh,uint32_t dimension,const render::mat4& transform,
        StaticSdfVolume& out,std::string& error){
    bake::MeshDistanceGrid grid;
    if(!bake::bakeMeshDistanceGrid(mesh.pos,mesh.idx,dimension,transform,grid,error))return false;
    StaticSdfVolume next;
    next.origin=grid.origin;next.spacing=grid.spacing;next.dimension=grid.dimension;
    next.samples=std::move(grid.samples);
    if(!next.valid()){error="baked lattice exceeds SpectraFX format";return false;}
    out=std::move(next);error.clear();return true;
}
inline bool bakeStaticSdfModel(const std::string& path,const render::FileReader& reader,
        uint32_t dimension,const render::mat4& transform,StaticSdfVolume& out,std::string& error){
    FxMesh mesh;
    if(!meshFromModelFile(path,reader,mesh)){error="model mesh unavailable";return false;}
    return bakeStaticSdf(mesh,dimension,transform,out,error);
}
inline Json staticSdfToJson(const StaticSdfVolume& sdf){
    if(!sdf.dimension||!sdf.valid())return {};
    auto j=Json::object(),origin=Json::array(),samples=Json::array();
    for(float x:{sdf.origin.x,sdf.origin.y,sdf.origin.z})origin.push(Json::number(double(x),9));
    for(float x:sdf.samples)samples.push(Json::number(double(x),9));
    j.set("origin",origin);j.set("spacing",Json::number(double(sdf.spacing),9));
    j.set("dimension",Json::number(int64_t(sdf.dimension)));j.set("samples",samples);
    return j;
}
}

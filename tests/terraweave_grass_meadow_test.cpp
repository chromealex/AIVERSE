// TerraWeave meadow grass: species, clumps, fields, budgets, JSON.
// Placement and shape must be deterministic and patch-border independent;
// an absent meadow block must keep the legacy generator byte-identical.
#include "engine/render/TerraWeaveGrassRender.h"
#include "engine/render/ModelLodTransition.h"
#include "engine/world/TerraWeaveGrassSpeciesJson.h"
#include "tests/terraweave_grass_legacy_reference.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

using namespace aiverse;
using namespace aiverse::terraweave;

namespace {
uint64_t fnv(const void* data,size_t n,uint64_t h) {
    const auto* p=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<n;++i){h^=p[i];h*=0x100000001b3ull;}
    return h;
}
// Hash of geometry only (positions, normals, uv, colour, indices).
uint64_t geometryHash(const render::Model& m) {
    uint64_t h=0xcbf29ce484222325ull;
    for(const auto& v:m.vertices){
        h=fnv(&v.pos,sizeof v.pos,h);h=fnv(&v.nrm,sizeof v.nrm,h);
        h=fnv(&v.uv,sizeof v.uv,h);h=fnv(&v.color,sizeof v.color,h);
    }
    return fnv(m.indices.data(),m.indices.size()*sizeof(uint32_t),h);
}
uint32_t triangles(const render::Model& m,size_t mesh) {
    uint32_t n=0;for(const auto& p:m.meshes[mesh].primitives)n+=p.indexCount/3;return n;
}
const GrassSoilHeight flat=[](int64_t,int64_t){return int32_t(123);};
bool throwsInvalid(const auto& f){try{f();}catch(const std::invalid_argument&){return true;}return false;}
}

int main() {
    GrassPatch patch;patch.key=7;patch.seed=99;patch.bounds={-400,-400,400,400};patch.maximumBlades=2048;

    // 1. Legacy compatibility: no meadow -> byte-identical to the frozen
    //    pre-meadow generator (tests/terraweave_grass_legacy_reference.h).
    {
        GrassRenderQuality q;q.maxBladesPerPatch=2048;q.bladeSegments=3;q.crossedBlades=false;
        const auto plain=makeGrassPatchModel(patch,flat,q);
        const auto plainRef=legacyref::legacyGrassPatchModel(patch,flat,q);
        q.crossedBlades=true;q.bladeSegments=1;
        const auto crossed=makeGrassPatchLodModel(patch,flat,q);
        const auto crossedRef=legacyref::legacyGrassPatchLodModel(patch,flat,q);
        q.crossedBlades=false;q.bladeSegments=3;
        q.naturalBlades=true;q.bladesPerTuft=16;q.tuftRadiusCm=24;q.heightVariationPermille=450;q.bladeHalfWidthCm=1;
        const auto mask=[](int64_t x,int64_t z){return (x+z)%7!=0;};
        const auto natural=makeGrassPatchLodModel(patch,flat,q,mask);
        const auto naturalRef=legacyref::legacyGrassPatchLodModel(patch,flat,q,mask);
        assert(plain&&natural&&crossed&&plainRef&&naturalRef&&crossedRef);
        std::printf("legacy plain %016llx natural-lod %016llx\n",
            (unsigned long long)geometryHash(*plain),(unsigned long long)geometryHash(*natural));
        assert(geometryHash(*plain)==geometryHash(*plainRef));
        assert(geometryHash(*crossed)==geometryHash(*crossedRef));
        assert(geometryHash(*natural)==geometryHash(*naturalRef));
        assert(natural->meshes.size()==naturalRef->meshes.size());
        for(size_t i=0;i<natural->meshes.size();++i)assert(triangles(*natural,i)==triangles(*naturalRef,i));
        assert(!GrassRenderQuality{}.meadow.enabled());
        assert(grassMeadowPreset("legacy")&&!grassMeadowPreset("legacy")->enabled());
    }

    // 2. Presets are valid; unknown names are rejected.
    for(auto name:grassSpeciesPresetNames())assert(grassSpeciesPreset(name));
    for(auto name:grassMeadowPresetNames()){auto m=grassMeadowPreset(name);assert(m);validateGrassMeadow(*m);}
    assert(!grassSpeciesPreset("bamboo")&&!grassMeadowPreset("jungle"));

    const auto meadow=*grassMeadowPreset("meadow");
    auto denseOverride=parseGrassMeadow(Json::parse(R"({"species":[{"preset":"sedge","basalLeaves":3}]})"));
    assert(denseOverride.species[0].basalLeaves==3);
    assert(throwsInvalid([]{parseGrassMeadow(Json::parse(R"({"species":[{"basalLeaves":5}]})"));}));
    // 3. World-anchored clumps: same clump regardless of which patch asks.
    {
        const auto a=grassClumpAt(meadow,3,-2),b=grassClumpAt(meadow,3,-2);
        assert(a.xCm==b.xCm&&a.zCm==b.zCm&&a.exists==b.exists&&a.species==b.species&&a.radiusCm==b.radiusCm);
        const int64_t s=meadow.clumps.spacingCm;
        assert(a.xCm>=3*s&&a.xCm<4*s&&a.zCm>=-2*s&&a.zCm<-1*s);
        // gap share close to the authored gap x field density
        uint32_t empty=0,total=0;std::set<uint32_t> species;
        for(int64_t z=-60;z<60;++z)for(int64_t x=-60;x<60;++x){
            const auto c=grassClumpAt(meadow,x,z);++total;if(!c.exists)++empty;else species.insert(c.species);
        }
        const uint32_t emptyPermille=empty*1000/total;
        std::printf("clump cells %u empty %u permille, species %zu\n",total,emptyPermille,species.size());
        assert(emptyPermille>=meadow.clumps.gapPermille && emptyPermille<700);
        assert(species.size()==meadow.species.size()); // every species dominates somewhere
    }

    // 4. Blade placement: deterministic, inside bounds, clumped (not uniform).
    {
        RectCm left{0,0,800,800};
        std::map<std::pair<int64_t,int64_t>,uint32_t> cells; // 20 cm counting grid
        uint32_t placed=0,clumped=0;
        for(uint64_t i=0;i<20000;++i){
            const uint64_t h=terraweave::detail::mix64(0x1234^(i*0x9e3779b97f4a7c15ull));
            const auto s=placeMeadowBlade(meadow,left,h),again=placeMeadowBlade(meadow,left,h);
            assert(bool(s)==bool(again));
            if(!s)continue;
            assert(s->xCm==again->xCm&&s->zCm==again->zCm&&s->species==again->species);
            assert(s->xCm>=0&&s->xCm<800&&s->zCm>=0&&s->zCm<800);
            ++placed;clumped+=s->clumped;++cells[{s->xCm/20,s->zCm/20}];
        }
        // index of dispersion >> 1 means tufts and gaps, ~1 would be uniform random
        double mean=double(placed)/1600.0,var=0;
        for(int64_t z=0;z<40;++z)for(int64_t x=0;x<40;++x){
            const auto it=cells.find({x,z});const double n=it==cells.end()?0:it->second;var+=(n-mean)*(n-mean);
        }
        var/=1600.0;
        const auto emptyCells=1600-cells.size();
        std::printf("placed %u clumped %u dispersion %.2f empty 20cm cells %zu\n",placed,clumped,var/mean,emptyCells);
        assert(placed>18000 && clumped>placed/2);
        assert(emptyCells<16); // roots occupy over 99% of the sampled ground
    }

    // 5. Meadow model: deterministic, species/heads present, filters honoured.
    GrassRenderQuality q=meadowGrassQuality(GrassTier::High,meadow);
    patch.maximumBlades=4608;
    {
        const auto a=makeGrassPatchModel(patch,flat,q),b=makeGrassPatchModel(patch,flat,q);
        assert(a&&b&&geometryHash(*a)==geometryHash(*b));
        auto singleLeaf=q;
        for(auto& species:singleLeaf.meadow.species)species.basalLeaves=0;
        const auto stems=makeGrassPatchModel(patch,flat,singleLeaf);
        assert(stems);
        // All three meadow species use three segments (5 main triangles).
        // Four basal leaves grow from each accepted root at this tier.
        assert(uint64_t(triangles(*a,0))*5==uint64_t(triangles(*stems,0))*9);
        assert(a->materials.size()==1&&a->materials[0].vertexColors&&a->materials[0].castShadow&&a->materials[0].receiveShadow&&a->materials[0].thinTransmission>0.f);
        assert(triangles(*a,0)<=uint64_t(q.maxBladesPerPatch)*meadowBladeTriangleBound(q.bladeSegments));
        float minY=1e9f,maxY=-1e9f;std::set<uint32_t> colours;
        for(const auto& v:a->vertices){minY=std::min(minY,v.pos.y);maxY=std::max(maxY,v.pos.y);colours.insert(v.color);}
        std::printf("meadow verts %zu tris %u height %.2f..%.2f colours %zu\n",a->vertices.size(),triangles(*a,0),minY,maxY,colours.size());
        assert(std::fabs(minY-1.23f)<.0001f);           // roots on the ground
        assert(maxY>1.23f+.6f&&maxY<1.23f+2.6f);        // tall stems exist, bounded
        assert(colours.size()>2000);                     // per-blade pigment variety
        // placement filter: nothing rooted where it is forbidden
        const auto half=makeGrassPatchModel(patch,flat,q,[](int64_t x,int64_t){return x>=0;});
        assert(half&&triangles(*half,0)<triangles(*a,0));
        for(const auto& v:half->vertices)if(std::fabs(v.pos.y-1.23f)<.0001f)assert(v.pos.x>=4.0f-0.6f);
        assert(!makeGrassPatchModel(patch,flat,q,[](int64_t,int64_t){return false;}));
        // coverage 0 / bare patch -> no GPU object
        auto bare=patch;bare.coveragePermille=0;assert(!makeGrassPatchModel(bare,flat,q));
        // a different world seed moves the clumps
        auto q2=q;q2.meadow.worldSeed^=1;
        assert(geometryHash(*makeGrassPatchModel(patch,flat,q2))!=geometryHash(*a));
    }

    // 6. Shared LOD chain: strictly fewer triangles per tier.
    {
        const auto lod=makeGrassPatchLodModel(patch,flat,q);
        assert(lod&&lod->meshes.size()==3&&lod->meshes[0].lods.size()==2);
        std::printf("lod tris %u %u %u\n",triangles(*lod,0),triangles(*lod,1),triangles(*lod,2));
        assert(triangles(*lod,0)>2*triangles(*lod,1)&&triangles(*lod,1)>triangles(*lod,2));
        assert(lod->batching.independentShadowLod);
        const auto& levels=lod->meshes[0].lods;
        assert(render::selectModelLod(0,levels,1000.f,0.f).level==0);
        assert(render::selectModelLod(0,levels,500.f,0.f).level==1);
        assert(render::selectModelLod(0,levels,200.f,0.f).level==2);
    }

    // 7. Tier budgets: mobile is bounded and cheaper than high.
    {
        const auto mobile=meadowGrassQuality(GrassTier::Mobile,meadow);
        const auto high=meadowGrassQuality(GrassTier::High,meadow);
        std::printf("budget mobile %llu high %llu\n",(unsigned long long)grassTriangleBudget(mobile),(unsigned long long)grassTriangleBudget(high));
        assert(grassTriangleBudget(mobile)<=400000);
        assert(grassTriangleBudget(mobile)*4<grassTriangleBudget(high));
        const auto m=makeGrassPatchModel(patch,flat,mobile);
        assert(m&&triangles(*m,0)<=uint64_t(mobile.maxBladesPerPatch)*meadowBladeTriangleBound(mobile.bladeSegments));
    }

    // 8. Validation and JSON authoring.
    {
        auto bad=meadow;bad.species[0].heightCm=500;
        assert(throwsInvalid([&]{validateGrassMeadow(bad);}));
        auto none=meadow;for(auto& s:none.species)s.weightPermille=0;
        assert(throwsInvalid([&]{validateGrassMeadow(none);}));
        auto q3=q;q3.meadow.clumps.spacingCm=1;
        assert(throwsInvalid([&]{validateGrassRenderQuality(q3);}));

        assert(parseGrassMeadow(Json::parse("\"dryMeadow\""))==*grassMeadowPreset("dryMeadow"));
        assert(!parseGrassMeadow(Json()).enabled());
        const auto j=parseGrassMeadow(Json::parse(R"({"preset":"meadow","seed":7,
            "species":[{"preset":"meadowFine","weightPermille":600,"heightCm":45},
                       {"name":"reed","heightCm":90,"halfWidthMm":7,"tipColor":[190,180,120],"head":"spike","headPermille":300}],
            "clumps":{"spacingCm":55,"gapPermille":250},"field":{"dryPatchPermille":300}})"));
        assert(j.species.size()==2&&j.species[0].heightCm==45&&j.species[0].taperPermille==grassSpeciesPreset("meadowFine")->taperPermille);
        assert(j.species[1].name=="reed"&&j.species[1].head==GrassHead::Spike&&j.species[1].tipColor==(GrassRgb{190,180,120}));
        assert(j.worldSeed==7&&j.clumps.spacingCm==55&&j.clumps.radiusCm==meadow.clumps.radiusCm&&j.field.dryPatchPermille==300);
        const auto message=[](const char* text){
            try{(void)parseGrassMeadow(Json::parse(text));}catch(const std::invalid_argument& e){return std::string(e.what());}
            return std::string();
        };
        assert(message(R"("jungle")").find("unknown meadow preset 'jungle'")!=std::string::npos);
        assert(message(R"({"species":[{"preset":"bamboo"}]})").find("species[0]: unknown species preset 'bamboo'")!=std::string::npos);
        assert(message(R"({"clumps":{"spacing":5}})").find("unknown key 'spacing'")!=std::string::npos);
        assert(message(R"({"species":[{"heightCm":900}]})").find("heightCm out of range")!=std::string::npos);
        assert(message(R"({"species":[{"head":"tulip"}]})").find("unknown head 'tulip'")!=std::string::npos);
    }
#if __has_include("engine/world/PhysisNatureGrass.h")
    // 9. Host presentation overlay (PHYSIS wind sway + wildfire) on meadow grass.
    {
        GrassPresentationOptions sway;sway.windSway=true;
        const auto swaying=makeGrassPatchModel(patch,flat,q,{},&sway);
        const auto still=makeGrassPatchModel(patch,flat,q);
        assert(swaying&&swaying->vertices.size()==still->vertices.size()&&swaying->indices==still->indices);
        bool rootStill=true,tipMoves=false;
        for(const auto& v:swaying->vertices){
            const uint32_t w=v.color>>24;
            if(std::fabs(v.pos.y-1.23f)<.0001f&&w!=0)rootStill=false;
            if(w>200)tipMoves=true;
        }
        assert(rootStill&&tipMoves);
        GrassPresentationOptions burn;
        burn.fire=[](int64_t x,int64_t){GrassFireSample s;if(x<0){s.charPermille=1000;}else if(x<200){s.firePermille=800;}return s;};
        const auto burnt=makeGrassPatchModel(patch,flat,q,{},&burn);
        assert(burnt&&burnt->materials.size()==2&&burnt->meshes[0].primitives.size()==2);
        assert(burnt->meshes[0].primitives[1].material==1&&burnt->materials[1].emissiveStrength>0);
        float maxCharred=0;
        // Select by root through the placement mask; fresh leaves can arch
        // across the boundary into the charred region.
        const auto stubble=makeGrassPatchModel(patch,flat,q,[](int64_t x,int64_t){return x<0;},&burn);
        assert(stubble);
        for(const auto& v:stubble->vertices)maxCharred=std::max(maxCharred,v.pos.y-1.23f);
        assert(maxCharred<.35f); // charred stubble is short
    }
#endif
    std::printf("terraweave_grass_meadow: ok\n");
    return 0;
}

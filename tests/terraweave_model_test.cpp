#include "engine/render/TerraWeaveGrassRender.h"
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include "engine/render/TerraWeaveTerrainLod.h"
#include <map>
#include <set>
#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include "engine/world/TerraWeaveTreePresets.h"
#include "engine/world/TerraWeaveBotanicalAppearance.h"
#include <cassert>
#include <cmath>
#include <cstdint>

using namespace aiverse::terraweave;

int main() {
    {
        EarthworksLayer ground(50,[](int64_t x,int64_t z){return int32_t(1000+x/5+z/10);});
        const auto patch=bakeEarthworksTerrainPatch(ground,{0,0,0},64,129);
        auto model=makeEarthworksTerrainModel(patch);
        const auto normal=aiverse::render::normalize(aiverse::render::float3{-.2f,1,-.1f});
        for(auto& v:model->vertices)v.nrm=normal;
        const auto indices=model->indices;const auto vertices=model->vertices;
        const auto levels=appendTerrainPatchLods(*model,patch);
        assert(levels.size()==3&&model->meshes[0].lods.size()==3);
        assert(std::equal(indices.begin(),indices.end(),model->indices.begin()));
        for(size_t i=0;i<vertices.size();++i) {
            const auto& a=vertices[i];const auto& b=model->vertices[i];
            assert(a.pos.x==b.pos.x&&a.pos.y==b.pos.y&&a.pos.z==b.pos.z);
            assert(a.nrm.x==b.nrm.x&&a.uv.x==b.uv.x&&a.uv.y==b.uv.y);
        }
        std::set<std::pair<uint32_t,uint32_t>> originalBorder;
        for(size_t level=0;level<3;++level) {
            assert((levels[level].triangles==std::array<uint32_t,3>{8952,2680,1080}[level]));
            assert(levels[level].errorMetres<.0001f);
            assert((levels[level].maxPixels==std::array<float,3>{512,256,128}[level]));
            const auto& p=model->meshes[level+1].primitives[0];
            std::map<std::pair<uint32_t,uint32_t>,uint32_t> edges;
            double area=0;
            const auto key=[&](uint32_t i){const auto& v=model->vertices[i];return uint32_t(std::lround(v.pos.z*2))*129+uint32_t(std::lround(v.pos.x*2));};
            for(uint32_t i=p.firstIndex;i<p.firstIndex+p.indexCount;i+=3) {
                const auto& a=model->vertices[model->indices[i]].pos;
                const auto& b=model->vertices[model->indices[i+1]].pos;
                const auto& c=model->vertices[model->indices[i+2]].pos;
                const double cross=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z);
                assert(cross>0);area+=cross*.5;
                for(uint32_t e=0;e<3;++e){auto x=key(model->indices[i+e]),y=key(model->indices[i+(e+1)%3]);if(x>y)std::swap(x,y);++edges[{x,y}];}
            }
            assert(std::abs(area-4096)<.00001);
            std::set<std::pair<uint32_t,uint32_t>> border;
            for(const auto& [edge,count]:edges) {
                assert(count==1||count==2);
                if(count!=1)continue;
                const auto [a,b]=edge;const auto ax=a%129,az=a/129,bx=b%129,bz=b/129;
                assert((ax==0&&bx==0)||(ax==128&&bx==128)||(az==0&&bz==0)||(az==128&&bz==128));
                assert(std::abs(int(ax)-int(bx))+std::abs(int(az)-int(bz))==1);
                border.insert(edge);
            }
            assert(border.size()==512);
            if(level==0)originalBorder=border;else assert(originalBorder==border);
        }
        // Existing LODs reject transactionally, preserving source values.
        const auto nv=model->vertices.size(),ni=model->indices.size();bool rejected=false;
        try{(void)appendTerrainPatchLods(*model,patch);}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected&&model->vertices.size()==nv&&model->indices.size()==ni);
        auto mismatched=makeEarthworksTerrainModel(patch);
        for(auto& v:mismatched->vertices)v.nrm=normal;
        mismatched->meshes[0].primitives[0].lightmapUv=true;
        rejected=false;try{(void)appendTerrainPatchLods(*mismatched,patch);}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected&&mismatched->meshes.size()==1&&mismatched->vertices.size()==vertices.size());
        mismatched->meshes[0].primitives[0].lightmapUv=false;
        mismatched->vertices[0].pos.y+=1;
        const auto revision=mismatched->geometryRevision;
        rejected=false;try{(void)appendTerrainPatchLods(*mismatched,patch);}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected&&mismatched->meshes.size()==1&&mismatched->indices==indices&&mismatched->geometryRevision==revision);
        EarthworksLayer spike(50,[](int64_t x,int64_t z){return x==3200&&z==3200?3000:0;});
        const auto rough=bakeEarthworksTerrainPatch(spike,{0,0,0},64,129);auto steep=makeEarthworksTerrainModel(rough);
        for(auto& v:steep->vertices)v.nrm={0,1,0};
        const auto errors=appendTerrainPatchLods(*steep,rough);
        for(size_t i=0;i<errors.size();++i) {
            assert(std::abs(errors[i].errorMetres-30.f*(1.f-1.f/errors[i].stride))<.0001f);
            assert(errors[i].maxPixels<10);
            assert(errors[i].errorMetres*errors[i].maxPixels/std::sqrt(64.f*64*2+30.f*30)<=1.5001f);
        }
    }

    // Opt-in conifer foliage keeps structural endpoints identical across LODs.
    TreeProfile conifer;conifer.shape=CrownShape::Conifer;conifer.primaryBranches=120;
    ArborMeshSettings sprays;sprays.naturalConifer=true;sprays.doubleSidedLeaves=false;
    const auto natural=makeArborModel(conifer,909,sprays);
    assert(natural->meshes.size()==3);
    auto triangles=[](const auto& mesh){size_t n=0;for(const auto& p:mesh.primitives)n+=p.indexCount/3;return n;};
    assert(triangles(natural->meshes[0])>triangles(natural->meshes[1]));
    assert(triangles(natural->meshes[1])>triangles(natural->meshes[2]));
    assert(triangles(natural->meshes[0])<20000);
    for(const auto& v:natural->vertices){assert(std::isfinite(v.pos.x)&&std::isfinite(v.nrm.y)&&std::isfinite(v.uv.x));}
    const auto repeat=makeArborModel(conifer,909,sprays);
    assert(repeat->indices==natural->indices);
    for(size_t i=0;i<natural->vertices.size();++i)assert(natural->vertices[i].pos.x==repeat->vertices[i].pos.x);
    // Native per-branch adapter remaps cluster ownership and never grows a
    // duplicate whole-tree leader/flare from every isolated limb.
    TreeProfile leafy;leafy.secondaryBranches=2;
    const auto leafySkeleton=generateTree(leafy,121);
    ArborMeshSettings broadleaf;broadleaf.naturalBroadleaf=true;
    const auto isolated=buildArborBranchMesh(leafySkeleton,1,BranchDetail::Full,broadleaf);
    assert(std::any_of(isolated.vertices.begin(),isolated.vertices.end(),[](const auto& v){return v.material==1;}));
    // Far forest geometry has an explicit bounded budget, not just LOD labels.
    conifer.primaryBranches=75;conifer.whorlSize=5;
    sprays.compactConifer=true;
    const auto compact=makeArborModel(conifer,909,sprays);
    assert(triangles(compact->meshes[0])<6000);
    assert(triangles(compact->meshes[1])<5000);
    assert(triangles(compact->meshes[2])<1000);
    assert(compact->meshes[0].lods[0].maxPixels==180.f);
    assert(compact->meshes[0].lods[1].maxPixels==48.f);
    for(const auto& v:compact->vertices)assert(std::abs(aiverse::render::length(v.nrm)-1.f)<.01f);
    const auto whorled=generateTree(conifer,909);
    assert(whorled.branches==generateTree(conifer,909).branches);
    assert(whorled.branches!=generateTree(conifer,910).branches);
    for(size_t i=1;i<whorled.branches.size();i+=5) {
        int lo=whorled.branches[i].y0,hi=lo;
        for(size_t j=i;j<std::min(i+5,whorled.branches.size());++j){lo=std::min(lo,whorled.branches[j].y0);hi=std::max(hi,whorled.branches[j].y0);assert(whorled.branches[j].parent==0);}
        assert(hi-lo<100);
    }
    bool invalidWhorl=false;conifer.whorlSize=1;
    try{(void)generateTree(conifer,909);}catch(const std::invalid_argument&){invalidWhorl=true;}
    assert(invalidWhorl);
    for(const auto& preset:botanicalTreePresets()) {
        const auto anatomy=botanicalArborMeshSettings(preset);
        const auto curved=makeArborModel(preset.profile,2718,anatomy);
        assert(curved->meshes.size()==3);
        assert(triangles(curved->meshes[0])>triangles(curved->meshes[1]));
        assert(triangles(curved->meshes[1])>triangles(curved->meshes[2]));
        for(const auto& v:curved->vertices)assert(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y)&&std::isfinite(v.pos.z));
    }
    {
        const auto profile=botanicalTreePresets()[4].profile;
        ArborMeshSettings appearance;appearance.naturalBroadleaf=true;
        appearance.organicRootFlare=true;appearance.organicBranches=true;
        ArborForestAdapter curvedForest;curvedForest.addPrototype(profile,2718,appearance);
        ForestWindState wind;wind.xCmPerSec=4500;wind.branchFlexPermille=1000;wind.maxLeanMilliDegrees=3000;
        curvedForest.setWind(wind);curvedForest.setPresentationTick(53);
        const auto skeleton=generateTree(profile,2718);
        const auto curves=arbor_detail::branchCurves(skeleton,appearance);
        FloraBatch batch;batch.speciesId=profile.speciesId;batch.lod=TreeLOD::High;
        batch.instances.push_back({11,profile.speciesId,0,0,0,0,1000});
        bool checked=false;
        curvedForest.forEachDraw({batch},{},[&](const auto& draw){
            assert(draw.nodeWorld&&draw.nodeWorld->size()==curves.size());
            for(size_t i=1;i<curves.size();++i){
                const auto at=arbor_detail::curveSample(curves[i],0);
                const aiverse::render::float3 pivot{at.x*.01f,at.y*.01f,at.z*.01f};
                const auto own=(*draw.nodeWorld)[i].transformPoint(pivot);
                const auto parent=(*draw.nodeWorld)[size_t(skeleton.branches[i].parent)].transformPoint(pivot);
                assert(aiverse::render::length(own-parent)<.0001f);
            }
            checked=true;
        });
        assert(checked);
    }
    // Bounded grass, exact terrain contact and a fully excluded patch.
    GrassPatch patch;patch.key=7;patch.seed=99;patch.bounds={-400,-400,400,400};patch.maximumBlades=2048;
    GrassRenderQuality quality;quality.maxBladesPerPatch=2048;quality.bladeSegments=3;quality.crossedBlades=false;
    const auto grass=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality);
    assert(grass && grass->indices.size()/3==2048*5);
    for(size_t i=0;i<grass->vertices.size();i+=7)assert(std::fabs(grass->vertices[i].pos.y-1.23f)<.0001f);
    assert(!makeGrassPatchModel(patch,[](auto,auto){return 123;},quality,[](auto,auto){return false;}));
    const auto masked=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality,[](auto x,auto){return x>=0;});
    assert(masked && masked->indices.size()<grass->indices.size());
    quality.bladesPerTuft=16;quality.tuftRadiusCm=15;
    const auto tufted=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality);assert(tufted&&tufted->indices.size()==grass->indices.size());
    for(size_t start=0;start<2048;start+=16){const auto centre=(tufted->vertices[start*7].pos+tufted->vertices[start*7+1].pos)*.5f;for(size_t i=start;i<start+16;++i){const auto blade=(tufted->vertices[i*7].pos+tufted->vertices[i*7+1].pos)*.5f;assert(std::abs(blade.x-centre.x)<=.301f&&std::abs(blade.z-centre.z)<=.301f);}}
    const auto grassLods=makeGrassPatchLodModel(patch,[](auto,auto){return 123;},quality);
    assert(grassLods&&grassLods->meshes.size()==3&&grassLods->meshes[0].lods.size()==2);
    assert(triangles(grassLods->meshes[0])>triangles(grassLods->meshes[1]));
    assert(triangles(grassLods->meshes[1])>triangles(grassLods->meshes[2]));
    assert(grassLods->batching.independentShadowLod&&!grassLods->materials[0].castShadow);
    // Every coarse root is a surviving high-detail root, including capped populations.
    for(auto population:{2048u,8192u}) {
        patch.maximumBlades=population;
        const auto chain=makeGrassPatchLodModel(patch,[](auto,auto){return 123;},quality);
        const auto& high=chain->meshes[0].primitives[0];
        for(size_t tier=1;tier<chain->meshes.size();++tier) {
            const auto& coarse=chain->meshes[tier].primitives[0];
            for(uint32_t v=coarse.firstVertex;v<coarse.firstVertex+coarse.vertexCount;v+=3) {
                const auto root=(chain->vertices[v].pos+chain->vertices[v+1].pos)*.5f;
                bool found=false;
                for(uint32_t h=high.firstVertex;h<high.firstVertex+high.vertexCount;h+=7) {
                    const auto original=(chain->vertices[h].pos+chain->vertices[h+1].pos)*.5f;
                    if(std::abs(root.x-original.x)<.0001f&&std::abs(root.z-original.z)<.0001f){found=true;break;}
                }
                assert(found);
            }
        }
    }
    quality.naturalBlades=true;
    const auto naturalGrass=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality);
    const auto naturalAgain=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality);
    assert(naturalGrass && naturalAgain);
    bool hasGradient=false;
    for(size_t i=0;i<naturalGrass->vertices.size();++i){
        const auto& v=naturalGrass->vertices[i];const auto& repeat=naturalAgain->vertices[i];
        assert(v.color==repeat.color && v.pos.x==repeat.pos.x && v.pos.y==repeat.pos.y && v.pos.z==repeat.pos.z);
        assert(std::isfinite(v.nrm.x)&&std::abs(aiverse::render::dot(v.nrm,v.nrm)-1.f)<.001f);
        if(v.uv.y==0)assert(std::abs(v.pos.y-1.23f)<.0001f);
        if(i%7==6 && v.color!=naturalGrass->vertices[i-6].color)hasGradient=true;
    }
    assert(hasGradient);
    // Pigment and profile variation add no vertices, triangles, materials or textures.
    quality.naturalBlades=false;
    const auto plainGrass=makeGrassPatchModel(patch,[](auto,auto){return 123;},quality);
    assert(plainGrass->vertices.size()==naturalGrass->vertices.size());
    assert(plainGrass->indices==naturalGrass->indices && naturalGrass->materials.size()==1);
    quality.bladeSegments=5;bool badGrass=false;
    try{(void)makeGrassPatchModel(patch,[](auto,auto){return 0;},quality);}catch(const std::invalid_argument&){badGrass=true;}
    assert(badGrass);

    TreeProfile oak;
    oak.speciesId = 1001;
    oak.primaryBranches = 24;
    const auto model = makeArborModel(oak, 0x123456789ULL);
    assert(model);
    assert(model->nodes.size() == 1 && model->nodes[0].mesh == 0);
    assert(model->materials.size() == 2);
    assert(model->meshes.size() == 3);
    assert(model->meshes[0].lods.size() == 2);
    assert(model->meshes[0].lods[0].mesh == 1);
    assert(model->meshes[0].lods[1].mesh == 2);
    assert(!model->batching.autoLod);
    assert(model->batching.independentShadowLod);
    assert(model->boundsMax.y > 5.0f && model->boundsMax.y < 30.0f);
    assert(model->boundsMin.y >= -1.0f);
    assert(model->materials[1].doubleSided);
    assert(model->materials[1].alpha == aiverse::render::AlphaMode::Opaque);
    assert(model->vertices.size() > 100);
    assert(model->indices.size() > 100);
    assert(model->order.size() == 1);
    for (const auto& mesh : model->meshes) {
        assert(mesh.primitives.size() == 2);
        for (const auto& primitive : mesh.primitives) {
            assert(primitive.indexCount > 0 && primitive.indexCount % 3 == 0);
            assert(primitive.firstIndex + primitive.indexCount <= model->indices.size());
            for (uint32_t k = primitive.firstIndex;
                 k < primitive.firstIndex + primitive.indexCount; ++k)
                assert(model->indices[k] < model->vertices.size());
        }
    }
    for (const auto& v : model->vertices) {
        assert(std::isfinite(v.pos.x));
        assert(std::isfinite(v.pos.y));
        assert(std::isfinite(v.pos.z));
    }
    const auto again = makeArborModel(oak, 0x123456789ULL);
    assert(model->vertices.size() == again->vertices.size());
    assert(model->indices == again->indices);

    // Hierarchical BranchLOD: each structural branch has an independent
    // 3-mesh chain, with its original parent and all local leaf clusters.
    TreeProfile hierarchical = oak;
    hierarchical.primaryBranches = 17;
    const auto skeleton = generateTree(hierarchical, 0x987654321ULL, TreeLOD::High);
    const auto branchModel = makeArborBranchLodModel(hierarchical, 0x987654321ULL);
    assert(branchModel && branchModel->nodes.size() == skeleton.branches.size());
    assert(branchModel->meshes.size() == skeleton.branches.size() * 3);
    assert(!branchModel->batching.autoLod);
    for (size_t i = 0; i < skeleton.branches.size(); ++i) {
        const auto& node = branchModel->nodes[i];
        assert(node.mesh == static_cast<int>(i * 3));
        assert(node.parent == skeleton.branches[i].parent);
        const auto& highBranch = branchModel->meshes[i * 3];
        const auto& midBranch = branchModel->meshes[i * 3 + 1];
        const auto& lowBranch = branchModel->meshes[i * 3 + 2];
        assert(highBranch.lods.size() == 2);
        assert(highBranch.lods[0].mesh == static_cast<int>(i * 3 + 1));
        assert(highBranch.lods[1].mesh == static_cast<int>(i * 3 + 2));
        const auto indicesFor = [](const aiverse::render::MeshDef& mesh) {
            uint32_t result = 0;
            for (const auto& prim : mesh.primitives) result += prim.indexCount;
            return result;
        };
        assert(indicesFor(highBranch) > indicesFor(midBranch));
        assert(indicesFor(midBranch) > indicesFor(lowBranch));
        for (const auto& tier : {highBranch, midBranch, lowBranch}) {
            for (const auto& prim : tier.primitives) {
                assert(prim.firstIndex + prim.indexCount <= branchModel->indices.size());
                assert(prim.firstVertex + prim.vertexCount <= branchModel->vertices.size());
            }
        }
    }
    for (const auto& leaf : skeleton.leaves) {
        assert(leaf.parentBranch > 0);
        assert(static_cast<size_t>(leaf.parentBranch) < skeleton.branches.size());
    }
    const auto branchRepeat = makeArborBranchLodModel(hierarchical, 0x987654321ULL);
    assert(branchRepeat->indices == branchModel->indices);
    assert(branchRepeat->vertices.size() == branchModel->vertices.size());
    BranchLodSettings invalidBranch;
    invalidBranch.minimalPixels = invalidBranch.reducedPixels;
    bool invalidBranchRejected = false;
    try { (void)makeArborBranchLodModel(oak, 17, invalidBranch); }
    catch (const std::invalid_argument&) { invalidBranchRejected = true; }
    assert(invalidBranchRejected);

    {
        ArborForestAdapter larchForest;
        const auto& larch=botanicalTreePresets()[3];
        ArborMeshSettings needles;needles.naturalConifer=true;
        larchForest.addPrototype(larch.profile,17,needles);
        CanopyPhenology winter;winter.broadleafCoverPermille=0;
        larchForest.setPhenology(winter);
        FloraBatch batch;batch.speciesId=larch.profile.speciesId;batch.lod=TreeLOD::High;
        FoliageInstance instance;instance.speciesId=batch.speciesId;instance.scalePermille=1000;instance.key=123;
        batch.instances.push_back(instance);bool checked=false;
        larchForest.forEachDraw(std::vector<FloraBatch>{batch},ForestRenderOriginCm{},[&](const auto& draw){
            assert(draw.materials&&draw.materials->size()==2);
            assert((*draw.materials)[0].baseColor.w==1);
            assert((*draw.materials)[1].baseColor.w==0);checked=true;
        });
        assert(checked);
    }

    // Stage 2f: shared prototype, CPU draw submission, stable identity and
    // origin-relative centimetre -> metre transforms. No GPU is required.
    ArborForestAdapter forest;
    forest.addPrototype(oak, 17);
    assert(forest.prototypeCount() == 1);
    const auto* shared = forest.prototype(oak.speciesId);
    assert(shared && shared->nodes.size() == oak.primaryBranches + 1);
    assert(shared->meshes.size() == shared->nodes.size() * 3);
    const auto* billboard = forest.billboardPrototype(oak.speciesId);
    assert(billboard && billboard != shared);
    assert(billboard->nodes.size() == 1 && billboard->meshes.size() == 1);
    // The current impostor has separate bark and foliage cards so winter
    // can hide leaves without removing the opaque trunk. They still share
    // ONE atlas texture and ONE mesh/node per species.
    assert(billboard->indices.size() == 12 && billboard->vertices.size() == 8);
    assert(billboard->materials.size() == 2);
    assert(billboard->meshes[0].primitives.size() == 2);
    for (size_t layer = 0; layer < 2; ++layer) {
        const auto& p = billboard->meshes[0].primitives[layer];
        assert(p.material >= 0 && static_cast<size_t>(p.material) == layer);
        assert(p.firstVertex == layer * 4 && p.vertexCount == 4);
        assert(p.firstIndex == layer * 6 && p.indexCount == 6);
        assert(billboard->materials[layer].alpha ==
               aiverse::render::AlphaMode::Mask);
        assert(!billboard->materials[layer].castShadow);
        for (size_t k = p.firstIndex; k < p.firstIndex + p.indexCount; ++k) {
            assert(billboard->indices[k] >= p.firstVertex);
            assert(billboard->indices[k] < p.firstVertex + p.vertexCount);
        }
    }
    assert(billboard->images.size() == 1);
    assert(forest.billboardAzimuthCount(oak.speciesId) == 8);
    // The left eight tiles hold bark azimuths; the right eight hold foliage.
    // Textured bake from the near tree: 256x512 view tiles.
    assert(billboard->images[0].width == 256 * 8 * 2);
    assert(billboard->images[0].height == 512);
    assert(billboard->images[0].rgba.size() == 256u * 8u * 2u * 512u * 4u);
    bool duplicateRejected = false;
    try { forest.addPrototype(oak, 18); }
    catch (const std::invalid_argument&) { duplicateRejected = true; }
    assert(duplicateRejected);
    assert(forest.prototypeCount() == 1);

    const FoliageInstance first{11, 1001, 50100, -50000, 1300, 90000, 2000};
    const FoliageInstance second{12, 1001, 50200, -50100, 1600, 0, 800};
    const std::vector<FloraBatch> forestBatches{
        FloraBatch{1001, TreeLOD::High, {first}},
        FloraBatch{1001, TreeLOD::Impostor, {second}}
    };
    std::vector<aiverse::render::ModelDraw> draws;
    const ForestRenderOriginCm origin{49100, 300, -49000};
    forest.forEachDraw(forestBatches, origin, [&](const auto& draw) {
        draws.push_back(draw);
    });
    assert(draws.size() == 2);
    assert(draws[0].model == shared && draws[1].model == billboard);
    assert(draws[0].persistentId && draws[0].persistentId != draws[1].persistentId);
    assert(draws[0].flags == aiverse::render::kDrawStatic);
    assert(std::fabs(draws[0].world.at(0, 3) - 10.0f) < 0.0001f);
    assert(std::fabs(draws[0].world.at(1, 3) - 10.0f) < 0.0001f);
    assert(std::fabs(draws[0].world.at(2, 3) + 10.0f) < 0.0001f);
    assert(std::fabs(draws[0].world.at(0, 2) - 2.0f) < 0.0001f);
    assert(std::fabs(draws[1].world.at(0, 3) - 11.0f) < 0.0001f);
    const auto stableId = draws[0].persistentId;
    draws.clear();
    forest.forEachDraw(forestBatches, origin, [&](const auto& draw) {
        draws.push_back(draw);
    });
    assert(draws[0].persistentId == stableId); // stable hash, not draw index
    assert(draws[0].model == shared && draws[1].model == billboard);


    // A full-tree impostor has real transparent pixels and cutout foliage.
    const auto& image = billboard->images[0].rgba;
    uint32_t transparent = 0, opaque = 0, brown = 0, green = 0;
    for (size_t k = 0; k < image.size(); k += 4) {
        if (image[k + 3] == 0) ++transparent;
        else {
            assert(image[k + 3] == 255);
            ++opaque;
            if (image[k + 1] > image[k]) ++green;
            else ++brown;
        }
    }
    assert(transparent > 0 && opaque > 0 && green > 0 && brown > 0);
    assert(image[3] == 0);
    // The adapter bakes the far atlas from the near tree itself
    // (bakeArborImpostorFromModel): deterministic, and different from the
    // legacy skeleton silhouette.
    ArborImpostorAtlasSettings hiRes;
    hiRes.tile = {256, 512, 8};
    const auto legacyPixels = makeArborImpostorAtlas(oak, 17).model->images[0].rgba;
    auto billboardRepeat = makeArborImpostorAtlas(oak, 17, hiRes);
    bakeArborImpostorFromModel(billboardRepeat, *shared, hiRes);
    assert(billboardRepeat.model->images[0].rgba == billboard->images[0].rgba);
    assert(legacyPixels.size() != billboard->images[0].rgba.size());
    {   // Quad matches the near tree's real height.
        float top = 0;
        for (const auto& v : shared->vertices) top = std::max(top, v.pos.y);
        float quadTop = 0;
        for (const auto& v : billboard->vertices) quadTop = std::max(quadTop, v.pos.y);
        assert(std::fabs(quadTop - top) < 1e-4f);
    }
    {   // Legacy silhouette stays available.
        ArborForestAdapter legacyForest;
        legacyForest.setImpostorFromNearModel(false);
        legacyForest.addPrototype(oak, 17);
        assert(legacyForest.billboardPrototype(oak.speciesId)->images[0].rgba == legacyPixels);
    }
    assert(billboardRepeat.model->indices == billboard->indices);
    assert(billboardRepeat.viewMaterials.size() == 8);
    for (size_t i = 0; i < billboardRepeat.viewMaterials.size(); ++i) {
        const auto& layers = billboardRepeat.viewMaterials[i];
        assert(layers.size() == 2);
        for (size_t layer = 0; layer < 2; ++layer) {
            assert(layers[layer].baseTex == 0);
            assert(layers[layer].uvScale.x > 0.0f);
            assert(layers[layer].uvScale.x < 1.0f / 16.0f);
        }
        // The two azimuth pages occupy disjoint halves of one texture.
        assert(layers[0].uvOffset.x < 0.5f);
        assert(layers[1].uvOffset.x > 0.5f);
        if (i > 0)
            for (size_t layer = 0; layer < 2; ++layer)
                assert(layers[layer].uvOffset.x >
                       billboardRepeat.viewMaterials[i - 1][layer].uvOffset.x);
    }
    assert(selectArborImpostorAzimuth(0, 0, 0, 0, 1000, 8) == 0);
    assert(selectArborImpostorAzimuth(0, 0, 0, 1000, 0, 8) == 2);
    assert(selectArborImpostorAzimuth(90000, 0, 0, 1000, 0, 8) == 0);
    bool badBillboardSettings = false;
    try { (void)makeArborBillboardModel(oak, 17, {16, 256, 2}); }
    catch (const std::invalid_argument&) { badBillboardSettings = true; }
    assert(badBillboardSettings);

    // A far tree rotates its +Z-facing card towards the real camera, not
    // towards the tree's authored yaw or the world origin.
    std::vector<aiverse::render::ModelDraw> eastFacing;
    forest.forEachDraw(
        std::vector<FloraBatch>{FloraBatch{1001, TreeLOD::Impostor, {first}}},
        origin, [&](const auto& draw) { eastFacing.push_back(draw); },
        ForestCameraCm{first.xCm + 10000, first.zCm});
    assert(eastFacing.size() == 1);
    assert(eastFacing.front().model == billboard);
    assert(std::fabs(eastFacing.front().world.at(0, 2) - 2.0f) < 0.0001f);
    assert(eastFacing.front().persistentId == stableId);
    assert(eastFacing.front().materials != nullptr);
    const auto frame0 = makeArborImpostorAtlas(oak, 17, hiRes).viewMaterials[0][0];
    assert(std::fabs(eastFacing.front().materials->at(0).uvOffset.x -
                     frame0.uvOffset.x) < 0.0001f);
    bool unknownRejected = false;
    try {
        forest.forEachDraw(
            std::vector<FloraBatch>{FloraBatch{999, TreeLOD::High, {first}}},
            origin, [](const auto&) {});
    } catch (const std::invalid_argument&) { unknownRejected = true; }
    assert(unknownRejected);
    bool extentRejected = false;
    try {
        forest.forEachDraw(forestBatches, ForestRenderOriginCm{INT64_MAX, 0, 0},
                           [](const auto&) {});
    } catch (const std::out_of_range&) { extentRejected = true; }
    assert(extentRejected);
}

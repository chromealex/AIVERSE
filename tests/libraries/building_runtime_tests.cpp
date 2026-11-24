// Portable runtime building generator smoke/regression. No Studio, GPU or GLB
// exporter in the tested dependency graph. Packageable native game API.
#include <engine/systems/RuntimeBuildingGenerator.h>
#include <engine/systems/CachedRuntimeBuildingGenerator.h>
#include <engine/systems/BuildingInstances.h>
#include <engine/systems/BuildingStreamPolicy.h>
#include <engine/systems/BuildingSurfaceCache.h>
#include <engine/buildings/BuildingDestructionAdapter.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace aiverse;
using namespace aiverse::lib::systems;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__)+":" + std::to_string(__LINE__)+": " #x); } while(false)

int main(int argc,char** argv) {
    try {
        const std::string root=argc>1?argv[1]:".";
        std::ifstream file(root+"/engine/assets/building-styles/scifi.json");
        CHECK(bool(file));
        std::stringstream buffer;
        buffer<<file.rdbuf();

        RuntimeBuildingGenerator generator;
        std::string error;
        CHECK(!generator.registerStyleJson("",&error)&&!error.empty());
        CHECK(generator.registerStyleJson(buffer.str(),&error)&&error.empty());
        CHECK(generator.styleCount()==1);
        CHECK(generator.style("scifi")!=nullptr);
        CHECK(generator.styleIds()==std::vector<std::string>{"scifi"});
        CHECK(!generator.registerStyleJson(buffer.str(),&error)&&!error.empty());
        CHECK(!generator.registerStyleJson("{oops}",&error)&&!error.empty());
        CHECK(generator.styleCount()==1); // registration failure cannot poison state
        CHECK(generator.generate({"unknown","factory",3,{}, {},0,false,{}}).error==RuntimeBuildingError::UnknownStyle);
        CHECK(generator.generate({"scifi","unknown",3,{}, {},0,false,{}}).error==RuntimeBuildingError::UnknownFunction);
        CHECK(generator.generate({"scifi","factory",3,{10,0,12}, {},0,false,{}}).error==RuntimeBuildingError::InvalidInput);
        CHECK(generator.generate({"scifi","factory",3,{}, {},4,false,{}}).error==RuntimeBuildingError::InvalidInput);

        for(const auto* kind:{"barracks","command","factory","depot"}) {
            for(const uint64_t seed:{3ull,7ull,42ull}) {
                RuntimeBuildingRequest request;
                request.style="scifi";
                request.function=kind;
                request.seed=seed;
                // Unsized generation is the backwards-compatible fallback,
                // not a substitute for failed explicit dimensions.
                auto result=generator.generate(request);
                auto repeat=generator.generate(request);
                CHECK(result && repeat);
                CHECK(!result.building.mesh.parts.empty());
                CHECK(!result.building.mesh.materials.empty());
                CHECK(!result.building.boxes.empty());
                CHECK(result.building.mesh.vertices.size()==repeat.building.mesh.vertices.size());
                CHECK(result.building.mesh.triangles()==repeat.building.mesh.triangles());
                CHECK(result.building.gates==repeat.building.gates);
                CHECK(result.building.boxes==repeat.building.boxes);
                for(size_t n=0;n<result.building.mesh.vertices.size();++n)
                    for(int c=0;c<3;++c) {
                        CHECK(result.building.mesh.vertices[n].pos[c]==repeat.building.mesh.vertices[n].pos[c]);
                        CHECK(result.building.mesh.vertices[n].nrm[c]==repeat.building.mesh.vertices[n].nrm[c]);
                    }
            }
        }

        RuntimeBuildingRequest sized;
        sized.style="scifi";
        sized.function="factory";
        sized.seed=7;
        sized.size={24,12,18};
        auto first=generator.generate(sized);
        auto second=generator.generate(sized);
        CHECK(first.error==second.error);
        CHECK(first.message==second.message);
        CHECK(first.building.mesh.triangles()==second.building.mesh.triangles());
        // Sized runtime generation may legitimately reject an impossible
        // envelope, but MUST report failure instead of returning a fake mesh.
        CHECK(first || (!first && first.error==RuntimeBuildingError::FitFailed));
        if(first) {
            CHECK(std::abs(first.building.mesh.boundsMin[0])<0.01f);
            CHECK(std::abs(first.building.mesh.boundsMax[0]-24.f)<0.01f);
            CHECK(std::abs(first.building.mesh.boundsMax[1]-12.f)<0.01f);
            CHECK(std::abs(first.building.mesh.boundsMax[2]-18.f)<0.01f);
        } else {
            CHECK(!first.message.empty());
        }
        // Native building damage adapter: speculative tick does not mutate
        // the owner, fragments retain source lineage, and archive roundtrips.
        RuntimeBuildingRequest damageRequest;damageRequest.style="scifi";
        damageRequest.composeFeatures=true;damageRequest.size={16,8,12};
        const auto initial=generator.generate(damageRequest);CHECK(initial);
        auto damage=aiverse::building::beginBuildingDestruction(initial.structureGraph,nullptr,aiverse::building::BuildingSupportMode::DamageOnly);
        CHECK(damage&&damage->structure.pieces.size()==initial.structureGraph.solids.size());
        const auto previous=damage->structure.pieces.size();
        aiverse::lib::mechanics::StructureDamage hit;
        hit.id=1;hit.center={Fixed::fromRatio(8,1),Fixed::fromRatio(2,1),Fixed::fromRatio(18,100)};
        hit.radius=Fixed::fromRatio(3,2);hit.amount=220;
        auto frame=aiverse::building::stageBuildingDestruction(*damage,1,Fixed::fromRatio(1,60),std::span<const aiverse::lib::mechanics::StructureDamage>(&hit,1));
        CHECK(frame&&damage->structure.pieces.size()==previous);
        CHECK(!frame->removed.empty()&&!frame->next.structure.debris.empty());
        CHECK(frame->next.structure.lastTick==1);
        for(const auto& piece:frame->added)CHECK(piece.source.sourceId>0);
        CHECK(aiverse::building::validBuildingDestruction(frame->next));
        std::string save;Archive write(&save);write(frame->next);CHECK(write.ok());
        aiverse::building::BuildingDestructionState restored;Archive read(save);read(restored);
        CHECK(read.ok()&&read.remaining()==0&&restored.fingerprint==damage->fingerprint);
        CHECK(!aiverse::building::stageBuildingDestruction(restored,1,Fixed::fromRatio(1,60),{}));
        CHECK(aiverse::building::commitBuildingDestruction(*damage,*frame));
        CHECK(!aiverse::building::commitBuildingDestruction(*damage,*frame));
        // A minimal supported stack: roof and upper wall fall when the
        // only load-bearing wall is removed; terminal glass cannot bridge.
        using namespace aiverse::building;
        using Part=BuildingGraphPart;
        BuildingStructureGraph stack;
        stack.solids={{Part::Floor,0,0,0,8,.24f,8},
            {Part::Wall,1,.24f,1,1.36f,3,5},
            {Part::Roof,1,3,1,5,3.28f,5},
            {Part::Wall,3,3.28f,2,3.36f,6,4},
            {Part::WindowPane,1.45f,.24f,1,1.50f,3,5}};
        auto supported=beginBuildingDestruction(stack);CHECK(supported);
        CHECK(buildingSupportedPieces(supported->structure,supported->sources).size()==5);
        aiverse::lib::mechanics::StructureDamage cut;
        cut.id=1;cut.center={Fixed::fromRatio(118,100),Fixed::fromRatio(16,10),Fixed::fromRatio(3,1)};
        cut.radius=Fixed::fromRatio(2,1);cut.amount=10000;
        auto collapse=stageBuildingDestruction(*supported,1,Fixed::fromRatio(1,60),std::span<const aiverse::lib::mechanics::StructureDamage>(&cut,1));
        CHECK(collapse);
        CHECK(std::any_of(collapse->events.begin(),collapse->events.end(),[](const auto& e){
            return e.kind==aiverse::lib::mechanics::StructureEventKind::Collapsed&&e.piece==4;
        }));
        CHECK(!collapse->next.structure.pieces.contains(4));
        CHECK(buildingSupportedPieces(collapse->next.structure,collapse->next.sources).size()==collapse->next.structure.pieces.size());
        BuildingStructureGraph glassBridge;
        glassBridge.solids={{Part::Floor,0,0,0,4,.24f,4},
            {Part::WindowPane,1,.24f,1,1.05f,3,3},
            {Part::Roof,1,3,1,3,3.28f,3}};
        CHECK(!beginBuildingDestruction(glassBridge));
        CHECK(beginBuildingDestruction(glassBridge,nullptr,BuildingSupportMode::DamageOnly));
        CachedRuntimeBuildingGenerator cached(64u*1024u*1024u,1);
        CHECK(cached.registerStyleJson(buffer.str()));
        RuntimeBuildingRequest cachedRequest;cachedRequest.style="scifi";cachedRequest.composeFeatures=true;cachedRequest.size={16,8,12};
        auto c0=cached.generate(cachedRequest);auto c1=cached.generate(cachedRequest);
        CHECK(c0&&*c0&&c0==c1&&cached.stats().hits==1);
        cachedRequest.seed++;auto c2=cached.generate(cachedRequest);
        CHECK(c2&&*c2&&c2!=c0&&cached.stats().evictions==1);
        cachedRequest.origin.x=Fixed::fromInt(100);auto c3=cached.generate(cachedRequest);
        CHECK(c3&&*c3&&c3!=c2&&c3->building.mesh.boundsMin[0]>=99.f);
        cached.clear();CHECK(cached.stats().entries==0&&*c0);
        CHECK(!*cached.generate(cachedRequest));
        BuildingSurfaceCache surfaces(128u*1024u,1);
        auto p0=surfaces.get("wood","desk",4,BuildingSurfaceChannel::BaseColor,128);
        auto p1=surfaces.get("wood","desk",4,BuildingSurfaceChannel::BaseColor,128);
        CHECK(p0==p1&&p0->rgba.size()==128u*128u*4u&&surfaces.stats().hits==1);
        auto p2=surfaces.get("wood","desk",4,BuildingSurfaceChannel::Normal,128);
        CHECK(p2!=p0&&surfaces.stats().evictions==1);
        CHECK(surfaces.get("bad","desk",4,BuildingSurfaceChannel::Normal,128)->rgba.empty());
        CHECK(surfaces.stats().entries==1);
        CachedRuntimeBuildingGenerator instanceCache;
        CHECK(instanceCache.registerStyleJson(buffer.str()));
        RuntimeBuildingRequest inst;inst.style="scifi";inst.composeFeatures=true;inst.size={16,8,12};
        auto templateA=acquireBuildingInstanceTemplate(instanceCache,inst,1,1,1);CHECK(templateA);
        inst.origin.x=Fixed::fromInt(90);inst.yaw=1;
        auto templateB=acquireBuildingInstanceTemplate(instanceCache,inst,1,1,1);CHECK(templateB);
        CHECK(templateA.local==templateB.local&&templateA.modelKey==templateB.modelKey);
        auto templateC=acquireBuildingInstanceTemplate(instanceCache,inst,1,1,2);CHECK(templateC);
        CHECK(templateC.modelKey!=templateB.modelKey);
        BuildingPlacedInstance placed;
        CHECK(placeBuildingInstance(templateB,{2,inst.origin,inst.yaw},placed));
        const auto direct=generator.generate(inst);CHECK(direct);
        CHECK(placed.boxes==direct.building.boxes&&placed.gates==direct.building.gates);
        CHECK(!placeBuildingInstance(templateB,{0,inst.origin,inst.yaw},placed));
        CHECK(placed.placement.id==2);
        // Streaming is presentation-only, deterministic and model-budgeted.
        BuildingPlacedInstance nearInstance,farInstance,otherModel;
        CHECK(placeBuildingInstance(templateA,{1,{},0},nearInstance));
        CHECK(placeBuildingInstance(templateA,{3,{Fixed::fromInt(120),{},{}},0},farInstance));
        CHECK(placeBuildingInstance(templateC,{4,{Fixed::fromInt(20),{},{}},0},otherModel));
        std::vector<BuildingPlacedInstance> streamCandidates{farInstance,otherModel,nearInstance};
        BuildingStreamPolicy streamPolicy;streamPolicy.enterDistance=100;streamPolicy.exitDistance=150;
        streamPolicy.maxNewModels=1;
        BuildingStreamPlan streamPlan;
        const auto absent=[](const std::string&){return false;};
        CHECK(planBuildingStream(streamCandidates,{},streamPolicy,{},absent,streamPlan));
        CHECK(streamPlan.selectedIds==std::set<uint64_t>{1}&&streamPlan.newModels==1);
        CHECK(streamPlan.outside==1&&streamPlan.deferred==1);
        CHECK(planBuildingStream(streamCandidates,{},streamPolicy,{3},absent,streamPlan));
        CHECK(streamPlan.selectedIds.size()==2&&streamPlan.selectedIds.contains(3)&&streamPlan.newModels==1);
        streamPolicy.maxNewModels=0;
        CHECK(planBuildingStream(streamCandidates,{},streamPolicy,{3},[&](const std::string& k){return k==templateA.modelKey;},streamPlan));
        CHECK(streamPlan.selectedIds.size()==2&&streamPlan.newModels==0);
        streamCandidates.push_back(nearInstance);
        CHECK(!planBuildingStream(streamCandidates,{},streamPolicy,{},absent,streamPlan));
        CHECK(streamPlan.selectedIds.size()==2);
        generator.clear();
        CHECK(generator.styleCount()==0);
        CHECK(generator.generate(sized).error==RuntimeBuildingError::UnknownStyle);
        // Results own their CPU vertices after registry data is released.
        if(first)CHECK(!first.building.mesh.vertices.empty());
        std::puts("building runtime API PASS");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"building runtime API FAIL: %s\n",e.what());
        return 1;
    }
}

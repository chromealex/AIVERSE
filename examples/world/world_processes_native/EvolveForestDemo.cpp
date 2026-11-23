#include "EvolveForestDemo.h"
#include "engine/physis/PhysisIntegration.h"
#include "engine/world/nereid/NereidNativeWater.h"
#include "aiverselib/world/WorldProcessesMapHydration.h"
#include <iostream>
#include <memory>
#include <fstream>
#include <sstream>

using namespace aiverse;
namespace wp=aiverse::lib::worldprocess::authoring;
namespace demo=aiverse::examples::world_processes_native;
struct ForestDemo final:GameModule {
    std::vector<Entity> entities;
    std::string name()const override{return "evolve-forest-demo";}
    void setup(Simulation& sim,const Json&,const Json& map)override{
        for(const auto& entry:map["entities"].items()){
            (void)entry;entities.push_back(sim.world().create());
        }
        physis::Settings weather;
        weather.originX=-2;weather.originZ=-1;
        weather.width=4;weather.height=2;weather.cellSizeMeters=32;
        physis::InstallOptions options;
        options.climate=weather;
        options.floodMode=physis::FloodMode::ExternalAuthority;
        physis::install(sim,options);
        wp::hydrateMap(sim,map,entities);
        for(int i=0;i<3;++i){
            const auto cell=physis::cellAtPosition(sim.world(),
                Vec3{Fixed::fromInt((i-1)*30),Fixed{},Fixed{}});
            const auto producer=sim.world().create();
            evolve::ClimateLink link;
            link.rainEnrichment=1000;link.droughtConsumption=1000;
            link.rainPurification=500;link.warmGrowth=1000;
            std::string error;
            if(!demo::attachEvolveClimate(sim.world(),producer,entities.at(i),cell,link,&error))
                throw std::runtime_error(error);
        }
        lib::nereid::NativeWaterPipelineConfig water;
        water.probeFromTransform=false;
        lib::nereid::registerNativeWaterPipeline(sim,"nereid",water);
        lib::nereid::WaterBodyComponent lake;
        lake.center={Fixed::fromInt(30),Fixed{},Fixed{}};
        lake.halfExtents={Fixed::fromInt(6),Fixed::fromInt(6)};
        lake.surfaceY=Fixed::fromInt(10);lake.bottomY=Fixed::fromInt(-100);
        lib::nereid::spawnNativeWaterBodies(sim.world(),{lake});
        terraweave::TerraWeaveEcsAuthorityConfig cfg;
        cfg.immutableRecipeId="evolve-demo-v1";
        terraweave::registerTerraWeaveAuthorityResource(sim,cfg);
        terraweave::registerTerraWeaveForestEcologyPersistence(sim);
        auto state=*sim.world().resourceConst<terraweave::TerraWeaveAuthorityResource>();
        auto ground=terraweave::materializeEcsTerrain(state,cfg);
        for(int region=0;region<3;++region)
            for(int i=0;i<9;++i){
                terraweave::AnchoredPlant p;
                p.anchor.speciesId=1;p.anchor.plantKey=1+region*9+i;
                p.anchor.xCm=(region-1)*3000+(i%3-1)*500;
                p.anchor.zCm=(i/3-1)*500;
                p.anchor.roots=terraweave::defaultRootProfile(terraweave::PlantHabit::Tree);
                p.anchor.plantedHeightCm=ground.sampleHeightCm(p.anchor.xCm,p.anchor.zCm);
                state.rooted.push_back(p);
            }
        terraweave::RootSupportRegistry roots;
        roots.setSnapshot(state.rooted);
        state.rooted=roots.snapshot();
        sim.world().setResource<terraweave::TerraWeaveAuthorityResource>(std::move(state));
        terraweave::ForestEcologySpecies species;species.speciesId=1;
        demo::WorldProcessGameplayHost::addEcologyEcsSystem(sim,
            "terraweave.forest_ecology",cfg,{species});
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
int main(int argc,char** argv){
    try{
        if(argc<2){std::cerr<<"Usage: aiverse_evolve_forest_demo MAP_JSON [ticks] [--replay] [--load FILE] [--save FILE]\n";return 2;}
        std::ifstream file(argv[1]);if(!file)throw std::runtime_error("Cannot open demo map");
        std::ostringstream data;data<<file.rdbuf();
        auto map=Json::parse(data.str());
        Simulation sim(SimConfig{12,20,0,true},std::make_unique<ForestDemo>());
        sim.setup(Json::object(),map);
        const auto ticks=argc>2?std::stoul(argv[2]):1024ul;
        bool replay=false;
        std::string loadPath,savePath;
        for(int i=3;i<argc;++i){
            const std::string arg=argv[i];
            if(arg=="--replay")replay=true;
            else if((arg=="--load"||arg=="--save")&&i+1<argc){
                (arg=="--load"?loadPath:savePath)=argv[++i];
            }else throw std::invalid_argument("Unknown or incomplete demo option: "+arg);
        }
        if(!loadPath.empty()){
            std::ifstream input(loadPath,std::ios::binary);
            if(!input)throw std::runtime_error("Cannot open save");
            std::ostringstream bytes;bytes<<input.rdbuf();
            std::string error;
            if(!sim.deserialize(bytes.str(),&error))throw std::runtime_error(error);
        }
        SimSnapshot checkpoint;
        if(replay)sim.saveState(checkpoint);
        if(ticks>1000000)throw std::invalid_argument("Tick budget exceeded");
        for(unsigned long i=0;i<ticks;++i)sim.step();
        if(replay){
            const auto expected=sim.checksum();
            sim.loadState(checkpoint);
            for(unsigned long i=0;i<ticks;++i)sim.step();
            if(sim.checksum()!=expected)
                throw std::runtime_error("Ecology rollback replay checksum mismatch");
            std::cout<<"rollback replay checksum matched\n";
        }
        sim.world().eachExistingConst<evolve::Region>([](Entity e,const evolve::Region& r){
            std::cout<<e.packed()<<" fertility="<<r.fertility<<" pollution="<<r.pollution<<"\n";
        });
        std::cout<<"tick="<<sim.tick()<<" checksum="<<sim.checksum()<<"\n";
        if(const auto* authority=sim.world().resourceConst<terraweave::TerraWeaveAuthorityResource>()){
            std::cout<<"rooted="<<authority->rooted.size()<<" revision="<<authority->stateRevision<<"\n";
        }
        if(const auto* ecology=sim.world().resourceConst<terraweave::ForestEcologyResource>()){
            std::size_t dead=0,recruited=0;
            for(const auto& p:ecology->plants){dead+=p.dead;recruited+=p.recruited;}
            std::cout<<"tracked="<<ecology->plants.size()<<" dead="<<dead
                     <<" recruited="<<recruited<<" lastStep="<<ecology->lastStepTick<<"\n";
        }
        if(!savePath.empty()){
            const auto bytes=sim.serialize();
            std::ofstream output(savePath,std::ios::binary|std::ios::trunc);
            output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
            output.close();
            if(!output)throw std::runtime_error("Cannot write save");
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}

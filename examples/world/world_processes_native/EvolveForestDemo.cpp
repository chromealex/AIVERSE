#include "EvolveForestDemo.h"
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
        wp::hydrateMap(sim,map,entities);
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
        if(argc<2){std::cerr<<"Usage: aiverse_evolve_forest_demo MAP_JSON [ticks]\n";return 2;}
        std::ifstream file(argv[1]);if(!file)throw std::runtime_error("Cannot open demo map");
        std::ostringstream data;data<<file.rdbuf();
        auto map=Json::parse(data.str());
        Simulation sim(SimConfig{12,20,0,true},std::make_unique<ForestDemo>());
        sim.setup(Json::object(),map);
        const auto ticks=argc>2?std::stoul(argv[2]):1024ul;
        if(ticks>1000000)throw std::invalid_argument("Tick budget exceeded");
        for(unsigned long i=0;i<ticks;++i)sim.step();
        sim.world().eachExistingConst<evolve::Region>([](Entity e,const evolve::Region& r){
            std::cout<<e.packed()<<" fertility="<<r.fertility<<" pollution="<<r.pollution<<"\n";
        });
        std::cout<<"tick="<<sim.tick()<<" checksum="<<sim.checksum()<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}

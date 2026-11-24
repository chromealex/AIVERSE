#include "engine/world/nereid/NereidQueryIndex.h"
#include "engine/world/NereidFlowAuthoring.h"
#include <memory>
#include <stdexcept>
#include <iostream>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Host final:GameModule {
    Entity river{};
    std::string name()const override{return "graded-river";}
    void setup(Simulation& sim,const Json&,const Json&)override{
        nw::registerNereidWaterPersistence(sim);nw::registerNereidGeometryPersistence(sim);nw::registerNereidRiverGradePersistence(sim);
        auto& w=sim.world();river=w.create();nw::WaterBodyComponent b;b.kind=nw::WaterKind::River;b.surfaceY=2_fx;b.bottomY=0_fx;
        nw::WaterGeometryComponent g;g.kind=nw::WaterGeometryKind::RiverSpline;g.points={{0_fx,-100_fx,5_fx},{0_fx,100_fx,5_fx}};
        nw::WaterRiverGradeComponent grade;grade.risePerMetre={0_fx,Fixed::fromRatio(1745,100000)};
        w.add<nw::WaterBodyComponent>(river,b);w.add<nw::WaterGeometryComponent>(river,g);w.add<nw::WaterRiverGradeComponent>(river,grade);
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
static std::unique_ptr<Simulation> create(){auto s=std::make_unique<Simulation>(SimConfig{42,20,0,true},std::make_unique<Host>());s->setup(Json::object(),Json::object());return s;}
int main(){
    auto a=create(),b=create();a->world().enableCowSnapshots(true);
    const Vec3 p{0_fx,1_fx,50_fx};auto sample=nw::sampleWater(a->world(),p);
    check(sample.surfaceY>2_fx&&sample.flow.z<0_fx&&sample.submerged,"sloped contact and downhill flow");
    const nw::WaterQueryIndex index(a->world());const auto indexed=index.sample(p);check(indexed.surfaceY==sample.surfaceY&&indexed.flow==sample.flow,"indexed parity");
    check(a->checksum()==b->checksum(),"two-host initial parity");
    auto bytes=a->serialize();std::string error;check(b->deserialize(bytes,&error),"native grade load");check(a->checksum()==b->checksum(),"native grade persistence");
    for(int i=0;i<8;++i){a->step();b->step();check(a->checksum()==b->checksum(),"grade replay parity");}
    SimSnapshot checkpoint;a->saveState(checkpoint);
    Entity river{};a->world().eachExistingConst<nw::WaterBodyComponent>([&](Entity e,const auto&){river=e;});
    a->world().get<nw::WaterRiverGradeComponent>(river).risePerMetre.y=Fixed::fromRatio(2,100);a->step();const auto changed=a->checksum();
    a->loadState(checkpoint);a->world().get<nw::WaterRiverGradeComponent>(river).risePerMetre.y=Fixed::fromRatio(2,100);a->step();check(a->checksum()==changed,"grade COW rollback");
    auto body=nw::WaterBodyComponent{};auto grade=nw::WaterRiverGradeComponent{};grade.risePerMetre={0_fx,0_fx};check(nw::gradedWaterBody(body,&grade,{}).flow==Vec3{},"flat auto flow");
    grade.automaticCurrent=false;body.flow={2_fx,0_fx,0_fx};check(nw::gradedWaterBody(body,&grade,{}).flow==body.flow,"manual flow unchanged");
    auto root=Json::parse(R"({"nereidWater":{"surfaces":[{"id":1,"kind":"river","shape":"river","surfaceY":2,"bottomY":0,"points":[[0,-100,5],[0,100,5]]}]}})");
    auto input=Json::parse(R"({"id":1,"mode":"automatic","slopeDegrees":1,"downhillHeadingDegrees":180})");root=world::editNereidFlow(root,input);
    auto scene=world::parseNereidWaterManifest(root).surfaces[0];check(scene.mesh.useGrade,"grade authored");
    auto mesh=nw::buildWaterSurfaceMesh(scene.body,&scene.geometry,scene.mesh);check(!mesh.empty(),"graded surface built");
    for(const auto& v:mesh.vertices){const Fixed z=Fixed::fromRaw(int64_t(v.z*Fixed::kOne));const auto expected=scene.body.surfaceY+scene.mesh.grade.risePerMetre.y*z;check(std::abs(v.y-expected.toDouble())<.002,"mesh and gameplay share grade");}
    bool rejected=false;try{world::editNereidFlow(root,Json::parse(R"({"id":1,"mode":"automatic","slopeDegrees":90})"));}catch(const std::exception&){rejected=true;}check(rejected,"invalid grade rejected");
    std::cout<<"PASS grade sampling, indexed parity, native persistence/replay, manual/flat flow, mesh and typed authoring\n";
}

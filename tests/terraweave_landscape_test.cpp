#include "engine/world/TerraWeaveRiverbed.h"
#include "engine/world/TerraWeaveAlpine.h"
#include "engine/world/TerraWeaveShoreCover.h"
#include "engine/world/nereid/NereidFootprintDistance.h"
#include "engine/world/NereidManifestHost.h"
#include "engine/world/NereidFlowAuthoring.h"
#include "engine/world/nereid/NereidQueryIndex.h"
#include <cassert>
using namespace aiverse;
using namespace aiverse::terraweave;
int main(){
    TerrainSettings base;base.reliefCm=0;
    std::vector<AlpinePeak> peaks{{0,0,10000,16000,18000,400}};
    assert(alpineHeightCm(base,peaks,30000,0)==heightAtCm(base,30000,0));
    assert(alpineHeightCm(base,peaks,0,0)>10000);
    bool varied=false;
    for(int x=-9000;x<9000;x+=150){auto other=base;other.seed+=1;varied|=alpineHeightCm(base,peaks,x,2000)!=alpineHeightCm(other,peaks,x,2000);}
    assert(varied);
    bool badPeak=false;try{alpineHeightCm(base,{{0,0,0,1000,1000,0}},0,0);}catch(const std::invalid_argument&){badPeak=true;}assert(badPeak);

    {
        auto massif=peaks;massif[0].shearPermille=0;
        massif[0].morphology=AlpineMorphology::RidgedMassif;
        auto alternate=base;alternate.seed+=1;
        bool changed=false;int32_t highest=0;
        for(int z=-14000;z<=14000;z+=500)for(int x=-9000;x<=9000;x+=500) {
            const auto h=alpineHeightCm(base,massif,x,z);
            assert(h==alpineHeightCm(base,massif,x,z));
            assert(h>=heightAtCm(base,x,z)&&h<=heightAtCm(base,x,z)+18000);
            highest=std::max(highest,h-heightAtCm(base,x,z));
            changed|=h!=alpineHeightCm(alternate,massif,x,z);
        }
        assert(changed&&highest>5000);
        // Exact support boundary, including a vanishing outer slope. This
        // prevents a truncated pedestal at neighbouring tile boundaries.
        assert(alpineHeightCm(base,massif,10000,0)==heightAtCm(base,10000,0));
        assert(alpineHeightCm(base,massif,9999,0)-heightAtCm(base,9999,0)<=1);
        assert(alpineHeightCm(base,massif,-10000,0)==heightAtCm(base,-10000,0));
        assert(alpineHeightCm(base,massif,0,0,3500,9000)==heightAtCm(base,0,0));
        massif[0].morphology=static_cast<AlpineMorphology>(9);
        bool bad=false;try{(void)alpineHeightCm(base,massif,0,0);}catch(const std::invalid_argument&){bad=true;}
        assert(bad);
    }

    {
        // A broad 320m / 260m massif must not grow metre-wide near-vertical
        // needles as high-frequency octaves are added. This exact fixture
        // exposed an 18.15m/m maximum slope before gradient rolloff.
        TerrainSettings source;source.seed=2718;source.reliefCm=0;
        AlpinePeak peak{0,0,26000,34000,32000,100};
        peak.morphology=AlpineMorphology::RidgedMassif;
        const std::vector<AlpinePeak> range{peak};
        double maximum=0;
        for(int z=-30000;z<=30000;z+=500)for(int x=-24000;x<=24000;x+=500) {
            const double dx=(alpineHeightCm(source,range,x+50,z)-alpineHeightCm(source,range,x-50,z))/100.;
            const double dz=(alpineHeightCm(source,range,x,z+50)-alpineHeightCm(source,range,x,z-50))/100.;
            maximum=std::max(maximum,dx*dx+dz*dz);
        }
        assert(maximum>1&&maximum<16);
    }

    {
        TerrainSettings source;source.seed=2718;source.reliefCm=0;
        AlpinePeak peak{0,0,26000,34000,32000,100};
        peak.morphology=AlpineMorphology::CraggedMassif;
        const std::vector<AlpinePeak> range{peak};auto alternate=source;alternate.seed+=1;
        double maximum=0;bool changed=false;int highest=0;
        for(int z=-30000;z<=30000;z+=500)for(int x=-24000;x<=24000;x+=500) {
            const auto h=alpineHeightCm(source,range,x,z);assert(h==alpineHeightCm(source,range,x,z));
            assert(h>=heightAtCm(source,x,z)&&h<=heightAtCm(source,x,z)+32000);
            highest=std::max(highest,h-heightAtCm(source,x,z));changed|=h!=alpineHeightCm(alternate,range,x,z);
            const double dx=(alpineHeightCm(source,range,x+50,z)-alpineHeightCm(source,range,x-50,z))/100.;
            const double dz=(alpineHeightCm(source,range,x,z+50)-alpineHeightCm(source,range,x,z-50))/100.;
            maximum=std::max(maximum,dx*dx+dz*dz);
        }
        assert(changed&&highest>20000&&maximum>1&&maximum<25);
        peak.shearPermille=0;assert(alpineHeightCm(source,{peak},26000,0)==heightAtCm(source,26000,0));
        assert(alpineHeightCm(source,{peak},25999,0)-heightAtCm(source,25999,0)<=1);
    }

    lib::nereid::WaterBodyComponent body;body.surfaceY=0_fx;body.bottomY=-2_fx;
    lib::nereid::WaterGeometryComponent river;river.kind=lib::nereid::WaterGeometryKind::RiverSpline;
    river.points={{0_fx,-20_fx,3_fx},{0_fx,20_fx,3_fx}};
    const auto near=shoreCover(*lib::nereid::waterFootprintDistance(body,&river,{4_fx,0_fx}),Fixed::fromRatio(5,10));
    const auto far=shoreCover(*lib::nereid::waterFootprintDistance(body,&river,{20_fx,0_fx}),Fixed::fromRatio(5,10));
    assert(near.sandPermille>500&&far.sandPermille==0&&far.wetPermille==0);
    const auto submerged=shoreCover(-1_fx,-1_fx);assert(submerged.sandPermille==1000&&submerged.wetPermille==1000);
    const auto island=shoreCover(-100_fx,3_fx);assert(island.sandPermille==0&&island.wetPermille==0);
    uint32_t previous=1000;
    for(int cm=0;cm<=600;cm+=10){const auto mask=shoreCover(Fixed::fromRatio(cm,100),0_fx);assert(mask.sandPermille<=previous);previous=mask.sandPermille;}
    assert(previous==0);
    EarthworksLayer whole(100,[](auto,auto){return 300;});
    EarthworksLayer split(100,[](auto,auto){return 300;});
    carveNereidRiverbed(whole,body,river,{-1000,-1000,1000,1000},500);
    carveNereidRiverbed(split,body,river,{-1000,-1000,0,1000},500);
    carveNereidRiverbed(split,body,river,{0,-1000,1000,1000},500);
    assert(whole.snapshot().heights==split.snapshot().heights);
    assert(whole.heightAt({0,0})==-200);
    assert(whole.heightAt({9,0})==300);
    assert(whole.heightAt({3,0})>0); // water perimeter buried in bank, no exposed cut edge
    assert(alpineHeightCm(base,peaks,0,0,3500,9000)==heightAtCm(base,0,0));
    assert(whole.heightAt({5,0})>whole.heightAt({3,0}));
    const auto before=whole.snapshot();bool rejected=false;
    try{carveNereidRiverbed(whole,body,river,{-1000,-1000,1000,1000},0);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected&&whole.snapshot().heights==before.heights);
    world::NereidManifestHost host;
    host.configure(Json::parse(R"({"nereidWater":{"surfaces":[{"id":1,"kind":"river","shape":"river","surfaceY":0,"bottomY":-2,"points":[[0,-20,3],[0,20,3]]}]}})"));
    assert(host.configuredSurfaces()==1);
    bool invalid=false;try{host.configure(Json::parse(R"({"nereidWater":{"surfaces":[{}]}})"));}catch(const std::exception&){invalid=true;}
    assert(invalid&&host.configuredSurfaces()==1);
    host.configure(Json::object());assert(host.configuredSurfaces()==0);
    auto config=Json::parse(R"({"nereidWater":{"surfaces":[{"id":1,"kind":"river","shape":"river","surfaceY":1,"bottomY":-1,"points":[[0,-20,3],[0,20,3]]}]}})");
    auto settings=Json::parse(R"({"id":1,"mode":"automatic","slopeDegrees":1,"downhillHeadingDegrees":180})");
    config=world::editNereidFlow(config,settings);auto slope=world::parseNereidWaterManifest(config).surfaces[0];
    assert(slope.mesh.useGrade&&slope.mesh.grade.risePerMetre.y>Fixed{});
    std::string savedGrade;Archive output(&savedGrade);output(slope.mesh.grade);lib::nereid::WaterRiverGradeComponent restored;Archive input(savedGrade);input(restored);assert(input.ok()&&lib::nereid::sameWaterRiverGrade(restored,slope.mesh.grade));
    const auto local=lib::nereid::gradedWaterBody(slope.body,&slope.mesh.grade,{0_fx,0_fx,10_fx},&slope.geometry);
    assert(local.surfaceY>slope.body.surfaceY&&local.flow.z<Fixed{});
    World world;auto entity=world.create();world.add<lib::nereid::WaterBodyComponent>(entity,slope.body);world.add<lib::nereid::WaterGeometryComponent>(entity,slope.geometry);world.add<lib::nereid::WaterRiverGradeComponent>(entity,slope.mesh.grade);
    const auto sample=lib::nereid::sampleWater(world,{0_fx,0_fx,10_fx});assert(sample.surfaceY==local.surfaceY&&sample.flow==local.flow);
    lib::nereid::WaterQueryIndex index(world);const auto indexed=index.sample({0_fx,0_fx,10_fx});assert(indexed.surfaceY==sample.surfaceY&&indexed.flow==sample.flow);
    settings=Json::parse(R"({"id":1,"mode":"manual","flow":[1,0,0]})");config=world::editNereidFlow(config,settings);slope=world::parseNereidWaterManifest(config).surfaces[0];assert(lib::nereid::gradedWaterBody(slope.body,&slope.mesh.grade,{}).flow.x==1_fx);
    const auto mesh=lib::nereid::buildWaterSurfaceMesh(slope.body,&slope.geometry,slope.mesh);
    assert(!mesh.empty());float lo=1e9f,hi=-1e9f;for(const auto& v:mesh.vertices){lo=std::min(lo,v.y);hi=std::max(hi,v.y);}assert(hi-lo>.5f);

}

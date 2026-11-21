// Optional host-owned presentation bridge. Never install/advance PHYSIS here.
#pragma once
#include "engine/physis/PhysisPresentation.h"
#include "engine/render/Prisma.h"

namespace aiverse::world {
struct PhysisSkyPolicy {
    float dryCoverage=.15f, humidCoverage=.65f, stormCoverage=.9f;
    float stormExtinctionMultiplier=2, stormHazeMultiplier=3;
};
struct PhysisSkyFrame {
    render::PhysicalSkySettings settings;
    render::float2 displacementMeters{};
    uint64_t tick=0;
    bool hasAtmosphere=false;
};
inline render::PhysicalSkySettings physisSkySettings(
        const physis::NatureFieldSample& sample,render::PhysicalSkySettings authored,
        PhysisSkyPolicy p={}) {
    auto s=render::sanitizePhysicalSky(authored);
    if(!sample.hasAtmosphere)return s;
    auto bounded=[](float v,float fallback,float lo,float hi){
        return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
    };
    const float humidity=std::clamp(float(sample.environment.humidity.toDouble()),0.f,1.f);
    // Rain/snow are normalized PHYSIS intensities, not water volume. Max avoids
    // counting a storm's already-applied precipitation twice.
    const float precipitation=std::clamp(float(std::max({sample.environment.rain,
        sample.environment.snow,sample.storms.precipitationBoost}).toDouble()),0.f,1.f);
    const float dry=bounded(p.dryCoverage,.15f,0,1);
    const float humid=bounded(p.humidCoverage,.65f,dry,1);
    const float storm=bounded(p.stormCoverage,.9f,humid,1);
    s.coverage=(dry+(humid-dry)*humidity)*(1-precipitation)+storm*precipitation;
    s.extinction*=1+(bounded(p.stormExtinctionMultiplier,2,1,8)-1)*precipitation;
    s.haze*=1+(bounded(p.stormHazeMultiplier,3,1,8)-1)*precipitation;
    s.windX=float(sample.windVelocity.x.toDouble());
    s.windZ=float(sample.windVelocity.z.toDouble());
    return render::sanitizePhysicalSky(s);
}

// One binding per independent view/source. Source token changes on map/load/source
// replacement, including restores at the SAME tick; a decreasing tick also resets.
// Cosmetic displacement is transient, not authoritative or a replay checkpoint.
class PhysisSkyBinding {
public:
    void reset(){*this=PhysisSkyBinding{};}
    PhysisSkyFrame update(const World& world,Vec3 positionMeters,uint64_t sourceToken,
                          double secondsPerTick,render::PhysicalSkySettings authored,
                          PhysisSkyPolicy policy={}) {
        physis::NatureFieldSample sample;
        PhysisSkyFrame frame;frame.settings=render::sanitizePhysicalSky(authored);
        if(!sourceToken||!std::isfinite(secondsPerTick)||secondsPerTick<=0||
           secondsPerTick>60||!physis::sampleNatureField(world,positionMeters,sample)||
           !sample.hasAtmosphere){reset();return frame;}
        frame.hasAtmosphere=true;frame.tick=sample.tick;
        frame.settings=physisSkySettings(sample,authored,policy);
        const bool discontinuity=!valid_||source_!=sourceToken||sample.tick<tick_||
            secondsPerTick!=secondsPerTick_||frame.settings.cloudScale!=scale_;
        if(discontinuity){x_=z_=0;}
        else if(sample.tick>tick_) {
            const double elapsed=double(sample.tick-tick_)*secondsPerTick;
            // Missing >60 seconds is a discontinuity, never an unbounded catch-up.
            if(elapsed>60){x_=z_=0;}
            else {x_+=windX_*elapsed;z_+=windZ_*elapsed;}
        }
        // Bound GPU coordinates after very long sessions, with a documented
        // cosmetic phase wrap. Integrate previous committed wind (zero-order hold).
        const double limit=double(frame.settings.cloudScale)*65536;
        x_=std::fmod(x_,limit);z_=std::fmod(z_,limit);
        frame.displacementMeters={float(x_),float(z_)};
        valid_=true;source_=sourceToken;tick_=sample.tick;secondsPerTick_=secondsPerTick;
        windX_=frame.settings.windX;windZ_=frame.settings.windZ;scale_=frame.settings.cloudScale;
        return frame;
    }
private:
    bool valid_=false;
    uint64_t source_=0,tick_=0;
    double x_=0,z_=0,secondsPerTick_=0;
    float windX_=0,windZ_=0,scale_=0;
};
inline void applyPhysisSkyFrame(render::Prisma& post,const PhysisSkyFrame& frame) {
    post.setPhysicalSky(frame.settings);
    if(frame.hasAtmosphere)post.setPhysicalSkyAdvection(frame.displacementMeters);
    else post.clearPhysicalSkyAdvection();
}
}

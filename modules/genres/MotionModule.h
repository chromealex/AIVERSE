#pragma once
#include "modules/fps/FpsModule.h"

namespace aiverse::genres {
enum class MotionKind { Platformer, Racing };
constexpr uint16_t kMotionInput=1;
struct MotionActor {
    fps::Actor body;
    Fixed speed{};
    int32_t axis=0,steer=0,checkpoint=0,lap=0,deaths=0;
    uint32_t buttons=0,lastInput=0;
    bool jumpHeld=false;
    Vec3 respawn{};
};
struct MotionState {std::vector<MotionActor> actors;int32_t winner=0;};
class MotionModule final : public GameModule {
public:
    std::string name() const override {return "motion";}
    void setup(Simulation&,const Json&,const Json&) override;
    void applyCommand(Simulation&,const Command&) override;
    void hashState(Simulation&,Hasher&) override;
    bool serializeState(Simulation&,Archive&) override;
    int result(Simulation& sim) override {return state(sim).winner;}
    MotionState& state(Simulation& sim) const {return *sim.world().resource<MotionState>();}
    MotionKind kind() const {return kind_;}
    const world::SpatialWorld& collision() const {return collision_;}
    const std::vector<world::Bounds>& checkpoints() const {return checkpoints_;}
private:
    void tick(Simulation&);
    void race(MotionActor&,Fixed dt);
    MotionKind kind_=MotionKind::Platformer;
    fps::MotorConfig motor_;
    world::SpatialWorld collision_;
    std::vector<world::Bounds> checkpoints_,hazards_;
    std::vector<bool> bots_;
    Fixed acceleration_=10_fx,braking_=20_fx,drag_=2_fx,maxSpeed_=20_fx,turnSpeed_=Fixed::fromRatio(1,4),limit_=1000_fx;
    int laps_=3,maxDeaths_=10,timeout_=12000;
    uint64_t contentHash_=0;
};
void io(Archive&,MotionActor&);
void io(Archive&,MotionState&);
}

#pragma once
#include "AnimationGraph.h"
namespace aiverse::render {
// Ordered forward-solve rig program. Bone names and parameter types are resolved at compile time.
class FullBodyIK;
struct AnimationRigState { std::vector<AnimationContactState> contacts; };
class AnimationRig {
public:
    enum class Kind { Transform, Copy, Aim, TwoBoneIK, Limit, FullBodyIK };
    struct Unit {
        Kind kind = Kind::Transform;
        std::shared_ptr<const FullBodyIK> fullBody;
        int bone=-1, source=-1, knee=-1, end=-1;
        bool global=false, additive=false;
        float weight=1;
        std::string weightParameter, translation, rotation, scale, target, pole;
        float3 t{}, r{}, s{1,1,1}, axis{0,0,1}, minimum{-1000000,-1000000,-1000000}, maximum{1000000,1000000,1000000};
    };
    static std::shared_ptr<const AnimationRig> compile(const Model&,const Json&,const AnimationParameters&);
    void execute(const Model&,const AnimationParameters&,Pose&,std::vector<mat4>& scratch, AnimationRigState* state=nullptr) const;
    const std::vector<Unit>& units() const {return units_;}
private:
    std::vector<Unit> units_;
};
}

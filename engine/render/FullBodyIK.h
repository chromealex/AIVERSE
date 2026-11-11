#pragma once
#include "AnimationGraph.h"
namespace aiverse::render {
// Multi-effector damped least-squares coordinate solver. Presentation only.
class FullBodyIK {
public:
    struct Effector { int bone=-1; std::string target, rotation, weightParameter; float weight=1, rotationWeight=0; };
    struct Joint { int bone=-1; float stiffness=0; float3 minimum{-3.14159f,-3.14159f,-3.14159f},maximum{3.14159f,3.14159f,3.14159f},preferred{}; bool limited=false; };
    struct Result { int iterations=0; float error=0; bool converged=false; AnimationContactReport contacts; };
    static std::shared_ptr<const FullBodyIK> compile(const Model&,const Json&,const AnimationParameters&);
    Result solve(const Model&,const AnimationParameters&,Pose&,float weight=1, AnimationContactState* state=nullptr) const;
    const std::vector<Effector>& effectors() const {return effectors_;}
    const AnimationContacts* contacts() const { return contacts_.get(); }
private:
    std::shared_ptr<const AnimationContacts> contacts_;
    int root_=0,iterations_=24;
    float tolerance_=.001f,damping_=.05f,rootPull_=0;
    std::vector<Joint> joints_;
    std::vector<Effector> effectors_;
    std::vector<std::vector<size_t>> affected_;
};
}

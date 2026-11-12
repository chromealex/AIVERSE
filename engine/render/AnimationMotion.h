#pragma once
#include "AnimationGraph.h"
namespace aiverse::render {
class MotionDatabase {
public:
    struct Frame {int clip=0;float time=0;std::vector<float> features;};
    struct Trajectory {float time=.3f;std::string parameter;};
    static std::shared_ptr<const MotionDatabase> compile(const Model&,const Json&,const AnimationParameters&);
    void features(int clip,float time,std::vector<float>& out) const;
    size_t search(std::span<const float> query,int continuationClip,float continuationTime,float& cost) const;
    const Model* model=nullptr;
    int root=0;
    float fps=30,interval=.1f,blend=.15f,continuationBias=.05f;
    size_t maxCandidates=8192;
    bool stripRoot=true;
    std::vector<int> bones;
    std::vector<Trajectory> trajectory;
    std::vector<float> weights;
    std::vector<Frame> frames;
};
class MotionPlayer {
public:
    void reset(std::shared_ptr<const MotionDatabase>);
    void update(float dt,const AnimationParameters&);
    void evaluate(Pose&) const;
    int clip() const {return clip_;} float time()const{return time_;} float cost()const{return cost_;}
private:
    std::shared_ptr<const MotionDatabase> db_;
    int clip_=-1;
    float time_=0,untilSearch_=0,fade_=0,cost_=0;
    Pose source_;
    std::vector<float> query_;
};
}

#include "AnimationMotion.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace aiverse::render {
namespace {
[[noreturn]] void fail(const std::string& s){throw std::runtime_error("motion matching: "+s);}
float num(const Json& j,float d){if(j.isNull())return d;double n=j.toDouble();if(!j.isNumber()||!std::isfinite(n)||std::abs(n)>1000000)fail("invalid number");return float(n);}
float3 pos(const mat4& m){return {m.at(0,3),m.at(1,3),m.at(2,3)};}
void append(std::vector<float>& v,float3 p){v.push_back(p.x);v.push_back(p.y);v.push_back(p.z);}
void sampleWorld(const Model& m,int clip,float t,Pose& p,std::vector<mat4>& w){bindPose(m,p);sampleClip(m.clips[size_t(clip)],std::clamp(t,0.f,m.clips[size_t(clip)].duration),p);worldMatrices(m,p,w);}
}
std::shared_ptr<const MotionDatabase> MotionDatabase::compile(const Model& m,const Json& j,const AnimationParameters& p){
  if (!j.isObject())
    fail("database must be object");
  auto d = std::make_shared<MotionDatabase>();
  d->model = &m;
  d->root = m.findNode(j.getStr("root"));
  if (d->root < 0)
    fail("unknown root bone");
  d->fps = num(j["fps"], 30);
  d->interval = num(j["searchInterval"], .1f);
  d->blend = num(j["blend"], .15f);
  d->continuationBias = num(j["continuationBias"], .05f);
  d->stripRoot = j.getBool("stripRoot", true);
  if (d->fps < 1 || d->fps > 120 || d->interval < .016f || d->interval > 10 || d->blend < 0 || d->blend > 10 ||
      d->continuationBias < 0)
    fail("invalid sampling/search settings");
  auto budget = j.getInt("maxCandidates", 8192);
  if (budget < 1 || budget > 8192)
    fail("maxCandidates must be 1..8192");
  d->maxCandidates = size_t(budget);
  if ((!j["bones"].isArray() && !(j["bones"].isObject() && j["bones"].size() == 0)) || j["bones"].size() > 8)
    fail("bones must have at most 8 entries");
  float pw = num(j["poseWeight"], 1), vw = num(j["velocityWeight"], .1f), tw = num(j["trajectoryWeight"], 2);
  if (pw < 0 || vw < 0 || tw <= 0)
    fail("invalid feature weights");
  for (const auto &b : j["bones"].items()) {
    int id = m.findNode(b.str());
    if (id < 0)
      fail("unknown feature bone");
    d->bones.push_back(id);
    for (int k = 0; k < 3; ++k)
      d->weights.push_back(pw);
    for (int k = 0; k < 3; ++k)
      d->weights.push_back(vw);}
    if(!j["trajectory"].isArray()||j["trajectory"].size()==0||j["trajectory"].size()>4)fail("trajectory must have 1..4 samples");
    for(const auto& t:j["trajectory"].items()){Trajectory sample{num(t["time"],.3f),t.getStr("parameter")};if(sample.time<=0||sample.time>2||!p.vectors.count(sample.parameter))fail("invalid trajectory control/time");d->trajectory.push_back(sample);for(int k=0;k<3;++k)d->weights.push_back(tw);}
    if(!j["clips"].isArray()||j["clips"].size()==0||j["clips"].size()>64)fail("clips must contain 1..64 names");
    for(const auto& c:j["clips"].items()){
        int clip=m.findClip(c.str());if(clip<0)fail("unknown clip '"+c.str()+"'");float duration=m.clips[size_t(clip)].duration;
        if(!std::isfinite(duration)||duration<=0)fail("clip duration must be positive");
        double count=std::ceil(double(duration)*d->fps);if(count>8192||d->frames.size()+size_t(count)>8192)fail("database exceeds 8192 frames; reduce fps/clips");
        for(size_t i=0;i<size_t(count);++i){Frame f{clip,float(i)/d->fps,{}};d->features(clip,f.time,f.features);d->frames.push_back(std::move(f));}
    }
    return d;
}
void MotionDatabase::features(int clip,float t,std::vector<float>& out)const{
    Pose p,q;std::vector<mat4>w,next;float step=1/fps;
    float duration=model->clips[size_t(clip)].duration;
    float t0=std::clamp(t,0.f,std::max(0.f,duration-step)),t1=std::min(duration,t0+step);
    sampleWorld(*model,clip,t,p,w);sampleWorld(*model,clip,t1,q,next);
    mat4 origin=w[size_t(root)].inverse();
    out.clear();out.reserve(weights.size());
    // Interleave position and velocity in the same root-local feature schema.
    Pose before;std::vector<mat4>prev;sampleWorld(*model,clip,t0,before,prev);
    for(int bone:bones){append(out,origin.transformPoint(pos(w[size_t(bone)])));append(out,origin.transformDir((pos(next[size_t(bone)])-pos(prev[size_t(bone)]))*(1/std::max(1e-6f,t1-t0))));}
    for(const auto& tr:trajectory){
        float future=t+tr.time;
        // Extrapolate root displacement across loop boundaries using the clip's net displacement.
        float loops=std::floor(future/duration);float wrapped=std::fmod(future,duration);
        sampleWorld(*model,clip,wrapped,q,next);float3 target=pos(next[size_t(root)]);
        if(loops>0){sampleWorld(*model,clip,0,q,next);float3 start=pos(next[size_t(root)]);sampleWorld(*model,clip,duration,q,next);target+=(pos(next[size_t(root)])-start)*loops;}
        append(out,origin.transformPoint(target));
    }
}
size_t MotionDatabase::search(std::span<const float> query,int continuing,float time,float& cost)const{
    cost=std::numeric_limits<float>::infinity();size_t best=0;if(query.size()!=weights.size()||frames.empty())return best;
    for(float v:query)if(!std::isfinite(v))return best;
    size_t budget=std::min(maxCandidates,frames.size());
    for(size_t k=0;k<budget;++k){size_t i=k*frames.size()/budget;const auto& f=frames[i];double c=0;
        for(size_t j=0;j<query.size();++j){double delta=double(query[j])-f.features[j];c+=delta*delta*weights[j];}
        if(f.clip!=continuing||std::abs(f.time-time)>2/fps)c+=continuationBias;
        if(c<cost){cost=float(c);best=i;}
    }return best;
}
void MotionPlayer::reset(std::shared_ptr<const MotionDatabase> db){db_=std::move(db);clip_=-1;time_=untilSearch_=fade_=cost_=0;source_.clear();query_.clear();}
void MotionPlayer::update(float dt,const AnimationParameters& parameters){
  if (!db_ || !std::isfinite(dt) || dt < 0)
    return;
  dt = std::min(dt, 1.f);
  fade_ += dt;
  untilSearch_ -= dt;
  if (clip_ >= 0)
    time_ = std::fmod(time_ + dt, db_->model->clips[size_t(clip_)].duration);
  if (clip_ < 0 || untilSearch_ <= 0) {
    int featureClip = clip_ < 0 ? db_->frames[0].clip : clip_;
    db_->features(featureClip, time_, query_);
    size_t at = db_->bones.size() * 6;
    for (const auto &tr : db_->trajectory) {
      auto it = parameters.vectors.find(tr.parameter);
      float3 v = it == parameters.vectors.end() ? float3{} : it->second;
      query_[at++] = v.x;
      query_[at++] = v.y;
      query_[at++] = v.z;
    }
    size_t selected = db_->search(query_, clip_, time_, cost_);
    const auto &f = db_->frames[selected];
    if (std::isfinite(cost_) && (clip_ < 0 || f.clip != clip_ || std::abs(f.time - time_) > 2 / db_->fps)) {
      Pose frozen;
      evaluate(frozen);
      source_ = std::move(frozen);
      clip_ = f.clip;
      time_ = f.time;
      fade_ = 0;
    }
    untilSearch_ = db_->interval;
    }
}
void MotionPlayer::evaluate(Pose& out)const{
    if(!db_){out.clear();return;}bindPose(*db_->model,out);
    if(clip_>=0)sampleClip(db_->model->clips[size_t(clip_)],time_,out);
    if(db_->stripRoot){const auto& n=db_->model->nodes[size_t(db_->root)];out[size_t(db_->root)].t=n.t;out[size_t(db_->root)].r=n.r;}
    if(source_.size()==out.size()&&db_->blend>0&&fade_<db_->blend){float w=std::clamp(fade_/db_->blend,0.f,1.f);for(size_t i=0;i<out.size();++i){out[i].t=lerp(source_[i].t,out[i].t,w);out[i].s=lerp(source_[i].s,out[i].s,w);out[i].r=nlerp(source_[i].r,out[i].r,w);for(int k=0;k<kMaxMorphTargets;++k)out[i].w[k]=source_[i].w[k]+(out[i].w[k]-source_[i].w[k])*w;}}
}
}

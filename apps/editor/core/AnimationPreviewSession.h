#pragma once
#include "AnimationDocument.h"
#include "AnimationOps.h"
#include "engine/render/AnimationGraph.h"
#include <array>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace aiverse::editor {

class AnimationPreviewSessions {
    struct Preview {
        std::string project;
        int64_t token = 0;
        std::unique_ptr<render::Model> model;
        render::AnimationGraphInstance player;
        render::Pose pose;
        std::vector<render::mat4> world;
    };
    std::array<std::unique_ptr<Preview>,2> panels_;
    int64_t serial_ = 0;
    std::mutex mutex_;

    static Json vector(render::float3 v) {
        Json a=Json::array();
        for(float x:{v.x,v.y,v.z})a.push(Json::number(double(x),6));
        return a;
    }
    static double numeric(const Json& v) {
        const double n=v.toDouble();
        if(!v.isNumber()||!std::isfinite(n)||std::abs(n)>1e9)throw std::runtime_error("Invalid animation control value");
        return n;
    }
    static Json evaluate(Preview& preview,float dt) {
        if(dt>0)preview.player.update(dt);
        preview.player.evaluate(preview.pose);
        if(!preview.player.healthy())throw std::runtime_error(preview.player.error());
        render::worldMatrices(*preview.model,preview.pose,preview.world);
        Json out=Json::object(),points=Json::array();
        for(const auto& matrix:preview.world)points.push(vector(matrix.transformPoint({})));
        out.set("points",std::move(points));
        out.set("state",Json::string(preview.player.state()));
        out.set("time",Json::number(double(preview.player.stateTime()),6));
        out.set("token",Json::number(preview.token));
        return out;
    }
public:
    Json command(const Workspace& ws,const Json& params) {
        const auto panel=params.getStr("panel"),action=params.getStr("action");
        if(panel!="graph"&&panel!="rig")throw std::runtime_error("Unknown animation preview panel");
        std::lock_guard lock(mutex_);
        auto& slot=panels_[panel=="rig"?1:0];
        if(action=="open"){
            const auto& doc=params["graph"];
            if(!doc.isObject()||doc.dump().size()>256*1024)throw std::runtime_error("Invalid animation preview document");
            auto next=std::make_unique<Preview>();
            next->project=ws.project.dir;
            next->model=loadAnimationModel(ws,doc.getStr("model"));
            const auto scope=params.getStr("scope");
            const bool bind=params.getBool("bindPose");
            Json graph=bind?animation::makeGraph():scope.empty()?doc:doc["graphs"][scope];
            animation::sort(graph);
            render::AnimationGraphs children;
            if(!bind)for(const auto& [name,child]:doc["graphs"].members())children.emplace(name,child);
            next->player.reset(render::AnimationGraph::compile(*next->model,graph,{}, {},children));
            next->token=++serial_;
            Json out=evaluate(*next,0),bones=Json::array(),inputs=Json::object();
            for(const auto& node:next->model->nodes){
                Json bone=Json::object();
                bone.set("name",Json::string(node.name));
                bone.set("parent",Json::number(int64_t(node.parent)));
                bones.push(std::move(bone));
            }
            for(const auto& [name,value]:next->player.parameters().values)inputs.set(name,Json::number(double(value)));
            for(const auto& [name,value]:next->player.parameters().vectors)inputs.set(name,vector(value));
            out.set("bones",std::move(bones));
            out.set("inputs",std::move(inputs));
            slot=std::move(next);
            return out;
        }
        if(!slot||slot->project!=ws.project.dir||slot->token!=params.getInt("token",-1))throw std::runtime_error("Animation preview has expired");
        if(action=="close"){slot.reset();return Json::object();}
        if(action!="frame")throw std::runtime_error("Unknown animation preview command");
        const auto dt=params.has("dt")?numeric(params["dt"]):0.;
        if(dt<0||dt>.25)throw std::runtime_error("Preview step must be within 0..0.25 seconds");
        auto controls=slot->player.parameters();
        for(const auto& [name,value]:params["inputs"].members()){
            if(value.isArray()&&value.size()==3&&controls.vectors.count(name)){
                controls.vectors[name]={float(numeric(value[0])),float(numeric(value[1])),float(numeric(value[2]))};
            }else if(value.isNumber()&&controls.values.count(name))controls.values[name]=float(numeric(value));
            else throw std::runtime_error("Unknown animation control or incompatible value");
        }
        slot->player.parameters()=std::move(controls);
        return evaluate(*slot,float(dt));
    }
};
}

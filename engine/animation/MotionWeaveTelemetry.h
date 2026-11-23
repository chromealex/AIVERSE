// Read-only diagnostics from the committed common presentation ECS.
#pragma once
#include "MotionWeaveSourceBridge.h"
#include "engine/render/graph/GraphTelemetry.h"
#include <limits>
#include <optional>

namespace aiverse::motionweave_telemetry {
struct Sample {
    graph_telemetry::Identity identity;
    uint64_t sequence=0;
    Json value;
};
// Call only after owner publication and worker join. The caller supplies
// a non-reused world epoch and its committed frame sequence. No model
// pointer leaves the call, no graph is stepped, no ECS component is added.
inline std::optional<Sample> read(const World& presentation,
        const lib::animation::MotionWeaveSourceBridge& bridge,
        uint64_t sourceId,uint64_t worldEpoch,uint64_t sequence){
    using namespace lib::animation;
    if(!sourceId||!worldEpoch||!sequence)return std::nullopt;
    const Entity entity=bridge.find(sourceId);
    const auto* link=presentation.tryGetConst<AnimationSourceLinkComponent>(entity);
    const auto* asset=presentation.tryGetConst<AnimationAssetComponent>(entity);
    const auto* player=presentation.tryGetConst<AnimationPlayerComponent>(entity);
    const auto* pose=presentation.tryGetConst<AnimationPoseComponent>(entity);
    const auto* diagnostic=presentation.tryGetConst<AnimationDiagnosticsComponent>(entity);
    if(!link||link->sourceId!=sourceId||!asset||!player||!pose||!diagnostic||
        !player->instance||player->restart||!player->active||
        asset->graph!=player->active||player->instance->graph()!=player->active.get()||
        !player->graphPublicationSerial||
        player->graphPublicationSerial==std::numeric_limits<uint64_t>::max())return std::nullopt;
    Sample out;
    out.identity={std::to_string(worldEpoch),
        std::to_string(sourceId)+":"+std::to_string(link->generation)+":"+std::to_string(entity.packed()),
        std::to_string(link->modelPublicationGeneration)+":"+std::to_string(player->graphPublicationSerial),"motionweave"};
    out.sequence=sequence;
    auto& r=out.value;r=Json::object();
    r.set("available",Json::fromBool(true));r.set("domain",Json::string("motionweave"));
    r.set("scope",Json::string("shared-presentation-ecs"));
    r.set("worldEpoch",Json::string(out.identity.world));
    r.set("sourceId",Json::string(std::to_string(sourceId)));
    r.set("sourceGeneration",Json::string(std::to_string(link->generation)));
    r.set("presentationEntity",Json::string(std::to_string(entity.packed())));
    r.set("modelPublication",Json::string(std::to_string(link->modelPublicationGeneration)));
    r.set("graphPublication",Json::string(std::to_string(player->graphPublicationSerial)));
    r.set("programIdentity",Json::string(out.identity.program));
    r.set("sequence",Json::string(std::to_string(sequence)));
    r.set("state",Json::string(player->instance->state()));
    r.set("stateTime",Json::number(double(player->instance->stateTime()),6));
    r.set("healthy",Json::fromBool(diagnostic->healthy&&player->instance->healthy()));
    r.set("errorMessage",Json::string(diagnostic->error));
    r.set("poseValid",Json::fromBool(pose->valid));
    r.set("updates",Json::string(std::to_string(diagnostic->updates)));
    r.set("evaluations",Json::string(std::to_string(diagnostic->evaluations)));
    r.set("playbackRestoreAccepted",Json::string(std::to_string(player->playbackRestoreAccepted)));
    r.set("playbackRestoreDeclined",Json::string(std::to_string(player->playbackRestoreDeclined)));
    Json program=Json::array();
    for(const auto& state:player->active->states())if(state.id==player->instance->state()){
        for(int idx:state.program)program.push(Json::string(player->active->nodes()[size_t(idx)].id));
        break;
    }
    r.set("activeProgram",program);
    const auto& notifies=player->instance->notifies();Json events=Json::array();
    for(size_t i=0;i<notifies.size()&&i<16;++i){
        Json n=Json::object();n.set("node",Json::string(notifies[i].node));n.set("name",Json::string(notifies[i].name));events.push(n);
    }
    r.set("notifyCount",Json::number(int64_t(notifies.size())));r.set("notifies",events);
    r.set("gpuTimingAvailable",Json::fromBool(false));
    r.set("note",Json::string("Published presentation state. Evaluation can be budget-throttled. Program IDs are dependencies, not measured node execution."));
    return out;
}
} // namespace aiverse::motionweave_telemetry

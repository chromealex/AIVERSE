#pragma once
#include "apps/editor/PhysisLiveEdit.h"

namespace aiverse::studio {

class StudioPhysisFieldEdit {
    editor::physis_live::FieldEditSession edit_;
    uint64_t epoch_=0;
public:
    Json inspectOrApply(Simulation* sim,uint64_t epoch,uint64_t mapRevision,const Json& request,const Json* authored) {
        using namespace editor::physis_live;
        if (epoch!=epoch_) { edit_={};epoch_=epoch; }
        edit_.observe(sim);
        std::string reason;
        const bool allowed=canSubmit(sim,&reason);
        const auto* state=sim?sim->world().resourceConst<physis::FieldRuntimeState>():nullptr;
        const auto revision=state?std::to_string(state->revision):std::string();
        if (authored) {
            if (!allowed) throw std::runtime_error(reason);
            if (request.getStr("runtimeEpoch")!=std::to_string(epoch) ||
                request.getStr("fieldRevision")!=revision || request.getStr("mapRevision")!=std::to_string(mapRevision))
                throw std::runtime_error("The play-test or map fields changed. Review the current revisions.");
            if (!edit_.submit(*sim,*authored,&reason)) throw std::runtime_error(reason);
        }
        Json out=Json::object();
        out.set("available",Json::fromBool(allowed && edit_.status!=EditStatus::Queued));
        out.set("runtimeEpoch",Json::string(std::to_string(epoch)));
        out.set("mapRevision",Json::string(std::to_string(mapRevision)));
        out.set("fieldRevision",Json::string(revision));
        out.set("status",Json::string(statusName(edit_.status)));
        out.set("message",Json::string(allowed?edit_.message:reason));
        if (sim) out.set("tick",Json::number(int64_t(sim->tick())));
        if (edit_.status==EditStatus::Queued) out.set("scheduledTick",Json::number(int64_t(edit_.scheduledTick)));
        return out;
    }
};

} // namespace aiverse::studio

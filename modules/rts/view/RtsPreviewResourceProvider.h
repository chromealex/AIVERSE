// RTS presentation resource provider for simple model previews.
// Reads an already-resolved, host-owned type appearance and returns a borrowed
// View-ready description. Resource upload, lifetime and model residency remain
// the responsibility of the calling game/editor host.
//
// The API is templated so private RTS look/choice structures need not be
// exported to the generic engine or to a separately compiled renderer.
#pragma once
#include "RtsStaticPreviewModel.h"

namespace aiverse::game::view_adapter {

template<class Look,class Type>
bool resolveStaticPreviewModel(const Look& look,const Type& type,
                               render::float4 playerColor,
                               StaticPreviewModel& output) {
    // A preview may bypass legacy drawObject only when the model has no
    // gameplay/animation authoring side effects, stage selection, or LOD.
    if(look.models.size()!=1 || !look.stages.empty() || !look.lods.empty())
        return false;
    const auto& choice=look.models.front();
    if(!choice.model || !choice.model->clips.empty() ||
       choice.graph || choice.needsPose || choice.baked ||
       !choice.procs.empty() || !choice.legs.empty() ||
       !choice.attach.empty() || !choice.fx.empty() ||
       !choice.additive.empty() || !choice.weaponPoses.empty())
        return false;

    StaticPreviewModel result;
    result.model=choice.model;
    result.geometry=choice.geometry;
    result.materials=&choice.materials;
    result.nodeMaterial=choice.nodeMaterial.empty()
        ?nullptr:&choice.nodeMaterial;
    result.fix=choice.fix;
    result.variant=choice.variant;
    result.tint=look.tint;
    result.team={playerColor.x*playerColor.x,
                 playerColor.y*playerColor.y,
                 playerColor.z*playerColor.z};
    result.structure=type.structure;
    result.shape=look.shape;
    result.air=look.air;
    result.transparent=look.transparent;
    result.staticObject=type.structure || type.speed<=decltype(type.speed){};
    result.scale=look.scale;
    result.sx=look.sx;
    result.sz=look.sz;
    result.flyHeight=look.flyHeight;
    output=result;
    return true;
}

} // namespace aiverse::game::view_adapter

#include "HumanRig.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace aiverse::vivid {
namespace {
bool descendant(const render::Model& model,int child,int root) {
    for(std::size_t i=0;i<model.nodes.size()&&child>=0;++i) {
        if(child==root)return true;
        if(child>=int(model.nodes.size()))return false;
        child=model.nodes[std::size_t(child)].parent;
    }
    return false;
}
}
HumanRigBinding HumanRigBinding::compile(const render::Model& model,const HumanRigDNA& dna) {
    auto find=[&](const std::string& name){return model.findNode(name);};
    HumanRigBinding b;
    b.root=find(dna.rootBone);b.head=find(dna.headBone);
    b.jaw=find(dna.jawBone);
    b.leftEye=find(dna.leftEyeBone);
    b.rightEye=find(dna.rightEyeBone);
    if(!b.valid()||b.root==b.head||!descendant(model,b.head,b.root))
        throw std::invalid_argument("VIVID HumanRig: root/head missing or outside hierarchy");
    for(int child:{b.jaw,b.leftEye,b.rightEye})
        if(child>=0&&(!descendant(model,child,b.head)||child==b.head))
            throw std::invalid_argument("VIVID HumanRig: facial joint outside head hierarchy");
    return b;
}
bool HumanRigBinding::compose(std::span<const render::mat4> input,
                              std::span<render::mat4> out,
                              const FaceFrame& face,float strength) const noexcept {
    if(!valid()||input.empty()||out.size()!=input.size()||
       root>=int(input.size())||head>=int(input.size())||
       !std::isfinite(strength)||strength<0||strength>1)
        return false;
    for(float w:face.values)if(!std::isfinite(w)||w<0||w>1)return false;
    // The animation backend can reject a stale/invalid source publication
    // without modifying the previous pose. Never propagate nonfinite node
    // matrices into the VIVID skinning and groom pipeline.
    for(const auto& matrix:input)
        for(float value:matrix.m)
            if(!std::isfinite(value))return false;
    for(int b:{jaw,leftEye,rightEye})if(b>=int(input.size()))return false;
    if(input.data()==out.data())return false; // preserve MotionWeave source
    std::copy(input.begin(),input.end(),out.begin());
    const auto value=[&](Channel c){return face.value(c)*strength;};
    if(jaw>=0) {
        const float open=value(Channel::JawOpen);
        const float lateral=(value(Channel::JawRight)-value(Channel::JawLeft));
        const float forward=value(Channel::JawForward);
        out[std::size_t(jaw)]=input[std::size_t(jaw)] *
            render::mat4::rotateY(lateral*0.14f) *
            render::mat4::rotateX(open*0.48f) *
            render::mat4::translate({0,0,forward*0.025f});
    }
    const float limit=0.58f;
    if(leftEye>=0) {
        const float horizontal=value(Channel::EyeLookInLeft)-
                               value(Channel::EyeLookOutLeft);
        const float vertical=value(Channel::EyeLookUpLeft)-
                             value(Channel::EyeLookDownLeft);
        out[std::size_t(leftEye)]=input[std::size_t(leftEye)] *
            render::mat4::rotateY(std::clamp(horizontal,-1.f,1.f)*limit) *
            render::mat4::rotateX(std::clamp(-vertical,-1.f,1.f)*limit);
    }
    if(rightEye>=0) {
        const float horizontal=value(Channel::EyeLookOutRight)-
                               value(Channel::EyeLookInRight);
        const float vertical=value(Channel::EyeLookUpRight)-
                             value(Channel::EyeLookDownRight);
        out[std::size_t(rightEye)]=input[std::size_t(rightEye)] *
            render::mat4::rotateY(std::clamp(horizontal,-1.f,1.f)*limit) *
            render::mat4::rotateX(std::clamp(-vertical,-1.f,1.f)*limit);
    }
    return true;
}
} // namespace aiverse::vivid

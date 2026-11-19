// AIVERSE LENS: camera-only capture views. Supports six 2D cube faces,
// stereo pairs, planar reflections and linked portals without requiring
// a genre module or mutating ECS. All targets are ordinary RHI textures.
// Cubemap packing, CPU readback and file export are separate layers.
#pragma once
#include "CameraRig.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aiverse::render {
struct CameraCaptureView {
    Camera camera{};
    uint64_t targetAssetKey=0;
    int width=0,height=0;
    bool hdr=false;
};
constexpr float cameraPi=3.14159265358979323846f;
inline quat cameraPoseRotation(const Camera& camera) {
    return camera.usePose?camera.rotation:cameraLookRotation(camera.eye(),camera.target);
}
inline std::array<CameraCaptureView,6> makeCubemapCameraViews(
        const Camera& source,const std::array<uint64_t,6>& targetKeys,
        int faceSize,bool hdr=false) {
    std::array<CameraCaptureView,6> result{};
    const auto pose=cameraPoseRotation(source);
    const float3 eye=source.eye();
    const std::array<quat,6> directions={{
        quat::axisAngle({0,1,0},-cameraPi*.5f), // +X
        quat::axisAngle({0,1,0}, cameraPi*.5f), // -X
        quat::axisAngle({1,0,0}, cameraPi*.5f), // +Y
        quat::axisAngle({1,0,0},-cameraPi*.5f), // -Y
        quat::axisAngle({0,1,0}, cameraPi),     // +Z
        quat{}                                 // -Z
    }};
    for(size_t i=0;i<result.size();++i) {
        Camera face=source;
        face.setPose(eye,qnormalize(pose*directions[i]));
        face.fovY=cameraPi*.5f;
        face.ortho=false;
        face.lensShift={};
        face.jitter={};
        result[i]={face,targetKeys[i],faceSize,faceSize,hdr};
    }
    return result;
}
inline std::array<CameraCaptureView,2> makeStereoCameraViews(
        const Camera& source,float ipd,float convergence,
        const std::array<uint64_t,2>& targetKeys,int width,int height) {
    std::array<CameraCaptureView,2> result{};
    const auto rotation=cameraPoseRotation(source);
    const auto right=rotation.rotate({1,0,0});
    const float distance=std::max(.01f,convergence);
    const float half=std::clamp(ipd,0.f,.2f)*.5f;
    for(size_t i=0;i<2;++i) {
        Camera eye=source;
        const float sign=i==0?-1.f:1.f;
        eye.setPose(source.eye()+right*(sign*half),rotation);
        eye.ortho=false;
        eye.lensShift={-sign*half/distance,0};
        eye.jitter={};
        result[i]={eye,targetKeys[i],width,height,false};
    }
    return result;
}
inline quat cameraOrientationFromBasis(float3 forward,float3 up) {
    const auto f=normalize(forward);
    const auto right=normalize(cross(f,up));
    const auto u=normalize(cross(right,f));
    const float m00=right.x,m01=u.x,m02=-f.x;
    const float m10=right.y,m11=u.y,m12=-f.y;
    const float m20=right.z,m21=u.z,m22=-f.z;
    quat q{};
    const float trace=m00+m11+m22;
    if(trace>0) {
        const float s=std::sqrt(trace+1.f)*2.f;
        q={ (m21-m12)/s,(m02-m20)/s,(m10-m01)/s,s*.25f };
    } else if(m00>m11&&m00>m22) {
        const float s=std::sqrt(1.f+m00-m11-m22)*2.f;
        q={s*.25f,(m01+m10)/s,(m02+m20)/s,(m21-m12)/s};
    } else if(m11>m22) {
        const float s=std::sqrt(1.f+m11-m00-m22)*2.f;
        q={(m01+m10)/s,s*.25f,(m12+m21)/s,(m02-m20)/s};
    } else {
        const float s=std::sqrt(1.f+m22-m00-m11)*2.f;
        q={(m02+m20)/s,(m12+m21)/s,s*.25f,(m10-m01)/s};
    }
    return qnormalize(q);
}
// A portal maps its local -Z forward plane into the exit's forward plane.
// Texture clip-plane handling remains the caller's responsibility.
inline Camera makePortalCamera(const Camera& source,const mat4& entrance,
                               const mat4& exit) {
    const mat4 bridge=exit*mat4::rotateY(cameraPi)*entrance.inverse();
    const auto rotation=cameraPoseRotation(source);
    const auto eye=bridge.transformPoint(source.eye());
    const auto forward=normalize(bridge.transformDir(rotation.rotate({0,0,-1})));
    const auto up=normalize(bridge.transformDir(rotation.rotate({0,1,0})));
    Camera result=source;
    result.setPose(eye,cameraOrientationFromBasis(forward,up));
    result.jitter={};
    return result;
}
// Plane equation dot(n,p)+offset=0; the reflected camera is appropriate for
// a planar mirror, with any oblique near clipping applied separately by host.
inline Camera makeMirrorCamera(const Camera& source,float3 normal,float offset) {
    const auto n=normalize(normal);
    const auto pose=cameraPoseRotation(source);
    const auto reflectDirection=[&](float3 v) {return v-n*(2.f*dot(v,n));};
    const auto position=source.eye();
    const auto eye=position-n*(2.f*(dot(position,n)+offset));
    Camera result=source;
    result.setPose(eye,cameraOrientationFromBasis(
        reflectDirection(pose.rotate({0,0,-1})),
        reflectDirection(pose.rotate({0,1,0}))));
    result.jitter={};
    return result;
}
} // namespace aiverse::render

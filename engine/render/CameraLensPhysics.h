// AIVERSE LENS: thin-lens exposure and depth-of-field maths for physical cameras.
// Pure CPU functions, no GPU, no ECS writes, no genre types. World units are
// treated as metres (focusDistance); lens lengths are millimetres.
#pragma once
#include "../sim/CameraRigComponents.h"
#include <algorithm>
#include <cmath>

namespace aiverse::render {

struct CameraLensValues {
    double focalMm=50, sensorHeightMm=24, fNumber=2.8;
    double focusMetres=10, shutterSeconds=1.0/60, iso=100;
    double cocMm=0; // 0 = automatic
};
inline CameraLensValues cameraLensValues(const CameraLensComponent& l) {
    return {l.focalLengthMm.toDouble(),l.sensorHeightMm.toDouble(),
            l.fNumber.toDouble(),l.focusDistance.toDouble(),
            l.shutterSeconds.toDouble(),l.iso.toDouble(),
            l.circleOfConfusionMm.toDouble()};
}
inline bool cameraLensValuesValid(const CameraLensValues& v) {
    const auto pos=[](double x){return std::isfinite(x)&&x>0;};
    return pos(v.focalMm)&&pos(v.sensorHeightMm)&&pos(v.fNumber)&&
           pos(v.focusMetres)&&pos(v.shutterSeconds)&&pos(v.iso)&&
           std::isfinite(v.cocMm)&&v.cocMm>=0;
}
// Photographic exposure value at ISO 100: EV100 = log2(N^2 / t * 100 / S).
inline double cameraLensEV100(const CameraLensValues& v) {
    return std::log2(v.fNumber*v.fNumber/v.shutterSeconds*100.0/v.iso);
}
// EV100 of the default CameraLensComponent: the zero-stop reference.
inline double cameraLensReferenceEV100() {
    return cameraLensEV100(cameraLensValues(CameraLensComponent{}));
}
// Exposure multiplier in stops (scene colour *= 2^stops). A wider aperture,
// longer shutter or higher ISO brightens; AIVERSE scenes are not photometric,
// so the result is relative to the default lens, not absolute luminance.
inline double cameraLensExposureStops(const CameraLensValues& v,double compensation=0) {
    if(!cameraLensValuesValid(v)||!std::isfinite(compensation))return 0;
    return cameraLensReferenceEV100()-cameraLensEV100(v)+compensation;
}
inline double cameraLensCocMm(const CameraLensValues& v) {
    return v.cocMm>0?v.cocMm:v.sensorHeightMm/800.0; // 24 mm -> 0.03 mm
}

struct CameraDepthOfFieldPlan {
    bool valid=false;
    double nearMetres=0, farMetres=0;   // sharp interval limits
    bool farInfinite=false;             // focus at/after hyperfocal distance
    double hyperfocalMetres=0;
    double blurRadiusAtInfinityPx=0;    // background disc radius, image pixels
};
// Thin-lens depth of field for an image `imageHeightPx` tall (the render target).
inline CameraDepthOfFieldPlan cameraLensDepthOfField(const CameraLensValues& v,
                                                     int imageHeightPx) {
    CameraDepthOfFieldPlan out;
    if(!cameraLensValuesValid(v)||imageHeightPx<=0)return out;
    const double f=v.focalMm, N=v.fNumber, c=cameraLensCocMm(v);
    const double s=v.focusMetres*1000.0;
    if(!(s>f))return out; // focus inside the focal length: no real image
    const double H=f*f/(N*c)+f;
    out.hyperfocalMetres=H/1000.0;
    out.nearMetres=s*(H-f)/(H+s-2.0*f)/1000.0;
    if(s>=H){out.farInfinite=true;out.farMetres=INFINITY;}
    else out.farMetres=s*(H-f)/(H-s)/1000.0;
    const double discMm=f*f/(N*(s-f));
    out.blurRadiusAtInfinityPx=0.5*discMm/v.sensorHeightMm*double(imageHeightPx);
    out.valid=std::isfinite(out.nearMetres)&&out.nearMetres>0&&
              std::isfinite(out.blurRadiusAtInfinityPx);
    return out;
}

// Parameters for a symmetric "focus plane + sharp interval" defocus pass
// (PRISMA depthOfField metric mode). The sharp interval is [near, far]; an
// infinite far limit is capped by `maxRangeMetres`.
struct CameraDofPassValues {
    bool valid=false;
    float focusDistance=0, focusRange=0, maxBlurPx=0;
};
inline CameraDofPassValues cameraLensDofPass(const CameraDepthOfFieldPlan& p,
                                             float maxRangeMetres=500.f,
                                             float maxBlurLimitPx=16.f) {
    CameraDofPassValues out;
    if(!p.valid)return out;
    const double nearM=p.nearMetres;
    const double farM=p.farInfinite?nearM+maxRangeMetres:
                      std::min(p.farMetres,nearM+double(maxRangeMetres));
    out.focusDistance=float(0.5*(nearM+farM));
    out.focusRange=float(std::max(0.01,farM-nearM));
    out.maxBlurPx=float(std::clamp(p.blurRadiusAtInfinityPx,0.0,double(maxBlurLimitPx)));
    out.valid=std::isfinite(out.focusDistance)&&std::isfinite(out.focusRange);
    return out;
}
} // namespace aiverse::render

// Offline bake of float skeletal root motion into deterministic Q16 tick samples.
// Import/authoring ONLY. Simulation consumes the fixed values, never samples floats.
#pragma once
#include "AnimationPlayback.h"
#include "GenericRetarget.h"
#include "EcsRootMotionSystems.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <set>
namespace aiverse::lib::animation {
inline Fixed quantizeRootValue(double number) {
    if (!std::isfinite(number) || std::abs(number)>1000.0)
        throw std::invalid_argument("root bake: nonfinite or excessive movement");
    return Fixed::fromRaw(int64_t(std::llround(number*double(Fixed::kOne))));
}
inline RootMotionTrack bakeRootMotion(const render::Model& model,
        const render::AnimClip& clip,int root,uint32_t id,
        uint32_t tickRate=60,bool loop=false) {
    if(!id || tickRate<10 || tickRate>240 ||
       !std::isfinite(clip.duration) || clip.duration<=0)
        throw std::invalid_argument("root bake: invalid clip/rate");
    const uint64_t count=uint64_t(std::ceil(double(clip.duration)*tickRate));
    if(!count || count>65536)
        throw std::invalid_argument("root bake: clip exceeds 65536 fixed frames");
    RootMotionTrack out;out.id=id;out.rate=tickRate;out.loop=loop;
    out.frames.reserve(size_t(count));
    for(uint64_t i=0;i<count;++i) {
        const double t0=double(i)/double(tickRate);
        const double t1=std::min(double(clip.duration),
                                  double(i+1)/double(tickRate));
        const auto delta=extractClipRootDelta(model,clip,root,t0,t1,false);
        if (!delta)throw std::invalid_argument("root bake: invalid root transform");
        const auto forward=delta->rotation.rotate({0,0,1});
        const double yaw=std::atan2(double(forward.x),double(forward.z))/
            6.28318530717958647692;
        out.frames.push_back({{
            quantizeRootValue(delta->position.x),
            quantizeRootValue(delta->position.y),
            quantizeRootValue(delta->position.z)},
            quantizeRootValue(yaw)});
    }
    if(!validRootTrack(out,tickRate))
        throw std::invalid_argument("root bake: invalid quantized track");
    return out;
}
inline Json encodeRootMotionTracks(const RootMotionTracksResource& library) {
    Json root=Json::object();root.set("format",Json::string("aiverse-root-motion-v1"));
    Json tracks=Json::array();
    for(const auto& track:library.clips) {
        if(!validRootTrack(track,track.rate))
            throw std::invalid_argument("root bake: invalid export track");
        Json clip=Json::object();
        clip.set("id",Json::number(int64_t(track.id)));
        clip.set("rate",Json::number(int64_t(track.rate)));
        clip.set("loop",Json::fromBool(track.loop));
        Json frames=Json::array();
        for(const auto& f:track.frames) {
            Json row=Json::array();
            for(auto raw:{f.local.x.raw,f.local.y.raw,f.local.z.raw,f.yawTurns.raw})
                row.push(Json::number(int64_t(raw)));
            frames.push(row);
        }
        clip.set("frames",frames);tracks.push(clip);
    }
    root.set("tracks",tracks);return root;
}
inline RootMotionTracksResource decodeRootMotionTracks(const Json& root) {
    if (!root.isObject() || root.getStr("format")!="aiverse-root-motion-v1" ||
        !root["tracks"].isArray() || root["tracks"].size()>1024)
        throw std::invalid_argument("root bake: unsupported track document");
    RootMotionTracksResource result;
    std::set<uint32_t> used;
    for(const auto& c:root["tracks"].items()) {
        if(!c.isObject() || !c["frames"].isArray() ||
           c.getInt("id")<=0 || c.getInt("id")>std::numeric_limits<int32_t>::max() ||
           c.getInt("rate")<10 || c.getInt("rate")>240 ||
           !used.insert(uint32_t(c.getInt("id"))).second)
            throw std::invalid_argument("root bake: corrupt clip header");
        RootMotionTrack out;out.id=uint32_t(c.getInt("id"));
        out.rate=uint32_t(c.getInt("rate"));out.loop=c.getBool("loop");
        for(const auto& row:c["frames"].items()) {
            if(!row.isArray() || row.size()!=4)
                throw std::invalid_argument("root bake: corrupt fixed row");
            for(const auto& n:row.items())
                if(!n.isNumber())throw std::invalid_argument("root bake: row not integer");
            out.frames.push_back({{
                Fixed::fromRaw(row[0].integer()),
                Fixed::fromRaw(row[1].integer()),
                Fixed::fromRaw(row[2].integer())},
                Fixed::fromRaw(row[3].integer())});
        }
        if(!validRootTrack(out,out.rate))
            throw std::invalid_argument("root bake: invalid fixed frame values");
        result.clips.push_back(std::move(out));
    }
    return result;
}
} // namespace aiverse::lib::animation

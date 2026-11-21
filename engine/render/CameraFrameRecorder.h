// AIVERSE LENS frame-sequence recorder over the existing screen capture API.
// Each capture is asynchronous. Callbacks run on the device owner thread.
// Does not claim arbitrary offscreen RT readback or real-time video encoding.
// CameraHdrFrameRecorder records an RGBA16F camera target into linear EXR
// frames through Device::readTextureAsync (backends that read RGBA16F back).
#pragma once
#include "../gfx/Gfx.h"
#include "CameraExr.h"
#include "CameraPng.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
namespace aiverse::render {
struct CameraCapturedFrame {
    uint64_t index=0;
    double seconds=0;
    int width=0,height=0;
    std::vector<uint8_t> rgba;
    bool valid()const {
        return width>0&&height>0&&
               rgba.size()==size_t(width)*size_t(height)*4;
    }
    bool writePng(const std::string& path)const {
        return valid() && cameraWritePngRgba8(path,width,height,rgba);
    }
};
class CameraFrameRecorder {
    struct State {
        bool recording=false,pending=false;
        double frequency=30,previous=-1;
        uint64_t frameIndex=0;
        std::function<void(CameraCapturedFrame)> onFrame;
    };
    std::shared_ptr<State> state_=std::make_shared<State>();
public:
    void start(double framesPerSecond,std::function<void(CameraCapturedFrame)> callback) {
        stop();
        state_=std::make_shared<State>();
        state_->frequency=std::clamp(framesPerSecond,1.,120.);
        state_->recording=true;state_->onFrame=std::move(callback);
    }
    void stop() {
        if(state_){state_->recording=false;state_->pending=false;state_->onFrame={};}
    }
    bool tickTexture(gfx::Device& device,gfx::TextureHandle texture,double seconds) {
        if(!texture.valid() || !recording() || state_->pending ||
           !std::isfinite(seconds)) return false;
        if(state_->previous>=0 && seconds-state_->previous+1e-9<1.0/state_->frequency)
            return false;
        auto state=state_;
        state->pending=true;
        const uint64_t index=state->frameIndex;
        std::weak_ptr<State> weak=state;
        const bool accepted=device.captureTexture(texture,
            [weak,index,seconds](int w,int h,std::vector<uint8_t> rgba){
                auto current=weak.lock();
                if(!current||!current->recording)return;
                current->pending=false;
                CameraCapturedFrame frame{index,seconds,w,h,std::move(rgba)};
                if(current->onFrame)current->onFrame(std::move(frame));
            });
        if(!accepted){state->pending=false;return false;}
        state->previous=seconds;
        ++state->frameIndex;
        return true;
    }
    bool recording()const{return state_&&state_->recording;}
    bool pending()const{return state_&&state_->pending;}
    // Invoke inside a valid gfx frame before Device::endFrame. Backends may
    // deliver an empty image on a failed capture or unsupported platform.
    bool tick(gfx::Device& device,double seconds) {
        if(!recording()||state_->pending||!std::isfinite(seconds))return false;
        if(state_->previous>=0 && seconds-state_->previous+1e-9<1.0/state_->frequency)
            return false;
        auto state=state_;
        state->pending=true;
        state->previous=seconds;
        const uint64_t index=state->frameIndex++;
        std::weak_ptr<State> weak=state;
        device.captureNextFrame([weak,index,seconds](int w,int h,std::vector<uint8_t> rgba){
            auto current=weak.lock();
            if(!current||!current->recording)return;
            current->pending=false;
            CameraCapturedFrame frame{index,seconds,w,h,std::move(rgba)};
            if(current->onFrame)current->onFrame(std::move(frame));
        });
        return true;
    }
};

// Linear HDR frame of an RGBA16F target: 8 bytes per pixel, top-down rows.
struct CameraCapturedHdrFrame {
    uint64_t index=0;
    double seconds=0;
    int width=0,height=0;
    std::vector<uint8_t> rgba16f;
    bool valid()const {
        return width>0&&height>0&&
               rgba16f.size()==size_t(width)*size_t(height)*8;
    }
    bool writeExr(const std::string& path)const {
        return valid() && cameraWriteExrRgba16F(path,width,height,rgba16f);
    }
};
// Same timing and single-pending rules as CameraFrameRecorder. A backend that
// cannot read the texture back as RGBA16F rejects the request (returns false)
// or delivers an invalid frame; it never silently converts to 8-bit.
class CameraHdrFrameRecorder {
    struct State {
        bool recording=false,pending=false;
        double frequency=30,previous=-1;
        uint64_t frameIndex=0,rejected=0;
        std::function<void(CameraCapturedHdrFrame)> onFrame;
    };
    std::shared_ptr<State> state_=std::make_shared<State>();
public:
    void start(double framesPerSecond,std::function<void(CameraCapturedHdrFrame)> callback) {
        stop();
        state_=std::make_shared<State>();
        state_->frequency=std::clamp(framesPerSecond,1.,120.);
        state_->recording=true;state_->onFrame=std::move(callback);
    }
    void stop() {
        if(state_){state_->recording=false;state_->pending=false;state_->onFrame={};}
    }
    bool recording()const{return state_&&state_->recording;}
    bool pending()const{return state_&&state_->pending;}
    uint64_t rejectedFrames()const{return state_?state_->rejected:0;}
    // Request after the pass writing `texture` has ended; keep the texture
    // alive until the GPU blit completes (Device contract).
    bool tick(gfx::Device& device,gfx::TextureHandle texture,double seconds) {
        if(!texture.valid()||!recording()||state_->pending||!std::isfinite(seconds))
            return false;
        if(state_->previous>=0 && seconds-state_->previous+1e-9<1.0/state_->frequency)
            return false;
        auto state=state_;
        state->pending=true;
        const uint64_t index=state->frameIndex;
        std::weak_ptr<State> weak=state;
        const bool accepted=device.readTextureAsync(texture,gfx::TextureReadbackDesc{},
            [weak,index,seconds](gfx::TextureReadbackResult result){
                auto current=weak.lock();
                if(!current||!current->recording)return;
                current->pending=false;
                CameraCapturedHdrFrame frame{index,seconds,result.width,result.height,{}};
                if(result.success()&&result.format==gfx::PixelFormat::RGBA16F)
                    frame.rgba16f=std::move(result.pixels);
                else ++current->rejected;
                if(current->onFrame)current->onFrame(std::move(frame));
            });
        if(!accepted){state->pending=false;++state->rejected;return false;}
        state->previous=seconds;
        ++state->frameIndex;
        return true;
    }
};
} // namespace aiverse::render

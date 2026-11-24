// Real Apple Metal + CPU fallback Studio SpectraFX preview acceptance.
// This never invokes ImGui; it submits a real native Scene pass and inspects
// lifecycle/clock and captured GPU pixels on the same render owner thread.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include "apps/studio-native/StudioScene.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace aiverse;
namespace {
void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
Json action(const char* name){Json r=Json::object();r.set("action",Json::string(name));return r;}
}
int aiverseMain(int,char**) { @autoreleasepool {
    try {
        [NSApplication sharedApplication];
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,192,192)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        CAMetalLayer* layer=[CAMetalLayer layer];layer.framebufferOnly=NO;
        window.contentView.wantsLayer=YES;window.contentView.layer=layer;
        [window orderFront:nil];
        studio::StudioScene scene(gfx::createMetalDevice((__bridge void*)layer));
        // The normal map view and VFX authoring view now own INDEPENDENT
        // Metal render targets and gfx Devices. Both stay visible and are
        // driven by one render owner thread in StudioHost.
        CAMetalLayer* mapLayer=[CAMetalLayer layer];
        mapLayer.frame=CGRectMake(0,0,192,192);
        mapLayer.drawableSize=CGSizeMake(192,192);
        mapLayer.framebufferOnly=NO;
        [window.contentView.layer addSublayer:mapLayer];
        studio::StudioScene mapScene(gfx::createMetalDevice((__bridge void*)mapLayer));
        editor::MapDoc map;map.reset(8,8);
        const auto mapCamera=scene.cameraState();
        scene.setSpectraDockMode(true);
        require(scene.spectraDockMode(),"native FX dock mode not entered");
        require(scene.cameraState().distance>2.f,
                "isolated FX camera was not initialized");
        const Json empty;
        const auto frame=[&]{scene.frame(192,192,map,false,"","",empty,0);};
        const auto mapFrame=[&]{mapScene.frame(192,192,map,false,"","",empty,0);};
        mapFrame();
        const auto independentMapCamera=mapScene.cameraState();
        const auto fxCameraBefore=scene.cameraState();
        scene.orbit(24,-12);
        require(scene.cameraState().yaw!=fxCameraBefore.yaw,
                "FX camera orbit was not applied");
        require(mapScene.cameraState().yaw==independentMapCamera.yaw,
                "FX orbit changed the independent map camera");
        Json graph=Json::parse(R"JSON({
          "version":2,"nodes":[
             {"id":"spawn","type":"Spawn","properties":{"burst":20}},
             {"id":"init","type":"Initialize","properties":{
                 "life":3,"speed":0,"size":0.45,"spread":0}},
             {"id":"out","type":"SpriteOutput","properties":{
                 "texture":"builtin:soft","blend":"additive"}}],
          "links":[{"from":"spawn","to":"init"},{"from":"init","to":"out"}],
          "outputs":["out"]})JSON");
        Json load=action("load");load.set("graph",graph);
        auto state=scene.spectraPreviewCommand(load,"");
        require(state.getBool("ready"),"preview failed to initialize real GPU/CPU runtime");
        require(state.getInt("tick",-1)==0,"preview starts at zero tick");
        const Json started=scene.spectraPreviewCommand(action("play"),"");
        require(started.getBool("playing"),"play did not start clock");
        const auto transferRejected=scene.spectraPreviewCommand(action("checkpointFileBegin"),"");
        require(!transferRejected.getStr("error").empty()&&transferRejected.getBool("playing"),
                "checkpoint import while playing must report request-local rejection");
        require(scene.spectraPreviewCommand(action("status"),"").getStr("error").empty(),
                "file transport rejection poisoned simulation error latch");
        for(int i=0;i<3;++i){frame();mapFrame();}
        require(mapScene.cameraState().distance==independentMapCamera.distance,
                "map camera was hijacked by the separate FX preview");
        state=scene.spectraPreviewCommand(action("status"),"");
        require(state.getInt("tick")==3,"native frame advanced preview exactly once");
        // A malformed edit must not dispose the last-known-good GPU/CPU
        // preview, and a failed load must report compiler diagnostics.
        Json invalid=action("load");
        invalid.set("graph",Json::parse(R"({"version":2,"nodes":[],"links":[],"outputs":[]})"));
        const Json rejected=scene.spectraPreviewCommand(invalid,"");
        require(rejected.getBool("ready") && !rejected.getStr("error").empty(),
                "invalid graph destroyed a working preview or hid diagnostics");
        const auto staleCancel=scene.spectraPreviewCommand(action("checkpointFileCancel"),"");
        require(!staleCancel.getStr("error").empty(),"stale transfer cancellation must be rejected");
        require(scene.spectraPreviewCommand(action("status"),"").getStr("error")==rejected.getStr("error"),
                "file transport rejection replaced existing error");
        scene.spectraPreviewCommand(action("pause"),"");
        frame();
        require(scene.spectraPreviewCommand(action("status"),"").getInt("tick")==3,
                "pause must freeze particle time");
        scene.spectraPreviewCommand(action("step"),"");
        frame();
        require(scene.spectraPreviewCommand(action("status"),"").getInt("tick")==4,
                "single-step must advance exactly one fixed 60 Hz tick");
        Json seek=action("seek");seek.set("tick",Json::number(int64_t(25)));
        scene.spectraPreviewCommand(seek,"");
        for(int i=0;i<4;++i)frame();
        state=scene.spectraPreviewCommand(action("status"),"");
        require(state.getInt("tick")==25 && !state.getBool("playing"),
                "bounded seek reached target and paused");
        bool captured=false,valid=false;
        std::vector<uint8_t> fxPixels;
        scene.captureNextFrame([&](int w,int h,std::vector<uint8_t> pixels){
            fxPixels=std::move(pixels);
            valid=(w==192&&h==192&&fxPixels.size()==size_t(w*h*4));
            captured=true;
        });
        for(int i=0;i<10&&!captured;++i){frame();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        require(captured&&valid,"actual native FX GPU frame capture failed");
        bool capturedMap=false,validMap=false;
        std::vector<uint8_t> mapPixels;
        mapScene.captureNextFrame([&](int w,int h,std::vector<uint8_t> pixels) {
            mapPixels=std::move(pixels);
            validMap=w==192&&h==192&&mapPixels.size()==size_t(w*h*4);
            capturedMap=true;
        });
        for(int i=0;i<10&&!capturedMap;++i){
            mapFrame();std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(capturedMap&&validMap,
                "independent map GPU capture failed while FX was loaded");
        size_t pixelDifferences=0;
        for(size_t i=0;i+3<fxPixels.size();i+=4)
            if(fxPixels[i]!=mapPixels[i]||fxPixels[i+1]!=mapPixels[i+1]||
               fxPixels[i+2]!=mapPixels[i+2])++pixelDifferences;
        require(pixelDifferences>1000,
                "Scene and FX rendered identical GPU frames despite separate outputs");
        Json longSeek=action("seek");longSeek.set("tick",Json::number(int64_t(100)));
        scene.spectraPreviewCommand(longSeek,"");
        scene.spectraPreviewCommand(action("pause"),"");
        frame();
        require(scene.spectraPreviewCommand(action("status"),"").getInt("tick")==25,
                "Pause failed to cancel unfinished seek");
        const Json restart=scene.spectraPreviewCommand(action("restart"),"");
        require(restart.getInt("tick")==0,"restart resets tick");
        frame();
        const Json stopped=scene.spectraPreviewCommand(action("stop"),"");
        require(!stopped.getBool("ready"),"shutdown left GPU visual source alive");
        scene.setSpectraDockMode(false);
        require(!scene.spectraDockMode(),"native Scene not restored after FX tab close");
        require(scene.cameraState().distance==mapCamera.distance,
                "authored Scene camera changed during isolated FX preview");
        const Json failed=scene.spectraPreviewCommand(action("step"),"");
        require(!failed.getStr("error").empty(),"invalid transition should report error");
        std::cout<<"Studio SpectraFX Metal preview PASS\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"Studio SpectraFX Metal preview FAIL: "<<e.what()<<"\n";
        return 1;
    }
} }

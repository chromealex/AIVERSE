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
        editor::MapDoc map;map.reset(8,8);
        const Json empty;
        const auto frame=[&]{scene.frame(192,192,map,false,"","",empty,0);};
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
        for(int i=0;i<3;++i)frame();
        state=scene.spectraPreviewCommand(action("status"),"");
        require(state.getInt("tick")==3,"native frame advanced preview exactly once");
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
        scene.captureNextFrame([&](int w,int h,std::vector<uint8_t> pixels){
            captured=true;
            valid=(w==192&&h==192&&pixels.size()==size_t(w*h*4));
        });
        for(int i=0;i<10&&!captured;++i){frame();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        require(captured&&valid,"actual native Scene GPU frame capture failed");
        const Json restart=scene.spectraPreviewCommand(action("restart"),"");
        require(restart.getInt("tick")==0,"restart resets tick");
        frame();
        const Json stopped=scene.spectraPreviewCommand(action("stop"),"");
        require(!stopped.getBool("ready"),"shutdown left GPU visual source alive");
        const Json failed=scene.spectraPreviewCommand(action("step"),"");
        require(!failed.getStr("error").empty(),"invalid transition should report error");
        std::cout<<"Studio SpectraFX Metal preview PASS\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"Studio SpectraFX Metal preview FAIL: "<<e.what()<<"\n";
        return 1;
    }
} }

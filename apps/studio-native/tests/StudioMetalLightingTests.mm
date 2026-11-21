#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include "apps/studio-native/mac/StudioMetalScene.h"
#include "apps/studio-native/StudioLightingPreview.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
using namespace aiverse;
int aiverseMain(int argc,char** argv) { @autoreleasepool {
    try {
        if(argc!=2)throw std::runtime_error("capture directory required");
        std::filesystem::create_directories(argv[1]);
        [NSApplication sharedApplication];
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,160,160)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        CAMetalLayer* layer=[CAMetalLayer layer];layer.framebufferOnly=NO;
        window.contentView.wantsLayer=YES;window.contentView.layer=layer;
        [window orderFront:nil];
        studio::StudioMetalScene scene((__bridge void*)layer);
        editor::MapDoc doc;doc.reset(8,8);
        // This GPU fixture explicitly authors a legacy terrain receiver.
        doc.data.extra.set("terrainPresentation",Json::string("legacy"));
        doc.data.extra.set("lights",Json::parse(R"([{"x":4,"z":4,"height":2,"radius":12,"intensity":6,"color":[1,0.3,0.1]}])"));
        const Json empty;
        bool fps=false;
        auto frame=[&](){scene.frame(160,160,doc,fps,"","",empty,0);};
        std::vector<uint8_t> litPixels,unlitPixels;
        for(const char* mode:{"cpu","gpu","off","unlit"}) {
            const std::string indirect=std::string(mode)=="unlit"?"off":mode;
            if(std::string(mode)=="unlit")doc.data.extra.set("lights",Json::array());
            doc.data.extra.set("renderLighting",Json::parse(std::string("{\"indirect\":\"")+indirect+"\",\"direct\":\"luma\",\"probes\":4,\"layers\":2,\"spacing\":2,\"samples\":8}"));doc.touched();
            for(int i=0;i<150;++i) {
                frame();
                if(scene.lightingStatus().find("converging")==std::string::npos)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const auto status=scene.lightingStatus();
            if(status.find("Direct: Luma")==std::string::npos || status.find("converging")!=std::string::npos)
                throw std::runtime_error(status);
            if(indirect!="off" && status.find(std::string("GI: ")+(std::string(mode)=="cpu"?"CPU":"GPU"))==std::string::npos)
                throw std::runtime_error(status);
            bool captured=false,valid=false;
            scene.captureNextFrame([&](int w,int h,std::vector<uint8_t> pixels){
                captured=true;valid=w==160&&h==160&&pixels.size()==size_t(w*h*4);
                if(!valid)return;
                uint64_t sum=0;std::ofstream file(std::filesystem::path(argv[1])/(std::string(mode)+".ppm"),std::ios::binary);
                file<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<pixels.size();i+=4){file.write(reinterpret_cast<const char*>(pixels.data()+i),3);sum+=pixels[i]+pixels[i+1]+pixels[i+2];}
                valid=sum>uint64_t(w*h*10);
                if(std::string(mode)=="off")litPixels=pixels;
                if(std::string(mode)=="unlit")unlitPixels=pixels;
            });
            for(int i=0;i<10&&!captured;++i){frame();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            if(!captured||!valid)throw std::runtime_error("native viewport capture invalid");
            std::cout<<status<<" PASS\n";
        }
        if(litPixels.size()!=unlitPixels.size() || litPixels.empty())throw std::runtime_error("missing on/off captures");
        int64_t energy=0;size_t changed=0;
        for(size_t i=0;i<litPixels.size();i+=4) {
            int delta=0;for(int c=0;c<3;++c)delta+=int(litPixels[i+c])-int(unlitPixels[i+c]);
            energy+=delta;if(delta>3)++changed;
        }
        std::cout<<"Luma light removal delta "<<energy<<" in "<<changed<<" pixels\n";
        if(energy<100 || changed<30)throw std::runtime_error("authored Luma source did not light preview pixels");
        // Turn off and restore to exercise resource retirement and view invalidation.
        doc.data.extra.set("renderLighting",Json::object());doc.touched();frame();
        if(scene.lightingStatus().find("GI: off · Direct: standard")==std::string::npos)throw std::runtime_error("disable failed");
        // FPS has no RTS size and may live entirely in negative coordinates.
        std::string error;
        if(!doc.loadJson(Json::parse(R"({"name":"Negative FPS",
          "colliders":[{"min":[-24,-0.2,-16],"max":[-16,0,-8]}],
          "spawns":[[-20,1,-12]],"players":[{"name":"P1","team":1}],
          "lights":[{"x":-20,"z":-12,"height":2,"radius":12,"intensity":6,"color":[1,0.3,0.1]}],
          "renderLighting":{"indirect":"gpu","direct":"luma","probes":4,"layers":2,"spacing":3,"samples":8}
        })"),&error))throw std::runtime_error(error);
        const auto preview=studio::studioLightingPreview(doc,true,1.5f);
        if(!preview || preview->boundsMax.x>=0 || preview->boundsMax.z>=0 ||
           preview->boundsMin.x>-24 || preview->boundsMin.z>-16)
            throw std::runtime_error("FPS preview includes unrelated RTS terrain");
        fps=true;scene.resetCamera();
        for(int i=0;i<150;++i) {
            frame();
            if(scene.lightingStatus().find("converging")==std::string::npos)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(scene.lightingStatus().find("GI: GPU")==std::string::npos ||
           scene.lightingStatus().find("Direct: Luma")==std::string::npos ||
           scene.lightingStatus().find("converging")!=std::string::npos)
            throw std::runtime_error("FPS GPU convergence: "+scene.lightingStatus());
        // A center-screen hit proves that the camera follows the negative scene.
        if(scene.pick(80,80,160,160,doc,true).empty())
            throw std::runtime_error("FPS camera misses authored scene");
        scene.frame(160,160,doc,true,"","Material",Json::object(),1);
        if(scene.lightingStatus().find("Material preview · GI: off")==std::string::npos)
            throw std::runtime_error("material preview reports stale GI");
        frame();
        if(scene.lightingStatus().find("Material preview")!=std::string::npos)
            throw std::runtime_error("scene status not restored");
        std::cout<<"Negative-coordinate FPS and material transition PASS\n";
        std::cout<<"Studio Metal lighting PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Studio Metal lighting FAIL: "<<e.what()<<"\n";return 1;}
} }

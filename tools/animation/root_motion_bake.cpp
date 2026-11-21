// aiverse_root_motion_bake MODEL CLIP ROOT --id INTEGER --rate INTEGER [--loop] [--out FILE]
#include <aiverselib/animation/RootMotionBaker.h>
#include "engine/render/ModelClips.h"
#include <fstream>
#include <iostream>
#include <iterator>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(int argc,char** argv){
    try {
        if(argc<5)throw std::runtime_error("usage: aiverse_root_motion_bake MODEL CLIP ROOT --id N [--rate N] [--loop] [--out FILE]");
        const std::string path=argv[1],name=argv[2],boneName=argv[3];
        uint32_t rate=60,id=0;bool loop=false;std::string output;
        for(int i=4;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--rate" && i+1<argc)rate=uint32_t(std::stoul(argv[++i]));
            else if(arg=="--id" && i+1<argc)id=uint32_t(std::stoul(argv[++i]));
            else if(arg=="--out" && i+1<argc)output=argv[++i];
            else if(arg=="--loop")loop=true;
            else throw std::invalid_argument("unknown root bake argument: "+arg);
        }
        auto read=[](const std::string& p,std::string& text){
            std::ifstream stream(p,std::ios::binary);
            if(!stream)return false;
            text.assign(std::istreambuf_iterator<char>(stream),{});
            return true;
        };
        auto model=loadModel(path,read);
        const int clip=model->findClip(name);
        const int root=aiverse::lib::animation::findNodePath(*model,boneName);
        if(clip<0 || root<0)
            throw std::invalid_argument("unknown root bone path or clip");
        RootMotionTracksResource asset;
        asset.clips.push_back(bakeRootMotion(*model,model->clips[size_t(clip)],
                                            root,id,rate,loop));
        const auto bytes=encodeRootMotionTracks(asset).dump(2);
        if(output.empty())std::cout<<bytes<<"\n";
        else {
            std::ofstream stream(output,std::ios::binary);
            if(!stream)throw std::runtime_error("cannot create root track file");
            stream<<bytes<<"\n";
            if(!stream)throw std::runtime_error("root track write failed");
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"root motion bake: "<<e.what()<<"\n";return 1;}
}

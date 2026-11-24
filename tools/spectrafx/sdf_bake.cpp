// Offline model bind-pose SDF baking, no GPU or gameplay simulation.
#include "engine/fx/SpectraSdfBaker.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(int argc,char** argv){
    using namespace aiverse;
    try{
        if(argc!=4)throw std::runtime_error("usage: aiverse_sdf_bake MODEL DIMENSION OUT.json");
        size_t parsed=0;const auto n=std::stoul(argv[2],&parsed);
        if(parsed!=std::string(argv[2]).size()||n<4||n>16)throw std::runtime_error("dimension must be 4..16");
        const auto read=[](const std::string& path,std::string& data){
            std::ifstream f(path,std::ios::binary|std::ios::ate);
            if(!f)return false;
            const auto size=f.tellg();
            if(size<0||size>std::streamoff(64*1024*1024))return false;
            data.resize(size_t(size));f.seekg(0);
            return bool(f.read(data.data(),std::streamsize(data.size())));
        };
        fx::StaticSdfVolume sdf;std::string error;
        if(!fx::bakeStaticSdfModel(argv[1],read,uint32_t(n),render::mat4{},sdf,error))
            throw std::runtime_error(error);
        const auto json=fx::staticSdfToJson(sdf);
        std::ofstream out(argv[3],std::ios::binary|std::ios::trunc);
        if(!out||!(out<<json.dump(2)<<"\n"))throw std::runtime_error("cannot write SDF output");
        return 0;
    }catch(const std::exception& e){std::cerr<<"SDF bake: "<<e.what()<<"\n";return 1;}
}

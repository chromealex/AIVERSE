// Offline model bind-pose SDF baking, no GPU or gameplay simulation.
#include "engine/fx/SpectraSdfBaker.h"
#include "engine/fx/SpectraSdfBinary.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(int argc,char** argv){
    using namespace aiverse;
    try{
        if(argc!=4)throw std::runtime_error("usage: aiverse_sdf_bake MODEL DIMENSION OUT.json|OUT.sdfbin");
        size_t parsed=0;const auto n=std::stoul(argv[2],&parsed);
        if(parsed!=std::string(argv[2]).size()||n<4||n>bake::kMeshDistanceMaxDimension)throw std::runtime_error("dimension must be 4..32");
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
        const std::string path=argv[3];
        std::string bytes;
        if(path.size()>=7&&path.compare(path.size()-7,7,".sdfbin")==0){
            if(!fx::encodeSdfBinary(sdf,bytes))throw std::runtime_error("cannot encode SDF binary");
        }else bytes=fx::staticSdfToJson(sdf).dump(2)+"\n";
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        if(!out||!out.write(bytes.data(),std::streamsize(bytes.size())))throw std::runtime_error("cannot write SDF output");
        out.close();if(!out)throw std::runtime_error("cannot close SDF output");
        return 0;
    }catch(const std::exception& e){std::cerr<<"SDF bake: "<<e.what()<<"\n";return 1;}
}

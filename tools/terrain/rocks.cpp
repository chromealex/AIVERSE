// Offline export of the SAME native TerraWeave generator, not a second solver.
#include "engine/render/TerraWeaveRockModel.h"
#include "tools/modeling/Modeling.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using aiverse::Json;
static uint32_t u32(const std::string& s,size_t p){return uint32_t(uint8_t(s[p]))|uint32_t(uint8_t(s[p+1]))<<8|uint32_t(uint8_t(s[p+2]))<<16|uint32_t(uint8_t(s[p+3]))<<24;}
static void write(const fs::path& path,const std::string& s){std::ofstream f(path,std::ios::binary);f.write(s.data(),std::streamsize(s.size()));if(!f)throw std::runtime_error("cannot write "+path.string());}
int main(int argc,char** argv){try{
    fs::path out,materials;uint64_t seed=1;uint32_t count=8;
    for(int i=1;i<argc;++i){std::string k=argv[i];if(i+1==argc)throw std::runtime_error("missing value: "+k);std::string v=argv[++i];
        if(k=="--out")out=v;else if(k=="--materials")materials=v;else if(k=="--seed")seed=std::stoull(v);else if(k=="--count"){const auto n=std::stoull(v);if(n>64)throw std::runtime_error("count <=64");count=uint32_t(n);}else throw std::runtime_error("unknown option "+k);}
    if(out.empty() || materials.empty() || count<4 || count%4)throw std::runtime_error("usage: aiverse_terraweave_rocks --out NEW_DIRECTORY --materials TERRAIN_TEXTURE_DIRECTORY [--seed N --count 4..64 multiple of 4]");
    if(fs::exists(out))throw std::runtime_error("output directory already exists; refusing overwrite");
    const char* suffixes[]={"basecolor","normal","orm","ao"};
    for(auto s:suffixes)if(!fs::is_regular_file(materials/(std::string("rock_")+s+"_repeat.png")))throw std::runtime_error("missing rock PBR texture");
    fs::create_directories(out/"textures");
    for(auto s:suffixes){auto name=std::string("rock_")+s+"_repeat.png";fs::copy_file(materials/name,out/"textures"/name);}
    auto catalog=Json::object();catalog.set("version",Json::number(int64_t(1)));auto list=Json::array();
    using namespace aiverse::terraweave;
    for(uint32_t i=0;i<count;++i){RockProfile profile;profile.shape=RockShape(i%4);profile.weatheringPermille=100+(i*173)%700;
        const auto variantSeed=detail::mix64(seed^i);auto model=makeRockModel(profile,variantSeed);
        model->materials[0].baseColor={1,1,1,1};model->materials[0].roughness=1;
        const std::string glb=aiverse::modeling::exportGlb(*model);const auto jsonBytes=u32(glb,12);auto doc=Json::parse(glb.substr(20,jsonBytes));
        const size_t binHeader=20+jsonBytes;const auto binBytes=u32(glb,binHeader);std::string name="rock-"+std::to_string(i);
        doc.member("buffers").at(0).set("uri",Json::string(name+".bin"));
        auto images=Json::array(),textures=Json::array();
        for(int k=0;k<4;++k){auto im=Json::object();im.set("uri",Json::string(std::string("textures/rock_")+suffixes[k]+"_repeat.png"));images.push(im);auto tx=Json::object();tx.set("source",Json::number(int64_t(k)));textures.push(tx);}
        doc.set("images",images);doc.set("textures",textures);
        auto binding=[](int k){auto t=Json::object();t.set("index",Json::number(int64_t(k)));return t;};
        auto& mat=doc.member("materials").at(0);mat.member("pbrMetallicRoughness").set("baseColorTexture",binding(0));mat.member("pbrMetallicRoughness").set("metallicRoughnessTexture",binding(2));mat.set("normalTexture",binding(1));mat.set("occlusionTexture",binding(3));
        write(out/(name+".bin"),glb.substr(binHeader+8,binBytes));write(out/(name+".gltf"),doc.dump());
        auto entry=Json::object();entry.set("file",Json::string(name+".gltf"));entry.set("shape",Json::string(i%4==0?"rounded":i%4==1?"boulder":i%4==2?"slab":"shard"));entry.set("seed",Json::string(std::to_string(variantSeed)));entry.set("triangles",Json::parse("[1280,320,80]"));list.push(entry);
    }
    catalog.set("models",list);write(out/"catalog.json",catalog.dump());std::cout<<"Exported "<<count<<" native rock prototypes, three LODs, four shared PBR maps\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

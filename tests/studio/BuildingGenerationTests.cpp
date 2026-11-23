// Focused native Studio building generator acceptance: no browser-side asset writes.
#include "apps/studio-native/StudioBuildingGeneration.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
using namespace aiverse;
using namespace aiverse::studio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
int main(int argc,char** argv){
    namespace fs=std::filesystem;
    if(argc<2)return 2;
    fs::path temp=fs::temp_directory_path()/(
        "aiverse-studio-buildings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directory(temp);
        const fs::path engine=argv[1];
        auto catalog=studioBuildingCatalog(engine);
        CHECK(catalog["styles"].isArray());
        bool found=false;
        for(const auto& style:catalog["styles"].items())
            if(style.getStr("id")=="scifi"){
                found=true;CHECK(style.getStr("setting")=="scifi");
                CHECK(style["functions"].size()>=4);
            }
        CHECK(found);
        const auto req=Json::parse(R"({"setting":"scifi","style":"scifi","function":"factory","width":28,"height":15,"depth":24,"seed":42})");
        auto a=studioBuildingInspect(engine,req),b=studioBuildingInspect(engine,req);
        CHECK(a==b);
        CHECK(a["triangles"].integer()>0);
        CHECK(a["width"].toDouble()>27.99&&a["width"].toDouble()<28.01);
        CHECK(a["height"].toDouble()>14.99&&a["height"].toDouble()<15.01);
        CHECK(a["depth"].toDouble()>23.99&&a["depth"].toDouble()<24.01);
        auto out=studioBuildingGenerate(engine,temp,req);
        CHECK(out.getStr("path").rfind("GeneratedBuildings/",0)==0);
        CHECK(studioValidGuid(out.getStr("guid")));
        CHECK(out["triangles"].integer()==a["triangles"].integer());
        const auto content=studioContentRoot(temp);
        CHECK(fs::is_regular_file(content/out.getStr("path")));
        CHECK(fs::is_regular_file(studioMetaPath(content/out.getStr("path"))));
        // Never silently replace generated assets: a repeated request creates
        // a different path/GUID with equivalent procedural content.
        auto out2=studioBuildingGenerate(engine,temp,req);
        CHECK(out2.getStr("path")!=out.getStr("path"));
        CHECK(out2.getStr("guid")!=out.getStr("guid"));
        auto invalid=[](const auto& fn){
            try{fn();}catch(const std::exception&){return true;}
            return false;
        };
        CHECK(invalid([&]{studioBuildingInspect(engine,Json::parse(R"({"setting":"scifi","style":"../hack","function":"factory","width":28,"height":15,"depth":24,"seed":42})"));}));
        CHECK(invalid([&]{studioBuildingInspect(engine,Json::parse(R"({"setting":"modern","style":"scifi","function":"factory","width":28,"height":15,"depth":24,"seed":42})"));}));
        CHECK(invalid([&]{studioBuildingInspect(engine,Json::parse(R"({"setting":"scifi","style":"scifi","function":"nonexistent","width":28,"height":15,"depth":24,"seed":42})"));}));
        CHECK(invalid([&]{studioBuildingInspect(engine,Json::parse(R"({"setting":"scifi","style":"scifi","function":"factory","width":28,"height":15,"depth":24,"seed":-5})"));}));
        fs::remove_all(temp);
        std::puts("studio building generator PASS");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"studio building generator FAILED: %s\n",e.what());
        std::error_code ec;fs::remove_all(temp,ec);return 1;
    }
}

// Focused native Studio building generator acceptance: no browser-side asset writes.
#include "apps/studio-native/StudioBuildingGeneration.h"
#include "apps/studio-native/StudioBuildingRegeneration.h"
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
        // Twelve brand-new setting IDs must be enumerated by the native
        // loader, not accidentally grouped by a hard-coded prefix.
        for(const char* id:{"medieval-fantasy","dark-fantasy","steampunk",
            "cyberpunk","scifi-space","alien-civilization",
            "post-apocalypse","modern","ancient-civilizations",
            "asian-historical","solarpunk","underwater"}){
            bool present=false;
            for(const auto& entry:catalog["styles"].items())
                if(entry.getStr("id")==id){
                    CHECK(entry.getStr("setting")==id);
                    CHECK(entry["functions"].size()==4);
                    CHECK(entry["materials"].size()>=16);
                    present=true;break;
                }
            CHECK(present);
        }
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
        // Independent functional recipe: gates/door/roof are selected
        // without a factory/barracks preset. The same recipe can change
        // visual style without changing geometry, doors or bounds.
        auto custom=Json::parseFile((engine/"engine/assets/building-recipes/multiuse-open.json").string());
        custom.set("mode",Json::string("features"));
        custom.set("style",Json::string("scifi"));
        custom.set("setting",Json::string("scifi"));
        auto recipePreview=studioBuildingInspect(engine,custom);
        CHECK(recipePreview["triangles"].integer()>0);
        CHECK(std::abs(recipePreview["width"].toDouble()-24.)<0.01);
        CHECK(std::abs(recipePreview["height"].toDouble()-12.)<0.01);
        CHECK(std::abs(recipePreview["depth"].toDouble()-18.)<0.01);
        auto recipeAsset=studioBuildingGenerate(engine,temp,custom);
        CHECK(recipeAsset.getStr("path").find("scifi-custom-7")!=std::string::npos);
        CHECK(studioValidGuid(recipeAsset.getStr("guid")));
        CHECK(fs::is_regular_file(studioMetaPath(content/recipeAsset.getStr("path"))));
        CHECK(fs::is_regular_file(content/recipeAsset.getStr("recipePath")));
        const auto savedRecipe=Json::parseFile((content/recipeAsset.getStr("recipePath")).string());
        CHECK(savedRecipe.getStr("mode")=="features");
        CHECK(savedRecipe["features"].size()==4);
        CHECK(savedRecipe["physicalSockets"].isArray());
        CHECK(savedRecipe["physicalSockets"].size()>0);
        for(const auto& socket:savedRecipe["physicalSockets"].items()){
            CHECK(!socket.getStr("id").empty());
            CHECK(socket["closedBoundsMin"].isArray());
            CHECK(socket["closedBoundsMax"].isArray());
            CHECK(socket["closedBoundsMin"].size()==3);
            CHECK(socket["closedBoundsMax"].size()==3);
        }
        const auto allSaved=studioBuildingRecipes(temp);
        CHECK(allSaved["recipes"].size()>=3);
        const auto reopened=studioBuildingReadRecipe(
            engine,temp,recipeAsset.getStr("recipePath"));
        CHECK(reopened["request"].getStr("style")=="scifi");
        CHECK(reopened["request"].getStr("mode")=="features");
        CHECK(reopened["request"]["features"].size()==4);
        CHECK(reopened["request"]["materialBindings"].isObject() ||
              reopened["request"]["materialBindings"].isNull());
        CHECK(savedRecipe["features"][1].getStr("kind")=="small-gate");
        CHECK(savedRecipe["features"][1]["count"].integer()==2);
        CHECK(std::abs(savedRecipe["features"][0]["position"].toDouble()-7.)<0.01);
        CHECK(std::abs(savedRecipe["features"][1]["position"].toDouble()-12.)<0.01);
        CHECK(std::abs(savedRecipe["features"][1]["spacing"].toDouble()-1.)<0.01);
        CHECK(std::abs(savedRecipe["features"][3]["position"].toDouble()-13.)<0.01);
        CHECK(std::abs(savedRecipe["features"][3]["positionZ"].toDouble()-10.)<0.01);
        custom.set("style",Json::string("modern"));
        custom.set("setting",Json::string("modern"));
        auto modernPreview=studioBuildingInspect(engine,custom);
        CHECK(modernPreview["triangles"].integer()==recipePreview["triangles"].integer());
        auto modernAsset=studioBuildingGenerate(engine,temp,custom);
        CHECK(modernAsset.getStr("path").find("modern-custom-7")!=std::string::npos);
        CHECK(studioValidGuid(modernAsset.getStr("guid")));
        // Floors, room layout and window parameters survive the native GLB
        // export into the project's GUID-indexed canonical recipe sidecar.
        auto multi=Json::parseFile(
            (engine/"engine/assets/building-recipes/two-floor-workshop.json").string());
        multi.set("mode",Json::string("features"));
        multi.set("style",Json::string("scifi"));
        multi.set("setting",Json::string("scifi"));
        auto multiPreview=studioBuildingInspect(engine,multi);
        CHECK(multiPreview["triangles"].integer()>recipePreview["triangles"].integer());
        CHECK(std::abs(multiPreview["height"].toDouble()-14.)<0.01);
        auto multiAsset=studioBuildingGenerate(engine,temp,multi);
        CHECK(studioValidGuid(multiAsset.getStr("guid")));
        CHECK(fs::is_regular_file(content/multiAsset.getStr("recipePath")));
        const auto savedMulti=Json::parseFile(
            (content/multiAsset.getStr("recipePath")).string());
        CHECK(savedMulti["floors"].integer()==2);
        CHECK(savedMulti["roomsX"].integer()==2);
        CHECK(savedMulti["roomsZ"].integer()==2);
        CHECK(savedMulti["features"][2].getStr("kind")=="window");
        CHECK(savedMulti["features"][2]["floor"].integer()==1);
        CHECK(std::abs(savedMulti["features"][2]["sill"].toDouble()-1.05)<0.02);
        multi.set("style",Json::string("modern"));
        multi.set("setting",Json::string("modern"));
        auto otherStyle=studioBuildingInspect(engine,multi);
        CHECK(otherStyle["triangles"].integer()==multiPreview["triangles"].integer());
        // Two-storey vertical circulation is a reusable recipe, not a
        // factory/command preset. The exact shaft sizes, X/Z and kind
        // survive native GLB + asset GUID + canonical recipe sidecar.
        auto vertical=Json::parseFile(
            (engine/"engine/assets/building-recipes/vertical-circulation.json").string());
        vertical.set("mode",Json::string("features"));
        vertical.set("style",Json::string("scifi"));
        vertical.set("setting",Json::string("scifi"));
        auto stairPreview=studioBuildingInspect(engine,vertical);
        CHECK(stairPreview["triangles"].integer()>0);
        auto stairAsset=studioBuildingGenerate(engine,temp,vertical);
        CHECK(studioValidGuid(stairAsset.getStr("guid")));
        CHECK(fs::is_regular_file(content/stairAsset.getStr("path")));
        CHECK(fs::is_regular_file(content/stairAsset.getStr("recipePath")));
        auto savedVertical=Json::parseFile(
            (content/stairAsset.getStr("recipePath")).string());
        CHECK(savedVertical["floors"].integer()==2);
        CHECK(savedVertical["features"][3].getStr("kind")=="stairs");
        CHECK(savedVertical["features"][4].getStr("kind")=="elevator");
        CHECK(std::abs(savedVertical["features"][3]["width"].toDouble()-2.6)<0.01);
        CHECK(std::abs(savedVertical["features"][3]["depth"].toDouble()-6.6)<0.01);
        CHECK(std::abs(savedVertical["features"][3]["position"].toDouble()-6.)<0.01);
        CHECK(std::abs(savedVertical["features"][4]["position"].toDouble()-18.)<0.01);
        vertical.set("style",Json::string("modern"));
        vertical.set("setting",Json::string("modern"));
        auto modernStairs=studioBuildingInspect(engine,vertical);
        CHECK(modernStairs["triangles"].integer()==stairPreview["triangles"].integer());
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

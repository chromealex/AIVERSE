// Stage 179: real project content contract for opt-in ModelView MotionWeave
// scenes. Deferred native CTest (not a GPU/renderer test).
#include "engine/scene/SceneAsset.h"
#include "engine/data/Json.h"
#include "tests/AlwaysCheck.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

using namespace aiverse;
namespace sc=aiverse::scene;

namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    AIVERSE_TEST_CHECK(bool(file));
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
sc::SceneAsset load(const std::filesystem::path& root,
                    const std::string& key) {
    sc::SceneAsset asset;
    std::string error;
    AIVERSE_TEST_CHECK(sc::SceneAsset::decode(
        Json::parse(read(root/(key+".json"))),asset,&error));
    AIVERSE_TEST_CHECK(asset.id==key);
    return asset;
}
}
int main() {
    const std::filesystem::path root=
        std::filesystem::path(AIVERSE_SOURCE_DIR)/"templates/showcase";
    const auto json=Json::parse(read(root/"motionweave-visuals.json"));
    AIVERSE_TEST_CHECK(json["version"].numberLiteral()=="1");
    AIVERSE_TEST_CHECK(json["residencyKey"].isString());
    AIVERSE_TEST_CHECK(json["prefabs"].isArray());
    std::set<std::string> named,nonvisual;
    std::map<std::string,std::string> models;
    for(const auto& prefab:json["prefabs"].items()) {
        AIVERSE_TEST_CHECK(prefab["key"].isString());
        const auto name=prefab["key"].str();
        AIVERSE_TEST_CHECK(sc::SceneAsset::validKey(name));
        AIVERSE_TEST_CHECK(named.insert(name).second);
        if(prefab.has("nonvisual")) {
            AIVERSE_TEST_CHECK(prefab["nonvisual"].boolean());
            AIVERSE_TEST_CHECK(!prefab.has("model"));
            nonvisual.insert(name);
        }else {
            AIVERSE_TEST_CHECK(prefab["model"].isString());
            const auto model=prefab["model"].str();
            AIVERSE_TEST_CHECK(sc::SceneAsset::validKey(model));
            const auto bytes=read(root/model);
            AIVERSE_TEST_CHECK(bytes.size()>=12);
            AIVERSE_TEST_CHECK(bytes.substr(0,4)=="glTF"); // actual GLB
            models.emplace(name,model);
        }
    }
    AIVERSE_TEST_CHECK(models.contains("visual/slime"));
    AIVERSE_TEST_CHECK(nonvisual.contains("logic/marker"));
    const auto persistent=load(root,"scenes/motionweave-global");
    const auto additive=load(root,"scenes/motionweave-area");
    const auto unknown=load(root,"scenes/motionweave-unknown");
    AIVERSE_TEST_CHECK(persistent.objects.size()==2);
    AIVERSE_TEST_CHECK(additive.objects.size()==2);
    for(const auto* scene:{&persistent,&additive})
        for(const auto& item:scene->objects)
            AIVERSE_TEST_CHECK(named.contains(item.prefab));
    AIVERSE_TEST_CHECK(unknown.objects.size()==1);
    AIVERSE_TEST_CHECK(!named.contains(unknown.objects[0].prefab));
}

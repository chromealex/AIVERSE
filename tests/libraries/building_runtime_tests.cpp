// Portable runtime building generator smoke/regression. No Studio, GPU or GLB
// exporter in the tested dependency graph. Packageable native game API.
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace aiverse;
using namespace aiverse::lib::systems;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__)+":" + std::to_string(__LINE__)+": " #x); } while(false)

int main(int argc,char** argv) {
    try {
        const std::string root=argc>1?argv[1]:".";
        std::ifstream file(root+"/engine/assets/building-styles/scifi.json");
        CHECK(bool(file));
        std::stringstream buffer;
        buffer<<file.rdbuf();

        RuntimeBuildingGenerator generator;
        std::string error;
        CHECK(!generator.registerStyleJson("",&error)&&!error.empty());
        CHECK(generator.registerStyleJson(buffer.str(),&error)&&error.empty());
        CHECK(generator.styleCount()==1);
        CHECK(generator.style("scifi")!=nullptr);
        CHECK(generator.styleIds()==std::vector<std::string>{"scifi"});
        CHECK(!generator.registerStyleJson(buffer.str(),&error)&&!error.empty());
        CHECK(!generator.registerStyleJson("{oops}",&error)&&!error.empty());
        CHECK(generator.styleCount()==1); // registration failure cannot poison state
        CHECK(generator.generate({"unknown","factory",3}).error==RuntimeBuildingError::UnknownStyle);
        CHECK(generator.generate({"scifi","unknown",3}).error==RuntimeBuildingError::UnknownFunction);
        CHECK(generator.generate({"scifi","factory",3,{10,0,12}}).error==RuntimeBuildingError::InvalidInput);
        CHECK(generator.generate({"scifi","factory",3,{}, {},4}).error==RuntimeBuildingError::InvalidInput);

        for(const auto* kind:{"barracks","command","factory","depot"}) {
            for(const uint64_t seed:{3ull,7ull,42ull}) {
                RuntimeBuildingRequest request;
                request.style="scifi";
                request.function=kind;
                request.seed=seed;
                // Unsized generation is the backwards-compatible fallback,
                // not a substitute for failed explicit dimensions.
                auto result=generator.generate(request);
                auto repeat=generator.generate(request);
                CHECK(result && repeat);
                CHECK(!result.building.mesh.parts.empty());
                CHECK(!result.building.mesh.materials.empty());
                CHECK(!result.building.boxes.empty());
                CHECK(result.building.mesh.vertices.size()==repeat.building.mesh.vertices.size());
                CHECK(result.building.mesh.triangles()==repeat.building.mesh.triangles());
                CHECK(result.building.gates==repeat.building.gates);
                CHECK(result.building.boxes==repeat.building.boxes);
                for(size_t n=0;n<result.building.mesh.vertices.size();++n)
                    for(int c=0;c<3;++c) {
                        CHECK(result.building.mesh.vertices[n].pos[c]==repeat.building.mesh.vertices[n].pos[c]);
                        CHECK(result.building.mesh.vertices[n].nrm[c]==repeat.building.mesh.vertices[n].nrm[c]);
                    }
            }
        }

        RuntimeBuildingRequest sized;
        sized.style="scifi";
        sized.function="factory";
        sized.seed=7;
        sized.size={24,12,18};
        auto first=generator.generate(sized);
        auto second=generator.generate(sized);
        CHECK(first.error==second.error);
        CHECK(first.message==second.message);
        CHECK(first.building.mesh.triangles()==second.building.mesh.triangles());
        // Sized runtime generation may legitimately reject an impossible
        // envelope, but MUST report failure instead of returning a fake mesh.
        CHECK(first || (!first && first.error==RuntimeBuildingError::FitFailed));
        if(first) {
            CHECK(std::abs(first.building.mesh.boundsMin[0])<0.01f);
            CHECK(std::abs(first.building.mesh.boundsMax[0]-24.f)<0.01f);
            CHECK(std::abs(first.building.mesh.boundsMax[1]-12.f)<0.01f);
            CHECK(std::abs(first.building.mesh.boundsMax[2]-18.f)<0.01f);
        } else {
            CHECK(!first.message.empty());
        }
        generator.clear();
        CHECK(generator.styleCount()==0);
        CHECK(generator.generate(sized).error==RuntimeBuildingError::UnknownStyle);
        // Results own their CPU vertices after registry data is released.
        if(first)CHECK(!first.building.mesh.vertices.empty());
        std::puts("building runtime API PASS");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"building runtime API FAIL: %s\n",e.what());
        return 1;
    }
}

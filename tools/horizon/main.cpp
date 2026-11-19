// AIVERSE Horizon static-content builder. Host tool only; never linked
// into the simulation/gameplay ECS or the graphics backend.
#include "engine/world/TerraWeaveHorizonBatch.h"
#include "engine/world/TerraWeaveHorizonManifest.h"
#include "engine/data/Json.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using namespace aiverse;

namespace {
int32_t integer(const char* s) {
    size_t used=0;
    const std::string value=s;
    const long long n=std::stoll(value,&used,10);
    if(used!=value.size() || n<std::numeric_limits<int32_t>::min() ||
       n>std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("expected a signed 32-bit region coordinate");
    return static_cast<int32_t>(n);
}
size_t count(const char* s) {
    size_t used=0;
    const std::string value=s;
    if(value.empty()||value.front()=='-')
        throw std::invalid_argument("expected positive integer");
    const unsigned long long n=std::stoull(value,&used,10);
    if(used!=value.size()||n==0||n>100000)
        throw std::invalid_argument("region count must be 1..100000");
    return static_cast<size_t>(n);
}
void help() {
    std::cout <<
        "aiverse_horizon_bake --manifest world.json --out <archive-directory> "
        "--cells minX minZ maxXExclusive maxZExclusive [--force] "
        "[--max-regions N] [--quiet]\n"
        "Region keys use the Horizon middleTileCm grid. Output files are "
        "<out>/<cellX>_<cellZ>.twh.\n"
        "Generate into the project's content folder matching the "
        "terraweaveHorizon.archivePrefix manifest option.\n"
        "This tool bakes procedural terrain/biomes only. Authored collision/"
        "road snapshot baking needs a native provider and nonzero content hash.\n";
}
bool readBytes(const fs::path& file,std::vector<uint8_t>& bytes) {
    std::error_code ec;
    const uintmax_t size=fs::file_size(file,ec);
    if(ec||size>terraweave::horizon_archive_detail::kMaxBytes)return false;
    std::ifstream f(file,std::ios::binary);
    if(!f)return false;
    bytes.assign(std::istreambuf_iterator<char>(f),{});
    return f.good()||f.eof();
}
void writeBytes(const fs::path& file,const std::vector<uint8_t>& bytes) {
    // Create the parent folder once, and replace through a staging filename.
    // This avoids publishing truncated .twh blobs if the build is interrupted.
    const fs::path temp=fs::path(file.string()+".partial");
    {
        std::ofstream f(temp,std::ios::binary|std::ios::trunc);
        if(!f)throw std::runtime_error("cannot write Horizon staging file: "+temp.string());
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        f.flush();
        if(!f)throw std::runtime_error("Horizon archive write failed: "+temp.string());
    }
    std::error_code ec;
    fs::rename(temp,file,ec);
    if(ec) {
        fs::remove(temp);
        throw std::runtime_error("cannot publish Horizon archive: "+
                                 file.string()+": "+ec.message());
    }
}
}
int main(int argc,char** argv) {
    try {
        std::string manifestFile,outDir;
        bool cells=false,force=false,quiet=false;
        size_t maximum=4096;
        terraweave::HorizonBatchBounds bounds;
        for(int i=1;i<argc;++i) {
            const std::string key=argv[i];
            if(key=="--help"||key=="-h"){help();return 0;}
            if(key=="--manifest" && i+1<argc)manifestFile=argv[++i];
            else if(key=="--out" && i+1<argc)outDir=argv[++i];
            else if(key=="--cells" && i+4<argc) {
                bounds.minCellX=integer(argv[++i]);
                bounds.minCellZ=integer(argv[++i]);
                bounds.maxCellX=integer(argv[++i]);
                bounds.maxCellZ=integer(argv[++i]);
                cells=true;
            } else if(key=="--max-regions" && i+1<argc)maximum=count(argv[++i]);
            else if(key=="--force")force=true;
            else if(key=="--quiet")quiet=true;
            else throw std::invalid_argument("unknown/incomplete argument: "+key);
        }
        if(manifestFile.empty()||outDir.empty()||!cells) {
            help();return 2;
        }
        Json manifest=Json::parseFile(manifestFile);
        // The WorldScene adapter inherits forest terrain seeds/biomes when
        // both options are present. Avoid constructing all near-tree models
        // merely to read its manifest during a mass asset bake.
        if(manifest["terraweaveForest"].isObject() &&
           manifest["terraweaveHorizon"].isObject()) {
            Json near=manifest["terraweaveForest"];
            Json far=manifest["terraweaveHorizon"];
            for(const char* key:{"terrainSeed","baseHeightCm","reliefCm",
                                  "basePeriodCm","octaves","biomeSeed",
                                  "seaLevelCm","closeForestEndCm"}) {
                if(!far.has(key)&&near.has(key))far.set(key,near[key]);
            }
            if(!far.has("closeForestEndCm"))
                far.set("closeForestEndCm",
                    Json::number(near.getInt("cullRangeCm",24000)));
            manifest.set("terraweaveHorizon",std::move(far));
        }
        // The read-only archive reader is intentionally empty here: the
        // build tool uses its own destination folder, not runtime assets.
        auto scene=world::horizonSceneFromManifest(manifest,nullptr,
            [](const std::string&,std::string&){return false;});
        if(!scene)
            throw std::invalid_argument("manifest has no terraweaveHorizon block");
        const fs::path directory=fs::path(outDir);
        std::error_code ec;
        fs::create_directories(directory,ec);
        if(ec)throw std::runtime_error("cannot create Horizon output directory: "+
                                        ec.message());
        terraweave::HorizonBatchOptions options;
        options.regions=bounds;
        options.maxRegions=maximum;
        options.skipExisting=!force;
        terraweave::HorizonBatchStorage io;
        const auto asset=[directory](int32_t x,int32_t z) {
            return directory/(std::to_string(x)+"_"+std::to_string(z)+".twh");
        };
        io.read=[asset](int32_t x,int32_t z,std::vector<uint8_t>& bytes) {
            return readBytes(asset(x,z),bytes);
        };
        io.write=[asset](int32_t x,int32_t z,
                         const std::vector<uint8_t>& bytes) {
            writeBytes(asset(x,z),bytes);
        };
        io.continueBake=[quiet](const terraweave::HorizonBatchProgress& p) {
            if(!quiet && p.visited && p.visited%16==0)
                std::cout<<"Horizon "<<p.visited<<"/"<<p.total
                         <<" generated="<<p.generated<<" reused="<<p.skipped<<"\n";
            return true;
        };
        const auto result=terraweave::bakeHorizonBatch(
            scene->terrainSettings(),scene->biomeSettings(),scene->settings(),
            options,io);
        std::cout<<"Horizon complete: "<<result.visited<<"/"<<result.total
                 <<", generated="<<result.generated
                 <<", reused="<<result.skipped
                 <<", bytes="<<result.bytesWritten<<"\n";
        return result.canceled?3:0;
    } catch(const std::exception& e) {
        std::cerr<<"aiverse_horizon_bake: "<<e.what()<<"\n";
        return 1;
    }
}

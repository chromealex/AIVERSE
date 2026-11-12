#include "ContentReader.h"
#include "engine/pack/Pack.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace aiverse::platform {
ContentReader makeContentReader(const std::string& projectRoot,size_t limit) {
    const auto root=std::filesystem::absolute(projectRoot).lexically_normal();
    auto pack=std::shared_ptr<aiverse::pack::Pack>(aiverse::pack::Pack::openFile((root/"content.rpak").string()));
    if(pack){for(size_t i=0;i<pack->chunks().size();++i){const auto& info=pack->chunks()[i];
        if(info.file.find("..")!=std::string::npos||std::filesystem::path(info.file).is_absolute())continue;
        auto source=aiverse::pack::openFileSource((root/info.file).string());if(source)pack->attachChunk(i+1,std::move(source));}}
    return [root,limit,pack](const std::string& name,std::string& out){
        if(name.empty()||name.find("..")!=std::string::npos||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos||name.find('\0')!=std::string::npos||std::filesystem::path(name).is_absolute())return false;
        if(pack){if(const auto* e=pack->find(name)){if(e->rawSize>limit)return false;return pack->read(*e,out,true);}}
        std::ifstream input(root/name,std::ios::binary|std::ios::ate);if(!input)return false;
        const auto end=input.tellg();if(end<0||uint64_t(end)>limit)return false;
        out.resize(size_t(end));input.seekg(0);return bool(input.read(out.data(),std::streamsize(out.size())));
    };
}
}

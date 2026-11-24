// Trusted project root supplied by host; untrusted resource path validated.
#pragma once
#include "SpectraSdfBinary.h"
#include "SpectraVolumetricParticleGraph.h"
#include <filesystem>
#include <fstream>
namespace aiverse::fx {
inline SdfResourceReader projectSdfReader(std::filesystem::path root){
    return [root=std::move(root)](const std::string& path,std::string& out){
        namespace fs=std::filesystem;
        if(root.empty()||(!validSdfResourcePath(path)&&!spectra::validParticleVolumeTexturePath(path)))return false;
        std::error_code ec;
        auto file=root;
        for(const auto& component:fs::path(path)){
            file/=component;
            const auto status=fs::symlink_status(file,ec);
            if(ec||fs::is_symlink(status))return false;
        }
        if(!fs::is_regular_file(file,ec)||ec)return false;
        const auto size=fs::file_size(file,ec);
        if(ec||size>kSdfBinaryMaxBytes)return false;
        std::ifstream input(file,std::ios::binary);
        if(!input)return false;
        std::string bytes(size_t(size),'\0');
        if(size&&!input.read(bytes.data(),std::streamsize(size)))return false;
        if(input.peek()!=std::char_traits<char>::eof()||input.bad())return false;
        out.swap(bytes);return true;
    };
}
}

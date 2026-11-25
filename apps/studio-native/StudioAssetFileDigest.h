// Streaming regular-file identity shared by asset transfers and recovery.
#pragma once
#include "engine/pack/Sha256.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
namespace aiverse::studio {
inline std::string studioAssetFileDigest(const std::filesystem::path& path){
    namespace fs=std::filesystem;
    if(!fs::is_regular_file(fs::symlink_status(path)))throw std::runtime_error("Invalid asset payload");
    const auto size=fs::file_size(path);const auto stamp=fs::last_write_time(path);
    pack::Sha256 hash;
    const auto field=[&](const std::string& value){
        const auto prefix=std::to_string(value.size())+":";
        hash.update(prefix.data(),prefix.size());hash.update(value.data(),value.size());
    };
    field("file-v1");field(std::to_string(size));
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Cannot read asset payload");
    char buffer[65536];std::uintmax_t read=0;
    while(in.read(buffer,sizeof(buffer))||in.gcount()){
        const auto n=static_cast<size_t>(in.gcount());hash.update(buffer,n);read+=n;
    }
    if(in.bad()||!in.eof()||read!=size||fs::file_size(path)!=size||fs::last_write_time(path)!=stamp)
        throw std::runtime_error("Asset changed while hashing");
    const auto result=hash.finish();return pack::toHex(result.data(),result.size());
}
}

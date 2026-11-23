#pragma once
#include "SpectraCheckpointFile.h"
#include <filesystem>
#include <fstream>
namespace aiverse::spectra {
// Trusted native caller selects path; never expose unrestricted paths via IPC.
inline bool readRestoreCheckpointFile(const std::filesystem::path& path,std::vector<uint8_t>& out){
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)return false;
    const auto length=file.tellg();
    if(length<=0||length>std::streamoff(kRestoreCheckpointMaxBytes))return false;
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))return false;
    if(file.peek()!=std::char_traits<char>::eof())return false;
    out.swap(bytes);return true;
}
inline bool writeRestoreCheckpointFile(const std::filesystem::path& path,const std::vector<uint8_t>& bytes){
    if(bytes.empty()||bytes.size()>kRestoreCheckpointMaxBytes)return false;
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    if(!file)return false;
    file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    file.close();return bool(file);
}
}

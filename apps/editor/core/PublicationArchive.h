#pragma once
#include <filesystem>
#include <stdexcept>
namespace aiverse::editor {
inline void archivePublicationJournal(const std::filesystem::path& active){
    namespace fs=std::filesystem;
    if(fs::is_symlink(active)||!fs::is_directory(active))throw std::runtime_error("Invalid publication journal");
    const auto archive=active.parent_path().parent_path()/(active.parent_path().filename().string()+"-completed");
    if(fs::is_symlink(archive))throw std::runtime_error("Unsafe publication archive");
    fs::create_directories(archive);
    // Exclusive container prevents overwriting old evidence even with same name.
    for(unsigned i=0;i<10000;++i){
        const auto slot=archive/(active.filename().string()+"-"+std::to_string(i));
        if(!fs::create_directory(slot))continue;
        try{fs::rename(active,slot/"journal");return;}
        catch(...){std::error_code ec;fs::remove(slot,ec);throw;}
    }
    throw std::runtime_error("Publication archive exhausted");
}
}

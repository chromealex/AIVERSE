// Transactional file I/O for project-authored Enhanced Input assets.
// Host-only: no device state or filesystem work inside deterministic ECS systems.
#pragma once
#include "EnhancedInputConfig.h"
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace aiverse::platform::enhanced_config {
namespace detail {
inline bool inputFileError(std::string* error,const std::string& description) {
    if(error)*error=description;
    return false;
}
inline bool saveValidatedJsonFile(const std::filesystem::path& file,const Json& json,std::string* error) {
    std::error_code ec;
    if(!file.parent_path().empty()){
        std::filesystem::create_directories(file.parent_path(),ec);
        if(ec)return inputFileError(error,"create directory: "+ec.message());
    }
    const std::filesystem::path temp=file.string()+".tmp";
    const std::filesystem::path backup=file.string()+".bak";
    {
        std::ofstream stream(temp,std::ios::binary|std::ios::trunc);
        if(!stream)return inputFileError(error,"cannot open temporary file");
        stream<<json.dump(2)<<'\n';
        stream.flush();
        if(!stream)return inputFileError(error,"cannot write temporary file");
        stream.close();
        if(!stream)return inputFileError(error,"cannot close temporary file");
    }
    // POSIX replaces the destination atomically when it exists.
    std::filesystem::rename(temp,file,ec);
    if(!ec)return true;
    // Windows rename cannot replace an existing file. Move the original aside
    // before renaming, restoring it if the new file fails to commit.
    ec.clear();
    const bool original=std::filesystem::exists(file,ec);
    if(ec)return inputFileError(error,"check destination: "+ec.message());
    if(!original)return inputFileError(error,"cannot commit temporary file");
    std::filesystem::remove(backup,ec);
    if(ec)return inputFileError(error,"remove stale backup: "+ec.message());
    std::filesystem::rename(file,backup,ec);
    if(ec)return inputFileError(error,"backup original: "+ec.message());
    std::filesystem::rename(temp,file,ec);
    if(ec){
        const auto commitError=ec;
        std::error_code restoreError;
        std::filesystem::rename(backup,file,restoreError);
        return inputFileError(error,std::string("commit: ")+commitError.message()+
            (restoreError?" (restore failed: "+restoreError.message()+")":""));
    }
    std::filesystem::remove(backup,ec); // a stale backup is harmless after a successful commit
    return true;
}
} // namespace detail

inline bool loadAssetFile(const std::filesystem::path& file,Asset& out,std::string* error=nullptr) {
    try {
        Asset parsed;
        if(!readAsset(Json::parseFile(file.string()),parsed,error))return false;
        out=std::move(parsed);
        return true;
    }catch(const std::exception& e){
        return detail::inputFileError(error,e.what());
    }
}
inline bool saveAssetFile(const std::filesystem::path& file,const Asset& asset,std::string* error=nullptr) {
    try {
        const Json serialized=writeAsset(asset);
        Asset checked;
        if(!readAsset(serialized,checked,error))return false;
        return detail::saveValidatedJsonFile(file,serialized,error);
    }catch(const std::exception& e){
        return detail::inputFileError(error,e.what());
    }
}
} // namespace aiverse::platform::enhanced_config

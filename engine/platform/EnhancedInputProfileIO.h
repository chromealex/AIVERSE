// Validated, crash-conscious user-profile persistence for Enhanced Input.
// Host/presentation only. Never call from deterministic ECS systems.
#pragma once
#include "EnhancedInputFileIO.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace aiverse::platform::enhanced_config {
namespace detail {
inline bool ioError(std::string* error,const char* prefix,const std::error_code& ec) {
    if(error)*error=std::string(prefix)+": "+ec.message();
    return false;
}
} // namespace detail

// A malformed or missing file never modifies the caller's current profile.
inline bool loadProfileFile(const std::filesystem::path& file,Profile& out,std::string* error=nullptr) {
    std::ifstream stream(file,std::ios::binary);
    if(!stream){if(error)*error="could not open input profile";return false;}
    const std::string text((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    if(stream.bad()){if(error)*error="could not read input profile";return false;}
    try {
        Profile parsed;
        if(!readProfile(Json::parse(text),parsed,error))return false;
        out=std::move(parsed);
        return true;
    } catch(const std::exception& e) {
        if(error)*error=e.what();
        return false;
    }
}

// Profile serialization shares the same validated atomic/backup file writer as project assets.
inline bool saveProfileFile(const std::filesystem::path& file,const Profile& profile,std::string* error=nullptr) {
    try {
        const Json serialized=writeProfile(profile);
        Profile checked;
        if(!readProfile(serialized,checked,error))return false;
        return detail::saveValidatedJsonFile(file,serialized,error);
    } catch(const std::exception& e) {
        return detail::inputFileError(error,e.what());
    }
}
} // namespace aiverse::platform::enhanced_config

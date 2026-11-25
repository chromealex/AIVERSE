#pragma once
#include "engine/pack/Sha256.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace aiverse::editor {
inline std::string publicationFileSha256(const std::filesystem::path& path) {
    namespace fs=std::filesystem;
    constexpr size_t limit=32*1024*1024;
    if(fs::is_symlink(path)||!fs::is_regular_file(path)||fs::file_size(path)>limit)
        throw std::runtime_error("Invalid publication file");
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot open publication file");
    pack::Sha256 hash;std::array<char,16384> buffer{};size_t total=0;
    while(stream){
        stream.read(buffer.data(),buffer.size());const auto n=stream.gcount();
        total+=size_t(n);if(total>limit)throw std::runtime_error("Publication file exceeds limit");
        if(n)hash.update(buffer.data(),size_t(n));
    }
    if(stream.bad()||!stream.eof())throw std::runtime_error("Cannot read publication file");
    const auto bytes=hash.finish();return pack::toHex(bytes.data(),bytes.size());
}
} // namespace aiverse::editor

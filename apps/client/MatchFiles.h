#pragma once
#include "engine/sim/Replay.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

namespace aiverse::client {
struct MatchFile {
    std::string map, setup, simulation;
    uint64_t seed = 0, contentHash = 0;
    std::vector<std::pair<uint8_t,std::string>> bots;
    Replay replay;
    bool nativeReplay = false;
};

class MatchFiles {
public:
    static bool validName(const std::string& name) {
        return !name.empty() && name.size() <= 64 && name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
    }
    static std::vector<std::string> list(const std::filesystem::path& root, bool replay) {
        std::vector<std::string> names;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(root,ec), end; !ec && it != end; it.increment(ec)) {
            const auto& path = it->path();
            const auto name = path.stem().string();
            if (!it->is_regular_file(ec) || !validName(name)) continue;
            if (path.extension() == suffix(replay)) names.push_back(name);
            else if (replay && path.extension() == ".rpl") names.push_back(name+".rpl");
        }
        std::sort(names.begin(),names.end());
        return names;
    }
    static bool write(const std::filesystem::path& root,const std::string& name,bool replay,
                      const MatchFile& data,std::string& error) {
        if (!validName(name)) { error="Invalid slot name"; return false; }
        ByteWriter w;
        w.u32(magic); w.u16(1); w.u8(replay);
        w.str(data.map); w.str(data.setup); w.u64(data.seed); w.u64(data.contentHash);
        if (replay) w.str(data.replay.encode());
        else {
            if (data.simulation.empty() || data.bots.size()>255) { error="Incomplete match snapshot"; return false; }
            w.str(data.simulation); w.u8(uint8_t(data.bots.size()));
            for (const auto& [player,state] : data.bots) { w.u8(player); w.str(state); }
        }
        if (w.buf.size() > maxBytes) { error="Match file exceeds size limit"; return false; }
        std::error_code ec;
        std::filesystem::create_directories(root,ec);
        if (ec) { error=ec.message(); return false; }
        const auto target = root/(name+suffix(replay));
        auto temp = target;
        temp += "." + std::to_string(std::random_device{}()) + ".tmp";
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(w.buf.data()), std::streamsize(w.buf.size()));
        out.close();
        if (out) std::filesystem::rename(temp,target,ec);
        if (!out || ec) {
            error = ec ? ec.message() : "Could not write match file";
            std::filesystem::remove(temp,ec);
            return false;
        }
        return true;
    }
    static bool read(const std::filesystem::path& root,const std::string& name,bool replay,
                     MatchFile& result,std::string& error) {
        const bool native = replay && name.size()>4 && name.substr(name.size()-4)==".rpl";
        const auto stem = native ? name.substr(0,name.size()-4) : name;
        if (!validName(stem)) { error="Invalid slot name"; return false; }
        const auto path = root/(native ? name : name+suffix(replay));
        std::error_code ec;
        const auto size = std::filesystem::file_size(path,ec);
        if (ec || size > maxBytes) { error=ec ? ec.message() : "Match file exceeds size limit"; return false; }
        std::ifstream in(path,std::ios::binary);
        std::string bytes(size_t(size),'\0');
        if (!in.read(bytes.data(),std::streamsize(size))) { error="Could not read match file"; return false; }
        if (native) {
            MatchFile data;
            if (!data.replay.decode(bytes,&error)) return false;
            data.nativeReplay = true;
            data.map = data.replay.mapPath; data.seed = data.replay.seed; data.contentHash = data.replay.dataHash;
            data.setup = R"({"spectator":true,"human":1})";
            result = std::move(data);
            return true;
        }
        ByteReader r(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size());
        if (r.u32()!=magic || r.u16()!=1 || r.u8()!=uint8_t(replay)) { error="Unsupported match file"; return false; }
        MatchFile data;
        data.map=r.str(); data.setup=r.str(); data.seed=r.u64(); data.contentHash=r.u64();
        if (replay) {
            if (!data.replay.decode(r.str(),&error)) return false;
        } else {
            data.simulation=r.str();
            const auto count=r.u8();
            for (unsigned i=0; i<count && r.ok(); ++i) {
                const auto player=r.u8();
                data.bots.emplace_back(player,r.str());
            }
        }
        if (!r.ok() || !r.atEnd() || data.map.empty()) { error="Invalid match file"; return false; }
        result=std::move(data);
        return true;
    }
private:
    static constexpr uint32_t magic=0x464d5641;
    static constexpr size_t maxBytes=256u<<20;
    static const char* suffix(bool replay) { return replay ? ".avreplay" : ".avsave"; }
};
} // namespace aiverse::client

// VIVID project/presentation settings. Quality is NOT part of HumanDNA,
// gameplay ECS, network saves, or the Renderer3D backend implementation.
// Values are resolved before preparing a Model and retained by the host.
#pragma once
#include "HumanMaterials.h"
#include "HumanGroom.h"
#include "engine/data/Json.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace aiverse::vivid {

struct VividPresentationSettings {
    static constexpr std::int64_t kVersion = 1;
    HumanMaterialQuality materialQuality = HumanMaterialQuality::High;
    GroomQuality groomQuality = GroomQuality::High;

    static const char* name(HumanMaterialQuality quality) noexcept {
        switch(quality) {
            case HumanMaterialQuality::Low: return "low";
            case HumanMaterialQuality::Medium: return "medium";
            case HumanMaterialQuality::High: return "high";
        }
        return "invalid";
    }
    static const char* name(GroomQuality quality) noexcept {
        switch(quality) {
            case GroomQuality::Low: return "low";
            case GroomQuality::Medium: return "medium";
            case GroomQuality::High: return "high";
        }
        return "invalid";
    }
    static bool parseMaterialQuality(const std::string& value,
                                     HumanMaterialQuality& output) noexcept {
        if(value=="low") output=HumanMaterialQuality::Low;
        else if(value=="medium") output=HumanMaterialQuality::Medium;
        else if(value=="high") output=HumanMaterialQuality::High;
        else return false;
        return true;
    }
    static bool parseGroomQuality(const std::string& value,
                                  GroomQuality& output) noexcept {
        if(value=="low") output=GroomQuality::Low;
        else if(value=="medium") output=GroomQuality::Medium;
        else if(value=="high") output=GroomQuality::High;
        else return false;
        return true;
    }
    bool valid() const noexcept {
        return name(materialQuality)[0]!='i' && name(groomQuality)[0]!='i';
    }
    Json toJson() const {
        if(!valid()) throw std::invalid_argument("VIVID invalid presentation quality");
        auto data=Json::object();
        data.set("version",Json::number(kVersion));
        data.set("materialQuality",Json::string(name(materialQuality)));
        data.set("groomQuality",Json::string(name(groomQuality)));
        return data;
    }
    static VividPresentationSettings fromJson(const Json& data) {
        if(!data.isObject() || data.size()!=3 ||
           !data["version"].isNumber() ||
           data["version"].integer(-1)!=kVersion ||
           !data["materialQuality"].isString() ||
           !data["groomQuality"].isString())
            throw std::invalid_argument("VIVID invalid presentation settings schema");
        for(const auto& [key,value]:data.members()) {
            (void)value;
            if(key!="version" && key!="materialQuality" && key!="groomQuality")
                throw std::invalid_argument("VIVID unknown presentation setting");
        }
        VividPresentationSettings result;
        if(!parseMaterialQuality(data["materialQuality"].str(),result.materialQuality) ||
           !parseGroomQuality(data["groomQuality"].str(),result.groomQuality))
            throw std::invalid_argument("VIVID unknown presentation quality");
        return result;
    }
    static VividPresentationSettings loadFile(const std::string& path) {
        if(path.empty()) throw std::invalid_argument("VIVID empty presentation path");
        std::error_code ec;
        const auto bytes=std::filesystem::file_size(path,ec);
        if(ec || bytes>16384)
            throw std::runtime_error("VIVID missing or oversized presentation settings");
        return fromJson(Json::parseFile(path));
    }
    // Publish only after a fully validated write. Leave last-good file alone
    // if writing fails. Caller controls the path and the host's file policy.
    bool saveFile(const std::string& path,std::string* error=nullptr) const {
        try {
            if(path.empty())throw std::invalid_argument("VIVID empty presentation path");
            const auto text=toJson().dump(2);
            const auto temporary=path+".vivid.tmp";
            {
                std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
                if(!out)throw std::runtime_error("VIVID presentation temp open failed");
                out.write(text.data(),std::streamsize(text.size()));
                out.flush();
                if(!out)throw std::runtime_error("VIVID presentation write failed");
            }
            std::error_code ec;
            std::filesystem::rename(temporary,path,ec);
            if(ec)throw std::runtime_error(
                "VIVID presentation atomic replacement failed: "+ec.message());
            if(error)error->clear();
            return true;
        } catch(const std::exception& e) {
            if(error)*error=e.what();
            return false;
        }
    }
};

} // namespace aiverse::vivid

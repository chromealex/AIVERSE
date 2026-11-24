#pragma once
// Create-only building material publication intent. Recovery is deliberately
// read-only: a second process may still own the publication, and other assets
// may reference the generated files. Never infer permission to delete them.
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioMaterialValidation.h"
#include "engine/data/Json.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace aiverse::studio {
class StudioMaterialPublicationJournal {
    std::filesystem::path dir_;
    static std::filesystem::path root(const std::filesystem::path& project) {
        namespace fs=std::filesystem;
        const auto cache=project/".aiverse-cache", journals=cache/"material-publications";
        if(fs::is_symlink(cache)||fs::is_symlink(journals))throw std::runtime_error("Unsafe publication journal path");
        return journals;
    }
public:
    StudioMaterialPublicationJournal(const std::filesystem::path& project,const std::string& name) {
        namespace fs=std::filesystem;
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid publication journal name");
        const auto parent=root(project);fs::create_directories(parent);dir_=parent/name;
        if(!fs::create_directory(dir_))throw std::runtime_error("Publication already pending; inspect recovery journal");
        try { write("intent.json",Json::parse(R"({"version":1,"kind":"building-material"})")); }
        catch(...) { finish();throw; }
    }
    void candidate(const Json& material) { write("candidate.json",material); }
    void finish() noexcept { std::error_code ec;std::filesystem::remove_all(dir_,ec); }
    static Json inspect(const std::filesystem::path& project) {
        namespace fs=std::filesystem;
        Json rows=Json::array();const auto parent=root(project);
        if(!fs::exists(parent))return rows;
        const auto libraryFile=project/"materials.json";
        Json library=Json::object();
        if(fs::exists(libraryFile)) {
            if(fs::is_symlink(libraryFile)||!fs::is_regular_file(libraryFile)||fs::file_size(libraryFile)>16*1024*1024)
                throw std::runtime_error("Unsafe material library for publication inspection");
            library=Json::parseFile(libraryFile.string())["materials"];
        }
        for(const auto& entry:fs::directory_iterator(parent)) {
            const auto name=entry.path().filename().string();
            if(entry.is_symlink()||!entry.is_directory()||!validStudioMaterialName(name))continue;
            Json row=Json::object();row.set("name",Json::string(name));
            const auto file=entry.path()/"candidate.json";
            std::string status="incomplete_or_active";
            if(fs::is_regular_file(file)&&!fs::is_symlink(file)&&fs::file_size(file)<=65536){
                const auto candidate=Json::parseFile(file.string());
                if(library[name].isObject())status=library[name]==candidate?"material_committed":"material_conflict";
            }
            row.set("status",Json::string(status));rows.push(std::move(row));
        }
        return rows;
    }
private:
    void write(const char* name,const Json& value) {
        namespace fs=std::filesystem;
        const auto temp=dir_/"pending.tmp";
        {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);stream<<value.dump(2)<<"\n";stream.close();
         if(!stream)throw std::runtime_error("Cannot write publication journal");}
        fs::rename(temp,dir_/name);
    }
};
} // namespace aiverse::studio

#pragma once
// Owner-side publication intent. Inspection never mutates or removes assets.
#include "engine/data/Json.h"
#include "PublicationIntegrity.h"
#include "PublicationArchive.h"
#include "ProjectAssetWriteLock.h"
#include <set>
#include "engine/render/graph/TextureGraph.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace aiverse::editor {
class TexturePublicationJournal {
    using Path=std::filesystem::path;
    Path dir_;
    static Path root(const Path& project){
        namespace fs=std::filesystem;
        const auto cache=project/".aiverse-cache",parent=cache/"texture-publications";
        if(fs::is_symlink(cache)||fs::is_symlink(parent))throw std::runtime_error("Unsafe texture journal path");
        return parent;
    }
    static bool validOutput(const std::string& output){
        try{(void)render::texture_graph::outputKind(output);return true;}catch(...){return false;}
    }
public:
    TexturePublicationJournal(const Path& project,const std::string& name,const std::string& output){
        namespace fs=std::filesystem;
        if(!render::texture_graph::safeId(name)||(!output.empty()&&!validOutput(output)))throw std::runtime_error("Invalid texture publication identity");
        const auto parent=root(project);fs::create_directories(parent);
        dir_=parent/(output.empty()?"set-"+name:"image-"+name+"."+output);
        if(!fs::create_directory(dir_))throw std::runtime_error("Texture publication pending; inspect journal");
        try{
            Json j=Json::object();j.set("version",Json::number(int64_t(2)));j.set("name",Json::string(name));j.set("output",Json::string(output));
            std::ofstream stream(dir_/"intent.tmp",std::ios::binary);stream<<j.dump(2)<<"\n";stream.close();
            if(!stream)throw std::runtime_error("Cannot write texture publication intent");
            fs::rename(dir_/"intent.tmp",dir_/"intent.json");
        }catch(...){finish();throw;}
    }
    Path stage() const {return dir_/"stage";}
    void evidence(const Json& files){
        namespace fs=std::filesystem;
        std::ofstream stream(dir_/"files.tmp",std::ios::binary);stream<<files.dump(2)<<"\n";stream.close();
        if(!stream)throw std::runtime_error("Cannot write publication evidence");
        fs::rename(dir_/"files.tmp",dir_/"files.json");
    }
    void finish() noexcept {std::error_code ec;std::filesystem::remove_all(dir_,ec);}
    static void finalize(const Path& project,const std::string& id){
        ProjectAssetWriteLock lock(project);
        for(const auto& row:inspect(project).items()){
            if(row.getStr("id")!=id)continue;
            if(row.getStr("status")!="destination_matches_evidence"||row.getBool("stagePresent"))
                throw std::runtime_error("Texture publication not verified or staging remains");
            archivePublicationJournal(root(project)/id);return;
        }
        throw std::runtime_error("Unknown texture publication");
    }
    // Only v2 intents own a stage inside their journal. Legacy content stages
    // may already be indexed or referenced, and are never moved by this API.
    static std::string quarantine(const Path& project,const std::string& id){
        namespace fs=std::filesystem;
        ProjectAssetWriteLock lock(project);
        const auto parent=root(project);
        for(const auto& row:inspect(project).items()){
            if(row.getStr("id")!=id)continue;
            if(!row.getBool("quarantinable"))throw std::runtime_error("Publication is not eligible for isolated quarantine");
            const auto active=parent/id,archive=parent.parent_path()/"texture-publications-quarantine";
            if(fs::is_symlink(archive))throw std::runtime_error("Unsafe quarantine directory");
            fs::create_directories(archive);
            for(unsigned i=0;i<10000;++i){
                const auto slot=archive/(id+"-"+std::to_string(i));
                if(!fs::create_directory(slot))continue;
                try{fs::rename(active,slot/"journal");return slot.filename().string();}
                catch(...){std::error_code ec;fs::remove(slot,ec);throw;}
            }
            throw std::runtime_error("Texture quarantine exhausted");
        }
        throw std::runtime_error("Unknown texture publication");
    }
    static Json inspect(const Path& project){
        namespace fs=std::filesystem;
        Json rows=Json::array();const auto parent=root(project);
        if(!fs::exists(parent))return rows;
        const auto textures=project/"content"/"textures";
        if(fs::is_symlink(project/"content")||fs::is_symlink(textures))throw std::runtime_error("Unsafe texture publication destination");
        for(const auto& entry:fs::directory_iterator(parent)){
            if(rows.size()>=256)break;
            if(entry.is_symlink()||!entry.is_directory())continue;
            const auto file=entry.path()/"intent.json";
            Json row=Json::object();row.set("id",Json::string(entry.path().filename().string()));
            row.set("status",Json::string("unreadable_or_active"));
            if(!fs::is_symlink(file)&&fs::is_regular_file(file)&&fs::file_size(file)<=4096)try{
                const auto j=Json::parseFile(file.string());const auto name=j.getStr("name"),output=j.getStr("output");
                const bool set=output.empty();
                if((j.getInt("version")==1||j.getInt("version")==2)&&render::texture_graph::safeId(name)&&(set||validOutput(output))&&entry.path().filename()==(set?"set-"+name:"image-"+name+"."+output)){
                    const auto dest=textures/(set?name:name+"_"+output+".png");
                    const bool isolated=j.getInt("version")==2;
                    const auto stage=isolated?entry.path()/"stage":(set?textures/("."+name+".stage"):Path(dest.string()+".stage"));
                    const bool published=!fs::is_symlink(dest)&&(set?fs::is_directory(dest):fs::is_regular_file(dest));
                    row.set("destination",Json::string(dest.lexically_relative(project).generic_string()));
                    row.set("stagePresent",Json::fromBool(fs::exists(stage)||fs::is_symlink(stage)));
                    row.set("quarantinable",Json::fromBool(isolated&&!fs::exists(dest)&&!fs::is_symlink(dest)&&!fs::is_symlink(stage)));
                    row.set("status",Json::string(published?"destination_present_unverified":"incomplete_or_active"));
                    const auto evidence=entry.path()/"files.json";
                    if(published&&!fs::is_symlink(evidence)&&fs::is_regular_file(evidence)&&fs::file_size(evidence)<=16384){
                        const auto expected=Json::parseFile(evidence.string());
                        bool valid=expected.isArray()&&expected.size()>0&&expected.size()<=10;
                        std::set<std::string> seen;
                        if(valid)for(const auto& item:expected.items()){
                            const auto fileName=item.getStr("file"),digest=item.getStr("sha256");
                            bool allowed=!set?fileName=="payload":fileName=="texture-set.json";
                            if(set&&!allowed&&fileName.size()>4&&fileName.ends_with(".png"))
                                allowed=validOutput(fileName.substr(0,fileName.size()-4));
                            if(!allowed||!seen.insert(fileName).second||digest.size()!=64){valid=false;break;}
                            try{if(publicationFileSha256(set?dest/fileName:dest)!=digest)valid=false;}catch(...){valid=false;}
                        }
                        if(set){
                            if(!seen.count("texture-set.json")||seen.size()<2)valid=false;
                            if(valid)try{
                                const auto manifest=Json::parseFile((dest/"texture-set.json").string());
                                const auto program=render::texture_graph::compile(manifest["graph"],manifest["overrides"]);
                                std::set<std::string> required{"texture-set.json"};
                                for(const auto& pair:program.outputs)required.insert(pair.first+".png");
                                if(required!=seen)valid=false;
                            }catch(...){valid=false;}
                        }
                        if(!set&&seen.size()!=1)valid=false;
                        row.set("status",Json::string(valid?"destination_matches_evidence":"destination_evidence_mismatch"));
                    }
                }
            }catch(...){}
            rows.push(std::move(row));
        }
        return rows;
    }
};
} // namespace aiverse::editor

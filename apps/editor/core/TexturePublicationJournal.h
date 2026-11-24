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
    struct Intent {std::string id,name,output;};
    static Intent readIsolated(const Path& journal){
        namespace fs=std::filesystem;
        const auto file=journal/"intent.json";
        if(fs::is_symlink(journal)||!fs::is_directory(journal)||fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)>4096)
            throw std::runtime_error("Invalid isolated publication journal");
        const auto j=Json::parseFile(file.string());
        const auto name=j.getStr("name"),output=j.getStr("output");
        if(j.getInt("version")!=2||!render::texture_graph::safeId(name)||(!output.empty()&&!validOutput(output)))
            throw std::runtime_error("Invalid isolated publication identity");
        return {output.empty()?"set-"+name:"image-"+name+"."+output,name,output};
    }
    static bool safeSlot(const std::string& slot){
        return !slot.empty()&&slot.size()<=128&&slot!="."&&slot!=".."&&slot.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")==std::string::npos;
    }
    static Path quarantineRoot(const Path& project){
        const auto parent=root(project).parent_path()/"texture-publications-quarantine";
        if(std::filesystem::is_symlink(parent))throw std::runtime_error("Unsafe quarantine directory");
        return parent;
    }
    static Path destination(const Path& project,const Intent& intent){
        namespace fs=std::filesystem;
        const auto textures=project/"content"/"textures";
        if(fs::is_symlink(project/"content")||fs::is_symlink(textures))throw std::runtime_error("Unsafe texture destination");
        return textures/(intent.output.empty()?intent.name:intent.name+"_"+intent.output+".png");
    }
    static void requireVacant(const Path& project,const Intent& intent){
        namespace fs=std::filesystem;
        const auto dest=destination(project,intent);
        const auto legacy=intent.output.empty()?dest.parent_path()/("."+intent.name+".stage"):Path(dest.string()+".stage");
        if(fs::exists(dest)||fs::is_symlink(dest)||fs::exists(legacy)||fs::is_symlink(legacy))
            throw std::runtime_error("Texture destination or legacy staging already exists");
    }
    static void verifyStage(const Path& journal,const Intent& intent){
        namespace fs=std::filesystem;
        const auto stage=journal/"stage",file=journal/"files.json";
        if(fs::is_symlink(stage)||!fs::is_directory(stage)||fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)>16384)
            throw std::runtime_error("Missing complete staged evidence");
        const auto expected=Json::parseFile(file.string());
        if(!expected.isArray()||expected.size()==0||expected.size()>10)throw std::runtime_error("Invalid staged evidence");
        std::set<std::string> seen,required;
        if(intent.output.empty()){
            const auto manifest=stage/"texture-set.json";
            if(fs::is_symlink(manifest)||!fs::is_regular_file(manifest)||fs::file_size(manifest)>512*1024)
                throw std::runtime_error("Invalid staged texture manifest");
            const auto doc=Json::parseFile(manifest.string());
            if(doc.getInt("version")!=1)throw std::runtime_error("Unsupported texture manifest");
            const auto program=render::texture_graph::compile(doc["graph"],doc["overrides"]);
            required.insert("texture-set.json");
            for(const auto& pair:program.outputs)required.insert(pair.first+".png");
        }else required.insert("payload");
        for(const auto& record:expected.items()){
            const auto name=record.getStr("file"),hash=record.getStr("sha256");
            if(!required.count(name)||!seen.insert(name).second||hash.size()!=64||publicationFileSha256(stage/name)!=hash)
                throw std::runtime_error("Staged texture evidence mismatch");
        }
        if(required!=seen)throw std::runtime_error("Incomplete staged texture set");
        std::set<std::string> actual;
        for(const auto& entry:fs::directory_iterator(stage)){
            if(entry.is_symlink()||!entry.is_regular_file()||!required.count(entry.path().filename().string()))
                throw std::runtime_error("Unexpected staged texture file");
            actual.insert(entry.path().filename().string());
        }
        if(actual!=required)throw std::runtime_error("Incomplete staged texture files");
    }
public:
    static Json inspectQuarantine(const Path& project){
        namespace fs=std::filesystem;
        Json rows=Json::array();const auto archive=quarantineRoot(project);
        if(!fs::exists(archive))return rows;
        for(const auto& slot:fs::directory_iterator(archive)){
            if(rows.size()>=256)break;
            if(slot.is_symlink()||!slot.is_directory())continue;
            const auto id=slot.path().filename().string();const auto journal=slot.path()/"journal";
            if(!safeSlot(id)||!fs::exists(journal))continue;
            Json row=Json::object();row.set("id",Json::string(id));
            row.set("status",Json::string("invalid"));row.set("restorable",Json::fromBool(false));
            try{
                const auto intent=readIsolated(journal);
                row.set("publicationId",Json::string(intent.id));
                row.set("destination",Json::string(destination(project,intent).lexically_relative(project).generic_string()));
                requireVacant(project,intent);
                const auto active=root(project)/intent.id;
                if(fs::exists(active)||fs::is_symlink(active))throw std::runtime_error("Active intent already exists");
                row.set("restorable",Json::fromBool(true));row.set("status",Json::string("incomplete"));
                try{verifyStage(journal,intent);row.set("status",Json::string("verified_stage"));}catch(...){}
            }catch(...){}
            rows.push(std::move(row));
        }
        return rows;
    }
    static std::string restoreQuarantine(const Path& project,const std::string& slot){
        namespace fs=std::filesystem;
        ProjectAssetWriteLock lock(project);
        if(!safeSlot(slot))throw std::runtime_error("Invalid quarantine identity");
        const auto folder=quarantineRoot(project)/slot;
        if(fs::is_symlink(folder)||!fs::is_directory(folder))throw std::runtime_error("Invalid quarantine slot");
        const auto journal=folder/"journal";const auto intent=readIsolated(journal);
        requireVacant(project,intent);
        const auto parent=root(project),active=parent/intent.id;
        fs::create_directories(parent);
        if(fs::exists(active)||fs::is_symlink(active))throw std::runtime_error("Active intent already exists");
        fs::rename(journal,active);
        std::error_code ec;fs::remove(folder,ec);
        return intent.id;
    }
    static void resume(const Path& project,const std::string& id){
        namespace fs=std::filesystem;
        ProjectAssetWriteLock lock(project);
        if(!safeSlot(id))throw std::runtime_error("Invalid publication identity");
        const auto journal=root(project)/id;
        const auto intent=readIsolated(journal);
        if(intent.id!=id)throw std::runtime_error("Publication identity mismatch");
        requireVacant(project,intent);verifyStage(journal,intent);
        const auto dest=destination(project,intent),stage=journal/"stage";
        fs::create_directories(dest.parent_path());
        requireVacant(project,intent);
        if(intent.output.empty())fs::rename(stage,dest);
        else{
            fs::create_hard_link(stage/"payload",dest);
            fs::remove(stage/"payload");
            fs::remove(stage);
        }
        archivePublicationJournal(journal);
    }
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

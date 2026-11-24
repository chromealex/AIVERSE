// Crash-recovery journal for internal Studio content moves.
// It lives outside content/, so it is never indexed as a user asset.
// A completed transaction uses a committed marker; uncommitted operations
// are reversed on the next project open. No browser-supplied paths are used.
#pragma once
#include "apps/studio-native/StudioContentDirectory.h"
#include "engine/data/Json.h"
#include "apps/studio-native/StudioAssetFileDigest.h"
#include "apps/editor/core/ProjectFileEdits.h"
#include "apps/studio-native/StudioModelRepairBytes.h"
#include <set>
#include <filesystem>
#include <cerrno>
#include <cstring>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace aiverse::studio {
namespace fs=std::filesystem;

struct StudioMoveJournalOperation {
    std::string from;
    std::string to;
    std::string digest={}; // required by replacement journal v3
};

class StudioMoveJournal {
public:
    explicit StudioMoveJournal(fs::path project):project_(std::move(project)),
        folder_(project_/".aiverse-cache"/"studio-move-journal") {}

    static void recover(const fs::path& project) {
        StudioMoveJournal(project).restoreOrCleanup();
    }

    // Persistence boundary for the project-local journal and renamed assets.
    // Fail closed when a filesystem cannot honor durable metadata writes.
    static void syncPath(const fs::path& path) {
        editor::syncProjectFilePath(path);
    }
    static void renamePath(const fs::path& source,const fs::path& target,bool replaceExisting=false){
        editor::renameProjectFilePath(source,target,replaceExisting);
    }

    void begin(const std::vector<StudioMoveJournalOperation>& operations,bool quarantine=false,bool replacement=false,
               const std::vector<editor::ProjectFileEdit>& resources={}) {
        if (operations.empty()) return;
        if(operations.size()>128)throw std::runtime_error("Too many Studio journal operations");
        if(replacement&&!quarantine)throw std::runtime_error("Invalid replacement journal mode");
        if(!resources.empty()&&quarantine)throw std::runtime_error("Invalid resource repair journal mode");
        validateResources(resources,operations,true);
        for(const auto& edit:resources){
            if(!studioResourceMatches(project_/edit.path,edit))throw std::runtime_error("Resource repair baseline changed");
            if(studioStreamingResource(edit)&&fs::space(project_).available<
                std::max<uint64_t>(edit.before.size(),edit.after.size())+edit.tailBytes+16ull*1024*1024)
                throw std::runtime_error("Insufficient disk for reversible streamed GLB repair");
        }
        if(replacement)validateReplacementOperations(operations);
        for(const auto& operation:operations){
            validateRelative(operation.from);validateRelative(operation.to);
            if(quarantine)validateQuarantinePair(operation.from,operation.to);
        }
        // Existing journal must never be overwritten by a new transaction.
        if (fs::exists(folder_))
            throw std::runtime_error("A previous Studio asset transfer needs recovery before another move");
        const fs::path cache=folder_.parent_path();
        if (!fs::is_directory(cache) || fs::is_symlink(fs::symlink_status(cache)))
            throw std::runtime_error("Invalid Studio transaction cache directory");
        if (!fs::create_directory(folder_))
            throw std::runtime_error("Could not reserve Studio move journal");
        try {
            const fs::path material=project_/"materials.json";
            std::error_code ec;
            const auto materialStatus=fs::symlink_status(material,ec);
            if (ec && ec!=std::errc::no_such_file_or_directory)
                throw std::runtime_error("Cannot inspect material library before move");
            if (fs::is_symlink(materialStatus))
                throw std::runtime_error("Material library symlinks are not supported by move journal");
            const bool existed=!quarantine&&fs::exists(materialStatus);
            if (existed) {
                if (!fs::is_regular_file(materialStatus))
                    throw std::runtime_error("Invalid material library before move");
                fs::copy_file(material,folder_/"materials.backup",fs::copy_options::none);
                syncPath(folder_/"materials.backup");
            }
            Json state=Json::object();
            state.set("version",Json::number(int64_t(replacement?3:quarantine?2:resources.empty()?1:8)));
            if(!resources.empty()){
                Json repairs=Json::array();
                for(const auto& edit:resources){
                    repairs.push(studioEncodeResourceEdit(edit));
                }
                state.set("resources",std::move(repairs));
            }
            state.set("materialExisted",Json::fromBool(existed));
            Json rows=Json::array();
            for (const auto& operation:operations) {
                validateRelative(operation.from);
                validateRelative(operation.to);
                Json row=Json::object();
                row.set("from",Json::string(operation.from));
                row.set("to",Json::string(operation.to));
                if(replacement)row.set("digest",Json::string(operation.digest));
                rows.push(std::move(row));
            }
            state.set("operations",std::move(rows));
            // Manifest is published atomically before the first asset rename.
            writeFile(folder_/"pending.tmp",state.dump(2)+"\n");
            renamePath(folder_/"pending.tmp",folder_/"pending.json");
            syncPath(folder_);syncPath(cache);
        } catch (...) {
            std::error_code ec;
            fs::remove_all(folder_,ec);
            throw;
        }
    }

    // Outer pending marker owns the compact snapshots. Stream to a private
    // sibling-cache file and atomically replace the live file only after the
    // complete source and staged bytes are validated. Recovery can retry.
    void applyResources(const std::vector<editor::ProjectFileEdit>& edits){
        std::vector<editor::ProjectFileEdit> ordinary;
        const auto root=fs::canonical(project_);
        std::set<fs::path> unique;
        for(const auto& edit:edits){
            const auto relative=edit.path.lexically_relative(root).generic_string();
            validateRelative(relative);safePath(root,edit.path);
            if(!unique.insert(edit.path).second||fs::canonical(edit.path)!=edit.path)
                throw std::runtime_error("Invalid or duplicate resource publication target");
            if(studioStreamingResource(edit)&&(!relative.starts_with("content/")||edit.path.extension()!=".glb"))
                throw std::runtime_error("Invalid streaming resource target");
            if(!studioResourceMatches(edit.path,edit))throw std::runtime_error("Resource publication baseline changed");
            if(!studioStreamingResource(edit))ordinary.push_back(edit);
        }
        for(const auto& edit:edits)if(studioStreamingResource(edit)){
            if(!fs::is_regular_file(fs::symlink_status(folder_/"pending.json")))
                throw std::runtime_error("Streaming resource requires pending move journal");
            const auto temp=folder_/"resource-stream.tmp";
            if(fs::exists(fs::symlink_status(temp))){
                if(!fs::is_regular_file(fs::symlink_status(temp)))throw std::runtime_error("Unsafe resource stream staging file");
                fs::remove(temp);
            }
            const auto required=studioResourceProjectedSize(edit);
            if(fs::space(folder_).available<required)throw std::runtime_error("Insufficient disk for streamed GLB publication");
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);
            if(!out)throw std::runtime_error("Cannot stage streamed GLB");
            studioStreamProjectedResource(edit.path,edit,[&](const char* data,size_t count){
                if(!out.write(data,static_cast<std::streamsize>(count)))throw std::runtime_error("Cannot write streamed GLB");
            });
            out.flush();out.close();if(!out)throw std::runtime_error("Cannot close streamed GLB");
            fs::permissions(temp,fs::status(edit.path).permissions());
            syncPath(temp);
            if(!studioResourceMatches(temp,edit,true)||!studioResourceMatches(edit.path,edit))
                throw std::runtime_error("Streamed GLB publication identity changed");
            renamePath(temp,edit.path,true);syncPath(edit.path);syncPath(edit.path.parent_path());
            if(!studioResourceMatches(edit.path,edit,true))throw std::runtime_error("Published GLB identity changed");
        }
        editor::applyProjectFileEdits(project_,ordinary);
    }
    void commit() {
        if (!fs::exists(folder_/"pending.json")) return;
        // Durable commit marker precedes best-effort cleanup. Recovery must
        // never reverse a committed transaction.
        const auto version=Json::parseFile((folder_/"pending.json").string())["version"].integer();
        const fs::path material=project_/"materials.json";
        if ((version==1||version==4||version==5||version==6||version==7||version==8)&&fs::exists(material)) syncPath(material);
        syncPath(project_);
        renamePath(folder_/"pending.json",folder_/"committed.json");
        syncPath(folder_);
        std::error_code ignored;
        fs::remove_all(folder_,ignored);
    }

    void restoreOrCleanup() {
        std::error_code ec;
        const auto status=fs::symlink_status(folder_,ec);
        if (ec==std::errc::no_such_file_or_directory) return;
        if (ec)
            throw std::runtime_error("Cannot inspect Studio recovery journal: "+ec.message());
        if (!fs::exists(status)) return;
        if (fs::is_symlink(status) || !fs::is_directory(status))
            throw std::runtime_error("Unsafe Studio move recovery journal");
        if (fs::exists(folder_/"committed.json")) {
            fs::remove_all(folder_);
            return;
        }
        const fs::path pending=folder_/"pending.json";
        if (!fs::exists(pending)) {
            // begin() only starts moving after pending.json has been published.
            fs::remove_all(folder_);
            return;
        }
        if (fs::is_symlink(fs::symlink_status(pending)) ||
            !fs::is_regular_file(pending))
            throw std::runtime_error("Unsafe Studio pending transaction manifest");
        if(fs::file_size(pending)>64ull*1024*1024)throw std::runtime_error("Oversized Studio move journal");
        const Json state=Json::parseFile(pending.string());
        const auto version=state["version"].integer();
        if ((version!=1&&version!=2&&version!=3&&version!=4&&version!=5&&version!=6&&version!=7&&version!=8) || !state["operations"].isArray() ||
            state["operations"].size()>128)
            throw std::runtime_error("Invalid Studio move recovery manifest");
        if(version==3){restoreReplacement(state["operations"]);fs::remove_all(folder_);return;}
        const fs::path root=version==2?fs::canonical(project_):studioContentRoot(project_);
        const auto& operations=state["operations"].items();
        struct RestoreStep { fs::path source,destination; bool moveBack; };
        std::vector<RestoreStep> steps;
        steps.reserve(operations.size());
        // Validate ALL records and collisions before changing any files.
        // A malformed later entry must not cause partial recovery of earlier ones.
        for (const auto& operation:operations) {
            if (!operation.isObject() || !operation["from"].isString() ||
                !operation["to"].isString())
                throw std::runtime_error("Invalid Studio recovery operation");
            const auto from=operation.getStr("from"),to=operation.getStr("to");
            validateRelative(from);validateRelative(to);
            if(version==2)validateQuarantinePair(from,to);
            if (from==to) throw std::runtime_error("Invalid no-op Studio recovery entry");
            const fs::path source=root/fs::path(from);
            const fs::path destination=root/fs::path(to);
            safePath(root,source);safePath(root,destination);
            const bool src=fs::exists(source),dst=fs::exists(destination);
            if (src && dst)
                throw std::runtime_error("Studio recovery collision: source and destination both exist");
            if (!src && !dst)
                throw std::runtime_error("Studio recovery missing both sides of a move");
            steps.push_back({source,destination,dst});
        }
        if(version==4||version==5||version==6||version==7||version==8){
            if(!state["resources"].isArray()||state["resources"].size()>32)throw std::runtime_error("Invalid resource repair recovery manifest");
            std::vector<editor::ProjectFileEdit> edits,reverse;
            std::vector<StudioMoveJournalOperation> moves;
            for(const auto& op:operations)moves.push_back({op.getStr("from"),op.getStr("to")});
            for(const auto& row:state["resources"].items()){
                if(version<8&&(row.has("tailDigest")||row.has("tailBytes")))throw std::runtime_error("Compact repair requires journal v8");
                edits.push_back(studioDecodeResourceEdit(row,version>=6));
            }
            validateResources(edits,moves,version>=5);
            editor::recoverProjectFileEdits(project_);
            for(const auto& edit:edits){
                const auto relative=edit.path.generic_string();
                const auto projected=resourceDestination(relative,moves);
                const auto projectRoot=fs::canonical(project_);
                auto path=projectRoot/edit.path;
                if(version>=5&&projected!=relative){
                    const auto other=projectRoot/projected;
                    const bool sourceExists=fs::exists(fs::symlink_status(path));
                    const bool targetExists=fs::exists(fs::symlink_status(other));
                    if(sourceExists==targetExists)throw std::runtime_error("Ambiguous moved resource recovery");
                    if(targetExists)path=other;
                }
                if(studioResourceMatches(path,edit))continue;
                if(!studioResourceMatches(path,edit,true))throw std::runtime_error("Resource repair recovery conflict");
                auto undo=edit;undo.path=path;std::swap(undo.before,undo.after);reverse.push_back(std::move(undo));
            }
            applyResources(reverse);
        }
        // Reverse order handles file + .meta pairs. Each replay is idempotent
        // when a crash happens midway through recovery itself.
        for (auto it=steps.rbegin();it!=steps.rend();++it) {
            if (!it->moveBack) continue;
            renamePath(it->destination,it->source);
            syncPath(it->source.parent_path());
            if (it->source.parent_path()!=it->destination.parent_path())
                syncPath(it->destination.parent_path());
        }
        const fs::path material=project_/"materials.json";
        if(version==2){fs::remove_all(folder_);return;} // quarantine does not own materials
        if (state.getBool("materialExisted")) {
            const fs::path backup=folder_/"materials.backup";
            if (!fs::is_regular_file(backup) || fs::is_symlink(fs::symlink_status(backup)))
                throw std::runtime_error("Missing Studio material recovery backup");
            const fs::path staged=folder_/"materials.restore";
            fs::copy_file(backup,staged,fs::copy_options::overwrite_existing);
            renamePath(staged,material,true);
            syncPath(material);
            syncPath(project_);
        } else {
            // No prior library: only remove a newly written file if recovery
            // requires doing so. Other writers must not run concurrently.
            if (fs::exists(material)) {
                fs::remove(material);
                syncPath(project_);
            }
        }
        fs::remove_all(folder_);
    }

    // Shared lexical projection for journal, checkpoint and recovery. Paths
    // are project-relative; operation endpoints are content-relative.
    static std::string resourceDestination(const std::string& path,const std::vector<StudioMoveJournalOperation>& moves){
        std::string result=path;size_t matched=0;
        for(const auto& op:moves){
            const auto from="content/"+op.from,to="content/"+op.to;
            if(path==from||path.starts_with(from+"/")){
                if(++matched>1||op.from.ends_with(".meta"))throw std::runtime_error("Ambiguous resource relocation");
                result=to+path.substr(from.size());
            }
        }
        for(const auto& op:moves){
            const auto from="content/"+op.from,to="content/"+op.to;
            if(path==to||path.starts_with(to+"/")||to.starts_with(path+"/")||
               from.starts_with(path+"/"))throw std::runtime_error("Resource repair overlaps another relocation");
        }
        return result;
    }
private:
    void validateResources(const std::vector<editor::ProjectFileEdit>& edits,
                           const std::vector<StudioMoveJournalOperation>& moves,bool allowMoved) const{
        if(edits.size()>32)throw std::runtime_error("Resource repair count exceeded");
        size_t bytes=0;std::set<std::string> seen,destinations;
        const auto root=fs::canonical(project_);
        for(const auto& edit:edits){
            const auto path=edit.path.generic_string();
            validateRelative(path);
            if(!path.starts_with("content/")||!studioRepairModelExtension(edit.path)||!seen.insert(path).second)
                throw std::runtime_error("Invalid resource repair path");
            for(const auto& segment:edit.path){
                const auto name=segment.string();
                if(name.find('\0')!=std::string::npos||name==".aiverse-cache"||name==".git"||name.starts_with(".aiverse-save-"))
                    throw std::runtime_error("Unsafe resource repair segment");
            }
            safePath(root,root/edit.path);
            const auto destination=resourceDestination(path,moves);
            validateRelative(destination);
            if(!destinations.insert(destination).second||(!allowMoved&&destination!=path)||
               !studioRepairModelExtension(fs::path(destination)))
                throw std::runtime_error("Invalid moved resource repair path");
            safePath(root,root/destination);
            for(size_t count:{edit.before.size(),edit.after.size()}){
                if(count>8u*1024u*1024u-bytes)throw std::runtime_error("Resource repair journal budget exceeded");
                bytes+=count;
            }
            studioValidateModelRepairBytes(edit);
        }
    }
    static void writeFile(const fs::path& filename,const std::string& data) {
        std::ofstream stream(filename,std::ios::binary|std::ios::trunc);
        if (!stream || !(stream<<data) || !(stream.flush()))
            throw std::runtime_error("Cannot write Studio move recovery journal");
        stream.close();
        if (!stream) throw std::runtime_error("Cannot close Studio move journal");
        syncPath(filename);
    }
    static void validateRelative(const std::string& value) {
        if (value.empty() || value.size()>1024 || value.front()=='/' ||
            value.find('\\')!=std::string::npos)
            throw std::runtime_error("Unsafe Studio recovery asset path");
        for (const auto& part:fs::path(value)) {
            const auto segment=part.string();
            if (segment.empty() || segment=="." || segment==".." || segment.find(':')!=std::string::npos)
                throw std::runtime_error("Unsafe Studio recovery asset segment");
        }
    }
    static void validateQuarantinePair(const std::string& from,const std::string& to) {
        const auto content=[](const std::string& p){return p.starts_with("content/")&&p.size()>8;};
        const auto trash=[](const std::string& p){
            const std::string prefix=".aiverse-cache/studio-asset-trash/";
            if(!p.starts_with(prefix))return false;
            const auto rest=p.substr(prefix.size());
            const auto slash=rest.find('/');
            if(slash!=24)return false;
            for(size_t i=0;i<slash;++i)if(!((rest[i]>='0'&&rest[i]<='9')||(rest[i]>='a'&&rest[i]<='f')))return false;
            return rest.substr(slash+1)=="asset"||rest.substr(slash+1)=="asset.meta";
        };
        if(!((content(from)&&trash(to))||(trash(from)&&content(to))))
            throw std::runtime_error("Unsafe asset quarantine recovery pair");
    }
    static void validateReplacementOperations(const std::vector<StudioMoveJournalOperation>& operations){
        if(operations.empty()||operations.size()>128||operations.size()%2)
            throw std::runtime_error("Invalid replacement recovery operation count");
        std::set<std::string> paths;
        for(size_t i=0;i<operations.size();i+=2){
            const auto& out=operations[i];const auto& in=operations[i+1];
            for(const auto* op:{&out,&in}){
                validateRelative(op->from);validateRelative(op->to);validateQuarantinePair(op->from,op->to);
                if(op->digest.size()!=64||op->digest.find_first_not_of("0123456789abcdef")!=std::string::npos)
                    throw std::runtime_error("Invalid replacement recovery digest");
            }
            if(!out.from.starts_with("content/")||out.from!=in.to||!out.to.ends_with("/asset")||
               !in.from.ends_with("/asset")||out.from.ends_with(".meta")||
               !paths.insert(out.from).second||!paths.insert(out.to).second||!paths.insert(in.from).second)
                throw std::runtime_error("Invalid replacement recovery chain");
        }
    }
    void restoreReplacement(const Json& rows){
        std::vector<StudioMoveJournalOperation> operations;
        for(const auto& row:rows.items()){
            if(!row.isObject()||!row["from"].isString()||!row["to"].isString()||!row["digest"].isString())
                throw std::runtime_error("Invalid replacement recovery record");
            operations.push_back({row.getStr("from"),row.getStr("to"),row.getStr("digest")});
        }
        validateReplacementOperations(operations);
        const auto root=fs::canonical(project_);
        const auto exists=[](const fs::path& p){
            std::error_code ec;const auto status=fs::symlink_status(p,ec);
            if(ec==std::errc::no_such_file_or_directory)return false;
            if(ec||(!fs::is_regular_file(status)&&fs::exists(status)))
                throw std::runtime_error("Invalid replacement recovery file");
            return fs::exists(status);
        };
        struct State {fs::path live,oldSlot,newSlot;int phase;};
        std::vector<State> states;
        // Validate every chain before moving any file. Only three occupancy
        // states are legal, including a crash midway through recovery itself.
        for(size_t i=0;i<operations.size();i+=2){
            const auto& out=operations[i];const auto& in=operations[i+1];
            const auto live=root/out.from,oldSlot=root/out.to,newSlot=root/in.from;
            for(const auto& path:{live,oldSlot,newSlot}){
                safePath(root,path);
                if(!fs::is_directory(fs::symlink_status(path.parent_path())))throw std::runtime_error("Missing replacement recovery parent");
            }
            const bool l=exists(live),o=exists(oldSlot),n=exists(newSlot);
            const int phase=l&&!o&&n?0:!l&&o&&n?1:l&&o&&!n?2:-1;
            if(phase<0)throw std::runtime_error("Replacement recovery collision");
            const auto oldPath=phase==0?live:oldSlot,newPath=phase==2?live:newSlot;
            if(studioAssetFileDigest(oldPath)!=out.digest||studioAssetFileDigest(newPath)!=in.digest)
                throw std::runtime_error("Replacement recovery payload changed");
            states.push_back({live,oldSlot,newSlot,phase});
        }
        const auto move=[](const fs::path& from,const fs::path& to){
            if(fs::exists(fs::symlink_status(to)))throw std::runtime_error("Replacement recovery destination occupied");
            renamePath(from,to);
        };
        for(auto it=states.rbegin();it!=states.rend();++it){
            if(it->phase==2)move(it->live,it->newSlot);
            if(it->phase!=0)move(it->oldSlot,it->live);
        }
    }
    static void safePath(const fs::path& root,const fs::path& target) {
        fs::path current=root;
        const auto relative=target.lexically_relative(root);
        for (const auto& part:relative) {
            current/=part;
            std::error_code ec;
            const auto state=fs::symlink_status(current,ec);
            if (!ec && fs::is_symlink(state))
                throw std::runtime_error("Symlink encountered during Studio move recovery");
            if (ec && ec!=std::errc::no_such_file_or_directory)
                throw std::runtime_error("Cannot inspect Studio recovery path");
        }
    }
    fs::path project_;
    fs::path folder_;
};
} // namespace aiverse::studio

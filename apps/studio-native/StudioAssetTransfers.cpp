#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include "apps/studio-native/StudioHistoryCoordinator.h"
#include "apps/editor/core/ProjectDocumentJournal.h"
#include "engine/pack/Sha256.h"
#include <algorithm>
#include <fstream>
#include <exception>
#include <map>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <system_error>
namespace fs=std::filesystem;
namespace aiverse::studio {
namespace {
void validateSegment(const std::string& s) {
    if(s.empty()||s=="."||s==".."||s.size()>150||s.front()=='.'||
       s.find_first_of("\\:")!=std::string::npos||
       s.find('/')!=std::string::npos || s.back()=='.' || s.back()==' ' ||
       fs::path(s).extension()==".meta")
        throw std::runtime_error("Invalid content filename or directory: "+s);
    for(unsigned char c:s)if(c<32||c==127)
        throw std::runtime_error("Control characters are forbidden in content paths");
}
fs::path checkedFolder(const fs::path& project,const std::string& relative) {
    const fs::path root=studioContentRoot(project);
    if(relative.empty())return root;
    if(relative.size()>512||relative[0]=='/'||relative.back()=='/'||
       relative.find('\\')!=std::string::npos)
        throw std::runtime_error("Invalid content destination folder");
    fs::path current=root;
    std::size_t from=0;
    while(from<relative.size()){
        const auto delim=relative.find('/',from);
        const auto segment=relative.substr(from,delim==std::string::npos?
            std::string::npos:delim-from);
        validateSegment(segment);
        current/=segment;
        std::error_code ec;
        const auto status=fs::symlink_status(current,ec);
        if(ec||!fs::is_directory(status)||fs::is_symlink(status))
            throw std::runtime_error("Content destination is not an existing safe folder: "+segment);
        if(delim==std::string::npos)break;
        from=delim+1;
    }
    return current;
}
fs::path checkedSource(const fs::path& project,const std::string& relative) {
    if(relative.empty()||relative.front()=='/'||relative.find('\\')!=std::string::npos||
       relative.size()>512)throw std::runtime_error("Invalid content source");
    const auto path=fs::path(relative);
    fs::path current=studioContentRoot(project);
    for(const auto& part:path){
        const auto name=part.string();
        validateSegment(name);
        current/=part;
        std::error_code ec;
        const auto status=fs::symlink_status(current,ec);
        if(ec||fs::is_symlink(status))
            throw std::runtime_error("Invalid or symlinked content source: "+relative);
    }
    if(!fs::exists(current))throw std::runtime_error("Content source does not exist: "+relative);
    return current;
}
std::string guidFromSidecar(const fs::path& source,bool& invalid) {
    const auto meta=studioMetaPath(source);
    invalid=false;
    std::error_code ec;
    const auto status=fs::symlink_status(meta,ec);
    if(ec && ec!=std::errc::no_such_file_or_directory){invalid=true;return {};}
    if(!fs::exists(status))return {};
    if(!fs::is_regular_file(status)||fs::is_symlink(status)){invalid=true;return {};}
    try{
        const auto doc=Json::parseFile(meta.string());
        const auto guid=doc.getStr("guid");
        if(!studioValidGuid(guid)){invalid=true;return {};}
        return guid;
    } catch(...){invalid=true;return {};}
}
// Hash a complete tree with deterministic path/type/length framing, including
// empty directories and all sidecars. Reject symlinks, special files and
// excessive entry counts. Memory is bounded independently of payload size.
using ResourceOverlay=std::map<fs::path,editor::ProjectFileEdit>;
std::string moveDigest(const fs::path& source,bool directory,const ResourceOverlay* overlay=nullptr){
    const auto replacement=[&](const fs::path& path)->const editor::ProjectFileEdit*{
        if(!overlay)return nullptr;
        const auto it=overlay->find(fs::absolute(path).lexically_normal());
        if(it==overlay->end())return nullptr;
        return &it->second;
    };
    if(!directory){
        if(const auto* bytes=replacement(source)){
            // Identical framing to studioAssetFileDigest, not bare SHA-256.
            pack::Sha256 hash;
            const auto field=[&](const std::string& value){
                const auto prefix=std::to_string(value.size())+":";
                hash.update(prefix.data(),prefix.size());hash.update(value.data(),value.size());
            };
            field("file-v1");field(std::to_string(studioResourceProjectedSize(*bytes)));
            studioStreamProjectedResource(source,*bytes,[&](const char* data,size_t count){hash.update(data,count);});
            const auto digest=hash.finish();return pack::toHex(digest.data(),digest.size());
        }
        return studioAssetFileDigest(source);
    }
    pack::Sha256 hash;
    const auto field=[&](const std::string& s){
        const auto size=std::to_string(s.size())+":";
        hash.update(size.data(),size.size());hash.update(s.data(),s.size());
    };
    const auto file=[&](const fs::path& path){
        if(!fs::is_regular_file(fs::symlink_status(path)))
            throw std::runtime_error("Unsupported file in asset history");
        if(const auto* bytes=replacement(path)){
            field(std::to_string(studioResourceProjectedSize(*bytes)));
            studioStreamProjectedResource(path,*bytes,[&](const char* data,size_t count){hash.update(data,count);});return;
        }
        const auto size=fs::file_size(path);
        const auto stamp=fs::last_write_time(path);
        field(std::to_string(size));
        std::ifstream stream(path,std::ios::binary);
        if(!stream)throw std::runtime_error("Cannot read move history file");
        char buffer[65536];std::uintmax_t total=0;
        while(stream.read(buffer,sizeof(buffer))||stream.gcount()){
            const auto n=static_cast<size_t>(stream.gcount());
            hash.update(buffer,n);total+=n;
        }
        if(stream.bad()||!stream.eof()||total!=size||fs::file_size(path)!=size||fs::last_write_time(path)!=stamp)
            throw std::runtime_error("Asset changed while hashing");
    };
    {
        if(!fs::is_directory(fs::symlink_status(source)))throw std::runtime_error("Asset directory changed");
        field("tree-v1");
        std::vector<fs::path> entries;
        for(const auto& item:fs::recursive_directory_iterator(source)){
            if(entries.size()>=20000)throw std::runtime_error("Asset history tree entry limit");
            const auto status=item.symlink_status();
            if(!fs::is_directory(status)&&!fs::is_regular_file(status))
                throw std::runtime_error("Unsupported entry in asset history tree");
            entries.push_back(item.path().lexically_relative(source));
        }
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.generic_string()<b.generic_string();});
        for(const auto& rel:entries){
            const auto path=source/rel;
            field(rel.generic_string());
            const auto status=fs::symlink_status(path);
            if(fs::is_directory(status))field("directory");
            else {field("file");file(path);}
        }
    }
    const auto result=hash.finish();return pack::toHex(result.data(),result.size());
}
bool matchesMoveFile(const fs::path& path,const editor::ProjectHistory::MoveFile& f,bool after=false){
    if(!f.digest.empty())return moveDigest(path,f.directory)==(after&&!f.digestAfter.empty()?f.digestAfter:f.digest);
    return fs::is_regular_file(fs::symlink_status(path))&&fs::file_size(path)==f.bytes.size()&&
           editor::readProjectFileBytes(path)==f.bytes;
}
std::string nonce() {
    std::random_device random;
    const char* hexdigits="0123456789abcdef";
    std::string output;
    for(int i=0;i<24;++i)output+=hexdigits[random()&15u];
    return output;
}
bool existsOrSymlink(const fs::path& target) {
    std::error_code ec;
    const auto status=fs::symlink_status(target,ec);
    if(ec && ec!=std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect transfer destination: "+target.string()+": "+ec.message());
    return !ec && fs::exists(status);
}
constexpr uint64_t RecoveryMiB=1024ull*1024;
constexpr int64_t RecoveryMaxQuotaMiB=1024*1024;
constexpr uint64_t RecoveryMaxAccountedBytes=1ull<<50;
fs::path recoveryCache(const fs::path& project){
    const auto cache=fs::canonical(project)/".aiverse-cache";
    if(existsOrSymlink(cache)&&!fs::is_directory(fs::symlink_status(cache)))
        throw std::runtime_error("Unsafe recovery cache");
    return cache;
}
int64_t recoveryQuotaMiB(const fs::path& project){
    const auto path=recoveryCache(project)/"studio-recovery-policy.json";
    if(!existsOrSymlink(path))return 0;
    if(!fs::is_regular_file(fs::symlink_status(path))||fs::file_size(path)>4096)
        throw std::runtime_error("Invalid recovery quota policy");
    const auto doc=Json::parseFile(path.string());
    const auto quota=doc.getInt("quotaMiB",-1);
    if(doc.getInt("version")!=1||!doc["quotaMiB"].isNumber()||
       doc["quotaMiB"].numberLiteral()!=std::to_string(quota)||quota<0||quota>RecoveryMaxQuotaMiB)
        throw std::runtime_error("Invalid recovery quota policy");
    return quota;
}
uint64_t recoveryBytes(const fs::path& root,size_t& visited){
    const auto status=fs::symlink_status(root);
    if(!fs::exists(status))return 0;
    uint64_t bytes=0;
    const auto add=[&](const fs::path& path,const fs::file_status& type){
        if(++visited>200000)throw std::runtime_error("Recovery accounting entry budget exceeded");
        if(fs::is_directory(type))return;
        if(!fs::is_regular_file(type))throw std::runtime_error("Unsafe recovery accounting entry");
        const auto size=fs::file_size(path);
        if(size>RecoveryMaxAccountedBytes-bytes)throw std::runtime_error("Recovery accounting byte budget exceeded");
        bytes+=size;
    };
    add(root,status);
    if(fs::is_directory(status))
        for(const auto& item:fs::recursive_directory_iterator(root))add(item.path(),item.symlink_status());
    return bytes;
}
// Logical source size plus a small reserve for staging metadata. This is a
// preflight, not a reservation against another process consuming free blocks.
void checkRecoveryStagingSpace(const fs::path& project,const fs::path& source){
    size_t visited=0;
    const auto bytes=source.empty()?uint64_t(0):recoveryBytes(source,visited);
    constexpr uint64_t reserve=16ull*RecoveryMiB;
    const auto available=fs::space(fs::canonical(project)).available;
    if(available<reserve||bytes>available-reserve)
        throw std::runtime_error("Insufficient disk space for asset staging (including 16 MiB reserve)");
}
bool inRecoveryTrash(const std::string& path){return path.starts_with(".aiverse-cache/studio-asset-trash/");}
// Net retained bytes, not transient staging occupancy. Sources have already
// been checked by their native owner. No deletion is performed to fit a quota.
template<class Moves>
void checkRecoveryGrowth(const fs::path& project,const Moves& moves,int64_t additional=0){
    const auto quota=recoveryQuotaMiB(project);
    if(!quota)return;
    if(!additional&&std::none_of(moves.begin(),moves.end(),[](const auto& move){
        return inRecoveryTrash(move.from)!=inRecoveryTrash(move.to);
    }))return;
    int64_t delta=additional;size_t visited=0;
    std::map<std::string,uint64_t> sizes;
    for(const auto& move:moves){
        const bool from=inRecoveryTrash(move.from),to=inRecoveryTrash(move.to);
        const auto found=sizes.find(move.from);
        const auto bytes=found==sizes.end()?recoveryBytes(project/move.from,visited):found->second;
        sizes.erase(move.from);sizes[move.to]=bytes;
        if(from==to)continue;
        const auto signedBytes=static_cast<int64_t>(bytes);
        if(to){
            if(delta>std::numeric_limits<int64_t>::max()-signedBytes)throw std::runtime_error("Recovery quota arithmetic exceeded");
            delta+=signedBytes;
        }else{
            if(delta<std::numeric_limits<int64_t>::min()+signedBytes)throw std::runtime_error("Recovery quota arithmetic exceeded");
            delta-=signedBytes;
        }
    }
    if(delta<=0)return; // recovery must be possible even while over quota
    const auto used=recoveryBytes(recoveryCache(project)/"studio-asset-trash",visited);
    const auto limit=uint64_t(quota)*RecoveryMiB;
    if(used>=limit||uint64_t(delta)>limit-used)
        throw std::runtime_error("Asset recovery quota exceeded; review Trash or raise the project quota");
}
fs::path keepBothName(const fs::path& dest,bool folder) {
    const auto ext=folder?std::string():dest.extension().string();
    const auto stem=folder?dest.filename().string():dest.stem().string();
    for(int i=1;i<10000;++i){
        const auto candidate=dest.parent_path()/(stem+" ("+std::to_string(i)+")"+ext);
        if(!existsOrSymlink(candidate)&&!existsOrSymlink(studioMetaPath(candidate)))
            return candidate;
    }
    throw std::runtime_error("Could not find an unused content name");
}
void copyFolderNoSymlinks(const fs::path& source,const fs::path& target) {
    if(!fs::create_directory(target))
        throw std::runtime_error("Destination folder already exists");
    try{
        std::error_code ec;
        for(fs::recursive_directory_iterator it(source,fs::directory_options::none,ec),end;
            it!=end;it.increment(ec)){
            if(ec)throw std::runtime_error("Error scanning external directory");
            const auto status=it->symlink_status(ec);
            if(ec||fs::is_symlink(status))
                throw std::runtime_error("Symlinks are not imported from external folders");
            const auto name=it->path().filename().string();
            // External .meta are never blindly copied for a folder import:
            // each imported child gets a NEW GUID on catalog reconciliation.
            if(fs::path(name).extension()==".meta"){it.disable_recursion_pending();continue;}
            validateSegment(name);
            const auto relative=it->path().lexically_relative(source);
            const auto destination=target/relative;
            if(fs::is_directory(status))fs::create_directory(destination);
            else if(fs::is_regular_file(status))
                fs::copy_file(it->path(),destination,fs::copy_options::none);
        }
        if(ec)throw std::runtime_error("Cannot complete external folder copy");
    }catch(...){std::error_code ec;fs::remove_all(target,ec);throw;}
}
// Stage inside a private project so the existing catalog creates canonical
// sidecars using the final filename/importer. No live content is published here.
void stageCreatedAsset(const fs::path& project,const std::string& path,
    const fs::path& source,bool directory,bool preserveSidecar,
    editor::ProjectHistory::MovePlan& plan){
    checkRecoveryStagingSpace(project,source);
    // Validate/create the cache root with the existing catalog owner.
    {StudioAssetCatalog live(project);}
    const auto root=project/".aiverse-cache"/"studio-asset-trash";
    if(!existsOrSymlink(root))fs::create_directory(root);
    if(!fs::is_directory(fs::symlink_status(root)))throw std::runtime_error("Unsafe asset recovery root");
    const auto id=nonce();
    const auto slot=root/id;
    if(!fs::create_directory(slot))throw std::runtime_error("Cannot reserve import recovery slot");
    try{
        const auto staging=slot/"staging";
        const auto content=staging/"content";
        fs::create_directories(content);
        const auto staged=content/fs::path(path).filename();
        if(directory){
            if(source.empty())fs::create_directory(staged);
            else copyFolderNoSymlinks(source,staged);
        }else{
            if(!fs::is_regular_file(fs::symlink_status(source)))throw std::runtime_error("Import source changed");
            fs::copy_file(source,staged,fs::copy_options::none);
            if(preserveSidecar){
                const auto meta=studioMetaPath(source);
                if(!fs::is_regular_file(fs::symlink_status(meta)))throw std::runtime_error("Import sidecar changed");
                fs::copy_file(meta,studioMetaPath(staged),fs::copy_options::none);
            }
        }
        {
            StudioAssetCatalog catalog(staging);catalog.rebuild();
            const auto entry=catalog.byPath(staged.filename().generic_string());
            if(!entry||entry->directory!=directory)throw std::runtime_error("Staged asset metadata unavailable");
        }
        const auto meta=studioMetaPath(staged);
        if(fs::file_size(meta)>1024*1024)throw std::runtime_error("Asset metadata too large for history");
        const auto bytes=editor::readProjectFileBytes(meta);
        const auto digest=moveDigest(staged,directory);
        const auto relative=".aiverse-cache/studio-asset-trash/"+id+"/asset";
        fs::rename(staged,slot/"asset");
        fs::rename(meta,slot/"asset.meta");
        // Flush the imported bytes before the journal can publish them.
        if(directory){
            std::vector<fs::path> dirs;
            for(const auto& item:fs::recursive_directory_iterator(slot/"asset")){
                if(fs::is_directory(item.symlink_status()))dirs.push_back(item.path());
                else StudioMoveJournal::syncPath(item.path());
            }
            for(auto it=dirs.rbegin();it!=dirs.rend();++it)StudioMoveJournal::syncPath(*it);
        }
        StudioMoveJournal::syncPath(slot/"asset");StudioMoveJournal::syncPath(slot/"asset.meta");
        Json record=Json::object();record.set("path",Json::string(path));
        record.set("guid",Json::string(Json::parse(bytes).getStr("guid")));
        record.set("directory",Json::fromBool(directory));record.set("digest",Json::string(digest));
        record.set("kind",Json::string("created"));
        record.set("metaDigest",Json::string(studioAssetFileDigest(slot/"asset.meta")));
        {std::ofstream out(slot/"record.json",std::ios::binary);out<<record.dump(2)<<"\n";out.close();if(!out)throw std::runtime_error("Cannot write import recovery record");}
        StudioMoveJournal::syncPath(slot/"record.json");
        fs::remove_all(staging);
        StudioMoveJournal::syncPath(slot);StudioMoveJournal::syncPath(root);StudioMoveJournal::syncPath(root.parent_path());
        plan.files.push_back({path,relative,{},digest,directory});
        plan.files.push_back({path+".meta",relative+".meta",bytes,{},false});
    }catch(...){
        // This slot has not been published or journaled yet.
        std::error_code ec;fs::remove_all(slot,ec);throw;
    }
}
void movePair(const fs::path& source,const fs::path& dest) {
    fs::rename(source,dest);
    const auto metaSrc=studioMetaPath(source),metaDst=studioMetaPath(dest);
    if(existsOrSymlink(metaSrc)){
        try{fs::rename(metaSrc,metaDst);}
        catch(...){fs::rename(dest,source);throw;}
    }
}
}
std::string studioCreateContentFolder(const fs::path& project,const std::string& parent,
                                     const std::string& name,editor::ProjectHistory::MovePlan* history,StudioBatchAssetPlan* staged) {
    if(staged&&!history)throw std::runtime_error("Staged folder creation requires history");
    validateSegment(name);
    const fs::path destination=checkedFolder(project,parent)/name;
    if(existsOrSymlink(destination)||existsOrSymlink(studioMetaPath(destination)))
        throw std::runtime_error("Folder already exists or has a metadata collision");
    if(history){
        editor::ProjectHistory::MovePlan plan;plan.quarantine=true;plan.created=true;
        const auto relative=parent.empty()?name:parent+"/"+name;
        stageCreatedAsset(project,relative,{},true,false,plan);
        studioReplayAssetQuarantine(project,plan,false,{},staged);
        *history=std::move(plan);return relative;
    }
    if(!fs::create_directory(destination))
        throw std::runtime_error("Could not create content folder");
    try{StudioAssetCatalog catalog(project);catalog.rebuild();}
    catch(...){fs::remove(destination);throw;}
    return parent.empty()?name:parent+"/"+name;
}
StudioTransferPlan StudioTransferPlan::external(const fs::path& project,
    const std::string& folder,const std::vector<fs::path>& sources){
    return plan(project,folder,StudioTransferType::External,sources);
}
StudioTransferPlan StudioTransferPlan::internal(const fs::path& project,
    const std::string& folder,const std::vector<std::string>& paths){
    std::vector<fs::path> source;
    for(const auto& p:paths)source.push_back(checkedSource(project,p));
    return plan(project,folder,StudioTransferType::Move,source);
}
StudioTransferPlan StudioTransferPlan::rename(const fs::path& project,
    const std::string& path,const std::string& newName){
    validateSegment(newName);
    const auto source=checkedSource(project,path);
    const auto directory=fs::is_directory(fs::symlink_status(source));
    if(newName==source.filename().string())throw std::runtime_error("Asset already has that name");
    if(!directory&&fs::path(newName).extension()!=source.extension())
        throw std::runtime_error("Rename must preserve file extension and importer");
    const auto parent=fs::path(path).parent_path().generic_string();
    auto result=internal(project,parent=="."?"":parent,{path});
    auto& entry=result.entries_.front();
    entry.destination=source.parent_path()/newName;
    entry.relativeDestination=entry.destination.lexically_relative(studioContentRoot(project)).generic_string();
    entry.conflict=existsOrSymlink(entry.destination)||existsOrSymlink(studioMetaPath(entry.destination))?"name":"";
    return result;
}
StudioTransferPlan StudioTransferPlan::plan(const fs::path& project,
    const std::string& destination,StudioTransferType type,
    const std::vector<fs::path>& sources){
    if(sources.empty()||sources.size()>64)
        throw std::runtime_error("Choose between 1 and 64 assets per transfer");
    const auto to=checkedFolder(project,destination);
    StudioTransferPlan result;
    result.project_=project;result.type_=type;result.token_=nonce();
    StudioAssetCatalog db(project);
    db.rebuild();
    std::set<fs::path> unique;
    for(const auto& input:sources){
        if(!unique.insert(input).second)
            throw std::runtime_error("Repeated asset selected for transfer");
        const auto name=input.filename().string();
        validateSegment(name);
        const auto status=fs::symlink_status(input);
        if(fs::is_symlink(status)||(!fs::is_directory(status)&&!fs::is_regular_file(status)))
            throw std::runtime_error("Cannot transfer symlink or unsupported file");
        const bool directory=fs::is_directory(status);
        const auto target=to/name;
        const auto sourceAbs=fs::weakly_canonical(input);
        if(type==StudioTransferType::Move){
            const auto root=fs::weakly_canonical(studioContentRoot(project));
            const auto rel=sourceAbs.lexically_relative(root);
            if(rel.empty()||rel.is_absolute()||rel.string().rfind("..",0)==0)
                throw std::runtime_error("Only assets under content/ can be moved internally");
            if(directory){
                auto dest=fs::weakly_canonical(to);
                if(dest==sourceAbs||dest.string().rfind(sourceAbs.string()+char(fs::path::preferred_separator),0)==0)
                    throw std::runtime_error("Cannot move a directory inside itself");
            }
        }
        StudioTransferEntry entry;
        entry.id=int(result.entries_.size());
        entry.source=input;entry.destination=target;entry.name=name;entry.directory=directory;
        entry.relativeDestination=target.lexically_relative(studioContentRoot(project)).generic_string();
        entry.relativeSource=type==StudioTransferType::Move?
            input.lexically_relative(studioContentRoot(project)).generic_string():name;
        const bool targetConflict=target!=input &&
            (existsOrSymlink(target)||existsOrSymlink(studioMetaPath(target)));
        bool invalidMeta=false;
        if(type==StudioTransferType::External && !directory){
            entry.guid=guidFromSidecar(input,invalidMeta);
        }
        const bool guidCollision=!entry.guid.empty() && db.byGuid(entry.guid).has_value();
        entry.conflict=targetConflict?"name":"";
        if(guidCollision)entry.conflict=entry.conflict.empty()?"guid":"name+guid";
        if(invalidMeta)entry.conflict=entry.conflict.empty()?"invalid-meta":entry.conflict+"+invalid-meta";
        if(type==StudioTransferType::External&&!directory&&targetConflict){
            const auto existing=db.byPath(entry.relativeDestination);
            if(existing&&!existing->directory&&fs::is_regular_file(fs::symlink_status(target))&&
               fs::is_regular_file(fs::symlink_status(studioMetaPath(target)))){
                entry.replacementGuid=existing->guid;
                entry.replacementDigest=studioAssetFileDigest(target);
                entry.replacementMetaDigest=studioAssetFileDigest(studioMetaPath(target));
                entry.replacementSourceDigest=studioAssetFileDigest(input);
            }
        }
        result.entries_.push_back(std::move(entry));
    }
    return result;
}
Json StudioTransferPlan::describe() const {
    Json response=Json::object();
    response.set("token",Json::string(token_));
    response.set("source",Json::string(type_==StudioTransferType::Move?"internal":"external"));
    Json items=Json::array();
    for(const auto& e:entries_){
        Json x=Json::object();
        x.set("id",Json::number(int64_t(e.id)));
        x.set("name",Json::string(e.name));
        x.set("from",Json::string(e.relativeSource));
        x.set("to",Json::string(e.relativeDestination));
        x.set("directory",Json::fromBool(e.directory));
        x.set("conflict",Json::string(e.conflict));
        x.set("replacementAvailable",Json::fromBool(!e.replacementGuid.empty()));
        x.set("guid",e.guid.empty()?Json():Json::string(e.guid));
        items.push(std::move(x));
    }
    response.set("items",std::move(items));
    return response;
}
Json StudioTransferPlan::apply(const std::string& token,
                         const std::vector<StudioTransferDecision>& decisions,
                         const std::function<void()>& beforeCommit,
                         std::optional<editor::ProjectHistory::MovePlan>* history,bool repairResources,StudioBatchAssetPlan* staged){
    if(staged&&(!history||beforeCommit))
        throw std::runtime_error("Staged transfer requires history and no live commit hook");
    // Even legacy external callers must not use the destructive replace path.
    std::optional<editor::ProjectHistory::MovePlan> localHistory;
    if(!history&&type_==StudioTransferType::External&&
       std::any_of(decisions.begin(),decisions.end(),[](const auto& d){return d.action=="replace";}))
        history=&localHistory;
    if(history)history->reset();
    if(token!=token_)throw std::runtime_error("Transfer preview expired or changed");
    Json result=Json::object(),copied=Json::array(),errors=Json::array();
    if (type_==StudioTransferType::Move) {
        // Internal moves must not produce partial success. Replacement can
        // destroy target identity and is explicitly refused until backed by a
        // recovery-capable backup protocol. Keep-both is safe if the final
        // destination is resolved before the first rename.
        struct Operation { fs::path source, destination; bool meta = false; };
        std::vector<Operation> operations;
        std::set<int> knownIds;
        for (const auto& e:entries_) knownIds.insert(e.id);
        std::map<int,std::string> actions;
        for (const auto& d:decisions) {
            if (!knownIds.count(d.id) || !actions.emplace(d.id,d.action).second)
                throw std::runtime_error("Duplicate or unknown asset transfer decision");
        }
        std::vector<std::pair<fs::path,fs::path>> planned;
        std::set<fs::path> reserved;
        const fs::path root=studioContentRoot(project_);
        for (const auto& e:entries_) {
            const std::string action=actions.count(e.id) ? actions.at(e.id) :
                (e.conflict.empty() ? "import" : "skip");
            if (action=="skip") continue;
            if (action!="import" && action!="keep-both")
                throw std::runtime_error("Internal move only supports import, keep-both or skip; replace requires an explicit recovery transaction");
            if (!e.conflict.empty() && action=="import")
                throw std::runtime_error("Conflicting asset requires an explicit decision");
            const fs::path source=checkedSource(project_,e.relativeSource);
            const auto parent=e.destination.parent_path().lexically_relative(root).generic_string();
            (void)checkedFolder(project_,parent=="."?"":parent);
            fs::path destination=e.destination;
            if (action=="keep-both") destination=keepBothName(destination,e.directory);
            if (source==destination || existsOrSymlink(destination) ||
                existsOrSymlink(studioMetaPath(destination)) ||
                !reserved.insert(destination).second)
                throw std::runtime_error("Move destination changed or collides with another selected asset");
            const fs::path sourceMeta=studioMetaPath(source);
            const bool hasMeta=existsOrSymlink(sourceMeta);
            if (hasMeta && !fs::is_regular_file(fs::symlink_status(sourceMeta)))
                throw std::runtime_error("Invalid source metadata sidecar");
            if (hasMeta && !reserved.insert(studioMetaPath(destination)).second)
                throw std::runtime_error("Metadata destination collision");
            planned.emplace_back(source,destination);
            operations.push_back({source,destination,false});
            if (hasMeta) operations.push_back({sourceMeta,studioMetaPath(destination),true});
        }
        // A directory move and its child cannot be separate operations; moving
        // the parent first would invalidate the child's preflight source.
        for (std::size_t i=0;i<planned.size();++i)
            for (std::size_t j=i+1;j<planned.size();++j) {
                const auto a=planned[i].first.generic_string();
                const auto b=planned[j].first.generic_string();
                if (a.rfind(b+"/",0)==0 || b.rfind(a+"/",0)==0)
                    throw std::runtime_error("Move selection contains a directory and its descendant");
            }
        std::vector<StudioReferenceRelocation> referenceMoves;
        for(const auto& [from,to]:planned)
            referenceMoves.push_back({"content/"+from.lexically_relative(root).generic_string(),
                "content/"+to.lexically_relative(root).generic_string(),fs::is_directory(fs::symlink_status(from))});
        std::vector<editor::ProjectFileEdit> repairs;
        if(repairResources&&history)repairs=studioPlanGltfRepairs(project_,referenceMoves);
        else studioCheckGltfRelocations(project_,referenceMoves);
        // Root sidecars and material documents retain exact bytes; asset payloads
        // use a streaming digest, including complete directory trees.
        std::optional<editor::ProjectHistory::MovePlan> recorded;
        if(history&&!planned.empty()){
            try {
                editor::ProjectHistory::MovePlan candidate;
                size_t used=0;
                candidate.resourceEdits=repairs;
                for(const auto& edit:repairs)used+=edit.before.size()+edit.after.size()+sizeof(editor::ProjectFileEdit)+edit.path.native().size()*sizeof(fs::path::value_type);
                if(used>editor::ProjectHistory::MaxBytes/2+1024*1024)throw std::runtime_error("Resource repair history budget exceeded");
                const auto capture=[&](const fs::path& file){
                    if(!fs::is_regular_file(fs::symlink_status(file)))throw std::runtime_error("Not a history file");
                    const auto size=fs::file_size(file);
                    if(size>editor::ProjectHistory::MaxBytes-used)throw std::runtime_error("Move history budget");
                    auto bytes=editor::readProjectFileBytes(file);
                    if(bytes.size()>editor::ProjectHistory::MaxBytes-used)throw std::runtime_error("Move history budget");
                    used+=bytes.size();return bytes;
                };
                for(const auto& pair:planned)
                    if(!fs::is_regular_file(fs::symlink_status(studioMetaPath(pair.first))))
                        throw std::runtime_error("Move history requires an existing sidecar");
                ResourceOverlay overlay;
                for(const auto& edit:repairs)overlay.emplace(fs::canonical(project_)/edit.path,edit);
                for(const auto& op:operations){
                    editor::ProjectHistory::MoveFile file;
                    file.from=op.source.lexically_relative(root).generic_string();
                    file.to=op.destination.lexically_relative(root).generic_string();
                    file.directory=!op.meta&&fs::is_directory(fs::symlink_status(op.source));
                    if(op.meta)file.bytes=capture(op.source);
                    else{
                        file.digest=moveDigest(op.source,file.directory);
                        const auto rel="content/"+file.from;
                        const bool affected=std::any_of(repairs.begin(),repairs.end(),[&](const auto& edit){
                            return studioReferenceMatchesPath(edit.path.generic_string(),rel,file.directory);
                        });
                        if(affected){
                            file.digestAfter=moveDigest(op.source,file.directory,&overlay);
                            if(file.digestAfter==file.digest)throw std::runtime_error("Unexpected no-op moved resource repair");
                        }
                    }
                    candidate.files.push_back(std::move(file));
                }
                const auto material=project_/"materials.json";
                if(existsOrSymlink(material))candidate.materialsBefore=capture(material);
                recorded=std::move(candidate);
            }catch(const std::exception&){recorded.reset();}
        }
        if(!repairs.empty()&&!recorded)throw std::runtime_error("Resource repair requires retained move history");
        if(staged){
            // No live rename, catalog mutation or material callback before
            // the outer cursor transaction. Resolve the actual keep-both
            // endpoints, not the original preview's proposed names.
            Json completed=Json::array();
            for(const auto& pair:planned)
                completed.push(Json::string(pair.second.lexically_relative(root).generic_string()));
            Json response=Json::object();
            response.set("files",std::move(completed));response.set("issues",Json::array());
            response.set("repairedResourceDocuments",Json::number(int64_t(repairs.size())));
            size_t materialUpdates=0;
            if(!planned.empty()){
                if(!recorded)throw std::runtime_error("Staged move requires a retained history plan");
                if(recorded->materialsBefore){
                    auto document=Json::parse(*recorded->materialsBefore);
                    if(!document.isObject()||!document["materials"].isObject())
                        throw std::runtime_error("Invalid material library");
                    StudioAssetCatalog catalog(project_);catalog.rebuild(false);
                    std::map<std::string,std::string> relocated;
                    for(const auto& entry:catalog.all()){
                        if(entry.directory)continue;
                        const auto from="content/"+entry.path;
                        const auto to=studioRelocatedReferencePath(from,referenceMoves);
                        if(from!=to)relocated.emplace(from,to);
                    }
                    auto materials=document["materials"];
                    materialUpdates=studioRemapMaterialReferences(materials,relocated);
                    recorded->materialsAfter=*recorded->materialsBefore;
                    if(materialUpdates){
                        document.set("materials",std::move(materials));
                        recorded->materialsAfter=document.dump(2)+"\n";
                    }
                }
                // Reuse the typed ordinary replay planner for directional
                // digests, moving/streamed model edits, GUIDs and URI edges.
                *staged=studioPlanAssetMoveChain(project_,{*recorded},true);
                if(recorded->materialsBefore)
                    staged->documents.push_back({fs::canonical(project_)/"materials.json",
                        *recorded->materialsBefore,*recorded->materialsAfter});
                editor::ProjectHistory probe;auto copy=*recorded;
                probe.recordMove(std::move(copy),"Move assets");
                if(!probe.canUndo())throw std::runtime_error("Staged move exceeds retained history budget");
                *history=std::move(recorded);
            }
            for(const auto& edit:repairs)if(view::materialAssetPath(edit.path.generic_string())){
                const auto before=Json::parse(edit.before),after=Json::parse(edit.after);
                for(const auto* field:kStudioTextureFields)if(before[field]!=after[field])++materialUpdates;
            }
            response.set("repairedMaterialReferences",Json::number(int64_t(materialUpdates)));
            return response;
        }
        StudioMoveJournal journal(project_);
        std::vector<StudioMoveJournalOperation> journalOps;
        journalOps.reserve(operations.size());
        for (const auto& operation:operations)
            journalOps.push_back({
                operation.source.lexically_relative(root).generic_string(),
                operation.destination.lexically_relative(root).generic_string()
            });
        // Publish the recovery plan before the first mutation. This journal is
        // outside content/ and persists until a committed marker is published.
        journal.begin(journalOps,false,false,repairs);
        try {
            for (const auto& operation:operations) {
                StudioMoveJournal::renamePath(operation.source,operation.destination);
                // Persist both directory entries before publishing the final
                // committed marker. A power loss replays the pending journal.
                StudioMoveJournal::syncPath(operation.source.parent_path());
                if (operation.source.parent_path()!=operation.destination.parent_path())
                    StudioMoveJournal::syncPath(operation.destination.parent_path());
            }
            journal.applyResources(studioAbsoluteResourceEdits(project_,repairs,referenceMoves));
            StudioAssetCatalog catalog(project_); catalog.rebuild();
            if (beforeCommit) beforeCommit();
            if(recorded){
                try {
                    const auto material=project_/"materials.json";
                    if(existsOrSymlink(material)){
                        if(fs::file_size(material)>editor::ProjectHistory::MaxBytes)
                            throw std::runtime_error("Move history material budget");
                        recorded->materialsAfter=editor::readProjectFileBytes(material);
                    }
                    if(bool(recorded->materialsBefore)!=bool(recorded->materialsAfter))
                        recorded.reset(); // creation/deletion is not supported by this replay
                    if(recorded)for(const auto& f:recorded->files)
                        if(!matchesMoveFile(root/f.to,f,true))
                            throw std::runtime_error("Asset changed during move");
                }catch(const std::exception&){recorded.reset();}
            }
            if(!repairs.empty()){
                if(!recorded)throw std::runtime_error("Resource repair history could not be retained");
                editor::ProjectHistory probe;auto copy=*recorded;probe.recordMove(std::move(copy),"Move assets");
                if(!probe.canUndo())throw std::runtime_error("Resource repair exceeds retained history budget");
            }
            journal.commit();
        } catch (...) {
            const std::exception_ptr failure=std::current_exception();
            try {
                journal.restoreOrCleanup();
                StudioAssetCatalog catalog(project_); catalog.rebuild();
            } catch (const std::exception& ex) {
                throw std::runtime_error(std::string(
                    "Asset transfer failed and durable recovery is incomplete: ")+ex.what());
            }
            std::rethrow_exception(failure);
        }
        Json completed=Json::array();
        for (const auto& entry:planned)
            completed.push(Json::string(entry.second.lexically_relative(root).generic_string()));
        Json result=Json::object();
        result.set("files",std::move(completed));
        result.set("issues",Json::array());
        result.set("repairedResourceDocuments",Json::number(int64_t(repairs.size())));
        if(history)*history=std::move(recorded);
        return result;
    }

    // Non-replacing imports and replacement-only batches each use a staged
    // journaled publication. Mixed batches must be reviewed separately.
    if(history){
        std::map<int,std::string> actions;
        for(const auto& d:decisions){
            if(d.id<0||size_t(d.id)>=entries_.size()||!actions.emplace(d.id,d.action).second)
                throw std::runtime_error("Unknown or duplicate import decision");
        }
        bool replacement=false,creation=false;
        for(const auto& e:entries_){
            const auto action=actions.count(e.id)?actions.at(e.id):(e.conflict.empty()?"import":"skip");
            if(action=="replace")replacement=true;
            else if(action!="skip")creation=true;
        }
        if(replacement){
            if(creation)throw std::runtime_error("Apply replacements separately from new imports");
            StudioAssetCatalog catalog(project_);catalog.rebuild(false);
            const auto root=studioContentRoot(project_);
            const auto cache=project_/".aiverse-cache"/"studio-asset-trash";
            if(!existsOrSymlink(cache))fs::create_directory(cache);
            if(!fs::is_directory(fs::symlink_status(cache)))throw std::runtime_error("Unsafe replacement storage");
            editor::ProjectHistory::MovePlan plan;plan.replacement=true;
            std::set<std::string> paths;
            for(const auto& e:entries_){
                const auto action=actions.count(e.id)?actions.at(e.id):(e.conflict.empty()?"import":"skip");
                if(action=="skip")continue;
                if(e.directory||e.conflict.find("name")==std::string::npos||
                   !fs::is_regular_file(fs::symlink_status(e.source)))
                    throw std::runtime_error("Replacement requires a regular file and reviewed name conflict");
                const auto relative=e.destination.lexically_relative(root).generic_string();
                const auto live=checkedSource(project_,relative);
                const auto entry=catalog.byPath(relative);
                if(!entry||entry->directory||entry->guid!=e.replacementGuid||
                   !fs::is_regular_file(fs::symlink_status(live))||!paths.insert(relative).second)
                    throw std::runtime_error("Replacement target changed");
                const auto meta=studioMetaPath(live);
                if(fs::file_size(meta)>1024*1024)throw std::runtime_error("Replacement metadata exceeds history budget");
                const auto sidecar=editor::readProjectFileBytes(meta);
                const auto oldDigest=studioAssetFileDigest(live);
                const auto newDigest=studioAssetFileDigest(e.source);
                if(oldDigest!=e.replacementDigest||newDigest!=e.replacementSourceDigest||
                   studioAssetFileDigest(meta)!=e.replacementMetaDigest)
                    throw std::runtime_error("Replacement source or target changed since preview; review again");
                if(oldDigest==newDigest)continue;
                const auto reserve=[&]{
                    const auto id=nonce();const auto folder=cache/id;
                    if(!fs::create_directory(folder))throw std::runtime_error("Cannot reserve replacement slot");
                    return ".aiverse-cache/studio-asset-trash/"+id+"/asset";
                };
                checkRecoveryStagingSpace(project_,e.source);
                const auto oldSlot=reserve(),newSlot=reserve();
                fs::copy_file(e.source,project_/newSlot,fs::copy_options::none);
                if(studioAssetFileDigest(project_/newSlot)!=newDigest)
                    throw std::runtime_error("Replacement source changed during staging");
                StudioMoveJournal::syncPath(project_/newSlot);
                for(const auto& slot:{oldSlot,newSlot}){
                    const auto folder=(project_/slot).parent_path();
                    Json record=Json::object();
                    record.set("kind",Json::string("replacement"));record.set("path",Json::string(relative));
                    record.set("guid",Json::string(entry->guid));
                    record.set("digest",Json::string(slot==oldSlot?oldDigest:newDigest));
                    record.set("role",Json::string(slot==oldSlot?"old":"new"));
                    {std::ofstream out(folder/"record.json");out<<record.dump(2)<<"\n";out.close();if(!out)throw std::runtime_error("Cannot write replacement record");}
                    StudioMoveJournal::syncPath(folder/"record.json");StudioMoveJournal::syncPath(folder);
                }
                plan.files.push_back({relative,oldSlot,sidecar,oldDigest,false});
                plan.files.push_back({relative,newSlot,{},newDigest,false});
            }
            StudioMoveJournal::syncPath(cache);StudioMoveJournal::syncPath(cache.parent_path());
            if(!plan.files.empty()){
                studioReplayAssetReplacement(project_,plan,true,staged);
                for(size_t i=0;i<plan.files.size();i+=2)copied.push(Json::string(plan.files[i].from));
                *history=std::move(plan);
            }
            result.set("files",std::move(copied));result.set("issues",std::move(errors));return result;
        }
        {
            const auto root=studioContentRoot(project_);
            editor::ProjectHistory::MovePlan plan;plan.quarantine=true;plan.created=true;
            std::set<fs::path> reserved;
            struct Import {const StudioTransferEntry* entry;fs::path destination;bool preserve;};
            std::vector<Import> imports;
            for(const auto& e:entries_){
                const auto action=actions.count(e.id)?actions.at(e.id):(e.conflict.empty()?"import":"skip");
                if(action=="skip")continue;
                if(action!="import"&&action!="keep-both"&&action!="new-guid")
                    throw std::runtime_error("Unknown import decision");
                if(action=="import"&&!e.conflict.empty())throw std::runtime_error("Import conflict requires decision");
                if(action=="new-guid"&&(e.directory||e.conflict.find("name")!=std::string::npos))
                    throw std::runtime_error("New GUID cannot resolve this conflict");
                const auto parent=e.destination.parent_path().lexically_relative(root).generic_string();
                (void)checkedFolder(project_,parent=="."?"":parent);
                const auto status=fs::symlink_status(e.source);
                if(e.directory?!fs::is_directory(status):!fs::is_regular_file(status))
                    throw std::runtime_error("Import source changed since preview");
                auto destination=action=="keep-both"?keepBothName(e.destination,e.directory):e.destination;
                if(existsOrSymlink(destination)||existsOrSymlink(studioMetaPath(destination))||
                   !reserved.insert(destination).second||!reserved.insert(studioMetaPath(destination)).second)
                    throw std::runtime_error("Import destinations collide; refresh the preview");
                const bool preserve=!e.directory&&action=="import"&&!e.guid.empty();
                if(preserve){
                    bool invalid=false;
                    if(guidFromSidecar(e.source,invalid)!=e.guid||invalid)
                        throw std::runtime_error("Import source GUID changed since preview");
                }
                imports.push_back({&e,destination,preserve});
            }
            for(const auto& i:imports)
                stageCreatedAsset(project_,i.destination.lexically_relative(root).generic_string(),
                    i.entry->source,i.entry->directory,i.preserve,plan);
            if(!plan.files.empty()){
                studioReplayAssetQuarantine(project_,plan,false,{},staged);
                for(const auto& i:imports)copied.push(Json::string(i.destination.lexically_relative(root).generic_string()));
                *history=std::move(plan);
            }
            result.set("files",std::move(copied));result.set("issues",std::move(errors));return result;
        }
    }
    std::set<int> used;
    for(const auto& e:entries_){
        std::string action=e.conflict.empty()?"import":"skip";
        for(const auto& d:decisions)if(d.id==e.id){
            if(!used.insert(d.id).second)throw std::runtime_error("Repeated conflict decision");
            action=d.action;
        }
        if(action!="import"&&action!="skip"&&action!="keep-both"&&
           action!="replace"&&action!="new-guid")
            throw std::runtime_error("Unknown conflict resolution");
        if(action=="skip")continue;
        if(!e.conflict.empty()&&action=="import")
            throw std::runtime_error("Conflicting asset requires an explicit decision");
        if(e.directory&&(action=="replace"||action=="new-guid"))
            throw std::runtime_error("Folder replacement requires a non-destructive keep-both decision");
        if(type_==StudioTransferType::Move&&action=="new-guid")
            throw std::runtime_error("Move must preserve the asset GUID");
        // The preview may be minutes old; verify roots again before writing.
        const fs::path contentRoot=studioContentRoot(project_);
        const auto relativeParent=e.destination.parent_path().lexically_relative(contentRoot).generic_string();
        (void)checkedFolder(project_,relativeParent=="."?"":relativeParent);
        if(type_==StudioTransferType::Move)
            (void)checkedSource(project_,e.relativeSource);
        else {
            const auto fresh=fs::symlink_status(e.source);
            if(fs::is_symlink(fresh) ||
               (!fs::is_regular_file(fresh)&&!fs::is_directory(fresh)))
                throw std::runtime_error("External source changed since preview");
        }
        fs::path destination=e.destination;
        if(action=="keep-both")destination=keepBothName(destination,e.directory);
        const bool exists=existsOrSymlink(destination);
        if(exists && action!="replace")
            throw std::runtime_error("Content changed after preview. Refresh the transfer plan");
        if(action=="replace" && (!exists||e.directory||fs::is_directory(destination)))
            throw std::runtime_error("Only existing regular files can be replaced");
        if(action=="new-guid"&&exists)
            throw std::runtime_error("New GUID does not resolve a filename collision");
        if((action=="import"||action=="new-guid") && !e.conflict.empty() &&
           e.conflict.find("name")!=std::string::npos)
            throw std::runtime_error("Filename collision must be resolved separately");
        try{
            if(type_==StudioTransferType::Move) {
                if(action=="replace"){
                    const auto backup=fs::path(destination.string()+".aiverse-backup-"+token_);
                    fs::rename(destination,backup);
                    try{fs::rename(e.source,destination);}
                    catch(...){fs::rename(backup,destination);throw;}
                    std::error_code ec;fs::remove(backup,ec);
                    // Keep destination .meta GUID. Remove source .meta identity.
                    fs::remove(studioMetaPath(e.source),ec);
                } else movePair(e.source,destination);
            } else if(e.directory) {
                copyFolderNoSymlinks(e.source,destination);
            } else {
                const auto sourceMeta=studioMetaPath(e.source);
                if(action=="replace"){
                    const auto backup=fs::path(destination.string()+".aiverse-backup-"+token_);
                    fs::rename(destination,backup);
                    try{fs::copy_file(e.source,destination,fs::copy_options::none);}
                    catch(...){fs::rename(backup,destination);throw;}
                    std::error_code ec;fs::remove(backup,ec);
                    // Preserve target GUID and import settings.
                } else {
                    fs::copy_file(e.source,destination,fs::copy_options::none);
                    if(action!="new-guid"&&action!="keep-both" && !e.guid.empty() &&
                       existsOrSymlink(sourceMeta) && e.conflict.empty()){
                        try{fs::copy_file(sourceMeta,studioMetaPath(destination),
                                          fs::copy_options::none);}
                        catch(...){fs::remove(destination);throw;}
                    }
                    // keep-both and new-guid get fresh .meta when catalog rebuilds.
                }
            }
            copied.push(Json::string(destination.lexically_relative(
                studioContentRoot(project_)).generic_string()));
        }catch(const std::exception& ex){
            Json problem=Json::object();
            problem.set("name",Json::string(e.name));
            problem.set("message",Json::string(ex.what()));
            errors.push(std::move(problem));
        }
    }
    StudioAssetCatalog catalog(project_);catalog.rebuild();
    result.set("files",std::move(copied));
    result.set("issues",std::move(errors));
    return result;
}
void studioReplayAssetMove(const fs::path& project,
                           const editor::ProjectHistory::MovePlan& plan,bool redo,
                           const std::function<void()>& validateReferences,StudioBatchAssetPlan* staged){
    const auto root=studioContentRoot(project);
    if(plan.files.empty()||plan.files.size()>128||plan.files.size()%2)
        throw std::runtime_error("Invalid recorded asset move");
    const auto& materialBefore=redo?plan.materialsBefore:plan.materialsAfter;
    const auto& materialAfter=redo?plan.materialsAfter:plan.materialsBefore;
    const auto material=fs::canonical(project)/"materials.json";
    const auto checkMaterial=[&]{
        if(bool(materialBefore)!=existsOrSymlink(material)||
           (materialBefore&&editor::readProjectFileBytes(material)!=*materialBefore))
            throw std::runtime_error("Material library changed since asset move");
    };
    if(bool(materialBefore)!=bool(materialAfter))throw std::runtime_error("Unsupported material topology change");
    if(materialAfter&&!Json::parse(*materialAfter)["materials"].isObject())
        throw std::runtime_error("Invalid recorded material library");
    checkMaterial();
    StudioAssetCatalog catalog(project);catalog.rebuild(false);
    std::set<fs::path> reserved;
    std::vector<StudioMoveJournalOperation> operations;
    auto checked=[&](const std::string& rel,bool sidecar,bool source,bool directory){
        const fs::path p(rel);
        if(rel.empty()||p.is_absolute()||rel.size()>1024)throw std::runtime_error("Invalid recorded move path");
        const auto parent=p.parent_path().generic_string();
        const auto folder=checkedFolder(project,parent=="."?"":parent);
        if(sidecar){
            if(p.extension()!=".meta")throw std::runtime_error("Expected metadata sidecar");
            validateSegment(p.stem().string());
        }else validateSegment(p.filename().string());
        const auto result=folder/p.filename();
        if(!reserved.insert(result).second)throw std::runtime_error("Overlapping recorded move paths");
        if(source?!(directory?fs::is_directory(fs::symlink_status(result)):fs::is_regular_file(fs::symlink_status(result))):existsOrSymlink(result))
            throw std::runtime_error("Asset move history path changed or collides: "+rel);
        return result;
    };
    for(size_t i=0;i<plan.files.size();i+=2){
        const auto& asset=plan.files[i];const auto& meta=plan.files[i+1];
        if(meta.from!=asset.from+".meta"||meta.to!=asset.to+".meta")
            throw std::runtime_error("Recorded asset/metadata pair mismatch");
        const auto doc=Json::parse(meta.bytes);
        const auto indexed=catalog.byGuid(doc.getStr("guid"));
        const auto& source=redo?asset.from:asset.to;
        if(!indexed||indexed->directory!=asset.directory||indexed->path!=source||indexed->importer!=doc.getStr("importer"))
            throw std::runtime_error("Asset GUID or importer changed since move");
        for(size_t j=i;j<i+2;++j){
            const auto& f=plan.files[j];const auto& from=redo?f.from:f.to;const auto& to=redo?f.to:f.from;
            const auto src=checked(from,j!=i,true,f.directory);
            (void)checked(to,j!=i,false,f.directory);
            if(!matchesMoveFile(src,f,!redo))
                throw std::runtime_error("Asset move history source bytes changed: "+from);
            operations.push_back({from,to});
        }
    }
    if(validateReferences)validateReferences();
    std::vector<StudioReferenceRelocation> referenceMoves;
    for(size_t i=0;i<operations.size();i+=2)
        referenceMoves.push_back({"content/"+operations[i].from,"content/"+operations[i].to,plan.files[i].directory});
    auto repairs=redo?plan.resourceEdits:editor::reverseProjectFileEdits(plan.resourceEdits);
    if(!redo){
        std::vector<StudioReferenceRelocation> forward;
        for(size_t i=0;i<plan.files.size();i+=2)
            forward.push_back({"content/"+plan.files[i].from,"content/"+plan.files[i].to,plan.files[i].directory});
        for(auto& edit:repairs)edit.path=studioRelocatedReferencePath(edit.path.generic_string(),forward);
        std::sort(repairs.begin(),repairs.end(),[](const auto& a,const auto& b){return a.path.generic_string()<b.path.generic_string();});
    }
    if(repairs.empty())studioCheckGltfRelocations(project,referenceMoves);
    else{
        if(staged)throw std::runtime_error("Resource repair history requires individual navigation");
        const auto generated=studioPlanGltfRepairs(project,referenceMoves,&repairs);
        if(generated.size()!=repairs.size())throw std::runtime_error("Resource repair history dependencies changed");
        for(size_t i=0;i<repairs.size();++i){
            const auto& saved=repairs[i];const auto& expected=generated[i];
            const auto destination=studioRelocatedReferencePath(saved.path.generic_string(),referenceMoves);
            if(saved.path!=expected.path||saved.before!=expected.before||
               saved.tailDigest!=expected.tailDigest||saved.tailBytes!=expected.tailBytes||
               studioCanonicalGltfRepair(destination,saved.after,saved.tailDigest,saved.tailBytes)!=
               studioCanonicalGltfRepair(destination,expected.after,expected.tailDigest,expected.tailBytes))
                throw std::runtime_error("Resource repair history no longer matches typed dependencies");
        }
    }
    checkMaterial();
    if(staged){
        for(size_t i=0;i<operations.size();++i){
            const auto& op=operations[i];const auto& f=plan.files[i];
            staged->relocations.push_back({"content/"+op.from,"content/"+op.to,moveDigest(root/op.from,f.directory),f.directory});
        }
        if(materialBefore)staged->documents.push_back({material,*materialBefore,*materialAfter});
        return;
    }
    StudioMoveJournal journal(project);journal.begin(operations,false,false,repairs);
    try{
        for(size_t i=0;i<operations.size();++i){
            const auto& op=operations[i];
            const auto from=root/op.from,to=root/op.to;
            if(!matchesMoveFile(from,plan.files[i],!redo))
                throw std::runtime_error("Asset changed during history replay");
            if(existsOrSymlink(to))throw std::runtime_error("Asset move destination occupied");
            StudioMoveJournal::renamePath(from,to);
            StudioMoveJournal::syncPath(from.parent_path());
            if(from.parent_path()!=to.parent_path())StudioMoveJournal::syncPath(to.parent_path());
        }
        for(size_t i=0;i<operations.size();++i)
            if(!matchesMoveFile(root/operations[i].to,plan.files[i],!redo))
                throw std::runtime_error("Asset bytes changed during replay");
        auto documents=studioAbsoluteResourceEdits(project,repairs,referenceMoves);
        if(materialBefore)documents.push_back({material,*materialBefore,*materialAfter});
        else checkMaterial();
        journal.applyResources(documents);
        for(size_t i=0;i<operations.size();++i)
            if(!matchesMoveFile(root/operations[i].to,plan.files[i],redo))
                throw std::runtime_error("Repaired asset bytes changed during replay");
        catalog.rebuild(false);
        for(size_t i=0;i<plan.files.size();i+=2){
            const auto& asset=plan.files[i];const auto& meta=plan.files[i+1];
            const auto indexed=catalog.byGuid(Json::parse(meta.bytes).getStr("guid"));
            if(!indexed||indexed->path!=(redo?asset.to:asset.from))
                throw std::runtime_error("Asset move history index reconciliation failed");
        }
        journal.commit();
    }catch(...){
        const auto failure=std::current_exception();
        try{journal.restoreOrCleanup();catalog.rebuild(false);}
        catch(const std::exception& e){throw std::runtime_error(std::string("Asset history recovery incomplete: ")+e.what());}
        std::rethrow_exception(failure);
    }
}
namespace {
fs::path quarantineFolder(const fs::path& project,const std::string& relative) {
    const fs::path p(relative);
    const auto parent=p.parent_path();
    const auto id=parent.filename().string();
    if(parent.parent_path().generic_string()!=".aiverse-cache/studio-asset-trash"||
       id.size()!=24||id.find_first_not_of("0123456789abcdef")!=std::string::npos||p.filename()!="asset")
        throw std::runtime_error("Invalid asset quarantine identity");
    fs::path current=fs::canonical(project);
    for(const auto& component:parent){
        current/=component;
        if(!fs::is_directory(fs::symlink_status(current)))throw std::runtime_error("Unsafe asset quarantine directory");
    }
    return current;
}
}
void studioReplayAssetQuarantine(const fs::path& project,
    const editor::ProjectHistory::MovePlan& plan,bool deleted,const std::function<void()>& validateReferences,StudioBatchAssetPlan* staged){
    if(!plan.quarantine||plan.files.empty()||plan.files.size()>128||plan.files.size()%2)
        throw std::runtime_error("Invalid asset quarantine plan");
    const auto root=studioContentRoot(project);(void)root;
    StudioAssetCatalog catalog(project);catalog.rebuild(false);
    std::vector<StudioMoveJournalOperation> operations;
    std::set<std::string> paths,guids;
    for(size_t i=0;i<plan.files.size();i+=2){
        const auto& asset=plan.files[i];const auto& meta=plan.files[i+1];
        if(meta.from!=asset.from+".meta"||meta.to!=asset.to+".meta"||asset.digest.empty())
            throw std::runtime_error("Invalid asset quarantine pair");
        const auto trash=quarantineFolder(project,asset.to);
        const auto relative=fs::path(asset.from);
        validateSegment(relative.filename().string());
        const auto parent=relative.parent_path().generic_string();
        const auto live=checkedFolder(project,parent=="."?"":parent)/relative.filename();
        const auto stored=trash/"asset";
        const auto document=Json::parse(meta.bytes);
        const auto guid=document.getStr("guid");
        if(!studioValidGuid(guid)||!guids.insert(guid).second)
            throw std::runtime_error("Invalid or duplicate quarantined GUID");
        const auto indexed=catalog.byGuid(guid);
        if(deleted?(!indexed||indexed->path!=asset.from||indexed->directory!=asset.directory||
             indexed->importer!=document.getStr("importer")):bool(indexed))
            throw std::runtime_error("Asset quarantine GUID conflict");
        if(!paths.insert(asset.from).second||!paths.insert(asset.to).second)
            throw std::runtime_error("Duplicate quarantine path");
        const auto source=deleted?live:stored,destination=deleted?stored:live;
        if(existsOrSymlink(destination)||existsOrSymlink(studioMetaPath(destination))||
           !matchesMoveFile(source,asset)||!matchesMoveFile(studioMetaPath(source),meta))
            throw std::runtime_error("Asset quarantine source or destination changed");
        const std::string liveRel="content/"+asset.from;
        operations.push_back(deleted?StudioMoveJournalOperation{liveRel,asset.to}:StudioMoveJournalOperation{asset.to,liveRel});
        operations.push_back(deleted?StudioMoveJournalOperation{liveRel+".meta",meta.to}:StudioMoveJournalOperation{meta.to,liveRel+".meta"});
    }
    if(deleted&&validateReferences)validateReferences();
    if(!staged)checkRecoveryGrowth(project,operations);
    if(staged){
        for(size_t i=0;i<operations.size();++i){
            const auto& op=operations[i];const auto& f=plan.files[i];
            staged->relocations.push_back({op.from,op.to,moveDigest(project/op.from,f.directory),f.directory});
        }
        return;
    }
    StudioMoveJournal journal(project);journal.begin(operations,true);
    try{
        for(size_t i=0;i<operations.size();++i){
            const auto from=project/operations[i].from,to=project/operations[i].to;
            if(existsOrSymlink(to)||!matchesMoveFile(from,plan.files[i]))
                throw std::runtime_error("Asset quarantine changed during replay");
            StudioMoveJournal::renamePath(from,to);
            StudioMoveJournal::syncPath(from.parent_path());StudioMoveJournal::syncPath(to.parent_path());
        }
        catalog.rebuild(false);
        for(size_t i=0;i<plan.files.size();i+=2){
            const auto& asset=plan.files[i];const auto& meta=plan.files[i+1];
            const auto after=catalog.byGuid(Json::parse(meta.bytes).getStr("guid"));
            if(deleted?bool(after):(!after||after->path!=asset.from||after->directory!=asset.directory))
                throw std::runtime_error("Asset quarantine index mismatch");
        }
        journal.commit();
    }catch(...){
        const auto failure=std::current_exception();
        try{journal.restoreOrCleanup();catalog.rebuild(false);}
        catch(const std::exception& e){throw std::runtime_error(std::string("Asset quarantine recovery incomplete: ")+e.what());}
        std::rethrow_exception(failure);
    }
}
editor::ProjectHistory::MovePlan studioQuarantineAsset(const fs::path& project,
    const std::string& path,const std::string& expectedGuid,const std::function<void()>& validateReferences,StudioBatchAssetPlan* staged){
    const auto source=checkedSource(project,path);
    StudioAssetCatalog catalog(project);catalog.rebuild(false);
    const auto current=catalog.byGuid(expectedGuid);
    if(!current||current->path!=path)throw std::runtime_error("Asset changed; refresh before deleting");
    const auto root=project/".aiverse-cache"/"studio-asset-trash";
    if(!existsOrSymlink(root))fs::create_directory(root);
    if(!fs::is_directory(fs::symlink_status(root)))throw std::runtime_error("Unsafe asset quarantine root");
    const auto id=nonce();
    const auto folder=root/id;
    if(!fs::create_directory(folder))throw std::runtime_error("Cannot reserve asset quarantine");
    const auto relative=".aiverse-cache/studio-asset-trash/"+id+"/asset";
    editor::ProjectHistory::MovePlan plan;plan.quarantine=true;
    const auto meta=studioMetaPath(source);
    if(!fs::is_regular_file(fs::symlink_status(meta)))throw std::runtime_error("Invalid asset quarantine sidecar");
    if(fs::file_size(meta)>1024*1024)throw std::runtime_error("Asset metadata too large for history");
    plan.files.push_back({path,relative,{},moveDigest(source,current->directory),current->directory});
    plan.files.push_back({path+".meta",relative+".meta",editor::readProjectFileBytes(meta),{},false});
    Json recovery=Json::object();
    recovery.set("path",Json::string(path));recovery.set("guid",Json::string(expectedGuid));
    recovery.set("directory",Json::fromBool(current->directory));
    recovery.set("digest",Json::string(plan.files.front().digest));
    recovery.set("metaDigest",Json::string(studioAssetFileDigest(meta)));
    const auto record=folder/"record.json";
    {std::ofstream out(record,std::ios::binary);out<<recovery.dump(2)<<"\n";out.close();if(!out)throw std::runtime_error("Cannot write asset quarantine record");}
    StudioMoveJournal::syncPath(record);StudioMoveJournal::syncPath(folder);StudioMoveJournal::syncPath(root);
    // Leave the record and any payload on disk even if the in-memory history is
    // later evicted. Manual recovery is always possible; no automatic purge.
    studioReplayAssetQuarantine(project,plan,true,validateReferences,staged);
    return plan;
}
void studioReplayAssetReplacement(const fs::path& project,
    const editor::ProjectHistory::MovePlan& plan,bool redo,StudioBatchAssetPlan* staged){
    if(!plan.replacement||plan.files.empty()||plan.files.size()>128||plan.files.size()%2)
        throw std::runtime_error("Invalid asset replacement plan");
    StudioAssetCatalog catalog(project);catalog.rebuild(false);
    std::vector<StudioMoveJournalOperation> operations;
    std::set<std::string> unique;
    for(size_t i=0;i<plan.files.size();i+=2){
        const auto& old=plan.files[i];const auto& next=plan.files[i+1];
        if(old.from!=next.from||old.directory||next.directory||!unique.insert(old.from).second)
            throw std::runtime_error("Invalid replacement target");
        const auto live=checkedSource(project,old.from);
        (void)quarantineFolder(project,old.to);(void)quarantineFolder(project,next.to);
        const auto& outgoing=redo?old:next;const auto& incoming=redo?next:old;
        const auto document=Json::parse(old.bytes);
        const auto entry=catalog.byGuid(document.getStr("guid"));
        if(!entry||entry->directory||entry->path!=old.from||entry->importer!=document.getStr("importer")||
           editor::readProjectFileBytes(studioMetaPath(live))!=old.bytes||
           studioAssetFileDigest(live)!=outgoing.digest||existsOrSymlink(project/outgoing.to)||
           studioAssetFileDigest(project/incoming.to)!=incoming.digest)
            throw std::runtime_error("Asset replacement history conflict: "+old.from);
        operations.push_back({"content/"+old.from,outgoing.to,outgoing.digest});
        operations.push_back({incoming.to,"content/"+old.from,incoming.digest});
    }
    if(!staged)checkRecoveryGrowth(project,operations);
    if(staged){
        for(const auto& op:operations)staged->relocations.push_back({op.from,op.to,op.digest,false});
        for(size_t i=0;i<plan.files.size();i+=2){
            const auto& old=plan.files[i];
            staged->documents.push_back({studioMetaPath(studioContentRoot(project)/old.from),old.bytes,old.bytes});
        }
        return;
    }
    StudioMoveJournal journal(project);journal.begin(operations,true,true);
    try{
        for(const auto& op:operations){
            const auto from=project/op.from,to=project/op.to;
            if(existsOrSymlink(to)||studioAssetFileDigest(from)!=op.digest)
                throw std::runtime_error("Replacement changed during publication");
            StudioMoveJournal::renamePath(from,to);
            StudioMoveJournal::syncPath(from.parent_path());StudioMoveJournal::syncPath(to.parent_path());
        }
        catalog.rebuild(false);
        for(size_t i=0;i<plan.files.size();i+=2){
            const auto& old=plan.files[i];const auto& next=plan.files[i+1];
            const auto live=studioContentRoot(project)/old.from;
            if(editor::readProjectFileBytes(studioMetaPath(live))!=old.bytes||
               studioAssetFileDigest(live)!=(redo?next.digest:old.digest))
                throw std::runtime_error("Replacement result changed");
        }
        journal.commit();
    }catch(...){
        const auto failure=std::current_exception();
        try{journal.restoreOrCleanup();catalog.rebuild(false);}
        catch(const std::exception& e){throw std::runtime_error(std::string("Replacement recovery incomplete: ")+e.what());}
        std::rethrow_exception(failure);
    }
}
namespace {
constexpr const char* trashPrefix=".aiverse-cache/studio-asset-trash/";
std::string trashRelative(const std::string& id){
    if(id.size()!=24||id.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid recovery record ID");
    return std::string(trashPrefix)+id+"/asset";
}
void checkTrashJournal(const fs::path& project){
    if(existsOrSymlink(project/".aiverse-cache"/"studio-move-journal")||
       existsOrSymlink(project/".aiverse-cache"/"studio-history-batch"))
        throw std::runtime_error("A pending asset journal needs recovery; reopen the project");
}
struct TrashRecord {
    std::string id,relative,path,guid,kind,role,digest,raw,meta;
    fs::path folder;
    bool directory=false;
};
TrashRecord readTrashRecord(const fs::path& project,const std::string& id,bool verify){
    TrashRecord r;r.id=id;r.relative=trashRelative(id);r.folder=quarantineFolder(project,r.relative);
    const auto record=r.folder/"record.json";
    if(!fs::is_regular_file(fs::symlink_status(record))||fs::file_size(record)>65536)
        throw std::runtime_error("Invalid recovery manifest");
    r.raw=editor::readProjectFileBytes(record);const auto doc=Json::parse(r.raw);
    r.path=doc.getStr("path");r.guid=doc.getStr("guid");r.kind=doc.getStr("kind","deleted");
    r.role=doc.getStr("role");r.digest=doc.getStr("digest");r.directory=doc.getBool("directory");
    if(!studioValidGuid(r.guid)||r.digest.size()!=64||r.digest.find_first_not_of("0123456789abcdef")!=std::string::npos||
       (r.kind!="deleted"&&r.kind!="created"&&r.kind!="replacement")||r.path.empty()||r.path.size()>512||
       fs::path(r.path).is_absolute()||(r.kind=="replacement"&&(r.directory||(r.role!="old"&&r.role!="new"))))
        throw std::runtime_error("Invalid recovery record identity");
    for(const auto& part:fs::path(r.path))validateSegment(part.string());
    const auto payload=r.folder/"asset",meta=r.folder/"asset.meta";
    const auto status=fs::symlink_status(payload);
    if(r.directory?!fs::is_directory(status):!fs::is_regular_file(status))
        throw std::runtime_error("Recovery payload missing or invalid");
    if(r.kind!="replacement"){
        if(!fs::is_regular_file(fs::symlink_status(meta))||fs::file_size(meta)>1024*1024)
            throw std::runtime_error("Recovery sidecar missing or invalid");
        r.meta=editor::readProjectFileBytes(meta);const auto m=Json::parse(r.meta);
        if(m.getStr("guid")!=r.guid||m.getStr("importer")!=studioImporterForPath(fs::path(r.path),r.directory)||!m["settings"].isObject())
            throw std::runtime_error("Recovery sidecar identity mismatch");
        const auto expected=doc.getStr("metaDigest");
        if(!expected.empty()&&studioAssetFileDigest(meta)!=expected)throw std::runtime_error("Recovery sidecar changed");
    }else if(existsOrSymlink(meta))throw std::runtime_error("Unexpected replacement sidecar");
    if(verify&&moveDigest(payload,r.directory)!=r.digest)throw std::runtime_error("Recovery payload changed");
    return r;
}
std::string trashToken(const TrashRecord& r,const std::string& action,const std::string& liveDigest,const std::string& liveMeta){
    pack::Sha256 hash;
    for(const auto* value:{&r.id,&r.raw,&r.meta,&action,&liveDigest,&liveMeta}){
        const auto size=std::to_string(value->size())+":";
        hash.update(size.data(),size.size());hash.update(value->data(),value->size());
    }
    const auto digest=hash.finish();return pack::toHex(digest.data(),digest.size());
}
struct TrashReview {TrashRecord record;std::string liveDigest,liveMeta,token;};
TrashReview reviewTrash(const fs::path& project,const std::string& id,const std::string& action){
    checkTrashJournal(project);
    if(action!="restore"&&action!="purge")throw std::runtime_error("Invalid recovery action");
    TrashReview result;result.record=readTrashRecord(project,id,true);const auto& r=result.record;
    // Never purge an unrecognized extra file or partially staged record.
    for(const auto& item:fs::directory_iterator(r.folder)){
        const auto name=item.path().filename().string();
        if(name!="asset"&&name!="asset.meta"&&name!="record.json")
            throw std::runtime_error("Recovery slot contains unexpected files; inspect manually");
    }
    if(action=="restore"){
        StudioAssetCatalog catalog(project);catalog.rebuild(false);
        const auto relative=fs::path(r.path);
        const auto parent=relative.parent_path().generic_string();
        const auto live=checkedFolder(project,parent=="."?"":parent)/relative.filename();
        if(r.kind=="replacement"){
            const auto entry=catalog.byGuid(r.guid);
            if(!entry||entry->path!=r.path||entry->directory)throw std::runtime_error("Replacement target GUID/path no longer matches");
            result.liveDigest=studioAssetFileDigest(live);
            if(result.liveDigest==r.digest)throw std::runtime_error("This version is already active");
            if(fs::file_size(studioMetaPath(live))>1024*1024)throw std::runtime_error("Target metadata exceeds recovery limit");
            result.liveMeta=editor::readProjectFileBytes(studioMetaPath(live));
            const auto m=Json::parse(result.liveMeta);
            if(m.getStr("guid")!=r.guid||m.getStr("importer")!=entry->importer)
                throw std::runtime_error("Replacement target sidecar changed");
        }else if(existsOrSymlink(live)||existsOrSymlink(studioMetaPath(live))||catalog.byGuid(r.guid))
            throw std::runtime_error("Original path or GUID is occupied");
    }
    result.token=trashToken(r,action,result.liveDigest,result.liveMeta);return result;
}
Json trashRow(const TrashRecord& r){
    Json row=Json::object();
    row.set("id",Json::string(r.id));row.set("path",Json::string(r.path));row.set("guid",Json::string(r.guid));
    row.set("kind",Json::string(r.kind));row.set("role",Json::string(r.role));row.set("directory",Json::fromBool(r.directory));
    row.set("status",Json::string("retained"));
    size_t visited=0;
    row.set("bytes",Json::number(int64_t(recoveryBytes(r.folder/"asset",visited))));
    row.set("slotBytes",Json::number(int64_t(recoveryBytes(r.folder,visited))));
    return row;
}
}
Json studioAssetTrashList(const fs::path& project,size_t offset,size_t limit){
    if(limit==0||limit>100||offset>10000)throw std::runtime_error("Invalid recovery page");
    Json out=Json::object(),rows=Json::array();
    const auto root=project/".aiverse-cache"/"studio-asset-trash";
    std::vector<std::string> ids;bool truncated=false;
    if(existsOrSymlink(root)){
        if(!fs::is_directory(fs::symlink_status(root))||!fs::is_directory(fs::symlink_status(root.parent_path())))
            throw std::runtime_error("Unsafe recovery storage");
        for(const auto& item:fs::directory_iterator(root)){
            if(ids.size()>=10000){truncated=true;break;}
            const auto id=item.path().filename().string();
            if(id.size()!=24||id.find_first_not_of("0123456789abcdef")!=std::string::npos)continue;
            if(!fs::is_directory(item.symlink_status()))continue;
            if(!existsOrSymlink(item.path()/"asset")&&!existsOrSymlink(item.path()/"asset.meta"))continue;
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(),ids.end());
    for(size_t i=offset;i<ids.size()&&rows.size()<limit;++i){
        try{rows.push(trashRow(readTrashRecord(project,ids[i],false)));}
        catch(const std::exception& e){
            Json row=Json::object();row.set("id",Json::string(ids[i]));row.set("status",Json::string("invalid"));
            row.set("error",Json::string(e.what()));rows.push(std::move(row));
        }
    }
    out.set("items",std::move(rows));out.set("total",Json::number(int64_t(ids.size())));
    out.set("truncated",Json::fromBool(truncated));
    const auto cache=recoveryCache(project);
    std::string reason;
    if(existsOrSymlink(cache/"studio-history-batch"))reason="Pending coordinated asset transaction; reopen project to recover.";
    else if(existsOrSymlink(cache/"studio-move-journal"))reason="Pending asset move transaction; reopen project to recover.";
    else if(existsOrSymlink(cache/"project-document-journal"))reason="Pending project document transaction; reopen project to recover.";
    else if(existsOrSymlink(cache/"asset-history.dirty"))reason="Interrupted legacy asset history publication; history is fenced. Recovery payloads are retained.";
    out.set("recoveryPending",Json::fromBool(!reason.empty()));
    if(!reason.empty())out.set("recoveryReason",Json::string(reason));
    try{out.set("storage",studioRecoveryStorage(project));}
    catch(const std::exception& e){out.set("storageError",Json::string(e.what()));}
    return out;
}
std::vector<std::string> studioProtectedTrashIds(const editor::ProjectHistory& history){
    std::set<std::string> ids;
    for(const auto& step:history.steps()){
        if(!step.move)continue;
        for(const auto& f:step.move->files){
            if(!inRecoveryTrash(f.to))continue;
            const auto id=fs::path(f.to).parent_path().filename().string();
            if(id.size()==24&&id.find_first_not_of("0123456789abcdef")==std::string::npos)ids.insert(id);
        }
    }
    return {ids.begin(),ids.end()};
}
Json studioReviewTrashBatch(const fs::path& project,const std::vector<std::string>& requested,const editor::ProjectHistory& history){
    checkTrashJournal(project);
    if(requested.empty()||requested.size()>50)throw std::runtime_error("Select 1–50 recovery records");
    const std::set<std::string> ids(requested.begin(),requested.end());
    if(ids.size()!=requested.size())throw std::runtime_error("Repeated recovery record");
    const auto protectedIds=studioProtectedTrashIds(history);
    Json items=Json::array();
    pack::Sha256 hash;
    const auto add=[&](const std::string& s){
        const auto size=std::to_string(s.size())+":";
        hash.update(size.data(),size.size());hash.update(s.data(),s.size());
    };
    add(fs::canonical(project).generic_string());add(history.versionToken());add("batch-purge-v1");
    uint64_t total=0;
    for(const auto& id:ids){
        if(std::binary_search(protectedIds.begin(),protectedIds.end(),id))
            throw std::runtime_error("Recovery record is protected by retained Undo/Redo: "+id);
        const auto reviewed=reviewTrash(project,id,"purge");
        auto row=trashRow(reviewed.record);
        const auto bytes=uint64_t(row.getInt("slotBytes"));
        if(bytes>RecoveryMaxAccountedBytes-total)throw std::runtime_error("Cleanup byte budget exceeded");
        total+=bytes;add(id);add(reviewed.token);add(std::to_string(bytes));
        items.push(std::move(row));
    }
    const auto digest=hash.finish();
    Json out=Json::object();out.set("items",std::move(items));out.set("bytes",Json::number(int64_t(total)));
    out.set("token",Json::string(pack::toHex(digest.data(),digest.size())));
    return out;
}
Json studioApplyTrashBatch(const fs::path& project,const std::vector<std::string>& ids,const std::string& token,const editor::ProjectHistory& history){
    const auto plan=studioReviewTrashBatch(project,ids,history);
    if(token.empty()||plan.getStr("token")!=token)throw std::runtime_error("Cleanup plan changed; review again");
    // Preflight all entries before irreversible work. Revalidate each entry
    // again immediately before deletion. A filesystem failure can be partial.
    std::vector<std::pair<std::string,std::string>> checked;
    for(const auto& row:plan["items"].items()){
        const auto id=row.getStr("id");
        const auto review=reviewTrash(project,id,"purge");
        checked.emplace_back(id,review.token);
    }
    // Ensure the complete set is still identical after capturing per-item tokens.
    if(studioReviewTrashBatch(project,ids,history).getStr("token")!=token)
        throw std::runtime_error("Cleanup plan changed; review again");
    Json removed=Json::array(),out=Json::object();
    for(const auto& [id,expected]:checked){
        try{studioAssetTrashApply(project,id,"purge",expected);removed.push(Json::string(id));}
        catch(const std::exception& e){out.set("failedId",Json::string(id));out.set("error",Json::string(e.what()));break;}
    }
    out.set("ok",Json::fromBool(!out.has("error")));out.set("removed",std::move(removed));return out;
}
Json studioAssetTrashReview(const fs::path& project,const std::string& id,const std::string& action){
    const auto reviewed=reviewTrash(project,id,action);
    Json out=trashRow(reviewed.record);
    out.set("action",Json::string(action));out.set("token",Json::string(reviewed.token));
    return out;
}
void studioAssetTrashApply(const fs::path& project,const std::string& id,const std::string& action,const std::string& expectedToken,StudioBatchAssetPlan* staged){
    if(staged&&action!="restore")throw std::runtime_error("Only reversible restore can be staged");
    const auto reviewed=reviewTrash(project,id,action);const auto& r=reviewed.record;
    if(expectedToken.empty()||expectedToken!=reviewed.token)throw std::runtime_error("Recovery record changed; review again");
    if(action=="purge"){
        // Explicit single-slot purge; never follows a live path or an unknown slot.
        fs::remove_all(r.folder);StudioMoveJournal::syncPath(r.folder.parent_path());return;
    }
    editor::ProjectHistory::MovePlan plan;
    if(r.kind!="replacement"){
        plan.quarantine=true;plan.created=r.kind=="created";
        plan.files.push_back({r.path,r.relative,{},r.digest,r.directory});
        plan.files.push_back({r.path+".meta",r.relative+".meta",r.meta,{},false});
        studioReplayAssetQuarantine(project,plan,false,{},staged);return;
    }
    if(reviewed.liveDigest==r.digest)throw std::runtime_error("This version is already active");
    // Save the current live version as a new recovery record before exchanging
    // payloads. Keep the live sidecar, even if import settings changed since the
    // original replacement. Both digests and current GUID were reviewed.
    const auto outgoing=trashRelative(nonce());
    const auto folder=(project/outgoing).parent_path();
    if(!fs::create_directory(folder))throw std::runtime_error("Cannot reserve recovery version slot");
    Json record=Json::object();record.set("kind",Json::string("replacement"));
    record.set("path",Json::string(r.path));record.set("guid",Json::string(r.guid));
    record.set("digest",Json::string(reviewed.liveDigest));record.set("role",Json::string("old"));
    {std::ofstream out(folder/"record.json");out<<record.dump(2)<<"\n";out.close();if(!out)throw std::runtime_error("Cannot write recovery version record");}
    StudioMoveJournal::syncPath(folder/"record.json");StudioMoveJournal::syncPath(folder);StudioMoveJournal::syncPath(folder.parent_path());
    plan.replacement=true;
    plan.files.push_back({r.path,outgoing,reviewed.liveMeta,reviewed.liveDigest,false});
    plan.files.push_back({r.path,r.relative,{},r.digest,false});
    studioReplayAssetReplacement(project,plan,true,staged);
}
namespace {
using AssetHistory=editor::ProjectHistory;
constexpr size_t AssetCheckpointMaxBytes=128u*1024u*1024u;
fs::path assetHistoryCache(const fs::path& project,bool create){
    const auto cache=fs::canonical(project)/".aiverse-cache";
    if(create&&!existsOrSymlink(cache))fs::create_directory(cache);
    if(existsOrSymlink(cache)&&!fs::is_directory(fs::symlink_status(cache)))
        throw std::runtime_error("Unsafe asset history cache");
    return cache;
}
void writeAssetCheckpoint(const fs::path& path,const std::string& text){
    if(existsOrSymlink(path)&&!fs::is_regular_file(fs::symlink_status(path)))
        throw std::runtime_error("Unsafe asset history checkpoint staging path");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(text.data(),static_cast<std::streamsize>(text.size()));out.close();
    if(!out)throw std::runtime_error("Cannot write asset history checkpoint");
    StudioMoveJournal::syncPath(path);
}
std::string historyHash(const std::string& bytes){
    const auto digest=pack::sha256(bytes.data(),bytes.size());
    return pack::toHex(digest.data(),digest.size());
}
bool validHistoryDigest(const std::string& value){
    return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;
}
void validateHistoryContentPath(const std::string& value,bool meta=false){
    if(value.empty()||value.size()>1024||value.find('\\')!=std::string::npos||
       fs::path(value).is_absolute()||fs::path(value).has_root_name())
        throw std::runtime_error("Unsafe asset history path");
    const auto path=fs::path(value);
    for(const auto& part:path.parent_path())validateSegment(part.string());
    if(meta){
        if(path.extension()!=".meta")throw std::runtime_error("Invalid asset history sidecar path");
        validateSegment(path.stem().string());
    }else validateSegment(path.filename().string());
}
void validateHistorySlot(const fs::path& project,const std::string& value,bool meta=false){
    if(meta&&!value.ends_with(".meta"))throw std::runtime_error("Invalid recovery sidecar path");
    const std::string payload=meta?value.substr(0,value.size()-5):value;
    (void)quarantineFolder(project,payload);
}
void validateHistoryMove(const fs::path& project,const AssetHistory::MovePlan& plan){
    if(plan.files.empty()||plan.files.size()>128||plan.files.size()%2||
       (plan.replacement&&plan.quarantine)||(plan.created&&!plan.quarantine)||
       (plan.materialsBefore.has_value()!=plan.materialsAfter.has_value())||
       ((plan.quarantine||plan.replacement)&&(plan.materialsBefore||plan.materialsAfter)))
        throw std::runtime_error("Invalid persisted asset move plan");
    if((plan.quarantine||plan.replacement)&&!plan.resourceEdits.empty())throw std::runtime_error("Invalid persisted resource repair");
    if(plan.resourceEdits.size()>32)throw std::runtime_error("Too many persisted resource edits");
    size_t repairBytes=0;std::set<std::string> repairPaths;
    std::vector<StudioMoveJournalOperation> repairMoves;
    if(!plan.quarantine&&!plan.replacement)
        for(const auto& f:plan.files)repairMoves.push_back({f.from,f.to});
    for(const auto& edit:plan.resourceEdits){
        const auto path=edit.path.generic_string();
        if(!path.starts_with("content/")||!studioRepairModelExtension(edit.path)||!repairPaths.insert(path).second)
            throw std::runtime_error("Invalid persisted resource path");
        validateHistoryContentPath(path.substr(8));
        for(size_t count:{edit.before.size(),edit.after.size()}){
            if(count>8u*1024u*1024u-repairBytes)throw std::runtime_error("Persisted resource budget exceeded");
            repairBytes+=count;
        }
        studioValidateModelRepairBytes(edit);
        const auto destination=StudioMoveJournal::resourceDestination(path,repairMoves);
        if(!destination.starts_with("content/")||!studioRepairModelExtension(fs::path(destination)))
            throw std::runtime_error("Invalid persisted repaired destination");
        validateHistoryContentPath(destination.substr(8));
    }
    std::set<std::string> sources,targets;
    for(size_t i=0;i<plan.files.size();i+=2){
        const auto& a=plan.files[i];const auto& b=plan.files[i+1];
        validateHistoryContentPath(a.from);
        const bool repaired=std::any_of(plan.resourceEdits.begin(),plan.resourceEdits.end(),[&](const auto& edit){
            return studioReferenceMatchesPath(edit.path.generic_string(),"content/"+a.from,a.directory);
        });
        if(!b.digestAfter.empty()||repaired!=!a.digestAfter.empty()||
           (!a.digestAfter.empty()&&(!validHistoryDigest(a.digestAfter)||a.digestAfter==a.digest)))
            throw std::runtime_error("Invalid persisted repaired asset digest");
        if(!sources.insert(a.from).second||!targets.insert(a.to).second||
           !validHistoryDigest(a.digest)||a.from==a.to)
            throw std::runtime_error("Invalid persisted asset move identity");
        if(plan.replacement){
            if(a.directory||b.directory||a.from!=b.from||!validHistoryDigest(b.digest)||
               !targets.insert(b.to).second||!b.bytes.empty())
                throw std::runtime_error("Invalid persisted replacement");
            validateHistorySlot(project,a.to);validateHistorySlot(project,b.to);
            if(!studioValidGuid(Json::parse(a.bytes).getStr("guid")))throw std::runtime_error("Invalid replacement metadata");
        }else{
            if(b.from!=a.from+".meta"||b.to!=a.to+".meta"||b.directory||!b.digest.empty()||
               !a.bytes.empty()||!studioValidGuid(Json::parse(b.bytes).getStr("guid")))
                throw std::runtime_error("Invalid persisted move sidecar");
            if(plan.quarantine){validateHistorySlot(project,a.to);validateHistorySlot(project,b.to,true);}
            else validateHistoryContentPath(a.to);
        }
    }
}
std::string metadataRelative(const fs::path& project,const fs::path& file){
    const auto root=fs::absolute(project).lexically_normal();
    const auto relative=fs::absolute(file).lexically_normal().lexically_relative(root).generic_string();
    if(!relative.starts_with("content/"))throw std::runtime_error("Asset metadata outside content");
    validateHistoryContentPath(relative.substr(8),true);return relative;
}
void validateHistoryMetadata(const editor::ProjectFileEdit& edit){
    auto before=Json::parse(edit.before),after=Json::parse(edit.after);
    if(!studioValidGuid(before.getStr("guid"))||!before["settings"].isObject()||!after["settings"].isObject())
        throw std::runtime_error("Invalid persisted metadata");
    before.erase("settings");after.erase("settings");
    if(before.dump()!=after.dump())throw std::runtime_error("Persisted metadata identity changed");
}
Json historyState(const fs::path& project,const AssetHistory& history){
    Json state=Json::object(),slots=Json::array();
    // Deliberately conservative: include the whole content tree, not only the
    // last edited path. This detects external edits, missing nested metadata
    // and changes to earlier entries in the retained stack.
    state.set("content",Json::string(moveDigest(studioContentRoot(project),true)));
    const auto material=project/"materials.json";
    if(existsOrSymlink(material))state.set("materials",Json::string(studioAssetFileDigest(material)));
    std::set<std::string> folders;
    for(const auto& step:history.steps())if(step.move&&(step.move->quarantine||step.move->replacement))
        for(size_t i=0;i<step.move->files.size();i+=step.move->replacement?1:2)
            folders.insert(step.move->files[i].to);
    for(const auto& slot:folders){
        const auto folder=quarantineFolder(project,slot);
        Json row=Json::object();row.set("slot",Json::string(slot));
        row.set("digest",Json::string(moveDigest(folder,true)));slots.push(std::move(row));
    }
    state.set("slots",std::move(slots));return state;
}
std::string encodeAssetHistory(const fs::path& project,const AssetHistory& history){
    Json body=Json::object(),rows=Json::array();body.set("position",Json::number(int64_t(history.position())));
    for(const auto& step:history.steps()){
        Json row=Json::object(),files=Json::array();row.set("label",Json::string(step.label));
        if(step.move){
            const auto& plan=*step.move;validateHistoryMove(project,plan);
            Json move=Json::object();move.set("quarantine",Json::fromBool(plan.quarantine));
            move.set("created",Json::fromBool(plan.created));move.set("replacement",Json::fromBool(plan.replacement));
            if(plan.materialsBefore){move.set("materialsBefore",Json::string(*plan.materialsBefore));move.set("materialsAfter",Json::string(*plan.materialsAfter));}
            for(const auto& f:plan.files){
                Json item=Json::object();item.set("from",Json::string(f.from));item.set("to",Json::string(f.to));
                item.set("bytes",Json::string(f.bytes));item.set("digest",Json::string(f.digest));item.set("directory",Json::fromBool(f.directory));
                if(!f.digestAfter.empty())item.set("digestAfter",Json::string(f.digestAfter));
                files.push(std::move(item));
            }
            if(!plan.resourceEdits.empty()){
                Json edits=Json::array();
                for(const auto& edit:plan.resourceEdits){
                    edits.push(studioEncodeResourceEdit(edit));
                }
                move.set("resourceEdits",std::move(edits));
            }
            move.set("files",std::move(files));row.set("move",std::move(move));
        }else{
            if(step.files.size()!=1)throw std::runtime_error("Unsupported asset metadata checkpoint");
            const auto& f=step.files.front();validateHistoryMetadata(f);
            row.set("path",Json::string(metadataRelative(project,f.path)));
            row.set("before",Json::string(f.before));row.set("after",Json::string(f.after));
        }
        rows.push(std::move(row));
    }
    body.set("steps",std::move(rows));
    if(!history.steps().empty())body.set("state",historyState(project,history));
    const auto payload=body.dump(-1);Json envelope=Json::object();
    envelope.set("version",Json::number(int64_t(1)));envelope.set("sha256",Json::string(historyHash(payload)));envelope.set("payload",Json::string(payload));
    auto text=envelope.dump(-1)+"\n";
    if(text.size()>AssetCheckpointMaxBytes)throw std::runtime_error("Asset history checkpoint exceeds disk budget");
    return text;
}
AssetHistory decodeAssetHistory(const fs::path& project,const std::string& text){
    const auto envelope=Json::parse(text);const auto payload=envelope.getStr("payload");
    if(envelope.getInt("version")!=1||historyHash(payload)!=envelope.getStr("sha256"))
        throw std::runtime_error("Asset history checkpoint checksum mismatch");
    const auto body=Json::parse(payload);const auto& rows=body["steps"];const auto position=body.getInt("position",-1);
    if(!rows.isArray()||rows.size()>AssetHistory::MaxSteps||position<0||size_t(position)>rows.size())
        throw std::runtime_error("Invalid asset history checkpoint cursor");
    AssetHistory history;
    for(size_t i=0;i<rows.size();++i){
        const auto& row=rows[i];const auto label=row.getStr("label");
        if(label.empty()||label.size()>256)throw std::runtime_error("Invalid asset history label");
        if(row.has("move")){
            const auto& move=row["move"];const auto& files=move["files"];
            if(!move.isObject()||!files.isArray()||files.size()>128)throw std::runtime_error("Invalid asset history move");
            AssetHistory::MovePlan plan;plan.quarantine=move.getBool("quarantine");plan.created=move.getBool("created");plan.replacement=move.getBool("replacement");
            if(move.has("materialsBefore")||move.has("materialsAfter")){
                if(!move["materialsBefore"].isString()||!move["materialsAfter"].isString())throw std::runtime_error("Invalid asset history materials");
                plan.materialsBefore=move.getStr("materialsBefore");plan.materialsAfter=move.getStr("materialsAfter");
            }
            for(const auto& f:files.items()){
                if(!f["from"].isString()||!f["to"].isString()||!f["bytes"].isString()||!f["digest"].isString())
                    throw std::runtime_error("Invalid asset history move entry");
                if(f.has("digestAfter")&&!f["digestAfter"].isString())throw std::runtime_error("Invalid repaired history digest");
                plan.files.push_back({f.getStr("from"),f.getStr("to"),f.getStr("bytes"),f.getStr("digest"),f.getBool("directory"),f.getStr("digestAfter")});
            }
            if(move.has("resourceEdits")){
                if(!move["resourceEdits"].isArray()||move["resourceEdits"].size()>32)throw std::runtime_error("Invalid resource history");
                for(const auto& edit:move["resourceEdits"].items()){
                    plan.resourceEdits.push_back(studioDecodeResourceEdit(edit,true));
                }
            }
            validateHistoryMove(project,plan);history.recordMove(std::move(plan),label);
        }else{
            const auto path=row.getStr("path");
            if(!path.starts_with("content/")||!row["before"].isString()||!row["after"].isString())
                throw std::runtime_error("Invalid asset history metadata entry");
            validateHistoryContentPath(path.substr(8),true);
            editor::ProjectFileEdit edit{project/fs::path(path),row.getStr("before"),row.getStr("after")};
            validateHistoryMetadata(edit);history.record({std::move(edit)},label);
        }
        if(history.steps().size()!=i+1)throw std::runtime_error("Asset history checkpoint exceeds retained budget");
    }
    if(!history.steps().empty()&&historyState(project,history).dump(-1)!=body["state"].dump(-1))
        throw std::runtime_error("Content, materials or recovery slots changed since asset history checkpoint");
    while(history.position()>size_t(position))history.advance(false);
    return history;
}
}
Json studioRecoveryStorage(const fs::path& project){
    const auto cache=recoveryCache(project);
    const auto quota=recoveryQuotaMiB(project);
    size_t visited=0;
    const auto trash=recoveryBytes(cache/"studio-asset-trash",visited);
    const auto checkpoint=recoveryBytes(cache/"asset-history.json",visited);
    const auto journal=recoveryBytes(cache/"studio-move-journal",visited)+
        recoveryBytes(cache/"studio-history-batch",visited)+
        recoveryBytes(cache/"asset-history.tmp",visited)+recoveryBytes(cache/"asset-history.dirty",visited);
    Json out=Json::object();
    out.set("trashBytes",Json::number(int64_t(trash)));
    out.set("checkpointBytes",Json::number(int64_t(checkpoint)));
    out.set("journalBytes",Json::number(int64_t(journal)));
    out.set("quotaMiB",Json::number(quota));
    out.set("overQuota",Json::fromBool(quota>0&&trash>uint64_t(quota)*RecoveryMiB));
    out.set("warning",Json::fromBool(trash>=(quota?uint64_t(quota)*RecoveryMiB*9/10:4ull*1024*RecoveryMiB)));
    out.set("availableBytes",Json::number(int64_t(std::min<uintmax_t>(fs::space(fs::canonical(project)).available,RecoveryMaxAccountedBytes))));
    return out;
}
Json studioSetRecoveryQuota(const fs::path& project,int64_t expectedMiB,int64_t quotaMiB){
    if(quotaMiB<0||quotaMiB>RecoveryMaxQuotaMiB)throw std::runtime_error("Recovery quota must be 0–1048576 MiB");
    const auto cache=assetHistoryCache(project,true);
    editor::project_file_detail::Lock lock(cache/"asset-history.lock");
    checkTrashJournal(project);
    if(recoveryQuotaMiB(project)!=expectedMiB)throw std::runtime_error("Recovery quota changed; refresh Trash");
    Json policy=Json::object();policy.set("version",Json::number(int64_t(1)));policy.set("quotaMiB",Json::number(quotaMiB));
    const auto target=cache/"studio-recovery-policy.json",tmp=cache/"studio-recovery-policy.tmp";
    writeAssetCheckpoint(tmp,policy.dump(-1)+"\n");
    StudioMoveJournal::renamePath(tmp,target,true);StudioMoveJournal::syncPath(cache);
    return studioRecoveryStorage(project);
}
static void beginAssetHistoryCheckpoint(const fs::path& project){
    checkTrashJournal(project);
    const auto cache=assetHistoryCache(project,true),marker=cache/"asset-history.dirty";
    if(existsOrSymlink(cache/"studio-history-batch"))throw std::runtime_error("Pending coordinated history recovery; reopen project");
    writeAssetCheckpoint(marker,"Asset history publication in progress\n");
    StudioMoveJournal::syncPath(cache);StudioMoveJournal::syncPath(cache.parent_path());
}
static void saveAssetHistoryCheckpoint(const fs::path& project,const AssetHistory& history){
    const auto cache=assetHistoryCache(project,true),marker=cache/"asset-history.dirty";
    if(!fs::is_regular_file(fs::symlink_status(marker)))throw std::runtime_error("Missing asset history publication marker");
    checkTrashJournal(project);
    const auto target=cache/"asset-history.json",tmp=cache/"asset-history.tmp";
    if(existsOrSymlink(target)&&!fs::is_regular_file(fs::symlink_status(target)))throw std::runtime_error("Unsafe asset history checkpoint");
    writeAssetCheckpoint(tmp,encodeAssetHistory(project,history));
    StudioMoveJournal::renamePath(tmp,target,true);
    fs::remove(marker);StudioMoveJournal::syncPath(cache);
}
struct StudioAssetHistoryPublication::Impl {
    fs::path project;
    editor::project_file_detail::Lock lock;
    explicit Impl(const fs::path& p):project(p),lock(assetHistoryCache(p,true)/"asset-history.lock"){}
};
StudioAssetHistoryPublication::StudioAssetHistoryPublication(const fs::path& project):impl_(std::make_unique<Impl>(project)){
    beginAssetHistoryCheckpoint(project);
}
StudioAssetHistoryPublication::~StudioAssetHistoryPublication()=default;
void StudioAssetHistoryPublication::save(const AssetHistory& history){saveAssetHistoryCheckpoint(impl_->project,history);}
bool StudioAssetHistoryPublication::cancelUnchanged(const AssetHistory& history){
    const auto cache=assetHistoryCache(impl_->project,true);
    const auto target=cache/"asset-history.json";
    if(!existsOrSymlink(target)&&history.steps().empty()){
        save(history);return true;
    }
    if(!fs::is_regular_file(fs::symlink_status(target))||fs::file_size(target)>AssetCheckpointMaxBytes||
       editor::readProjectFileBytes(target)!=encodeAssetHistory(impl_->project,history))return false;
    checkTrashJournal(impl_->project);
    fs::remove(cache/"asset-history.dirty");StudioMoveJournal::syncPath(cache);return true;
}
namespace {
void checkCleanupHistory(const fs::path& root,const AssetHistory& history){
    checkTrashJournal(root);
    const auto cache=assetHistoryCache(root,true);
    if(existsOrSymlink(cache/"asset-history.dirty")||existsOrSymlink(cache/"project-document-journal"))
        throw std::runtime_error("Pending history recovery; reopen project before cleanup");
    const auto target=cache/"asset-history.json";
    if(!existsOrSymlink(target)){
        if(!history.steps().empty())throw std::runtime_error("Missing retained asset checkpoint");
        return;
    }
    if(!fs::is_regular_file(fs::symlink_status(target))||fs::file_size(target)>AssetCheckpointMaxBytes||
       editor::readProjectFileBytes(target)!=encodeAssetHistory(root,history))
        throw std::runtime_error("Asset history baseline changed; reopen project before cleanup");
}
}
Json studioPurgeUnprotectedTrash(const fs::path& project,const std::string& id,
    const std::string& token,const AssetHistory& history){
    const auto root=fs::canonical(project);
    editor::project_file_detail::Lock lock(assetHistoryCache(root,true)/"asset-history.lock");
    checkCleanupHistory(root,history);
    const auto protectedIds=studioProtectedTrashIds(history);
    if(std::binary_search(protectedIds.begin(),protectedIds.end(),id))
        throw std::runtime_error("Recovery record is protected by retained Undo/Redo");
    studioAssetTrashApply(root,id,"purge",token);
    Json out=Json::object();out.set("ok",Json::fromBool(true));return out;
}
Json studioPurgeUnprotectedTrashBatch(const fs::path& project,const std::vector<std::string>& ids,
    const std::string& token,const AssetHistory& history){
    const auto root=fs::canonical(project);
    editor::project_file_detail::Lock lock(assetHistoryCache(root,true)/"asset-history.lock");
    checkCleanupHistory(root,history);
    return studioApplyTrashBatch(root,ids,token,history);
}
void studioRecoverAssetJournal(const fs::path& project){
    const auto cache=assetHistoryCache(project,false);
    if(!existsOrSymlink(cache))return;
    editor::project_file_detail::Lock lock(cache/"asset-history.lock");
    StudioMoveJournal::recover(project);
    studioRecoverHistoryBatch(project);
}
AssetHistory studioLoadAssetHistoryCheckpoint(const fs::path& project){
    const auto cache=assetHistoryCache(project,false);
    if(!existsOrSymlink(cache))return {};
    editor::project_file_detail::Lock lock(cache/"asset-history.lock");
    if(existsOrSymlink(cache/"asset-history.dirty"))
        throw std::runtime_error("Interrupted asset history publication; recovery payloads remain in Trash");
    const auto path=cache/"asset-history.json";
    if(!existsOrSymlink(path))return {};
    if(!fs::is_regular_file(fs::symlink_status(path))||fs::file_size(path)>AssetCheckpointMaxBytes)
        throw std::runtime_error("Unsafe or oversized asset history checkpoint");
    return decodeAssetHistory(project,editor::readProjectFileBytes(path));
}
namespace {
struct AssetResetReview {
    std::optional<std::string> checkpoint,marker;
    std::string token,reason;
};
AssetResetReview reviewAssetResetLocked(const fs::path& root){
    checkTrashJournal(root);
    const auto cache=assetHistoryCache(root,true);
    if(existsOrSymlink(cache/"project-document-journal"))
        throw std::runtime_error("Pending project document recovery; reopen project first");
    const auto read=[&](const fs::path& path,size_t limit)->std::optional<std::string>{
        if(!existsOrSymlink(path))return {};
        if(!fs::is_regular_file(fs::symlink_status(path))||fs::file_size(path)>limit)
            throw std::runtime_error("Unsafe or oversized recovery file; inspect manually");
        auto bytes=editor::readProjectFileBytes(path);
        if(bytes.size()>limit)throw std::runtime_error("Recovery file exceeds budget");
        return bytes;
    };
    AssetResetReview review;
    review.checkpoint=read(cache/"asset-history.json",AssetCheckpointMaxBytes);
    review.marker=read(cache/"asset-history.dirty",65536);
    if(review.marker)review.reason="Interrupted legacy asset history publication";
    else if(review.checkpoint){
        try{(void)decodeAssetHistory(root,*review.checkpoint);}
        catch(const std::exception& e){review.reason=e.what();}
    }
    // Never discard healthy history or another session's valid cursor.
    if(review.reason.empty())throw std::runtime_error("No damaged asset history to reset; reopen to load valid history");
    Json identity=Json::object();
    identity.set("project",Json::string(root.generic_string()));
    identity.set("checkpoint",Json::string(review.checkpoint?"file:"+historyHash(*review.checkpoint):"absent"));
    identity.set("marker",Json::string(review.marker?"file:"+historyHash(*review.marker):"absent"));
    identity.set("action",Json::string("archive-and-reset-asset-history-v1"));
    review.token=historyHash(identity.dump(-1));
    return review;
}
}
Json studioReviewAssetHistoryReset(const fs::path& project){
    const auto root=fs::canonical(project);
    editor::project_file_detail::Lock lock(assetHistoryCache(root,true)/"asset-history.lock");
    const auto review=reviewAssetResetLocked(root);
    Json out=Json::object();out.set("token",Json::string(review.token));
    out.set("reason",Json::string(review.reason));
    out.set("checkpointBytes",Json::number(int64_t(review.checkpoint?review.checkpoint->size():0)));
    out.set("legacyMarker",Json::fromBool(review.marker.has_value()));
    return out;
}
void studioApplyAssetHistoryReset(const fs::path& project,const std::string& token){
    const auto root=fs::canonical(project),cache=assetHistoryCache(root,true);
    editor::project_file_detail::Lock lock(cache/"asset-history.lock");
    const auto review=reviewAssetResetLocked(root);
    if(token.empty()||review.token!=token)throw std::runtime_error("Asset history reset review changed");
    // Archive before publishing. An interruption may leave an extra archive,
    // but cannot discard the only copy of the damaged history.
    const auto archives=cache/"asset-history-recovery";
    editor::project_file_detail::directory(archives);
    const auto folder=archives/nonce();
    if(!fs::create_directory(folder))throw std::runtime_error("Cannot reserve history archive");
    if(review.checkpoint)writeAssetCheckpoint(folder/"asset-history.json",*review.checkpoint);
    if(review.marker)writeAssetCheckpoint(folder/"asset-history.dirty",*review.marker);
    Json manifest=Json::object();
    manifest.set("token",Json::string(review.token));manifest.set("reason",Json::string(review.reason));
    manifest.set("checkpointHash",Json::string(review.checkpoint?historyHash(*review.checkpoint):""));
    manifest.set("markerHash",Json::string(review.marker?historyHash(*review.marker):""));
    writeAssetCheckpoint(folder/"record.json",manifest.dump(2)+"\n");
    StudioMoveJournal::syncPath(folder);StudioMoveJournal::syncPath(archives);
    StudioMoveJournal::syncPath(cache);
    const auto after=encodeAssetHistory(root,AssetHistory{});
    editor::ProjectFileEdit edit{cache/"asset-history.json",review.checkpoint.value_or(""),after,{},0,!review.checkpoint};
    editor::applyProjectFileEdits(root,{edit});
    // Remove the old fence only after the empty checkpoint is durable.
    // If interrupted here, repeating the explicit reviewed reset is safe.
    if(review.marker){
        const auto marker=cache/"asset-history.dirty";
        if(editor::readProjectFileBytes(marker)!=*review.marker)
            throw std::runtime_error("Legacy history marker changed; retained archive and fence");
        if(!fs::remove(marker))throw std::runtime_error("Cannot retire legacy history marker");
        StudioMoveJournal::syncPath(cache);
    }
}
namespace {
using BatchState=std::map<std::string,std::string>; // absent = empty, f/d prefix + digest
bool batchOverlap(const std::string& a,const std::string& b){
    return a==b||a.starts_with(b+"/")||b.starts_with(a+"/");
}
fs::path batchPath(const fs::path& root,const std::string& rel){
    if(rel.starts_with("content/")){
        const auto value=rel.substr(8);
        validateHistoryContentPath(value,value.ends_with(".meta"));
    }else{
        validateHistorySlot(root,rel,rel.ends_with(".meta"));
    }
    fs::path path=root;
    for(const auto& segment:fs::path(rel)){
        path/=segment;
        if(existsOrSymlink(path)&&fs::is_symlink(fs::symlink_status(path)))throw std::runtime_error("Symlink in coordinated history path");
    }
    if(!fs::is_directory(fs::symlink_status(path.parent_path())))throw std::runtime_error("Missing coordinated history parent");
    return path;
}
std::string batchIdentity(const fs::path& path){
    if(!existsOrSymlink(path))return {};
    const auto status=fs::symlink_status(path);
    const bool directory=fs::is_directory(status);
    if(!directory&&!fs::is_regular_file(status))throw std::runtime_error("Invalid coordinated history payload");
    return std::string(directory?"d":"f")+moveDigest(path,directory);
}
struct BatchTimeline {
    BatchState initial;
    std::vector<BatchState> phases;
};
BatchTimeline batchTimeline(const fs::path& root,const std::vector<StudioBatchRelocation>& moves){
    if(moves.size()>256)throw std::runtime_error("Invalid coordinated relocation count");
    BatchTimeline timeline;
    BatchState simulated;
    for(const auto& m:moves){
        (void)batchPath(root,m.from);(void)batchPath(root,m.to);
        if(m.from==m.to||!validHistoryDigest(m.digest)||
           (!m.from.starts_with("content/")&&!m.to.starts_with("content/")))
            throw std::runtime_error("Invalid coordinated relocation");
        const auto identity=std::string(m.directory?"d":"f")+m.digest;
        if(!simulated.count(m.from)){simulated[m.from]=identity;timeline.initial[m.from]=identity;}
        if(!simulated.count(m.to)){simulated[m.to]="";timeline.initial[m.to]="";}
        if(simulated.at(m.from)!=identity||!simulated.at(m.to).empty())
            throw std::runtime_error("Inconsistent coordinated relocation chain");
        simulated[m.from]="";simulated[m.to]=identity;
    }
    for(auto it=timeline.initial.begin();it!=timeline.initial.end();++it)
        for(auto next=std::next(it);next!=timeline.initial.end();++next)
            if(batchOverlap(it->first,next->first))throw std::runtime_error("Nested relocation endpoints require separate history navigation");
    simulated=timeline.initial;timeline.phases.push_back(simulated);
    for(const auto& m:moves){
        simulated[m.to]=simulated[m.from];simulated[m.from]="";
        if(std::find(timeline.phases.begin(),timeline.phases.end(),simulated)!=timeline.phases.end())
            throw std::runtime_error("Ambiguous coordinated relocation cycle");
        timeline.phases.push_back(simulated);
    }
    return timeline;
}
size_t batchPhase(const fs::path& root,const BatchTimeline& timeline){
    BatchState actual;
    for(const auto& [path,identity]:timeline.initial){
        (void)identity;actual[path]=batchIdentity(batchPath(root,path));
    }
    size_t phase=0;bool found=false;
    for(size_t i=0;i<timeline.phases.size();++i)if(timeline.phases[i]==actual){
        if(found)throw std::runtime_error("Ambiguous coordinated recovery phase");
        found=true;phase=i;
    }
    if(!found)throw std::runtime_error("Coordinated recovery conflict: payload state changed");
    return phase;
}
void validateBatchDocuments(const fs::path& root,const BatchTimeline& timeline,
                            const std::vector<editor::ProjectFileEdit>& docs){
    std::set<std::string> unique;size_t bytes=0;
    if(docs.size()>4096)throw std::runtime_error("Too many coordinated document dependencies");
    for(const auto& edit:docs){
        const auto rel=edit.path.lexically_relative(root).generic_string();
        (void)editor::project_file_detail::safeTarget(root,rel);
        if(edit.createOnly)throw std::runtime_error("Creation-only edit cannot enter asset batch");
        if(!unique.insert(rel).second)throw std::runtime_error("Duplicate coordinated document");
        if(!edit.tailDigest.empty()||edit.tailBytes){
            if(!rel.starts_with("content/")||!studioStreamingResource(edit))throw std::runtime_error("Invalid coordinated streaming resource");
        }
        if(rel.starts_with("content/")&&studioRepairModelExtension(edit.path))studioValidateModelRepairBytes(edit);
        for(const auto& [endpoint,identity]:timeline.initial){
            (void)identity;if(batchOverlap(rel,endpoint))throw std::runtime_error("Document overlaps relocation; navigate separately");
        }
        for(const size_t count:{edit.before.size(),edit.after.size()}){
            if(count>128u*1024u*1024u-bytes)throw std::runtime_error("Coordinated document budget exceeded");
            bytes+=count;
        }
    }
}
// Resource paths may be inside a moved directory whose destination parent
// does not exist until the move. Validate lexical paths separately from live
// filesystem targets, and prove the destination by following the entire chain.
std::string batchResourceDestination(std::string path,const std::vector<StudioBatchRelocation>& moves){
    for(const auto& m:moves)
        if(path==m.from||(m.directory&&path.starts_with(m.from+"/")))
            path=m.to+path.substr(m.from.size());
    return path;
}
bool batchMetadataPath(const std::string& path){
    return path.ends_with(".meta")&&(path.starts_with("content/")||inRecoveryTrash(path));
}
void validateBatchMetadataPath(const fs::path& root,const std::string& path){
    if(path.starts_with("content/"))validateHistoryContentPath(path.substr(8),true);
    else validateHistorySlot(root,path,true);
}
void validateMovingBatchResources(const fs::path& root,const std::vector<StudioBatchRelocation>& moves,
    const std::vector<StudioBatchResourceEdit>& resources){
    if(resources.size()>32)throw std::runtime_error("Too many moving batch resource edits");
    size_t bytes=0;std::set<std::string> sources,targets;
    for(const auto& resource:resources){
        const auto& edit=resource.edit;
        const auto source=edit.path.generic_string();
        const auto& target=resource.destination;
        if(source==target||!sources.insert(source).second||!targets.insert(target).second||
           batchResourceDestination(source,moves)!=target)
            throw std::runtime_error("Invalid moving resource chain");
        if(batchMetadataPath(source)&&batchMetadataPath(target)){
            validateBatchMetadataPath(root,source);validateBatchMetadataPath(root,target);
            if(!edit.tailDigest.empty()||edit.tailBytes)throw std::runtime_error("Invalid metadata snapshot mode");
            validateHistoryMetadata(edit);
        }else{
            if(!source.starts_with("content/")||!target.starts_with("content/")||
               !studioRepairModelExtension(edit.path)||!studioRepairModelExtension(fs::path(target)))
                throw std::runtime_error("Invalid moving model resource path");
            validateHistoryContentPath(source.substr(8));validateHistoryContentPath(target.substr(8));
            studioValidateModelRepairBytes(edit);
        }
        for(const auto count:{edit.before.size(),edit.after.size()}){
            if(count>StudioResourceEditBudget-bytes)throw std::runtime_error("Moving batch resource budget exceeded");
            bytes+=count;
        }
    }
}
std::vector<editor::ProjectFileEdit> finalMovingBatchEdits(const fs::path& root,
    const std::vector<StudioBatchResourceEdit>& resources){
    std::vector<editor::ProjectFileEdit> result;
    for(const auto& resource:resources){
        auto edit=resource.edit;
        edit.path=editor::project_file_detail::safeTarget(root,resource.destination);
        result.push_back(std::move(edit));
    }
    return result;
}
// At the final relocation phase some resource writes may already have
// committed. Reconstruct the original payload identity by streaming a reverse
// overlay without changing any live file. Earlier phases have no byte edits.
void checkFinalBatchPayloads(const fs::path& root,const BatchTimeline& timeline,
    const std::vector<StudioBatchResourceEdit>& resources){
    ResourceOverlay reverse;
    for(auto edit:finalMovingBatchEdits(root,resources)){
        if(studioResourceMatches(edit.path,edit))continue;
        if(!studioResourceMatches(edit.path,edit,true))
            throw std::runtime_error("Moving resource recovery conflict: "+edit.path.string());
        std::swap(edit.before,edit.after);
        const auto path=edit.path;reverse.emplace(path,std::move(edit));
    }
    for(const auto& [rel,expected]:timeline.phases.back()){
        const auto path=batchPath(root,rel);
        std::string actual;
        if(existsOrSymlink(path)){
            const auto status=fs::symlink_status(path);const bool directory=fs::is_directory(status);
            if(!directory&&!fs::is_regular_file(status))throw std::runtime_error("Invalid final batch payload");
            actual=std::string(directory?"d":"f")+moveDigest(path,directory,&reverse);
        }
        if(actual!=expected)throw std::runtime_error("Final batch payload identity changed: "+rel);
    }
}
size_t batchPhaseWithResources(const fs::path& root,const BatchTimeline& timeline,
    const std::vector<StudioBatchResourceEdit>& resources){
    if(resources.empty())return batchPhase(root,timeline);
    try{return batchPhase(root,timeline);}
    catch(const std::runtime_error&){
        checkFinalBatchPayloads(root,timeline,resources);
        return timeline.phases.size()-1;
    }
}
fs::path batchFolder(const fs::path& project){return fs::canonical(project)/".aiverse-cache"/"studio-history-batch";}
void retireBatch(const fs::path& folder){
    // No pending marker remains once the outcome is known.
    std::error_code ec;fs::remove_all(folder,ec);
    StudioMoveJournal::syncPath(folder.parent_path());
}
void reverseBatch(const fs::path& root,const fs::path& folder,
                  const std::vector<StudioBatchRelocation>& moves,
                  const std::vector<editor::ProjectFileEdit>& docs,
                  const std::vector<StudioBatchResourceEdit>& resources={},
                  const std::optional<editor::ProjectFileEdit>& checkpoint={},bool checkpointReady=true){
    const auto timeline=batchTimeline(root,moves);
    validateMovingBatchResources(root,moves,resources);
    const auto phase=batchPhaseWithResources(root,timeline,resources);
    validateBatchDocuments(root,timeline,docs);
    std::vector<editor::ProjectFileEdit> reverse;
    auto published=docs;
    if(phase==moves.size()){
        auto moved=finalMovingBatchEdits(root,resources);
        published.insert(published.end(),moved.begin(),moved.end());
    }
    const auto checkpointBefore=[&](){
        return checkpoint->createOnly?!existsOrSymlink(checkpoint->path):
            studioResourceMatches(checkpoint->path,*checkpoint);
    };
    if(checkpoint){
        if(!checkpointBefore()&&(!checkpointReady||!studioResourceMatches(checkpoint->path,*checkpoint,true)))
            throw std::runtime_error("Asset checkpoint recovery conflict");
    }
    for(const auto& f:published){
        if(studioResourceMatches(f.path,f))continue;
        if(!studioResourceMatches(f.path,f,true))throw std::runtime_error("Coordinated document recovery conflict: "+f.path.string());
        auto undo=f;std::swap(undo.before,undo.after);reverse.push_back(std::move(undo));
    }
    StudioMoveJournal(root).applyResources(reverse,true);
    if(checkpoint&&!checkpointBefore()){
        if(!checkpointReady||!studioResourceMatches(checkpoint->path,*checkpoint,true))
            throw std::runtime_error("Asset checkpoint changed during recovery");
        if(checkpoint->createOnly){
            if(!fs::remove(checkpoint->path))throw std::runtime_error("Cannot remove first asset checkpoint");
            StudioMoveJournal::syncPath(checkpoint->path.parent_path());
        }else{
            auto edit=*checkpoint;std::swap(edit.before,edit.after);
            editor::applyProjectFileEdits(root,{edit});
        }
    }
    for(size_t i=phase;i>0;--i){
        const auto& m=moves[i-1];
        const auto from=batchPath(root,m.to),to=batchPath(root,m.from);
        if(batchIdentity(from)!=std::string(m.directory?"d":"f")+m.digest||existsOrSymlink(to))
            throw std::runtime_error("Coordinated relocation changed during recovery");
        StudioMoveJournal::renamePath(from,to);
    }
    if(batchPhase(root,timeline)!=0)throw std::runtime_error("Coordinated rollback not restored");
    StudioMoveJournal::renamePath(folder/"pending.json",folder/"rolledback.json");
    retireBatch(folder);
}
}
StudioBatchAssetPlan studioPlanAssetMoveChain(const fs::path& project,
    const std::vector<editor::ProjectHistory::MovePlan>& ordered,bool redo){
    const auto root=fs::canonical(project);
    if(ordered.empty()||ordered.size()>64)throw std::runtime_error("Invalid ordinary move chain count");
    StudioBatchAssetPlan out;
    const auto bytesDigest=[](const std::string& bytes){
        pack::Sha256 hash;
        const auto field=[&](const std::string& value){
            const auto prefix=std::to_string(value.size())+":";
            hash.update(prefix.data(),prefix.size());hash.update(value.data(),value.size());
        };
        field("file-v1");field(std::to_string(bytes.size()));hash.update(bytes.data(),bytes.size());
        const auto digest=hash.finish();return pack::toHex(digest.data(),digest.size());
    };
    std::set<std::string> endpoints,assetEndpoints;
    size_t moveCount=0;
    for(const auto& plan:ordered){
        validateHistoryMove(root,plan);
        if(plan.quarantine||plan.replacement||plan.created)
            throw std::runtime_error("Ordinary move chain cannot include Trash or replacement");
        if(plan.files.size()>256-moveCount)throw std::runtime_error("Too many coordinated relocations");
        moveCount+=plan.files.size();
        for(size_t i=0;i<plan.files.size();++i){
            const auto& f=plan.files[i];
            const auto from="content/"+f.from,to="content/"+f.to;
            endpoints.insert(from);endpoints.insert(to);
            if(i%2==0){assetEndpoints.insert(from);assetEndpoints.insert(to);}
        }
    }
    for(auto it=endpoints.begin();it!=endpoints.end();++it)
        for(auto next=std::next(it);next!=endpoints.end();++next)
            if(batchOverlap(*it,*next))throw std::runtime_error("Nested relocation endpoints require separate history navigation");
    BatchState initial;
    std::map<std::string,std::string> origins;
    for(const auto& path:endpoints){
        const auto identity=batchIdentity(batchPath(root,path));initial[path]=identity;
        if(!identity.empty())origins.emplace(path,path);
    }
    StudioAssetCatalog catalog(root);catalog.rebuild(false);
    ResourceOverlay overlay;
    const auto physical=[&](const std::string& path){
        for(const auto& endpoint:assetEndpoints){
            if(path!=endpoint&&!path.starts_with(endpoint+"/"))continue;
            const auto it=origins.find(endpoint);
            if(it==origins.end())throw std::runtime_error("Resource owner absent in virtual history");
            return it->second+path.substr(endpoint.size());
        }
        return path;
    };
    const auto checkDigest=[&](const std::string& origin,bool directory,const std::string& digest){
        const auto& identity=initial.at(origin);
        if(identity.empty()||identity.front()!=(directory?'d':'f')||
           moveDigest(root/origin,directory,&overlay)!=digest)
            throw std::runtime_error("Virtual move-chain payload digest changed: "+origin);
    };
    for(const auto& plan:ordered){
        std::vector<StudioReferenceRelocation> forward;
        for(size_t i=0;i<plan.files.size();i+=2){
            const auto& f=plan.files[i];
            forward.push_back({"content/"+f.from,"content/"+f.to,f.directory});
        }
        auto edits=redo?plan.resourceEdits:editor::reverseProjectFileEdits(plan.resourceEdits);
        // Resolve all edits to their original physical identity before moving
        // any of this step's endpoints. Undo records use forward-source paths.
        for(auto& edit:edits){
            auto current=edit.path.generic_string();
            if(!redo)current=studioRelocatedReferencePath(current,forward);
            edit.path=root/physical(current);
        }
        for(size_t i=0;i<plan.files.size();++i){
            const auto& f=plan.files[i];
            const auto from="content/"+(redo?f.from:f.to),to="content/"+(redo?f.to:f.from);
            const auto it=origins.find(from);
            if(it==origins.end()||origins.count(to))throw std::runtime_error("Invalid virtual move-chain occupancy");
            const auto origin=it->second;
            const auto expected=i%2?bytesDigest(f.bytes):(!redo&&!f.digestAfter.empty()?f.digestAfter:f.digest);
            checkDigest(origin,f.directory,expected);
            if(i%2==0){
                const auto meta=Json::parse(plan.files[i+1].bytes);
                const auto indexed=catalog.byGuid(meta.getStr("guid"));
                if(!indexed||"content/"+indexed->path!=origin||indexed->directory!=f.directory||
                   indexed->importer!=meta.getStr("importer"))
                    throw std::runtime_error("Asset chain GUID or type changed");
            }
            // Journal moves retain initial bytes. Only virtual digests advance
            // with the retained repairs; the final overlay is published once.
            out.relocations.push_back({from,to,initial.at(origin).substr(1),f.directory});
            origins.erase(it);origins.emplace(to,origin);
        }
        for(auto& edit:edits){
            const auto it=overlay.find(edit.path);
            if(it==overlay.end()){
                if(!studioResourceMatches(edit.path,edit))
                    throw std::runtime_error("Resource chain initial bytes changed");
                const auto originalPath=edit.path;
                overlay.emplace(originalPath,std::move(edit));
            }else{
                if(it->second.after!=edit.before||it->second.tailDigest!=edit.tailDigest||
                   it->second.tailBytes!=edit.tailBytes)
                    throw std::runtime_error("Inconsistent virtual resource byte chain");
                it->second.after=std::move(edit.after);
            }
        }
        for(size_t i=0;i<plan.files.size();i+=2){
            const auto& f=plan.files[i];
            const auto destination="content/"+(redo?f.to:f.from);
            checkDigest(origins.at(destination),f.directory,redo&&!f.digestAfter.empty()?f.digestAfter:f.digest);
        }
    }
    const auto timeline=batchTimeline(root,out.relocations);
    if(batchPhase(root,timeline)!=0)throw std::runtime_error("Ordinary move chain baseline changed");
    std::vector<StudioReferenceRelocation> net;
    for(const auto& [path,origin]:origins){
        if(path==origin||!assetEndpoints.count(origin))continue;
        net.push_back({origin,path,initial.at(origin).front()=='d'});
    }
    for(const auto& ref:studioCollectGltfReferences(root,{})){
        for(const auto& endpoint:assetEndpoints){
            if(!timeline.initial.at(endpoint).empty()||
               !timeline.phases.back().at(endpoint).empty())continue;
            if(studioReferenceMatchesPath(ref.path,endpoint,true))
                throw std::runtime_error("Move chain crosses an unresolved intermediate reference");
        }
    }
    std::vector<editor::ProjectFileEdit> retained;
    for(auto& [path,edit]:overlay){
        if(edit.before==edit.after)continue;
        edit.path=path.lexically_relative(root);
        retained.push_back(edit);
        const auto source=edit.path.generic_string();
        const auto destination=batchResourceDestination(source,out.relocations);
        if(destination!=source)out.movingResources.push_back({edit,destination});
        else{
            auto absolute=edit;absolute.path=path;out.documents.push_back(std::move(absolute));
        }
    }
    std::sort(retained.begin(),retained.end(),[](const auto& a,const auto& b){return a.path.generic_string()<b.path.generic_string();});
    validateBatchDocuments(root,timeline,out.documents);
    validateMovingBatchResources(root,out.relocations,out.movingResources);
    const auto generated=studioPlanGltfRepairs(root,net,&retained);
    if(generated.size()!=retained.size())throw std::runtime_error("Coordinated resource repair set changed");
    for(size_t i=0;i<retained.size();++i){
        const auto& saved=retained[i];const auto& expected=generated[i];
        const auto path=batchResourceDestination(saved.path.generic_string(),out.relocations);
        if(saved.path!=expected.path||saved.before!=expected.before||
           saved.tailDigest!=expected.tailDigest||saved.tailBytes!=expected.tailBytes||
           studioCanonicalGltfRepair(path,saved.after,saved.tailDigest,saved.tailBytes)!=
           studioCanonicalGltfRepair(path,expected.after,expected.tailDigest,expected.tailBytes))
            throw std::runtime_error("Coordinated resource chain no longer matches typed dependencies");
    }
    return out;
}
// Mixed chains reuse exact-endpoint relocation and virtual identity. Model
// dependency edges are checked against virtual content occupancy at each step.
bool studioCanPlanMixedAssetChain(const std::vector<editor::ProjectHistory::Step>& ordered){
    if(ordered.empty()||ordered.size()>64)return false;
    const auto simple=[](const editor::ProjectHistory::MoveFile& file){
        auto ext=fs::path(file.from).extension().string();
        for(auto& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto importer=studioImporterForPath(fs::path(file.from),false);
        return (file.directory||importer=="texture"||importer=="audio"||ext==".bin"||
            ext==".gltf"||ext==".glb");
    };
    bool hasMove=false;
    for(const auto& step:ordered){
        if(!step.move){if(step.files.size()!=1)return false;continue;}
        hasMove=true;
        const auto& move=*step.move;
        if(move.files.empty()||move.files.size()%2)return false;
        for(const auto& edit:move.resourceEdits)
            if(!studioRepairGltfExtension(edit.path))return false;
        for(size_t i=0;i<move.files.size();i+=2){
            const auto& file=move.files[i];
            if(!simple(file))return false;
            if(!move.quarantine&&!move.replacement){
                auto destination=file;destination.from=file.to;
                if(!simple(destination)||studioImporterForPath(fs::path(file.from),file.directory)!=
                    studioImporterForPath(fs::path(file.to),file.directory))return false;
            }
        }
    }
    return hasMove;
}
StudioBatchAssetPlan studioPlanMixedAssetChain(const fs::path& project,
    const std::vector<editor::ProjectHistory::Step>& ordered,bool redo){
    if(!studioCanPlanMixedAssetChain(ordered))throw std::runtime_error("Unsupported mixed asset chain topology");
    const auto root=fs::canonical(project);
    StudioBatchAssetPlan out;
    const auto bytesDigest=[](const std::string& bytes){
        pack::Sha256 hash;
        const auto field=[&](const std::string& value){
            const auto prefix=std::to_string(value.size())+":";
            hash.update(prefix.data(),prefix.size());hash.update(value.data(),value.size());
        };
        field("file-v1");field(std::to_string(bytes.size()));hash.update(bytes.data(),bytes.size());
        const auto digest=hash.finish();return pack::toHex(digest.data(),digest.size());
    };
    struct LogicalMove{std::string from,to,digest;bool directory=false;std::string logicalName;std::string afterDigest{};};
    struct MetaCheck{std::string path,bytes;bool directory=false;};
    struct LogicalStep{
        std::vector<LogicalMove> moves;
        std::vector<MetaCheck> metaChecks;
        std::vector<editor::ProjectFileEdit> edits,resourceEdits;
        std::vector<std::pair<std::string,bool>> paths;
    };
    std::vector<LogicalStep> steps;
    std::set<std::string> endpoints,repairOwners;
    size_t operationCount=0;
    for(const auto& step:ordered){
        LogicalStep logical;
        if(step.move){
            const auto& plan=*step.move;validateHistoryMove(root,plan);
            std::vector<StudioReferenceRelocation> forward;
            if(!plan.quarantine&&!plan.replacement)
                for(size_t i=0;i<plan.files.size();i+=2){
                    const auto& f=plan.files[i];forward.push_back({"content/"+f.from,"content/"+f.to,f.directory});
                }
            logical.resourceEdits=redo?plan.resourceEdits:editor::reverseProjectFileEdits(plan.resourceEdits);
            for(auto& edit:logical.resourceEdits){
                const auto source=edit.path.generic_string();
                const auto dest=studioRelocatedReferencePath(source,forward);
                repairOwners.insert(source);repairOwners.insert(dest);
                if(!redo)edit.path=fs::path(dest);
            }
            for(size_t i=0;i<plan.files.size();i+=2){
                const auto& asset=plan.files[i];const auto& meta=plan.files[i+1];
                const auto live="content/"+asset.from;
                if(plan.replacement){
                    const auto& outgoing=redo?asset:meta;const auto& incoming=redo?meta:asset;
                    logical.moves.push_back({live,outgoing.to,outgoing.digest,false,asset.from});
                    logical.moves.push_back({incoming.to,live,incoming.digest,false,asset.from});
                    logical.metaChecks.push_back({live+".meta",asset.bytes,false});
                }else if(plan.quarantine){
                    const bool deleted=plan.created?!redo:redo;
                    const auto from=deleted?live:asset.to,to=deleted?asset.to:live;
                    logical.moves.push_back({from,to,asset.digest,asset.directory,asset.from});
                    logical.moves.push_back({from+".meta",to+".meta",bytesDigest(meta.bytes),false,{}});
                    logical.metaChecks.push_back({from+".meta",meta.bytes,asset.directory});
                    logical.paths.emplace_back(live,asset.directory);
                }else{
                    const auto from="content/"+(redo?asset.from:asset.to),to="content/"+(redo?asset.to:asset.from);
                    const auto repaired=asset.digestAfter.empty()?asset.digest:asset.digestAfter;
                    logical.moves.push_back({from,to,redo?asset.digest:repaired,asset.directory,asset.from,redo?repaired:asset.digest});
                    logical.moves.push_back({from+".meta",to+".meta",bytesDigest(meta.bytes),false,{}});
                    logical.metaChecks.push_back({from+".meta",meta.bytes,asset.directory});
                    logical.paths.emplace_back(from,asset.directory);logical.paths.emplace_back(to,asset.directory);
                }
            }
        }else{
            auto edit=step.files.front();
            validateHistoryMetadata(edit);
            if(!redo)std::swap(edit.before,edit.after);
            const auto rel=metadataRelative(project,edit.path);
            edit.path=fs::path(rel);logical.edits.push_back(std::move(edit));
        }
        for(const auto& m:logical.moves){
            if(++operationCount>256)throw std::runtime_error("Too many mixed asset relocations");
            endpoints.insert(m.from);endpoints.insert(m.to);
        }
        steps.push_back(std::move(logical));
    }
    // Directory roots are supported, but relocation endpoints cannot nest.
    for(auto it=endpoints.begin();it!=endpoints.end();++it)
        for(auto next=std::next(it);next!=endpoints.end();++next)
            if(batchOverlap(*it,*next))throw std::runtime_error("Nested mixed history endpoints");
    BatchState initial;
    std::map<std::string,std::string> origins;
    for(const auto& path:endpoints){
        auto identity=batchIdentity(batchPath(root,path));
        initial.emplace(path,identity);if(!identity.empty())origins.emplace(path,path);
    }
    StudioAssetCatalog catalog(root);catalog.rebuild(false);
    std::map<std::string,std::string> liveGuids;
    for(const auto& entry:catalog.all())liveGuids.emplace(entry.guid,"content/"+entry.path);
    ResourceOverlay overlay;
    const auto physical=[&](const std::string& rel)->fs::path{
        for(const auto& endpoint:endpoints){
            if(rel!=endpoint&&!rel.starts_with(endpoint+"/"))continue;
            const auto it=origins.find(endpoint);
            if(it==origins.end())throw std::runtime_error("Mixed history source absent: "+rel);
            if(rel!=endpoint&&initial.at(it->second).front()!='d')
                throw std::runtime_error("Mixed history descendant under a file");
            return root/(it->second+rel.substr(endpoint.size()));
        }
        return root/rel;
    };
    const auto currentBytes=[&](const std::string& rel){
        const auto path=physical(rel);const auto it=overlay.find(path);
        if(it!=overlay.end())return it->second.after;
        if(!fs::is_regular_file(fs::symlink_status(path))||fs::file_size(path)>1024u*1024u)
            throw std::runtime_error("Invalid mixed metadata source");
        return editor::readProjectFileBytes(path);
    };
    std::map<std::pair<fs::path,std::string>,std::vector<std::string>> verifiedPayloads;
    std::uintmax_t modelBytes=0;
    size_t treeEntries=0;
    const auto modelExtension=[](const fs::path& path){
        auto ext=path.extension().string();
        for(auto& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext==".glb"||ext==".gltf";
    };
    const std::vector<std::string> noUris;
    const auto verifyFile=[&](const fs::path& file,const fs::path& logical)->const std::vector<std::string>&{
        auto ext=logical.extension().string();
        for(auto& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto importer=studioImporterForPath(logical,false);
        if(importer=="texture"||importer=="audio"||ext==".bin")return noUris;
        if(ext!=".glb"&&ext!=".gltf")throw std::runtime_error("Unsupported mixed directory payload: "+logical.generic_string());
        const auto key=std::make_pair(file,ext);
        const auto cached=verifiedPayloads.find(key);
        if(cached!=verifiedPayloads.end())return cached->second;
        std::vector<std::string> uris;
        auto doc=studioReadModelJson(file,modelBytes,ext==".glb");
        const auto edited=overlay.find(file);
        if(edited!=overlay.end()){
            const auto& edit=edited->second;
            studioAccountModelJson(edit.after.size(),modelBytes);
            doc=studioStreamingResource(edit)?studioDecodeGlbPrefix(edit.after,edit.tailBytes):
                studioDecodeModelRepair(logical,edit.after).json;
        }
        if(!doc||!doc->isObject()||!(*doc)["asset"].isObject()||
           (*doc)["asset"].getStr("version")!="2.0")
            throw std::runtime_error("Mixed model must be a valid glTF 2.0 document");
        // Unknown extension payloads may name external resources; don't
        // claim they are self-contained. Embedded BIN / data URI is allowed.
        size_t nodes=0;
        const auto rejectExtensions=[&](const auto& self,const Json& value,size_t depth)->void{
            if(++nodes>2000000||depth>128)throw std::runtime_error("Mixed model inspection budget exceeded");
            if(value.isObject()){
                if(value.has("extensions"))throw std::runtime_error("Mixed model extensions require dependency review");
                for(const auto& [key,child]:value.members()){
                    if((key=="extensionsUsed"||key=="extensionsRequired")&&(!child.isArray()||child.size()))
                        throw std::runtime_error("Mixed model extensions require dependency review");
                    self(self,child,depth+1);
                }
            }else if(value.isArray())for(const auto& child:value.items())self(self,child,depth+1);
        };
        rejectExtensions(rejectExtensions,*doc,0);
        const bool binary=ext==".glb"&&studioGlbMagicFile(file);
        for(const char* group:{"buffers","images"}){
            if(doc->has(group)&&!(*doc)[group].isArray())throw std::runtime_error("Invalid mixed model resource array");
            for(const auto& entry:(*doc)[group].items()){
                if(!entry.isObject())throw std::runtime_error("Invalid mixed model resource entry");
                if(entry.has("uri")){
                    if(!entry["uri"].isString())throw std::runtime_error("Invalid mixed model resource URI");
                    const auto uri=entry.getStr("uri");
                    if(uri.starts_with("data:"))continue;
                    if(uris.size()>=32768)throw std::runtime_error("Mixed model URI budget exceeded");
                    uris.push_back(uri);
                }else if(std::string(group)=="buffers"? !binary : !entry["bufferView"].isNumber())
                    throw std::runtime_error("Mixed model resource has no embedded source");
            }
        }
        return verifiedPayloads.emplace(key,std::move(uris)).first->second;
    };
    // Manage every glTF owner that can appear at an exact relocation root
    // or within a relocated directory. Other formats and stationary owners
    // keep their existing conservative incoming/outgoing reference fence.
    const auto managed=[&](const std::string& path){
        if(!modelExtension(fs::path(path)))return false;
        if(repairOwners.count(path))return true;
        for(const auto& endpoint:endpoints)
            if(path==endpoint||path.starts_with(endpoint+"/"))return true;
        return false;
    };
    using EdgeKey=std::pair<fs::path,size_t>;
    using EdgeGraph=std::map<EdgeKey,std::string>;
    const auto graph=[&](){
        EdgeGraph result;
        std::map<std::string,std::string> byPath;
        for(const auto& [guid,path]:liveGuids)
            if(!byPath.emplace(path,guid).second)throw std::runtime_error("Duplicate mixed content path");
        for(const auto& [path,guid]:byPath){
            (void)guid;
            if(!managed(path))continue;
            const auto owner=physical(path);
            if(fs::is_directory(fs::symlink_status(owner)))continue;
            if(!studioSafeModelResourceFile(root,owner.lexically_relative(root).generic_string()))
                throw std::runtime_error("Missing mixed model owner: "+path);
            const auto& uris=verifyFile(owner,fs::path(path));
            for(size_t slot=0;slot<uris.size();++slot){
                if(result.size()>=32768)throw std::runtime_error("Mixed projected graph budget exceeded");
                const auto target=studioGltfResourcePath(path,uris[slot]);
                if(target.empty())throw std::runtime_error("Unsafe external mixed model resource URI");
                validateHistoryContentPath(target.substr(8));
                const auto found=byPath.find(target);
                if(found==byPath.end())throw std::runtime_error("External mixed model resource is unindexed: "+target);
                const auto resource=physical(target);
                if(!studioSafeModelResourceFile(root,resource.lexically_relative(root).generic_string()))
                    throw std::runtime_error("Missing external mixed model resource: "+target);
                result.emplace(EdgeKey{owner,slot},found->second);
            }
        }
        return result;
    };
    bool hasPathChanges=false;
    for(const auto& step:steps)if(!step.paths.empty())hasPathChanges=true;
    const auto baselineRefs=hasPathChanges?studioCollectGltfReferences(root,{}):std::vector<StudioAssetReference>{};
    auto priorGraph=graph();
    const auto nestedGuids=[&](const std::string& origin){
        std::vector<std::pair<std::string,std::string>> result;
        const auto tree=root/origin;
        std::set<std::string> guids;
        for(const auto& entry:fs::recursive_directory_iterator(tree)){
            if(++treeEntries>100000)throw std::runtime_error("Mixed directory inspection budget exceeded");
            const auto status=entry.symlink_status();
            const bool directory=fs::is_directory(status);
            if(!directory&&!fs::is_regular_file(status))throw std::runtime_error("Unsupported mixed tree entry");
            const auto rel=entry.path().lexically_relative(tree).generic_string();
            if(!directory&&entry.path().extension()==".meta"){
                const auto owner=entry.path().parent_path()/entry.path().stem();
                if(!existsOrSymlink(owner))throw std::runtime_error("Orphan sidecar in mixed directory");
                continue;
            }
            if(!directory)verifyFile(entry.path(),fs::path(rel));
            const auto metaPath=studioMetaPath(entry.path());
            const auto it=overlay.find(metaPath);
            if(!fs::is_regular_file(fs::symlink_status(metaPath))||fs::file_size(metaPath)>1024u*1024u)
                throw std::runtime_error("Missing or invalid nested asset sidecar");
            const auto bytes=it==overlay.end()?editor::readProjectFileBytes(metaPath):it->second.after;
            const auto meta=Json::parse(bytes);const auto guid=meta.getStr("guid");
            if(!studioValidGuid(guid)||!guids.insert(guid).second||!meta["settings"].isObject()||
               meta.getStr("importer")!=studioImporterForPath(fs::path(rel),directory))
                throw std::runtime_error("Invalid nested asset GUID/importer");
            result.emplace_back(guid,rel);
        }
        return result;
    };
    // Only external buffer/image URI spelling may change. All other JSON
    // and the GLB suffix are protected, including embedded resource bytes.
    const auto withoutExternalUris=[](const editor::ProjectFileEdit& edit,const std::string& bytes){
        auto doc=studioStreamingResource(edit)?studioDecodeGlbPrefix(bytes,edit.tailBytes):
            studioDecodeModelRepair(edit.path,bytes).json;
        for(const char* group:{"buffers","images"}){
            if(!doc.has(group))continue;
            auto array=doc[group];
            if(!array.isArray())throw std::runtime_error("Invalid mixed repair resource array");
            for(size_t i=0;i<array.size();++i){
                auto item=array[i];
                if(item["uri"].isString()&&!item.getStr("uri").starts_with("data:")){
                    item.set("uri",Json::string(""));array.at(i)=std::move(item);
                }
            }
            doc.set(group,std::move(array));
        }
        return doc.dump(-1);
    };
    for(auto& step:steps){
        std::set<fs::path> repairedOrigins;
        for(auto& edit:step.resourceEdits){
            studioValidateModelRepairBytes(edit);
            if(withoutExternalUris(edit,edit.before)!=withoutExternalUris(edit,edit.after))
                throw std::runtime_error("Mixed repair changed non-URI model content");
            const auto current=edit.path.generic_string();
            if(std::none_of(liveGuids.begin(),liveGuids.end(),[&](const auto& row){return row.second==current;}))
                throw std::runtime_error("Mixed repair owner is unindexed");
            edit.path=physical(current);
            if(!edit.path.lexically_relative(root).generic_string().starts_with("content/"))
                throw std::runtime_error("Mixed model repair requires a live physical origin");
            if(!repairedOrigins.insert(edit.path).second)
                throw std::runtime_error("Duplicate mixed physical repair owner");
        }
        for(const auto& check:step.metaChecks){
            const auto& path=check.path;const auto& expected=check.bytes;
            if(currentBytes(path)!=expected)throw std::runtime_error("Mixed history metadata baseline changed");
            const auto meta=Json::parse(expected);
            const auto guid=meta.getStr("guid");
            if(!studioValidGuid(guid))throw std::runtime_error("Invalid mixed history GUID");
            if(path.starts_with("content/")){
                const auto live=path.substr(0,path.size()-5);
                if(meta.getStr("importer")!=studioImporterForPath(fs::path(live),check.directory))
                    throw std::runtime_error("Mixed history importer changed");
                const auto it=liveGuids.find(guid);
                if(it==liveGuids.end()||it->second!=live)throw std::runtime_error("Mixed history GUID changed");
            }else if(liveGuids.count(guid))throw std::runtime_error("Restored mixed asset GUID conflict");
        }
        for(const auto& m:step.moves){
            const auto it=origins.find(m.from);
            if(it==origins.end()||origins.count(m.to))throw std::runtime_error("Mixed history occupancy changed");
            const auto origin=it->second;
            if(initial.at(origin).front()!=(m.directory?'d':'f')||
               moveDigest(root/origin,m.directory,&overlay)!=m.digest)
                throw std::runtime_error("Mixed virtual payload changed: "+m.from);
            if(m.directory){
                const auto children=nestedGuids(origin);
                for(const auto& [guid,relative]:children){
                    if(m.from.starts_with("content/")){
                        const auto it=liveGuids.find(guid);
                        if(it==liveGuids.end()||it->second!=m.from+"/"+relative)
                            throw std::runtime_error("Nested GUID changed before mixed move");
                        liveGuids.erase(it);
                    }else if(liveGuids.count(guid))throw std::runtime_error("Nested restored GUID collision");
                    if(m.to.starts_with("content/")&&!liveGuids.emplace(guid,m.to+"/"+relative).second)
                        throw std::runtime_error("Nested destination GUID collision");
                }
            }else if(!m.logicalName.empty())verifyFile(root/origin,fs::path(m.logicalName));
            if(batchMetadataPath(m.from)&&batchMetadataPath(m.to)){
                const auto meta=Json::parse(currentBytes(m.from));const auto guid=meta.getStr("guid");
                if(!studioValidGuid(guid))throw std::runtime_error("Invalid mixed sidecar GUID");
                if(m.from.starts_with("content/"))liveGuids.erase(guid);
                if(m.to.starts_with("content/")){
                    const auto live=m.to.substr(0,m.to.size()-5);
                    const auto payload=origins.find(live);
                    if(payload==origins.end()||meta.getStr("importer")!=studioImporterForPath(fs::path(live),initial.at(payload->second).front()=='d')||
                       !liveGuids.emplace(guid,live).second)
                        throw std::runtime_error("Mixed chain importer mismatch or duplicate GUID");
                }
            }
            out.relocations.push_back({m.from,m.to,initial.at(origin).substr(1),m.directory});
            origins.erase(it);origins.emplace(m.to,origin);
        }
        for(auto& edit:step.resourceEdits){
            auto it=overlay.find(edit.path);
            if(it==overlay.end()){
                if(!studioResourceMatches(edit.path,edit))
                    throw std::runtime_error("Mixed model repair baseline changed");
                const auto path=edit.path;overlay.emplace(path,std::move(edit));
            }else{
                if(it->second.after!=edit.before||it->second.tailDigest!=edit.tailDigest||
                   it->second.tailBytes!=edit.tailBytes)
                    throw std::runtime_error("Inconsistent mixed model repair chain");
                it->second.after=std::move(edit.after);
            }
        }
        for(auto it=verifiedPayloads.begin();it!=verifiedPayloads.end();)
            if(repairedOrigins.count(it->first.first))it=verifiedPayloads.erase(it);else ++it;
        for(const auto& m:step.moves)
            if(!m.afterDigest.empty()&&moveDigest(physical(m.to),m.directory,&overlay)!=m.afterDigest)
                throw std::runtime_error("Mixed repaired payload digest changed");
        for(auto& edit:step.edits){
            const auto rel=edit.path.generic_string();
            const auto live=rel.substr(0,rel.size()-5);
            const auto meta=Json::parse(edit.before);
            const auto guid=liveGuids.find(meta.getStr("guid"));
            if(guid==liveGuids.end()||guid->second!=live||
               meta.getStr("importer")!=studioImporterForPath(fs::path(live),false)||
               currentBytes(rel)!=edit.before)
                throw std::runtime_error("Mixed metadata history identity changed");
            const auto path=physical(rel);
            // Nested sidecar changes ending in Trash need record-digest and
            // generic document journal support; do not publish those yet.
            if(inRecoveryTrash(path.lexically_relative(root).generic_string())&&
               path.filename()!="asset.meta")throw std::runtime_error("Nested Trash metadata requires separate navigation");
            edit.path=path;
            auto it=overlay.find(path);
            if(it==overlay.end())overlay.emplace(path,std::move(edit));
            else it->second.after=std::move(edit.after);
        }
        // Check only complete logical steps, not transient states between
        // a payload and its sidecar or the two halves of a version exchange.
        // A surviving physical model must keep every external slot bound
        // to the same asset GUID, even when both endpoints moved.
        auto nextGraph=graph();
        for(const auto& [edge,target]:nextGraph){
            const auto old=priorGraph.find(edge);
            if(old!=priorGraph.end()&&old->second!=target)
                throw std::runtime_error("Mixed model resource identity changed");
        }
        for(const auto& owner:repairedOrigins){
            for(const auto& [edge,target]:priorGraph)
                if(edge.first==owner&&(!nextGraph.count(edge)||nextGraph.at(edge)!=target))
                    throw std::runtime_error("Mixed repair lost a resource edge");
            for(const auto& [edge,target]:nextGraph)
                if(edge.first==owner&&(!priorGraph.count(edge)||priorGraph.at(edge)!=target))
                    throw std::runtime_error("Mixed repair introduced a resource edge");
        }
        priorGraph=std::move(nextGraph);
        for(const auto& ref:baselineRefs){
            if(ref.owner=="glTF resource"&&managed(ref.document))continue;
            for(const auto& [path,directory]:step.paths)
                if(studioReferenceMatchesPath(ref.path,path,directory)||
                   studioReferenceMatchesPath(ref.document,path,directory))
                    throw std::runtime_error("Asset operation blocked by unmanaged model resource: "+ref.document+" "+ref.field);
        }
    }
    const auto timeline=batchTimeline(root,out.relocations);
    if(batchPhase(root,timeline)!=0)throw std::runtime_error("Mixed history baseline changed");
    for(auto& [path,edit]:overlay){
        if(edit.before==edit.after)continue;
        const auto source=path.lexically_relative(root).generic_string();
        const auto destination=batchResourceDestination(source,out.relocations);
        if(destination!=source){
            if(inRecoveryTrash(destination)&&!destination.ends_with("/asset.meta"))
                throw std::runtime_error("Nested Trash metadata requires separate navigation");
            edit.path=fs::path(source);out.movingResources.push_back({edit,destination});
        }
        else out.documents.push_back(edit);
    }
    validateBatchDocuments(root,timeline,out.documents);
    validateMovingBatchResources(root,out.relocations,out.movingResources);
    return out;
}
void studioRecoverHistoryBatch(const fs::path& project){
    const auto root=fs::canonical(project),folder=batchFolder(root);
    if(!existsOrSymlink(folder))return;
    if(!fs::is_directory(fs::symlink_status(folder)))throw std::runtime_error("Unsafe coordinated history journal");
    const auto pending=folder/"pending.json",committed=folder/"committed.json",rolledback=folder/"rolledback.json";
    if(existsOrSymlink(committed)||existsOrSymlink(rolledback)){
        if(existsOrSymlink(pending)||(existsOrSymlink(committed)&&existsOrSymlink(rolledback))||
           (existsOrSymlink(committed)&&!fs::is_regular_file(fs::symlink_status(committed)))||
           (existsOrSymlink(rolledback)&&!fs::is_regular_file(fs::symlink_status(rolledback))))
            throw std::runtime_error("Ambiguous coordinated history journal");
        retireBatch(folder);return;
    }
    if(!existsOrSymlink(pending)){retireBatch(folder);return;}
    if(!fs::is_regular_file(fs::symlink_status(pending))||fs::file_size(pending)>256u*1024u*1024u)
        throw std::runtime_error("Invalid coordinated history journal");
    const auto envelope=Json::parseFile(pending.string());const auto payload=envelope.getStr("payload");
    const auto version=envelope.getInt("version");
    if((version!=1&&version!=2&&version!=3&&version!=4&&version!=5)||historyHash(payload)!=envelope.getStr("sha256"))
        throw std::runtime_error("Coordinated journal checksum mismatch");
    const auto body=Json::parse(payload);
    if(!body["moves"].isArray()||body["moves"].size()>256||!body["documents"].isArray()||body["documents"].size()>4096)
        throw std::runtime_error("Invalid coordinated journal rows");
    std::vector<StudioBatchRelocation> moves;
    std::vector<editor::ProjectFileEdit> docs;
    std::vector<StudioBatchResourceEdit> resources;
    if(version==3||version==4||version==5){
        if(!body["movingResources"].isArray()||body["movingResources"].size()>32)
            throw std::runtime_error("Invalid moving resource journal rows");
        for(const auto& row:body["movingResources"].items()){
            if(!row["destination"].isString())throw std::runtime_error("Invalid moving resource destination");
            if(row.getBool("metadata")){
                if(!row["path"].isString()||!row["before"].isString()||!row["after"].isString()||
                   row.has("encoding")||row.has("tailDigest")||row.has("tailBytes"))
                    throw std::runtime_error("Invalid moving metadata snapshot");
                editor::ProjectFileEdit edit{fs::path(row.getStr("path")),row.getStr("before"),row.getStr("after")};
                validateHistoryMetadata(edit);
                resources.push_back({std::move(edit),row.getStr("destination")});
            }else resources.push_back({studioDecodeResourceEdit(row,true),row.getStr("destination")});
        }
    }else if(body.has("movingResources"))throw std::runtime_error("Moving resource journal version mismatch");
    for(const auto& row:body["moves"].items())
        moves.push_back({row.getStr("from"),row.getStr("to"),row.getStr("digest"),row.getBool("directory")});
    for(const auto& row:body["documents"].items()){
        if(!row["before"].isString()||!row["after"].isString())throw std::runtime_error("Invalid coordinated document snapshot");
        const auto path=editor::project_file_detail::safeTarget(root,row.getStr("path"));
        if(version>=2&&row.getBool("resource")){
            auto edit=studioDecodeResourceEdit(row,true);edit.path=path;docs.push_back(std::move(edit));
        }else{
            if(row.has("encoding")||row.has("tailDigest")||row.has("tailBytes"))
                throw std::runtime_error("Invalid coordinated document encoding");
            docs.push_back({path,row.getStr("before"),row.getStr("after")});
        }
    }
    // Resolve any nested existing-file journal before examining its baseline.
    editor::recoverProjectFileEdits(root);
    std::optional<editor::ProjectFileEdit> checkpoint;
    bool checkpointReady=false;
    if(version==4||version==5){
        const bool createOnly=version==5;
        const auto target=editor::project_file_detail::safeTarget(root,".aiverse-cache/asset-history.json",createOnly);
        const auto readSnapshot=[&](const fs::path& file,const std::string& hash){
            if(!validHistoryDigest(hash)||!fs::is_regular_file(fs::symlink_status(file))||
               fs::file_size(file)>AssetCheckpointMaxBytes)
                throw std::runtime_error("Invalid coordinated asset checkpoint snapshot");
            auto bytes=editor::readProjectFileBytes(file);
            if(historyHash(bytes)!=hash)throw std::runtime_error("Coordinated asset checkpoint checksum mismatch");
            return bytes;
        };
        auto before=readSnapshot(folder/"asset-before",body.getStr("assetCheckpointBefore"));
        if(createOnly&&!before.empty())throw std::runtime_error("Invalid absent asset checkpoint baseline");
        auto after=before; // no ready marker means no checkpoint publication was allowed
        const auto ready=folder/"asset-ready.json";
        if(existsOrSymlink(ready)){
            if(!fs::is_regular_file(fs::symlink_status(ready))||fs::file_size(ready)>4096)
                throw std::runtime_error("Invalid asset checkpoint readiness marker");
            const auto row=Json::parseFile(ready.string());
            if(row.getStr("transaction")!=historyHash(payload))
                throw std::runtime_error("Asset checkpoint belongs to another transaction");
            after=readSnapshot(folder/"asset-after",row.getStr("afterHash"));
            checkpointReady=true;
        }
        checkpoint=editor::ProjectFileEdit{target,std::move(before),std::move(after),{},0,createOnly};
        for(const auto& f:docs)if(f.path==target)
            throw std::runtime_error("Duplicate coordinated asset checkpoint");
    }else if(body.has("assetCheckpointBefore")||existsOrSymlink(folder/"asset-ready.json"))
        throw std::runtime_error("Asset checkpoint journal version mismatch");
    if(version<4&&moves.empty())throw std::runtime_error("Empty legacy coordinated relocation journal");
    reverseBatch(root,folder,moves,docs,resources,checkpoint,checkpointReady);
}
void studioApplyHistoryBatch(const fs::path& project,const std::vector<StudioBatchRelocation>& moves,
                            const std::vector<editor::ProjectFileEdit>& documents,
                            const std::vector<StudioBatchResourceEdit>& movingResources,
                            const AssetHistory* currentAssets,const AssetHistory* nextAssets){
    if(bool(currentAssets)!=bool(nextAssets))throw std::runtime_error("Both asset history cursors are required");
    if(moves.empty()&&!movingResources.empty())throw std::runtime_error("Moving resources require relocation chain");
    if(moves.empty()&&!nextAssets){editor::applyProjectFileEdits(project,documents);return;}
    const auto root=fs::canonical(project),folder=batchFolder(root);
    editor::project_file_detail::directory(folder.parent_path());
    std::unique_ptr<editor::project_file_detail::Lock> assetLock;
    std::optional<editor::ProjectFileEdit> checkpoint;
    if(nextAssets){
        const auto cache=assetHistoryCache(root,true);
        assetLock=std::make_unique<editor::project_file_detail::Lock>(cache/"asset-history.lock");
        checkTrashJournal(root);
        if(existsOrSymlink(cache/"asset-history.dirty"))
            throw std::runtime_error("Interrupted legacy asset publication; reopen project");
        const auto target=editor::project_file_detail::safeTarget(root,".aiverse-cache/asset-history.json",true);
        const bool exists=existsOrSymlink(target);
        if(exists&&fs::file_size(target)>AssetCheckpointMaxBytes)throw std::runtime_error("Oversized asset checkpoint");
        auto before=exists?editor::readProjectFileBytes(target):std::string{};
        if(exists?before!=encodeAssetHistory(root,*currentAssets):!currentAssets->steps().empty())
            throw std::runtime_error("Asset history checkpoint baseline changed; reopen project");
        checkpoint=editor::ProjectFileEdit{target,std::move(before),{}, {},0,!exists};
    }
    if(existsOrSymlink(folder.parent_path()/"studio-move-journal"))throw std::runtime_error("Pending asset move journal");
    const auto timeline=batchTimeline(root,moves);
    if(batchPhase(root,timeline)!=0)throw std::runtime_error("Coordinated relocation baseline changed");
    validateMovingBatchResources(root,moves,movingResources);
    int64_t metadataGrowth=0;
    for(const auto& resource:movingResources)if(inRecoveryTrash(resource.destination)){
        // Payload relocation accounting uses original physical bytes. Add
        // the exact final sidecar size difference for retained Trash data.
        metadataGrowth+=static_cast<int64_t>(resource.edit.after.size())-
                        static_cast<int64_t>(resource.edit.before.size());
    }
    checkRecoveryGrowth(root,moves,metadataGrowth);
    for(const auto& resource:movingResources){
        const auto& edit=resource.edit;
        const auto path=editor::project_file_detail::safeTarget(root,edit.path.generic_string());
        if(!studioResourceMatches(path,edit))throw std::runtime_error("Moving resource baseline changed");
        if(studioStreamingResource(edit)&&fs::space(root).available<
           std::max<uint64_t>(edit.before.size(),edit.after.size())+edit.tailBytes+16ull*1024*1024)
            throw std::runtime_error("Insufficient disk for moving streamed repair");
    }
    std::vector<editor::ProjectFileEdit> docs=documents;
    for(auto& f:docs)f.path=fs::canonical(f.path);
    validateBatchDocuments(root,timeline,docs);
    for(const auto& f:docs){
        if(checkpoint&&f.path==checkpoint->path)throw std::runtime_error("Duplicate coordinated asset checkpoint");
        if(!studioResourceMatches(f.path,f))throw std::runtime_error("Coordinated document baseline changed");
        if(studioStreamingResource(f)&&fs::space(root).available<
           std::max<uint64_t>(f.before.size(),f.after.size())+f.tailBytes+16ull*1024*1024)
            throw std::runtime_error("Insufficient disk for coordinated streamed repair");
    }
    if(existsOrSymlink(folder)||!fs::create_directory(folder))throw std::runtime_error("Previous coordinated history journal needs recovery");
    try{
        Json body=Json::object(),rows=Json::array(),files=Json::array();
        for(const auto& m:moves){
            Json row=Json::object();row.set("from",Json::string(m.from));row.set("to",Json::string(m.to));
            row.set("digest",Json::string(m.digest));row.set("directory",Json::fromBool(m.directory));rows.push(std::move(row));
        }
        for(const auto& f:docs){
            auto relative=f;relative.path=f.path.lexically_relative(root);
            Json row=Json::object();
            if(relative.path.generic_string().starts_with("content/")&&studioRepairModelExtension(relative.path)){
                studioValidateModelRepairBytes(relative);
                row=studioEncodeResourceEdit(relative);row.set("resource",Json::fromBool(true));
            }else{
                row.set("path",Json::string(relative.path.generic_string()));
                row.set("before",Json::string(f.before));row.set("after",Json::string(f.after));
            }
            files.push(std::move(row));
        }
        body.set("moves",std::move(rows));body.set("documents",std::move(files));
        if(!movingResources.empty()||checkpoint){
            Json resources=Json::array();
            for(const auto& resource:movingResources){
                Json row=Json::object();
                if(batchMetadataPath(resource.edit.path.generic_string())){
                    row.set("metadata",Json::fromBool(true));
                    row.set("path",Json::string(resource.edit.path.generic_string()));
                    row.set("before",Json::string(resource.edit.before));row.set("after",Json::string(resource.edit.after));
                }else row=studioEncodeResourceEdit(resource.edit);
                row.set("destination",Json::string(resource.destination));resources.push(std::move(row));
            }
            body.set("movingResources",std::move(resources));
        }
        if(checkpoint){
            writeAssetCheckpoint(folder/"asset-before",checkpoint->before);
            body.set("assetCheckpointBefore",Json::string(historyHash(checkpoint->before)));
        }
        const auto payload=body.dump(-1);Json envelope=Json::object();
        envelope.set("version",Json::number(int64_t(checkpoint?(checkpoint->createOnly?5:4):(movingResources.empty()?2:3))));
        envelope.set("sha256",Json::string(historyHash(payload)));envelope.set("payload",Json::string(payload));
        const auto text=envelope.dump(-1)+"\n";
        if(text.size()>256u*1024u*1024u)throw std::runtime_error("Coordinated journal exceeds disk budget");
        writeAssetCheckpoint(folder/"pending.tmp",text);
        StudioMoveJournal::renamePath(folder/"pending.tmp",folder/"pending.json");
        StudioMoveJournal::syncPath(folder.parent_path());
        for(const auto& m:moves){
            const auto from=batchPath(root,m.from),to=batchPath(root,m.to);
            if(batchIdentity(from)!=std::string(m.directory?"d":"f")+m.digest||existsOrSymlink(to))
                throw std::runtime_error("Coordinated relocation changed during publication");
            StudioMoveJournal::renamePath(from,to);
        }
        if(batchPhase(root,timeline)!=moves.size())throw std::runtime_error("Coordinated relocation result mismatch");
        auto finalDocs=docs;
        auto moved=finalMovingBatchEdits(root,movingResources);
        finalDocs.insert(finalDocs.end(),moved.begin(),moved.end());
        StudioMoveJournal(root).applyResources(finalDocs,true);
        for(const auto& edit:finalDocs)
            if(!studioResourceMatches(edit.path,edit,true))throw std::runtime_error("Coordinated final document identity changed");
        if(!movingResources.empty())checkFinalBatchPayloads(root,timeline,movingResources);
        if(checkpoint){
            // Compute the exact content/material/Trash fingerprint from the
            // actual final disk state while the outer pending marker is live.
            // Persist both byte images and a transaction-bound ready marker
            // before the inner journal may touch the checkpoint.
            checkpoint->after=encodeAssetHistory(root,*nextAssets);
            writeAssetCheckpoint(folder/"asset-after",checkpoint->after);
            Json ready=Json::object();
            ready.set("transaction",Json::string(historyHash(payload)));
            ready.set("afterHash",Json::string(historyHash(checkpoint->after)));
            writeAssetCheckpoint(folder/"asset-ready.tmp",ready.dump(-1));
            StudioMoveJournal::renamePath(folder/"asset-ready.tmp",folder/"asset-ready.json");
            editor::applyProjectFileEdits(root,{*checkpoint});
            if(!studioResourceMatches(checkpoint->path,*checkpoint,true)||
               encodeAssetHistory(root,*nextAssets)!=checkpoint->after)
                throw std::runtime_error("Coordinated final asset checkpoint changed");
        }
        // The outer marker is authoritative even after the inner journal commits.
        StudioMoveJournal::renamePath(folder/"pending.json",folder/"committed.json");
    }catch(const std::exception& e){
        const std::string reason=e.what();
        if(existsOrSymlink(folder/"committed.json"))
            throw std::runtime_error("Coordinated commit outcome uncertain; reopen project: "+reason);
        try{studioRecoverHistoryBatch(root);}
        catch(const std::exception& rollback){throw std::runtime_error(reason+"; coordinated recovery incomplete: "+rollback.what());}
        throw std::runtime_error(reason);
    }
    // A cleanup failure after durable commit must not be reported as rollback.
    try{retireBatch(folder);}catch(...){}
}
} // namespace aiverse::studio

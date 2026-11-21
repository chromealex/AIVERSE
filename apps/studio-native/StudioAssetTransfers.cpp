#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include <algorithm>
#include <fstream>
#include <exception>
#include <map>
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
    return !ec && fs::exists(status);
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
                                     const std::string& name) {
    validateSegment(name);
    const fs::path destination=checkedFolder(project,parent)/name;
    if(existsOrSymlink(destination)||existsOrSymlink(studioMetaPath(destination)))
        throw std::runtime_error("Folder already exists or has a metadata collision");
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
                if(dest==sourceAbs||dest.string().rfind(sourceAbs.string()+fs::path::preferred_separator,0)==0)
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
        x.set("guid",e.guid.empty()?Json():Json::string(e.guid));
        items.push(std::move(x));
    }
    response.set("items",std::move(items));
    return response;
}
Json StudioTransferPlan::apply(const std::string& token,
                         const std::vector<StudioTransferDecision>& decisions,
                         const std::function<void()>& beforeCommit){
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
        journal.begin(journalOps);
        try {
            for (const auto& operation:operations) {
                fs::rename(operation.source,operation.destination);
                // Persist both directory entries before publishing the final
                // committed marker. A power loss replays the pending journal.
                StudioMoveJournal::syncPath(operation.source.parent_path());
                if (operation.source.parent_path()!=operation.destination.parent_path())
                    StudioMoveJournal::syncPath(operation.destination.parent_path());
            }
            StudioAssetCatalog catalog(project_); catalog.rebuild();
            if (beforeCommit) beforeCommit();
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
        return result;
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
} // namespace aiverse::studio

#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include "engine/pack/Sha256.h"
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
// Hash a complete tree with deterministic path/type/length framing, including
// empty directories and all sidecars. Reject symlinks, special files and
// excessive entry counts. Memory is bounded independently of payload size.
std::string moveDigest(const fs::path& source,bool directory){
    pack::Sha256 hash;
    const auto field=[&](const std::string& s){
        const auto size=std::to_string(s.size())+":";
        hash.update(size.data(),size.size());hash.update(s.data(),s.size());
    };
    const auto file=[&](const fs::path& path){
        if(!fs::is_regular_file(fs::symlink_status(path)))
            throw std::runtime_error("Unsupported file in asset history");
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
    if(!directory){field("file-v1");file(source);}
    else {
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
bool matchesMoveFile(const fs::path& path,const editor::ProjectHistory::MoveFile& f){
    if(!f.digest.empty())return moveDigest(path,f.directory)==f.digest;
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
    StudioAssetCatalog live(project); // also validates/creates the cache root
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
                                     const std::string& name,editor::ProjectHistory::MovePlan* history) {
    validateSegment(name);
    const fs::path destination=checkedFolder(project,parent)/name;
    if(existsOrSymlink(destination)||existsOrSymlink(studioMetaPath(destination)))
        throw std::runtime_error("Folder already exists or has a metadata collision");
    if(history){
        editor::ProjectHistory::MovePlan plan;plan.quarantine=true;plan.created=true;
        const auto relative=parent.empty()?name:parent+"/"+name;
        stageCreatedAsset(project,relative,{},true,false,plan);
        studioReplayAssetQuarantine(project,plan,false);
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
                         const std::function<void()>& beforeCommit,
                         std::optional<editor::ProjectHistory::MovePlan>* history){
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
        // Root sidecars and material documents retain exact bytes; asset payloads
        // use a streaming digest, including complete directory trees.
        std::optional<editor::ProjectHistory::MovePlan> recorded;
        if(history&&!planned.empty()){
            try {
                editor::ProjectHistory::MovePlan candidate;
                size_t used=0;
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
                for(const auto& op:operations){
                    editor::ProjectHistory::MoveFile file;
                    file.from=op.source.lexically_relative(root).generic_string();
                    file.to=op.destination.lexically_relative(root).generic_string();
                    file.directory=!op.meta&&fs::is_directory(fs::symlink_status(op.source));
                    if(op.meta)file.bytes=capture(op.source);
                    else file.digest=moveDigest(op.source,file.directory);
                    candidate.files.push_back(std::move(file));
                }
                const auto material=project_/"materials.json";
                if(existsOrSymlink(material))candidate.materialsBefore=capture(material);
                recorded=std::move(candidate);
            }catch(const std::exception&){recorded.reset();}
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
                        if(!matchesMoveFile(root/f.to,f))
                            throw std::runtime_error("Asset changed during move");
                }catch(const std::exception&){recorded.reset();}
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
        if(history)*history=std::move(recorded);
        return result;
    }

    // Non-replacing imports are one staged publication, including their GUIDs.
    // Existing explicit replacement remains a history barrier until its own
    // reversible old/new payload owner is introduced.
    if(history){
        std::map<int,std::string> actions;
        for(const auto& d:decisions){
            if(d.id<0||size_t(d.id)>=entries_.size()||!actions.emplace(d.id,d.action).second)
                throw std::runtime_error("Unknown or duplicate import decision");
        }
        bool replacement=false;
        for(const auto& [id,action]:actions){(void)id;if(action=="replace")replacement=true;}
        if(!replacement){
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
                studioReplayAssetQuarantine(project_,plan,false);
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
                           const std::function<void()>& validateReferences){
    const auto root=studioContentRoot(project);
    if(plan.files.empty()||plan.files.size()>128||plan.files.size()%2)
        throw std::runtime_error("Invalid recorded asset move");
    const auto& materialBefore=redo?plan.materialsBefore:plan.materialsAfter;
    const auto& materialAfter=redo?plan.materialsAfter:plan.materialsBefore;
    const auto material=project/"materials.json";
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
            if(!matchesMoveFile(src,f))
                throw std::runtime_error("Asset move history source bytes changed: "+from);
            operations.push_back({from,to});
        }
    }
    if(validateReferences)validateReferences();
    checkMaterial();
    StudioMoveJournal journal(project);journal.begin(operations);
    try{
        for(size_t i=0;i<operations.size();++i){
            const auto& op=operations[i];
            const auto from=root/op.from,to=root/op.to;
            if(!matchesMoveFile(from,plan.files[i]))
                throw std::runtime_error("Asset changed during history replay");
            if(existsOrSymlink(to))throw std::runtime_error("Asset move destination occupied");
            fs::rename(from,to);
            StudioMoveJournal::syncPath(from.parent_path());
            if(from.parent_path()!=to.parent_path())StudioMoveJournal::syncPath(to.parent_path());
        }
        for(size_t i=0;i<operations.size();++i)
            if(!matchesMoveFile(root/operations[i].to,plan.files[i]))
                throw std::runtime_error("Asset bytes changed during replay");
        if(materialBefore)editor::applyProjectFileEdits(project,{{material,*materialBefore,*materialAfter}});
        else checkMaterial();
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
    const editor::ProjectHistory::MovePlan& plan,bool deleted,const std::function<void()>& validateReferences){
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
    StudioMoveJournal journal(project);journal.begin(operations,true);
    try{
        for(size_t i=0;i<operations.size();++i){
            const auto from=project/operations[i].from,to=project/operations[i].to;
            if(existsOrSymlink(to)||!matchesMoveFile(from,plan.files[i]))
                throw std::runtime_error("Asset quarantine changed during replay");
            fs::rename(from,to);
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
    const std::string& path,const std::string& expectedGuid,const std::function<void()>& validateReferences){
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
    const auto record=folder/"record.json";
    {std::ofstream out(record,std::ios::binary);out<<recovery.dump(2)<<"\n";out.close();if(!out)throw std::runtime_error("Cannot write asset quarantine record");}
    StudioMoveJournal::syncPath(record);StudioMoveJournal::syncPath(folder);StudioMoveJournal::syncPath(root);
    // Leave the record and any payload on disk even if the in-memory history is
    // later evicted. Manual recovery is always possible; no automatic purge.
    studioReplayAssetQuarantine(project,plan,true,validateReferences);
    return plan;
}
} // namespace aiverse::studio

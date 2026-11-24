#include "ProjectHistoryPersistence.h"
#include "ProjectDocumentJournal.h"
#include <map>
namespace aiverse::editor {
namespace {
namespace fs=std::filesystem;
constexpr std::size_t MaxCheckpoint=128u*1024u*1024u;
const char* Checkpoint=".aiverse-cache/project-history.json";
std::string relativePath(const fs::path& project,const fs::path& file){
    const auto root=fs::canonical(project);
    const auto rel=fs::canonical(file).lexically_relative(root).generic_string();
    if(rel==Checkpoint)throw std::runtime_error("History cannot edit itself");
    (void)project_file_detail::safeTarget(root,rel);
    return rel;
}
}
std::string encodeProjectHistory(const fs::path& project,const ProjectHistory& history){
    Json body=Json::object(),rows=Json::array();
    body.set("position",Json::number(int64_t(history.position())));
    for(const auto& step:history.steps()){
        if(step.move)throw std::runtime_error("Asset history cannot be saved by Project owner");
        Json row=Json::object(),files=Json::array();row.set("label",Json::string(step.label));
        for(const auto& f:step.files){
            Json entry=Json::object();entry.set("path",Json::string(relativePath(project,f.path)));
            entry.set("before",Json::string(f.before));entry.set("after",Json::string(f.after));files.push(std::move(entry));
        }
        row.set("files",std::move(files));rows.push(std::move(row));
    }
    body.set("steps",std::move(rows));
    const auto payload=body.dump(-1);
    Json envelope=Json::object();envelope.set("version",Json::number(int64_t(1)));
    envelope.set("sha256",Json::string(project_file_detail::digest(payload)));
    envelope.set("payload",Json::string(payload));
    auto bytes=envelope.dump(-1)+"\n";
    if(bytes.size()>MaxCheckpoint)throw std::runtime_error("Project history checkpoint exceeds disk budget");
    return bytes;
}
ProjectHistory decodeProjectHistory(const fs::path& project,const std::string& bytes,
                                    const ProjectHistoryBaseline& baseline){
    if(bytes.size()>MaxCheckpoint)throw std::runtime_error("Project history checkpoint exceeds disk budget");
    const auto envelope=Json::parse(bytes);
    const auto payload=envelope.getStr("payload");
    if(envelope.getInt("version")!=1||project_file_detail::digest(payload)!=envelope.getStr("sha256"))
        throw std::runtime_error("Project history checkpoint checksum mismatch");
    const auto body=Json::parse(payload);
    const auto& rows=body["steps"];const auto position=body.getInt("position",-1);
    if(!rows.isArray()||rows.size()>ProjectHistory::MaxSteps||position<0||size_t(position)>rows.size())
        throw std::runtime_error("Invalid project history checkpoint cursor");
    ProjectHistory history;
    ProjectHistoryBaseline previous;
    const auto root=fs::canonical(project);
    for(size_t i=0;i<rows.size();++i){
        const auto& row=rows[i];const auto label=row.getStr("label");
        const auto& files=row["files"];
        if(label.empty()||label.size()>256||!files.isArray()||files.size()!=baseline.size())
            throw std::runtime_error("Project history document set changed");
        std::vector<ProjectFileEdit> edits;ProjectHistoryBaseline before,after;
        for(const auto& f:files.items()){
            const auto path=f.getStr("path");
            if(!baseline.count(path)||!f["before"].isString()||!f["after"].isString()||
               !before.emplace(path,f.getStr("before")).second)
                throw std::runtime_error("Invalid project history dependency");
            after.emplace(path,f.getStr("after"));
            // Keep the owner-relative spelling used by Workspace historyStep.
            (void)project_file_detail::safeTarget(root,path);
            edits.push_back({project/fs::path(path),before.at(path),after.at(path)});
        }
        if(i&&before!=previous)throw std::runtime_error("Discontinuous project history checkpoint");
        if((i==0&&position==0&&before!=baseline)||(size_t(position)==i+1&&after!=baseline))
            throw std::runtime_error("Project files changed since history checkpoint");
        previous=std::move(after);
        history.record(std::move(edits),label);
        if(history.steps().size()!=i+1)throw std::runtime_error("Project history checkpoint exceeds retained budget");
    }
    while(history.position()>size_t(position))history.advance(false);
    return history; // new epoch; old client versions are never reused
}
std::optional<std::string> readProjectHistoryCheckpoint(const fs::path& project){
    const auto root=fs::canonical(project),cache=root/".aiverse-cache",path=root/Checkpoint;
    if(!project_file_detail::journalExists(cache))return {};
    if(!fs::is_directory(fs::symlink_status(cache)))throw std::runtime_error("Unsafe history cache");
    if(!project_file_detail::journalExists(path))return {};
    if(!fs::is_regular_file(fs::symlink_status(path))||fs::file_size(path)>MaxCheckpoint)
        throw std::runtime_error("Unsafe or oversized project history checkpoint");
    return readProjectFileBytes(path);
}
ProjectFileEdit prepareProjectHistoryCheckpoint(const fs::path& project,
    const std::optional<std::string>& expected,const ProjectHistory& next){
    using namespace project_file_detail;
    const auto root=fs::canonical(project),cache=root/".aiverse-cache",path=root/Checkpoint;
    directory(cache);
    Lock lock(cache/"project-document.lock");
    const auto existing=readProjectHistoryCheckpoint(root);
    if(existing!=expected)throw std::runtime_error("Project history changed in another editor; reopen the project");
    // The existing-file transaction needs a baseline. Initialize only a
    // missing checkpoint, with a valid empty history, before the transaction.
    auto before=existing.value_or("");
    if(!existing){
        before=encodeProjectHistory(root,ProjectHistory{});
        const auto tmp=cache/"project-history.init";
        if(journalExists(tmp)){
            if(!fs::is_regular_file(fs::symlink_status(tmp)))throw std::runtime_error("Unsafe history initialization");
            fs::remove(tmp);
        }
        write(tmp,before);replace(tmp,path);sync(root);
    }
    return {path,std::move(before),encodeProjectHistory(project,next)};
}
}

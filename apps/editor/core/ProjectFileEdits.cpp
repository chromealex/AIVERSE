#include "ProjectDocumentJournal.h"
namespace aiverse::editor {
void syncProjectFilePath(const std::filesystem::path& path){
    project_file_detail::sync(path);
}
void renameProjectFilePath(const std::filesystem::path& source,const std::filesystem::path& target,bool replaceExisting){
    namespace fs=std::filesystem;
    if(!replaceExisting&&project_file_detail::journalExists(target))
        throw std::runtime_error("Asset destination occupied: "+target.string());
#if defined(_WIN32)
    const DWORD flags=MOVEFILE_WRITE_THROUGH|(replaceExisting?MOVEFILE_REPLACE_EXISTING:0);
    if(!MoveFileExW(source.c_str(),target.c_str(),flags))
        throw std::runtime_error("Cannot publish asset path: "+target.string());
#else
    fs::rename(source,target);
#endif
    project_file_detail::sync(target.parent_path());
    if(source.parent_path()!=target.parent_path())project_file_detail::sync(source.parent_path());
}
bool recoverProjectFileEdits(const std::filesystem::path& project){
    namespace fs=std::filesystem;using namespace project_file_detail;
    const auto cache=fs::canonical(project)/".aiverse-cache";
    if(!journalExists(cache))return false;
    if(!fs::is_directory(fs::symlink_status(cache)))throw std::runtime_error("Unsafe project cache directory");
    if(!journalExists(cache/"project-document-journal"))return false;
    ProjectDocumentJournal journal(project);return journal.recover();
}
void applyProjectFileEdits(const std::filesystem::path& project,const std::vector<ProjectFileEdit>& edits){
    if(edits.empty())return;
    ProjectDocumentJournal journal(project);
    if(!journal.begin(edits))return;
    try{
        for(size_t i=0;i<edits.size();++i)journal.publish(i);
        journal.commit();
    }catch(const std::exception& failure){
        const std::string reason=failure.what();
        try{
            const bool rolledBack=journal.recover();
            if(!rolledBack)throw std::runtime_error("Commit outcome uncertain; reopen project before further edits");
        }
        catch(const std::exception& e){throw std::runtime_error(reason+"; project document recovery incomplete: "+e.what());}
        throw std::runtime_error(reason);
    }
}
} // namespace aiverse::editor

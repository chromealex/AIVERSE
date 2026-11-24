#include "ProjectDocumentJournal.h"
namespace aiverse::editor {
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

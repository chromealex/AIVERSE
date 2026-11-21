#include "HumanCreatorSession.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace aiverse::vivid {
HumanCreatorSession::HumanCreatorSession(HumanDNA dna):dna_(std::move(dna)) {
    dna_.validate();
}
void HumanCreatorSession::commit(HumanDNA next) {
    if(undo_.size()>=kHistoryLimit)undo_.erase(undo_.begin());
    undo_.push_back(std::move(dna_));
    dna_=std::move(next);
    redo_.clear();
    ++revision_;dirty_=true;
}
bool HumanCreatorSession::edit(const std::function<void(HumanDNA&)>& operation,
                               std::string* error) {
    try {
        HumanDNA next=dna_;
        operation(next);
        next.validate();
        if(next.toJson()==dna_.toJson())return true;
        commit(std::move(next));
        if(error)error->clear();
        return true;
    }catch(const std::exception& e) {
        if(error)*error=e.what();
        return false;
    }
}
bool HumanCreatorSession::loadJson(const Json& document,std::string* error) {
    return edit([&](HumanDNA& dna){dna=HumanDNA::fromJson(document);},error);
}
bool HumanCreatorSession::preset(const std::string& id,std::string* error) {
    return edit([&](HumanDNA& dna){dna=HumanDNA::preset(id);},error);
}
bool HumanCreatorSession::undo() {
    if(undo_.empty())return false;
    if(redo_.size()>=kHistoryLimit)redo_.erase(redo_.begin());
    redo_.push_back(std::move(dna_));
    dna_=std::move(undo_.back());
    undo_.pop_back();
    ++revision_;dirty_=true;
    return true;
}
bool HumanCreatorSession::redo() {
    if(redo_.empty())return false;
    if(undo_.size()>=kHistoryLimit)undo_.erase(undo_.begin());
    undo_.push_back(std::move(dna_));
    dna_=std::move(redo_.back());
    redo_.pop_back();
    ++revision_;dirty_=true;
    return true;
}
bool HumanCreatorSession::loadFile(const std::string& path,std::string* error) {
    try {
        std::ifstream in(path,std::ios::binary);
        if(!in)throw std::runtime_error("VIVID creator: cannot open DNA file");
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if(content.size()>1024*1024)
            throw std::runtime_error("VIVID creator: DNA file exceeds 1 MiB");
        const bool result=loadJson(Json::parse(content),error);
        if(result)dirty_=false;
        return result;
    }catch(const std::exception& e) {
        if(error)*error=e.what();
        return false;
    }
}
bool HumanCreatorSession::saveFile(const std::string& path,std::string* error) {
    try {
        if(path.empty())throw std::invalid_argument("VIVID creator: empty save path");
        const auto data=dna_.toJson().dump(2);
        const std::filesystem::path destination(path);
        const auto staging=std::filesystem::path(path+".vivid.tmp");
        {
            std::ofstream out(staging,std::ios::binary|std::ios::trunc);
            if(!out)throw std::runtime_error("VIVID creator: unable to open temporary file");
            out.write(data.data(),std::streamsize(data.size()));
            out.flush();
            if(!out)throw std::runtime_error("VIVID creator: save write failed");
        }
        // POSIX rename atomically replaces the old recipe on macOS/Linux.
        // Never delete the old target as a fallback: on a platform that
        // forbids overwrite-by-rename, report the error and leave both the
        // last-good target and the staging file available for recovery.
        std::error_code ec;
        std::filesystem::rename(staging,destination,ec);
        if(ec)
            throw std::runtime_error(
                "VIVID creator: atomic recipe replacement failed: "+ec.message());
        dirty_=false;
        if(error)error->clear();
        return true;
    }catch(const std::exception& e) {
        if(error)*error=e.what();
        return false;
    }
}
} // namespace aiverse::vivid

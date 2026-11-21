// Editor-facing VIVID DNA transaction/session. Pure C++20, undoable,
// versioned, no dependency on a particular UI toolkit or game genre.
#pragma once
#include "HumanDNA.h"
#include <functional>
#include <string>
#include <vector>

namespace aiverse::vivid {
class HumanCreatorSession {
public:
    explicit HumanCreatorSession(HumanDNA dna=HumanDNA{});
    const HumanDNA& dna() const noexcept {return dna_;}
    std::size_t revision() const noexcept {return revision_;}
    bool dirty() const noexcept {return dirty_;}
    bool canUndo() const noexcept {return !undo_.empty();}
    bool canRedo() const noexcept {return !redo_.empty();}
    // Returns false without changing the session on any invalid edit.
    bool edit(const std::function<void(HumanDNA&)>& change,std::string* error=nullptr);
    bool loadJson(const Json& document,std::string* error=nullptr);
    bool loadFile(const std::string& path,std::string* error=nullptr);
    bool saveFile(const std::string& path,std::string* error=nullptr);
    bool preset(const std::string& id,std::string* error=nullptr);
    bool undo();
    bool redo();
private:
    void commit(HumanDNA next);
    static constexpr std::size_t kHistoryLimit=128;
    HumanDNA dna_;
    std::vector<HumanDNA> undo_,redo_;
    std::size_t revision_=0;
    bool dirty_=false;
};
} // namespace aiverse::vivid

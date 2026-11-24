#pragma once
// Shared editor-side graph asset transaction. All cooperating writers must
// acquire the same lock before comparing their original document and replacing
// the file. This is not an external-editor or network-filesystem guarantee.
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include "Workspace.h"

namespace aiverse::editor::graph_asset {
namespace fs = std::filesystem;

// Web clients can reorder object members when returning an original document.
inline bool sameDocument(const Json& original, const Json& expected) {
    if (original.isObject() && expected.isObject()) {
        if (original.size() != expected.size()) return false;
        std::map<std::string_view, const Json*> fields;
        for (const auto& [key, value] : expected.members())
            if (!fields.emplace(key, &value).second) return false;
        for (const auto& [key, value] : original.members()) {
            const auto it = fields.find(key);
            if (it == fields.end() || !sameDocument(value, *it->second)) return false;
            fields.erase(it);
        }
        return true;
    }
    if (original.isArray() && expected.isArray()) {
        if (original.size() != expected.size()) return false;
        for (size_t i = 0; i < original.size(); ++i)
            if (!sameDocument(original[i], expected[i])) return false;
        return true;
    }
    return original == expected;
}

class Transaction {
public:
    explicit Transaction(const fs::path& path, const fs::path& projectRoot) : target_(fs::weakly_canonical(path)) {
        const auto relative=target_.lexically_relative(fs::weakly_canonical(projectRoot));
        if(projectRoot.empty() || relative.empty() || relative=="." || *relative.begin()==".." || fs::is_symlink(fs::symlink_status(path)))
            throw std::runtime_error("Graph write path is outside the project or is a symlink");
        const auto status = fs::symlink_status(target_);
        if (fs::exists(status) && !fs::is_regular_file(status))
            throw std::runtime_error("Graph write target must be a regular file");
        fs::create_directories(target_.parent_path());
        lock_ = target_; lock_ += ".aiverse-write-lock";
        std::error_code ec;
        if (!fs::create_directory(lock_, ec))
            throw std::runtime_error("Graph write lock unavailable; retry after the other writer finishes (or inspect a stale lock): " + target_.filename().string());
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction() {
        std::error_code ec;
        fs::remove(lock_ / "candidate", ec);
        fs::remove(lock_, ec);
    }
    const fs::path& target() const { return target_; }
    // A missing expected document means create-only, never blind overwrite.
    template<class Load>
    bool checkExpected(const Json& request, Load&& load) const {
        const Json& expected = request["expected"];
        if (!expected.isNull() && !expected.isObject())
            throw std::runtime_error("expected must be the original graph object or null for create");
        const bool exists = fs::exists(target_);
        if (exists) {
            if (!expected.isObject() || !sameDocument(load(), expected))
                throw std::runtime_error("Graph changed or already exists; reload before saving");
        } else if (expected.isObject()) {
            throw std::runtime_error("Graph was removed; reload before saving");
        }
        return exists;
    }
    void replace(const std::string& text, size_t maxBytes) const {
        if (text.size() > maxBytes) throw std::runtime_error("Graph exceeds asset byte limit");
        const auto temp = lock_ / "candidate";
        { std::ofstream out(temp, std::ios::binary | std::ios::trunc);
          out.write(text.data(), static_cast<std::streamsize>(text.size()));
          out.close(); if (!out) throw std::runtime_error("Cannot write graph candidate"); }
        // Same filesystem; the original remains untouched on write/rename failure.
        fs::rename(temp, target_);
    }
private:
    fs::path target_, lock_;
};
} // namespace aiverse::editor::graph_asset

// Crash-recovery journal for internal Studio content moves.
// It lives outside content/, so it is never indexed as a user asset.
// A completed transaction uses a committed marker; uncommitted operations
// are reversed on the next project open. No browser-supplied paths are used.
#pragma once
#include "apps/studio-native/StudioContentDirectory.h"
#include "engine/data/Json.h"
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
#if defined(__unix__) || defined(__APPLE__)
        const int descriptor=::open(path.c_str(),O_RDONLY|O_NOFOLLOW);
        if (descriptor<0)
            throw std::runtime_error("Cannot open Studio journal sync path: "+path.string());
        int code=0;
#if defined(__APPLE__)
        // F_FULLFSYNC is stronger than fsync on filesystems that implement it.
        // Some filesystems do not implement it; ordinary fsync is the fallback.
        code=::fcntl(descriptor,F_FULLFSYNC);
        if (code!=0) code=::fsync(descriptor);
#else
        code=::fsync(descriptor);
#endif
        const int failure=code!=0?errno:0;
        ::close(descriptor);
        if (failure)
            throw std::runtime_error("Cannot synchronize Studio journal path: "+
                path.string()+" ("+std::strerror(failure)+")");
#else
        (void)path;
        throw std::runtime_error("Studio durable journal requires a POSIX filesystem");
#endif
    }

    void begin(const std::vector<StudioMoveJournalOperation>& operations) {
        if (operations.empty()) return;
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
            const bool existed=fs::exists(materialStatus);
            if (existed) {
                if (!fs::is_regular_file(materialStatus))
                    throw std::runtime_error("Invalid material library before move");
                fs::copy_file(material,folder_/"materials.backup",fs::copy_options::none);
                syncPath(folder_/"materials.backup");
            }
            Json state=Json::object();
            state.set("version",Json::number(int64_t(1)));
            state.set("materialExisted",Json::fromBool(existed));
            Json rows=Json::array();
            for (const auto& operation:operations) {
                validateRelative(operation.from);
                validateRelative(operation.to);
                Json row=Json::object();
                row.set("from",Json::string(operation.from));
                row.set("to",Json::string(operation.to));
                rows.push(std::move(row));
            }
            state.set("operations",std::move(rows));
            // Manifest is published atomically before the first asset rename.
            writeFile(folder_/"pending.tmp",state.dump(2)+"\n");
            fs::rename(folder_/"pending.tmp",folder_/"pending.json");
            syncPath(folder_);
        } catch (...) {
            std::error_code ec;
            fs::remove_all(folder_,ec);
            throw;
        }
    }

    void commit() {
        if (!fs::exists(folder_/"pending.json")) return;
        // Durable commit marker precedes best-effort cleanup. Recovery must
        // never reverse a committed transaction.
        const fs::path material=project_/"materials.json";
        if (fs::exists(material)) syncPath(material);
        syncPath(project_);
        fs::rename(folder_/"pending.json",folder_/"committed.json");
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
        const Json state=Json::parseFile(pending.string());
        if (state["version"].integer()!=1 || !state["operations"].isArray() ||
            state["operations"].size()>128)
            throw std::runtime_error("Invalid Studio move recovery manifest");
        const fs::path root=studioContentRoot(project_);
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
        // Reverse order handles file + .meta pairs. Each replay is idempotent
        // when a crash happens midway through recovery itself.
        for (auto it=steps.rbegin();it!=steps.rend();++it) {
            if (!it->moveBack) continue;
            fs::rename(it->destination,it->source);
            syncPath(it->source.parent_path());
            if (it->source.parent_path()!=it->destination.parent_path())
                syncPath(it->destination.parent_path());
        }
        const fs::path material=project_/"materials.json";
        if (state.getBool("materialExisted")) {
            const fs::path backup=folder_/"materials.backup";
            if (!fs::is_regular_file(backup) || fs::is_symlink(fs::symlink_status(backup)))
                throw std::runtime_error("Missing Studio material recovery backup");
            const fs::path staged=folder_/"materials.restore";
            fs::copy_file(backup,staged,fs::copy_options::overwrite_existing);
            fs::rename(staged,material);
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

private:
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

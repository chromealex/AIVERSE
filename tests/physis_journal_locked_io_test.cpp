// PHYSIS: cross-module native journal lock + pinned directory I/O contract.
// Deliberately builds without ECS, networking, editor, or Box3D.
#include "engine/physis/PhysisJournalProcessLock.h"
#include "engine/physis/PhysisJournalPosixIO.h"
#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace io = aiverse::physis::remote::posix_journal;
using aiverse::physis::remote::JournalProcessLock;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("PHYSIS LOCKED IO FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

int main() {
    std::string pattern = (fs::temp_directory_path()/"physis-locked-XXXXXX").string();
    char* rootName = ::mkdtemp(pattern.data());
    if (!rootName) return 2;
    const fs::path root = rootName;
    const fs::path journal = root/"receipts";
    const fs::path lockPath = root/"receipts.lock";
    const fs::path oldLock = root/"old-receipts.lock";
    const std::vector<std::uint8_t> firstBytes{1,3,5,7,9};
    const std::vector<std::uint8_t> secondBytes{2,4,6,8};
    std::string error;
    {
        JournalProcessLock owner;
        CHECK(owner.acquire(journal, &error));
        CHECK(owner.held() && owner.valid());
        const int dir = owner.directoryFd();
        CHECK(dir >= 0);
        if (dir >= 0) {
            CHECK(io::write(dir, "receipts", firstBytes, "initial", &error));
            bool missing = true;
            std::vector<std::uint8_t> loaded;
            CHECK(io::read(dir, "receipts", 1, 1024, loaded, missing, &error));
            CHECK(!missing && loaded == firstBytes);
            JournalProcessLock competitor;
            CHECK(!competitor.acquire(journal, &error));
            CHECK(!competitor.held());

            // Same-UID pathname replacement cannot steal the original
            // flock, but must invalidate the original owner's published ID.
            fs::rename(lockPath, oldLock);
            CHECK(!owner.valid());
            CHECK(owner.directoryFd() == -1);
            CHECK(fs::exists(journal));
            CHECK(fs::file_size(journal) == firstBytes.size());

            // A new owner can lock the NEW inode: this proves why the
            // canonical endpoint must check valid() before every write.
            JournalProcessLock replacement;
            CHECK(replacement.acquire(journal, &error));
            CHECK(replacement.valid());
            const int newDir = replacement.directoryFd();
            CHECK(newDir >= 0);
            if (newDir >= 0) {
                CHECK(io::write(newDir, "receipts", secondBytes, "replacement", &error));
                loaded.clear();
                CHECK(io::read(newDir, "receipts", 1, 1024, loaded, missing, &error));
                CHECK(loaded == secondBytes);
            }
            owner.release();
            CHECK(!owner.held());
            CHECK(replacement.valid());
            replacement.release();
        }
    }
    {
        JournalProcessLock restarted;
        CHECK(restarted.acquire(journal, &error));
        CHECK(restarted.directoryFd() >= 0);
        CHECK(restarted.valid());
    }
    fs::remove_all(root);
    std::printf("PHYSIS locked journal I/O: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
#else
int main() { return 0; }
#endif

// Native POSIX persistence smoke without ECS, networking or editor linkage.
#include "engine/physis/PhysisJournalPosixIO.h"
#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs=std::filesystem;
namespace io=aiverse::physis::remote::posix_journal;
static int failures=0;
#define CHECK(expr) do {if(!(expr)) {std::printf("PHYSIS POSIX JOURNAL FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;}} while(0)

int main() {
    std::string pattern=(fs::temp_directory_path()/"physis-posix-XXXXXX").string();
    char* rootName=::mkdtemp(pattern.data());
    if(!rootName)return 2;
    const fs::path root=rootName, parent=root/"journal-dir", moved=root/"moved";
    fs::create_directory(parent);
    CHECK(::chmod(parent.c_str(),0700)==0);
    int flags=O_RDONLY;
#ifdef O_DIRECTORY
    flags|=O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags|=O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    const int dir=::open(parent.c_str(),flags);
    if(dir<0) {fs::remove_all(root);return 2;}
    std::string reason;
    std::vector<std::uint8_t> data{9,1,4,7,2,0,8,3};
    std::vector<std::uint8_t> readback{77};
    bool missing=false;
    CHECK(io::validBasename("receipts"));
    CHECK(!io::validBasename("../escape"));
    CHECK(!io::validBasename("."));
    const std::string invalidName=std::string("receipts")+
        char(0)+"other";
    const std::string invalidSuffix=std::string("nonce")+
        char(0)+"other";
    CHECK(!io::validBasename(invalidName));
    CHECK(!io::read(dir,invalidName,1,1024,readback,missing,&reason));
    CHECK(!io::write(dir,"receipts",data,invalidSuffix,&reason));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(missing && readback==std::vector<std::uint8_t>{77});
    // A crash before rename may leave a temp file. Never treat the
    // incomplete intent as the canonical receipt journal.
    const fs::path orphan=parent/"receipts.next-abandoned";
    const int orphanFd=::open(orphan.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    CHECK(orphanFd>=0);
    if(orphanFd>=0) {
        const char malformed[]="incomplete-intent";
        CHECK(::write(orphanFd,malformed,sizeof(malformed))==sizeof(malformed));
        CHECK(::close(orphanFd)==0);
    }
    CHECK(!io::write(dir,"receipts",{},"empty-payload",&reason));
    CHECK(!fs::exists(parent/"receipts.next-empty-payload"));
    // The low-level helper is public: enforce the 16 MiB FileJournal
    // cap even if a caller bypasses canonical V1 serialization.
    const std::vector<std::uint8_t> oversized(io::kMaxJournalBytes+1,0x5a);
    CHECK(!io::write(dir,"receipts",oversized,"oversized",&reason));
    CHECK(!fs::exists(parent/"receipts.next-oversized"));
    readback={77};
    CHECK(!io::read(dir,"receipts",1,io::kMaxJournalBytes+1,
                    readback,missing,&reason));
    CHECK(readback==std::vector<std::uint8_t>{77});
    CHECK(!io::read(dir,"receipts",11,10,readback,missing,&reason));
    CHECK(readback==std::vector<std::uint8_t>{77});
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(missing);
    CHECK(io::write(dir,"receipts",data,"nonce-a",&reason));
    CHECK(fs::exists(orphan));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(!missing && readback==data);
    CHECK(fs::remove(orphan));
    CHECK(::chmod((parent/"receipts").c_str(),0644)==0);
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(::chmod((parent/"receipts").c_str(),0600)==0);
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    readback={77};
    CHECK(!io::read(dir,"receipts",9,1024,readback,missing,&reason));
    CHECK(readback==std::vector<std::uint8_t>{77});
    CHECK(!io::write(dir,"../wrong",data,"nonce-a",&reason));
    CHECK(!io::write(dir,"receipts",data,"../wrong",&reason));
    CHECK(fs::remove(parent/"receipts"));
    CHECK(::mkfifo((parent/"receipts").c_str(),0600)==0);
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(fs::remove(parent/"receipts"));
    const fs::path target=root/"somewhere-else";
    CHECK(::symlink(target.c_str(),(parent/"receipts").c_str())==0);
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    // Atomic rename replaces the symlink entry; never follow it to
    // create or modify the outside target.
    CHECK(io::write(dir,"receipts",data,"nonce-link",&reason));
    CHECK(!fs::exists(target));
    CHECK(!fs::is_symlink(parent/"receipts"));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    const fs::path otherLink=root/"hardlink";
    CHECK(::link((parent/"receipts").c_str(),otherLink.c_str())==0);
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(fs::remove(otherLink));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    // O_EXCL must reject a colliding random temp basename without
    // clobbering either the existing receipt file or that temp file.
    const fs::path occupied=parent/"receipts.next-collision";
    const int occupiedFd=::open(occupied.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    CHECK(occupiedFd>=0);
    if(occupiedFd>=0)CHECK(::close(occupiedFd)==0);
    CHECK(!io::write(dir,"receipts",data,"collision",&reason));
    CHECK(fs::exists(occupied));
    CHECK(fs::file_size(occupied)==0);
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(readback==data);
    CHECK(fs::remove(occupied));
    CHECK(::chmod(parent.c_str(),0777)==0);
    readback={77};
    CHECK(!io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(readback==std::vector<std::uint8_t>{77});
    CHECK(!io::write(dir,"receipts",data,"nonce-b",&reason));
    CHECK(::chmod(parent.c_str(),0700)==0);
    struct stat priorReceipt{}, publishedReceipt{};
    CHECK(::fstatat(dir,"receipts",&priorReceipt,AT_SYMLINK_NOFOLLOW)==0);
    CHECK(io::write(dir,"receipts",data,"nonce-c",&reason));
    CHECK(::fstatat(dir,"receipts",&publishedReceipt,AT_SYMLINK_NOFOLLOW)==0);
    CHECK(S_ISREG(publishedReceipt.st_mode));
    CHECK(publishedReceipt.st_dev==priorReceipt.st_dev);
    CHECK(publishedReceipt.st_ino!=priorReceipt.st_ino);
    CHECK(publishedReceipt.st_nlink==1);
    CHECK(publishedReceipt.st_uid==::geteuid());
    CHECK((publishedReceipt.st_mode & 0077)==0);
    CHECK(!fs::exists(parent/"receipts.next-nonce-c"));
    fs::rename(parent,moved);
    fs::create_directory(parent);
    CHECK(::chmod(parent.c_str(),0700)==0);
    CHECK(io::write(dir,"receipts",data,"nonce-d",&reason));
    CHECK(fs::exists(moved/"receipts"));
    CHECK(!fs::exists(parent/"receipts"));
    CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
    CHECK(readback==data);
#ifdef O_PATH
    // Linux fault injection: O_PATH can resolve openat()/renameat()
    // and pass fstat(), but cannot fsync() its directory descriptor.
    // The atomic rename may already have happened; save must return
    // false rather than positively acknowledging crash durability.
    const int noSync=::open(moved.c_str(),O_PATH|O_DIRECTORY);
    CHECK(noSync>=0);
    if(noSync>=0) {
        const std::vector<std::uint8_t> pending{7,6,5,4,3,2,1,0};
        CHECK(!io::write(noSync,"receipts",pending,"no-fsync",&reason));
        CHECK(reason.find("fsync")!=std::string::npos);
        CHECK(io::read(dir,"receipts",1,1024,readback,missing,&reason));
        CHECK(readback==pending); // visible != confirmed crash-durable
        CHECK(::close(noSync)==0);
    }
#endif
    CHECK(::close(dir)==0);
    fs::remove_all(root);
    std::printf("PHYSIS POSIX journal I/O: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
#else
int main(){return 0;}
#endif

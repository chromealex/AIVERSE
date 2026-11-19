// PHYSIS: cross-process crash recovery for the pinned POSIX receipt journal.
// Exercises real production lock and I/O headers, without ECS or mocks.
#include "engine/physis/PhysisJournalProcessLock.h"
#include "engine/physis/PhysisJournalPosixIO.h"
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs=std::filesystem;
namespace io=aiverse::physis::remote::posix_journal;
using aiverse::physis::remote::JournalProcessLock;
static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("PHYSIS JOURNAL CRASH FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while(0)

int main() {
    std::string pattern=(fs::temp_directory_path()/"physis-crash-XXXXXX").string();
    char* created=::mkdtemp(pattern.data());
    if(!created)return 2;
    const fs::path root=created;
    const fs::path journal=root/"receipts";
    const fs::path orphan=root/"receipts.next-uncommitted";
    const std::vector<std::uint8_t> committed{1,3,5,7,9};
    const std::vector<std::uint8_t> staged{2,4,6,8};
    const std::vector<std::uint8_t> resumed{9,8,7,6};
    int ready[2]={-1,-1};
    if(::pipe(ready)!=0) {fs::remove_all(root);return 2;}
    const pid_t child=::fork();
    if(child<0) {
        ::close(ready[0]);::close(ready[1]);
        fs::remove_all(root);return 2;
    }
    if(child==0) {
        ::close(ready[0]);
        JournalProcessLock writer;
        std::string reason;
        bool ok=writer.acquire(journal,&reason);
        const int dir=ok?writer.directoryFd():-1;
        ok=ok && dir>=0 && io::write(dir,"receipts",committed,"durable",&reason);
        // Simulate a crash after temporary-file fsync but BEFORE renameat.
        int tmp=-1;
        if(ok)tmp=::openat(dir,"receipts.next-uncommitted",O_WRONLY|O_CREAT|O_EXCL,0600);
        ok=ok && tmp>=0;
        if(tmp>=0) {
            ok=ok && ::write(tmp,staged.data(),staged.size())==static_cast<ssize_t>(staged.size());
            ok=ok && ::fsync(tmp)==0;
            const bool closed=(::close(tmp)==0);
            ok=ok && closed;
        }
        const char state=ok?'Y':'N';
        (void)::write(ready[1],&state,1);
        ::close(ready[1]);
        if(!ok)::_exit(42);
        for(;;)::pause(); // parent kills the writer without destructors
    }
    ::close(ready[1]);
    struct pollfd pfd{ready[0],POLLIN,0};
    int polled=-1;
    do {polled=::poll(&pfd,1,10000);} while(polled<0 && errno==EINTR);
    char acquired='N';
    if(polled>0 && (pfd.revents&POLLIN))
        (void)::read(ready[0],&acquired,1);
    ::close(ready[0]);
    CHECK(polled>0 && acquired=='Y');
    if(acquired=='Y') {
        JournalProcessLock competitor;
        std::string why;
        CHECK(!competitor.acquire(journal,&why));
        CHECK(!competitor.held());
    }
    CHECK(::kill(child,SIGKILL)==0);
    int status=0;
    pid_t waited=-1;
    do {waited=::waitpid(child,&status,0);} while(waited<0 && errno==EINTR);
    CHECK(waited==child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    if(acquired=='Y') {
        CHECK(fs::exists(orphan));
        JournalProcessLock recovered;
        std::string reason;
        CHECK(recovered.acquire(journal,&reason));
        CHECK(recovered.valid());
        const int dir=recovered.directoryFd();
        CHECK(dir>=0);
        if(dir>=0) {
            bool missing=true;
            std::vector<std::uint8_t> bytes;
            CHECK(io::read(dir,"receipts",1,1024,bytes,missing,&reason));
            CHECK(!missing && bytes==committed); // never replay orphan
            CHECK(io::write(dir,"receipts",resumed,"resumed",&reason));
            CHECK(fs::exists(orphan)); // recovery does not erase evidence
            CHECK(io::read(dir,"receipts",1,1024,bytes,missing,&reason));
            CHECK(!missing && bytes==resumed);
        }
    }
    fs::remove_all(root);
    std::printf("PHYSIS journal cross-process crash: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
#else
int main(){return 0;}
#endif

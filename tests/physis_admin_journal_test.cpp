#include "engine/physis/PhysisAdminEndpoint.h"
#include "engine/physis/PhysisRemoteClient.h"
#include "engine/net/Conn.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace aiverse;
using namespace aiverse::physis;
using namespace aiverse::physis::remote;
using namespace aiverse::physis::remote::admin;
namespace {
int failed=0;
#define CHECK(x) do{if(!(x)){std::printf("PHYSIS ADMIN FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failed;}}while(0)
constexpr std::uint16_t kEdit=53012;
net::Key key() {
    net::Key result{};
    for(std::size_t i=0;i<result.size();++i)
        result[i]=std::uint8_t(i*7+3);
    return result;
}
Json map() {
    return Json::parse(R"({"physis":{
      "enabled":true,
      "grid":{"originX":0,"originZ":0,"width":2,"height":2,"cellSizeMeters":100,"seed":8},
      "climate":{"baseTemperature":16,"baseHumidity":0.5},
      "enableWindZones":true,"enableStorms":true,"enableGravityFields":true,
      "windZones":[{"id":"wind-one","center":[0,0,0],"radiusMeters":70,"velocity":[1,0,0]}]
    }})");
}
class Host final:public GameModule {
public:
    std::string name()const override{return "physis-admin-journal-test";}
    void setup(Simulation& sim,const Json&,const Json& m)override{
        if(!installEditableFromMap(sim,m))throw std::runtime_error("PHYSIS install");
    }
    void applyCommand(Simulation& sim,const Command& cmd)override{
        if(cmd.type==kEdit)(void)applyFieldEditCommand(sim,cmd,kEdit);
    }
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
std::unique_ptr<Simulation> make() {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>());
    sim->setup(Json::object(),map());
    return sim;
}
FieldEditBatch desired(int speed=15,std::uint64_t revision=0) {
    const auto j=map();
    auto assets=parseFieldAssets(j["physis"],*optionsFromMap(j));
    auto wind=std::get<WindZone>(assets.front().data);
    wind.velocity.x=Fixed::fromInt(speed);
    assets.front().data=wind;
    return {revision,std::move(assets)};
}
Packet packet(int speed=15,std::uint64_t instance=144,
              std::uint64_t request=1) {
    Packet p;
    p.kind=Message::Submit;
    p.clientInstance=instance;p.requestId=request;
    if(!encodeFieldEditCommand(desired(speed),kEdit,p.command))
        throw std::runtime_error("cannot encode test PHYSIS command");
    p.command.player=4;
    return p;
}
bool reader(const std::string& user){return user=="admin";}
bool writer(const std::string& user,const Command& cmd){
    return user=="admin" && cmd.player==4 && cmd.type==kEdit;
}
std::filesystem::path tempPath() {
    net::Key random=net::randomKey();
    const auto label=net::toHexString(random.data(),8);
    return std::filesystem::temp_directory_path()/("aiverse-physis26-"+label);
}

void codecAndCorruption() {
    ServerGateway g;
    auto sim=make();
    auto [client,server]=net::makeLoopbackPair();
    const auto p=packet();
    net::Bytes b;
    CHECK(encode(p,b));
    client->send(b);
    g.poll(*sim,*server,"admin",reader,writer,1);
    CHECK(g.entries().size()==1);
    std::map<ServerGateway::Key,ServerGateway::Entry> restored;
    net::Bytes snapshot;
    std::string error;
    CHECK(FileJournal::encodeEntries(g.entries(),snapshot,&error));
    CHECK(FileJournal::decodeEntries(snapshot,restored,&error));
    CHECK(restored.size()==1);
    auto bad=snapshot;
    bad.back()^=1;
    CHECK(!FileJournal::decodeEntries(bad,restored,&error));
    CHECK(restored.size()==1); // unchanged on error
    bad=snapshot;
    bad.pop_back();
    CHECK(!FileJournal::decodeEntries(bad,restored,&error));
}

void journalWriteFailureNeverAcknowledgesDurableSuccess() {
    // Fail either the Queued receipt write or the post-tick Applied write.
    // ECS may still commit, but a client must get Unknown once the durable
    // receipt journal has become unavailable.
    for(const int failOnWrite : {2,3}) {
        auto sim=make();
        ServerGateway gateway;
        int writes=0;
        gateway.setJournalWriter([&](const auto&,std::string* error) {
            if(++writes>=failOnWrite) {
                if(error)*error="PHYSIS injected receipt journal IO failure";
                return false;
            }
            return true;
        });
        auto [client,server]=net::makeLoopbackPair();
        const Packet submission=packet(39,730+failOnWrite,40);
        net::Bytes frame;
        CHECK(encode(submission,frame));
        client->send(frame);
        gateway.poll(*sim,*server,"admin",reader,writer,1);
        CHECK(gateway.entries().size()==1);
        CHECK(gateway.journalHealthy()==(failOnWrite==3));
        client->update(1);
        net::Bytes responseBytes;
        Packet response;
        CHECK(client->recv(responseBytes));
        CHECK(decode(responseBytes,response));
        CHECK(response.receipt.result==
              (failOnWrite==2 ? Result::Unknown : Result::Queued));
        for(int i=0;i<4;++i)sim->step();
        gateway.observe(*sim);
        CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
        CHECK(!gateway.journalHealthy());

        Packet question;
        question.kind=Message::Query;
        question.clientInstance=submission.clientInstance;
        question.requestId=submission.requestId;
        CHECK(encode(question,frame));
        client->send(frame);
        gateway.poll(*sim,*server,"admin",reader,writer,100);
        client->update(100);
        CHECK(client->recv(responseBytes));
        CHECK(decode(responseBytes,response));
        CHECK(response.receipt.result==Result::Unknown);
        CHECK(response.receipt.scheduledTick==0);

        // An unknown ID must not be advertised as safe to retry while
        // the journal is unhealthy (even if the ECS state is readable).
        question.requestId=submission.requestId+100;
        CHECK(encode(question,frame));
        client->send(frame);
        gateway.poll(*sim,*server,"admin",reader,writer,100);
        client->update(100);
        CHECK(client->recv(responseBytes));
        CHECK(decode(responseBytes,response));
        CHECK(response.receipt.result==Result::Unknown);
        CHECK(response.receipt.detail.find("retry disabled")!=std::string::npos ||
              response.receipt.detail.find("retries suspended")!=std::string::npos);

        // Replaying the exact bytes must not reveal an unpersisted Applied.
        CHECK(encode(submission,frame));
        client->send(frame);
        gateway.poll(*sim,*server,"admin",reader,writer,101);
        client->update(101);
        CHECK(client->recv(responseBytes));
        CHECK(decode(responseBytes,response));
        CHECK(response.receipt.result==Result::Unknown);

        // Fresh writes are suspended until a server restart/reconciliation.
        CHECK(encode(packet(50,submission.clientInstance,41),frame));
        client->send(frame);
        gateway.poll(*sim,*server,"admin",reader,writer,102);
        client->update(102);
        CHECK(client->recv(responseBytes));
        CHECK(decode(responseBytes,response));
        CHECK(response.receipt.result==Result::Rejected);
        CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    }
}

void durableIntentAndReload() {
    auto sim=make();
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journalPath=dir/"physis.receipts";
    {
        ServerGateway gateway;
        FileJournal file(journalPath);
        std::string error;
        CHECK(file.attach(gateway,&error));
        auto [client,server]=net::makeLoopbackPair();
        net::Bytes bytes;CHECK(encode(packet(),bytes));
        client->send(bytes);
        gateway.poll(*sim,*server,"admin",reader,writer,1);
        CHECK(gateway.entries().size()==1);
        CHECK(std::filesystem::exists(journalPath));
        CHECK(std::filesystem::file_size(journalPath)>18);
        const auto status=gateway.entries().begin()->second.receipt.result;
        CHECK(status==Result::Queued);
        for(int t=0;t<4;++t)sim->step();
        gateway.observe(*sim);
        CHECK(gateway.entries().begin()->second.receipt.result==Result::Applied);
    }
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    const auto checkpoint=sim->serialize();
    auto recovered=make();
    std::string error;
    CHECK(recovered->deserialize(checkpoint,&error));
    {
        ServerGateway gateway;
        FileJournal file(journalPath);
        CHECK(file.attach(gateway,&error));
        CHECK(gateway.entries().size()==1);
        gateway.observe(*recovered);
        auto [client,server]=net::makeLoopbackPair();
        net::Bytes bytes;CHECK(encode(packet(),bytes));
        client->send(bytes);
        gateway.poll(*recovered,*server,"admin",reader,writer,100);
        CHECK(gateway.entries().size()==1);
        CHECK(recovered->world().resourceConst<FieldRuntimeState>()->revision==1);
        net::Bytes reply;
        Packet response;
        client->update(100);
        CHECK(client->recv(reply));
        CHECK(decode(reply,response));
        CHECK(response.receipt.result==Result::Applied);
    }
    std::filesystem::remove_all(dir);
}
void missingJournalFailsClosed() {
    auto sim=make();
    const auto dir=tempPath();
    const auto p=dir/"missing"/"record";
    ServerGateway gateway;
    FileJournal journal(p);
    std::string error;
    CHECK(journal.attach(gateway,&error));
    auto [client,server]=net::makeLoopbackPair();
    net::Bytes bytes;CHECK(encode(packet(),bytes));
    client->send(bytes);
    gateway.poll(*sim,*server,"admin",reader,writer,1);
    CHECK(!gateway.journalHealthy());
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==0);
    for(int i=0;i<4;++i)sim->step();
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==0);
}

void queuedReceiptBeforeCrash() {
    // The sidecar has a durable Queued receipt, but a crashed process lost
    // the in-memory Simulation.pending command. The same original request
    // must be retryable after World state is restored.
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto filePath=dir/"journal";
    const auto original=packet(23,101,11);
    net::Bytes bytes;
    CHECK(encode(original,bytes));
    {
        auto preCrash=make();
        ServerGateway server;
        FileJournal file(filePath);
        std::string error;
        CHECK(file.attach(server,&error));
        auto [client,conn]=net::makeLoopbackPair();
        client->send(bytes);
        server.poll(*preCrash,*conn,"admin",reader,writer,5);
        CHECK(server.entries().size()==1);
        CHECK(server.entries().begin()->second.receipt.result==Result::Queued);
        CHECK(preCrash->world().resourceConst<FieldRuntimeState>()->revision==0);
        // No Simulation::step: client and old server die with a pending
        // command; the journal has already been fsynced.
    }
    {
        auto recovered=make();
        ServerGateway server;
        FileJournal file(filePath);
        std::string error;
        CHECK(file.attach(server,&error));
        CHECK(server.entries().size()==1);
        CHECK(server.entries().begin()->second.receipt.result==Result::Unknown);
        server.observe(*recovered);
        CHECK(server.entries().begin()->second.receipt.result==Result::Unknown);
        // Query returns Unknown, not the stale Queued receipt from before
        // the crash. The editor then retransmits its ORIGINAL packet.
        auto [client,conn]=net::makeLoopbackPair();
        Packet query;
        query.kind=Message::Query;
        query.clientInstance=101;
        query.requestId=11;
        net::Bytes question,replyBytes;
        CHECK(encode(query,question));
        client->send(question);
        server.poll(*recovered,*conn,"admin",reader,writer,100);
        client->update(100);
        Packet answer;
        CHECK(client->recv(replyBytes));
        CHECK(decode(replyBytes,answer));
        CHECK(answer.receipt.result==Result::Unknown);
        client->send(bytes);
        server.poll(*recovered,*conn,"admin",reader,writer,101);
        CHECK(server.entries().begin()->second.receipt.result==Result::Queued);
        for(int i=0;i<4;++i)recovered->step();
        server.observe(*recovered);
        CHECK(server.entries().begin()->second.receipt.result==Result::Applied);
        CHECK(recovered->world().resourceConst<FieldRuntimeState>()->revision==1);
    }
    std::filesystem::remove_all(dir);
}

void queuedReceiptAlreadyAppliedBeforeCrash() {
    // Crash after World commit but before post-tick gateway.observe() has
    // persisted Applied: on restart the saved World proves the outcome.
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto filePath=dir/"journal";
    auto preCrash=make();
    {
        ServerGateway server;
        FileJournal file(filePath);
        std::string error;
        CHECK(file.attach(server,&error));
        auto [client,conn]=net::makeLoopbackPair();
        net::Bytes bytes;
        CHECK(encode(packet(27,102,12),bytes));
        client->send(bytes);
        server.poll(*preCrash,*conn,"admin",reader,writer,5);
        CHECK(server.entries().begin()->second.receipt.result==Result::Queued);
        for(int i=0;i<4;++i)preCrash->step();
        CHECK(preCrash->world().resourceConst<FieldRuntimeState>()->revision==1);
        // Intentionally omit server.observe(): journal remains Queued.
    }
    const auto checkpoint=preCrash->serialize();
    auto recovered=make();
    std::string error;
    CHECK(recovered->deserialize(checkpoint,&error));
    ServerGateway server;
    FileJournal journal(filePath);
    CHECK(journal.attach(server,&error));
    CHECK(server.entries().begin()->second.receipt.result==Result::Unknown);
    server.observe(*recovered);
    CHECK(server.entries().begin()->second.receipt.result==Result::Applied);
    CHECK(server.entries().begin()->second.receipt.revision==1);
    CHECK(recovered->world().resourceConst<FieldRuntimeState>()->revision==1);
    std::filesystem::remove_all(dir);
}

void recoveredIntent() {
    auto sim=make();
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto p=dir/"journal";
    ServerGateway gateway;
    ServerGateway::Key key{"admin",123,6};
    ServerGateway::Entry entry;
    CHECK(encode(packet(19,123,6),entry.originalFrame));
    entry.expectedRevision=0;
    entry.observedTick=0;
    entry.receipt={Result::Unknown,0,0,"PHYSIS durable submission intent"};
    const auto assets=desired(19).desired;
    for(const auto& field:assets)
        entry.desired.emplace(field.id,
            std::make_pair(field.kind,authoredFingerprint(field)));
    std::map<ServerGateway::Key,ServerGateway::Entry> records;
    records.emplace(key,std::move(entry));
    FileJournal journal(p);
    std::string error;
    CHECK(journal.save(records,&error));
    CHECK(journal.attach(gateway,&error));
    auto [client,server]=net::makeLoopbackPair();
    net::Bytes b;CHECK(encode(packet(19,123,6),b));
    client->send(b);
    gateway.poll(*sim,*server,"admin",reader,writer,5);
    CHECK(gateway.entries().size()==1);
    CHECK(gateway.entries().begin()->second.receipt.result==Result::Queued);
    for(int i=0;i<4;++i)sim->step();
    gateway.observe(*sim);
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    std::filesystem::remove_all(dir);
}
void exclusiveJournalOwnership() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto path=dir/"journal";
    std::string error;
    {
        JournalProcessLock first;
        JournalProcessLock second;
        CHECK(first.acquire(path,&error));
        CHECK(first.held());
        CHECK(!first.acquire(path,&error));
        CHECK(!second.acquire(path,&error));
        CHECK(!second.held());
        first.release();
        CHECK(!first.held());
        CHECK(second.acquire(path,&error));
        CHECK(second.held());
        second.release();
        CHECK(first.acquire(path,&error));
    }
    std::filesystem::remove_all(dir);
}
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
// Two genuinely independent OS processes. The child owns a fresh flock fd;
// SIGKILL intentionally skips JournalProcessLock's RAII destructor.
void unsafeJournalLockInodesRejected() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journal=dir/"journal";
    const auto lock=std::filesystem::path(journal.string()+".lock");
    std::string error;
    {
        JournalProcessLock owner;
        CHECK(owner.acquire(journal,&error));
    }
    CHECK(::chmod(lock.c_str(),0666)==0);
    {
        JournalProcessLock unsafe;
        CHECK(!unsafe.acquire(journal,&error));
        CHECK(!unsafe.held());
    }
    CHECK(::chmod(lock.c_str(),0600)==0);
    const auto alias=dir/"alias.lock";
    CHECK(::link(lock.c_str(),alias.c_str())==0);
    {
        JournalProcessLock linked;
        CHECK(!linked.acquire(journal,&error));
        CHECK(!linked.held());
    }
    std::filesystem::remove(alias);
    {
        JournalProcessLock safe;
        CHECK(safe.acquire(journal,&error));
    }
    // A FIFO with no readers must not block the privileged process while
    // JournalProcessLock attempts to open() it. The regular-file check
    // must reject it immediately without acquiring any lock.
    CHECK(std::filesystem::remove(lock));
    if(::mkfifo(lock.c_str(),0600)==0) {
        JournalProcessLock fifo;
        CHECK(!fifo.acquire(journal,&error));
        CHECK(!fifo.held());
        CHECK(std::filesystem::remove(lock));
    } else {
        CHECK(false);
    }
    std::filesystem::remove_all(dir);
}
// The journal must live in a trusted direct parent: creating a secure
// lock file in a group/world-writable or symlinked directory is unsafe.
void unsafeJournalLockParentsRejected() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto privateDir=dir/"private";
    std::filesystem::create_directories(privateDir);
    const auto journal=privateDir/"receipts";
    std::string error;
    CHECK(::chmod(privateDir.c_str(),0777)==0);
    {
        JournalProcessLock unsafe;
        CHECK(!unsafe.acquire(journal,&error));
        CHECK(!unsafe.held());
    }
    CHECK(::chmod(privateDir.c_str(),0700)==0);
    {
        JournalProcessLock owner;
        CHECK(owner.acquire(journal,&error));
        CHECK(owner.held());
    }
    // The basename can be safe while the direct parent is a symlink.
    const auto alias=dir/"alias";
    CHECK(::symlink(privateDir.c_str(),alias.c_str())==0);
    {
        JournalProcessLock unsafe;
        CHECK(!unsafe.acquire(alias/"receipts",&error));
        CHECK(!unsafe.held());
    }
    std::filesystem::remove_all(dir);
}

// flock protects an open inode, not its pathname. A same-UID actor could
// rename the locked file away and create a second one at the original path.
// The live endpoint must detect this before any privileged journal write.
void replacedLiveLockSuspendsAdmin() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journal=dir/"receipts";
    const auto lock=std::filesystem::path(journal.string()+".lock");
    const auto renamed=dir/"old.lock";
    std::string error;
    auto sim=make();
    {
        LoopbackAdminEndpoint first(journal);
        CHECK(first.open(0,kEdit,
            [](const std::string& name)->std::optional<net::Key>{
                return name=="admin"?std::optional<net::Key>(key()):std::nullopt;
            },reader,writer,&error));
        CHECK(first.journalHealthy());
        CHECK(std::filesystem::exists(lock));
        std::filesystem::rename(lock,renamed);
        CHECK(!first.journalHealthy());
        // Another process could lock a newly created inode even while
        // the old endpoint still holds flock on its renamed inode.
        JournalProcessLock replacement;
        CHECK(replacement.acquire(journal,&error));
        CHECK(replacement.valid());
        CHECK(!first.journalHealthy());
        first.afterTick(*sim);
        CHECK(!first.gateway().journalHealthy());
        CHECK(!first.gateway().persistJournal(&error));
        // The admin gateway must reject a fresh edit rather than append
        // to the now-ambiguous durable journal path.
        auto [client,server]=net::makeLoopbackPair();
        net::Bytes frame;
        CHECK(encode(packet(51,943,7),frame));
        client->send(frame);
        first.gateway().poll(*sim,*server,"admin",reader,writer,20);
        client->update(20);
        net::Bytes responseFrame;
        Packet reply;
        CHECK(client->recv(responseFrame));
        CHECK(decode(responseFrame,reply));
        CHECK(reply.receipt.result==Result::Rejected);
        CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==0);
    }
    std::filesystem::remove_all(dir);
}

// Also monitor the parent after admission: chmod can invalidate
// the trust boundary without changing the directory's dev/inode.
void liveParentPermissionChangeSuspendsAdmin() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journal=dir/"receipts";
    std::string error;
    auto sim=make();
    {
        LoopbackAdminEndpoint endpoint(journal);
        CHECK(endpoint.open(0,kEdit,
            [](const std::string& name)->std::optional<net::Key>{
                return name=="admin"?std::optional<net::Key>(key()):std::nullopt;
            },reader,writer,&error));
        CHECK(endpoint.journalHealthy());
        CHECK(::chmod(dir.c_str(),0777)==0);
        CHECK(!endpoint.journalHealthy());
        endpoint.afterTick(*sim);
        CHECK(!endpoint.gateway().journalHealthy());
        CHECK(!endpoint.gateway().persistJournal(&error));
        CHECK(::chmod(dir.c_str(),0700)==0);
        // Fatal journal suspension is sticky even after fixing the mode.
        CHECK(!endpoint.journalHealthy());
    }
    std::filesystem::remove_all(dir);
}
// A verified dirfd pins the actual directory inode. Even if the
// textual parent pathname is replaced before savePinned, the atomic
// rename cannot be redirected into a newly created lookalike folder.
// Production endpoint additionally refuses the operation once valid()
// detects path drift; this direct fixture isolates savePinned's I/O.
void pinnedJournalSaveDoesNotFollowMovedParent() {
    const auto base=tempPath();
    const auto parent=base/"server";
    std::filesystem::create_directories(parent);
    const auto journal=parent/"receipts";
    const auto moved=base/"original";
    std::string error;
    {
        JournalProcessLock owner;
        CHECK(owner.acquire(journal,&error));
        const int anchored=owner.directoryFd();
        CHECK(anchored>=0);
        if(anchored>=0) {
            FileJournal file(journal);
            const std::map<ServerGateway::Key,ServerGateway::Entry> empty;
            CHECK(file.savePinned(anchored,empty,&error));
            CHECK(std::filesystem::exists(journal));
            std::filesystem::rename(parent,moved);
            std::filesystem::create_directory(parent);
            CHECK(!owner.valid());
            CHECK(owner.directoryFd()<0);
            // Direct test of descriptor-relative rename, NOT authorized
            // endpoint behavior. No write may land in the new parent.
            CHECK(file.savePinned(anchored,empty,&error));
            CHECK(std::filesystem::exists(moved/"receipts"));
            CHECK(!std::filesystem::exists(parent/"receipts"));
        }
    }
    std::filesystem::remove_all(base);
}

// On startup, the privileged endpoint must not read a substituted
// symlink, FIFO or publicly-readable journal. Both journal I/O directions
// use the same pinned trusted parent dirfd.
void pinnedJournalLoadRejectsUnsafeInodes() {
    const auto base=tempPath();
    std::filesystem::create_directories(base);
    const auto journal=base/"receipts";
    std::string error;
    {
        JournalProcessLock lock;
        CHECK(lock.acquire(journal,&error));
        const int dir=lock.directoryFd();
        CHECK(dir>=0);
        if(dir>=0) {
            FileJournal file(journal);
            ServerGateway gateway;
            CHECK(file.loadPinned(dir,gateway,&error)); // absent is fresh
            const std::map<ServerGateway::Key,ServerGateway::Entry> empty;
            CHECK(file.savePinned(dir,empty,&error));
            CHECK(file.loadPinned(dir,gateway,&error));
            CHECK(::chmod(journal.c_str(),0644)==0);
            CHECK(!file.loadPinned(dir,gateway,&error));
            CHECK(::chmod(journal.c_str(),0600)==0);
            CHECK(std::filesystem::remove(journal));
            const auto target=base/"other-receipts";
            CHECK(::symlink(target.c_str(),journal.c_str())==0);
            CHECK(!file.loadPinned(dir,gateway,&error));
            CHECK(std::filesystem::remove(journal));
            CHECK(::mkfifo(journal.c_str(),0600)==0);
            CHECK(!file.loadPinned(dir,gateway,&error));
            CHECK(std::filesystem::remove(journal));
            CHECK(file.loadPinned(dir,gateway,&error));
        }
    }
    std::filesystem::remove_all(base);
}

void killedProcessReleasesJournalLock() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journal=dir/"journal";
    int ready[2]={-1,-1};
    if(::pipe(ready)!=0) {
        CHECK(false);
        std::filesystem::remove_all(dir);
        return;
    }
    const pid_t child=::fork();
    if(child<0) {
        CHECK(false);
        ::close(ready[0]);
        ::close(ready[1]);
        std::filesystem::remove_all(dir);
        return;
    }
    if(child==0) {
        ::close(ready[0]);
        JournalProcessLock held;
        std::string reason;
        const char state=held.acquire(journal,&reason)?'Y':'N';
        ssize_t sent=-1;
        do {sent=::write(ready[1],&state,1);} while(sent<0 && errno==EINTR);
        if(sent!=1)::_exit(44);
        ::close(ready[1]);
        if(state!='Y')::_exit(42);
        for(;;)::pause();
    }
    ::close(ready[1]);
    struct pollfd descriptor{ready[0],POLLIN,0};
    int polled=-1;
    do {polled=::poll(&descriptor,1,3000);}
    while(polled<0 && errno==EINTR);
    char acquired='N';
    if(polled>0 && (descriptor.revents&POLLIN))
    {
        ssize_t count=-1;
        do {count=::read(ready[0],&acquired,1);} while(count<0 && errno==EINTR);
        if(count!=1)acquired='N';
    }
    ::close(ready[0]);
    CHECK(polled>0 && acquired=='Y');
    if(acquired=='Y') {
        JournalProcessLock competing;
        std::string reason;
        CHECK(!competing.acquire(journal,&reason));
        CHECK(!competing.held());
    }
    // Crash: no normal release(), no C++ destruction; the OS must drop
    // the independent child's lock when the process is killed.
    (void)::kill(child,SIGKILL);
    int status=0;
    CHECK(::waitpid(child,&status,0)==child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    if(acquired=='Y') {
        JournalProcessLock restarted;
        std::string reason;
        CHECK(restarted.acquire(journal,&reason));
        CHECK(restarted.held());
    }
    std::filesystem::remove_all(dir);
}

// This integrates the disk journal, a real child endpoint process and a new
// authoritative host after SIGKILL. A pre-crash Queued receipt must restore
// as Unknown and only the exact authorized original may be re-admitted.
// Privileged startup must never bind a listener using an unsafe or
// corrupted receipt journal. Failure must release the process lock so an
// operator can repair the file and restart against the same checkpoint.
void pinnedEndpointRejectsUnsafeStartupJournal() {
    const auto root=tempPath();
    std::filesystem::create_directories(root);
    const auto journal=root/"receipts";
    const auto external=root/"external";
    const auto lookup=[](const std::string& name)->std::optional<net::Key>{
        return name=="admin"?std::optional<net::Key>(key()):std::nullopt;
    };
    std::string error;
    {
        FileJournal file(journal);
        const std::map<ServerGateway::Key,ServerGateway::Entry> empty;
        CHECK(file.save(empty,&error));
    }
    // First establish the happy path: an empty valid V1 journal loads.
    {
        LoopbackAdminEndpoint service(journal);
        CHECK(service.open(0,kEdit,lookup,reader,writer,&error));
        CHECK(service.journalHealthy());
        CHECK(service.gateway().entries().empty());
    }
    // Break the magic/checksum without changing inode type or permissions.
    const int fileFd=::open(journal.c_str(),O_RDWR);
    CHECK(fileFd>=0);
    if(fileFd>=0) {
        std::uint8_t first=0;
        CHECK(::pread(fileFd,&first,1,0)==1);
        first^=0x5a;
        CHECK(::pwrite(fileFd,&first,1,0)==1);
        CHECK(::close(fileFd)==0);
    }
    {
        LoopbackAdminEndpoint service(journal);
        CHECK(!service.open(0,kEdit,lookup,reader,writer,&error));
        CHECK(!service.journalHealthy());
        CHECK(service.gateway().entries().empty());
        JournalProcessLock retry;
        CHECK(retry.acquire(journal,&error)); // startup released its flock
    }
    CHECK(std::filesystem::remove(journal));
    // A symlink is never silently followed as an authorized journal file.
    const int outside=::open(external.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    CHECK(outside>=0);
    if(outside>=0)CHECK(::close(outside)==0);
    CHECK(::symlink(external.c_str(),journal.c_str())==0);
    {
        LoopbackAdminEndpoint service(journal);
        CHECK(!service.open(0,kEdit,lookup,reader,writer,&error));
        CHECK(!service.journalHealthy());
    }
    CHECK(std::filesystem::remove(journal));
    // FIFO must fail without blocking server startup.
    CHECK(::mkfifo(journal.c_str(),0600)==0);
    {
        LoopbackAdminEndpoint service(journal);
        CHECK(!service.open(0,kEdit,lookup,reader,writer,&error));
        CHECK(!service.journalHealthy());
    }
    CHECK(std::filesystem::remove(journal));
    // After remediation the same service path can start successfully.
    {
        LoopbackAdminEndpoint recovered(journal);
        CHECK(recovered.open(0,kEdit,lookup,reader,writer,&error));
        CHECK(recovered.gateway().entries().empty());
    }
    std::filesystem::remove_all(root);
}

void killedEndpointRecoversQueuedJournal() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journalPath=dir/"receipts";
    const auto request=packet(33,551,88);
    ServerGateway::Entry queued;
    CHECK(encode(request,queued.originalFrame));
    queued.expectedRevision=0;
    queued.receipt.result=Result::Queued;
    queued.receipt.scheduledTick=15;
    queued.receipt.revision=0;
    queued.receipt.detail="pre-crash queued receipt";
    for(const auto& field:desired(33).desired)
        queued.desired.emplace(field.id,
            std::make_pair(field.kind,authoredFingerprint(field)));
    std::map<ServerGateway::Key,ServerGateway::Entry> saved;
    saved.emplace(ServerGateway::Key{"admin",551,88},std::move(queued));
    std::string error;
    FileJournal file(journalPath);
    const bool wrote=file.save(saved,&error);
    CHECK(wrote);
    if(!wrote) {
        std::filesystem::remove_all(dir);
        return;
    }
    int ready[2]={-1,-1};
    if(::pipe(ready)!=0) {
        CHECK(false);
        std::filesystem::remove_all(dir);
        return;
    }
    const auto lookup=[](const std::string& name)->std::optional<net::Key>{
        return name=="admin"?std::optional<net::Key>(key()):std::nullopt;
    };
    const pid_t child=::fork();
    if(child<0) {
        CHECK(false);
        ::close(ready[0]);::close(ready[1]);
        std::filesystem::remove_all(dir);
        return;
    }
    if(child==0) {
        ::close(ready[0]);
        LoopbackAdminEndpoint endpoint(journalPath);
        std::string why;
        const bool opened=endpoint.open(0,kEdit,lookup,reader,writer,&why);
        const bool unknown=opened && endpoint.gateway().entries().size()==1 &&
            endpoint.gateway().entries().begin()->second.receipt.result==Result::Unknown;
        const char state=unknown?'Y':'N';
        ssize_t sent=-1;
        do {sent=::write(ready[1],&state,1);} while(sent<0 && errno==EINTR);
        if(sent!=1)::_exit(44);
        ::close(ready[1]);
        if(!unknown)::_exit(43);
        for(;;)::pause();
    }
    ::close(ready[1]);
    struct pollfd descriptor{ready[0],POLLIN,0};
    int polled=-1;
    do {polled=::poll(&descriptor,1,3000);}
    while(polled<0 && errno==EINTR);
    char received='N';
    if(polled>0 && (descriptor.revents&POLLIN))
    {
        ssize_t count=-1;
        do {count=::read(ready[0],&received,1);} while(count<0 && errno==EINTR);
        if(count!=1)received='N';
    }
    ::close(ready[0]);
    CHECK(polled>0 && received=='Y');
    if(received=='Y') {
        LoopbackAdminEndpoint competing(journalPath);
        CHECK(!competing.open(0,kEdit,lookup,reader,writer,&error));
    }
    (void)::kill(child,SIGKILL);
    int status=0;
    CHECK(::waitpid(child,&status,0)==child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    if(received=='Y') {
        auto sim=make();
        net::Bytes encoded;
        CHECK(encode(request,encoded));
        {
            LoopbackAdminEndpoint recovered(journalPath);
            const bool started=recovered.open(0,kEdit,lookup,reader,writer,&error);
            CHECK(started);
            if(started) {
                CHECK(recovered.gateway().entries().size()==1);
                CHECK(recovered.gateway().entries().begin()->second.receipt.result==Result::Unknown);
                auto [client,server]=net::makeLoopbackPair();
                client->send(encoded);
                recovered.gateway().poll(*sim,*server,"admin",reader,writer,100);
                CHECK(recovered.gateway().entries().begin()->second.receipt.result==Result::Queued);
                for(int i=0;i<4;++i) {
                    sim->step();
                    recovered.afterTick(*sim);
                }
                CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
                CHECK(recovered.gateway().entries().begin()->second.receipt.result==Result::Applied);
            }
        }
        // The Applied receipt must survive a second clean endpoint restart
        // when its authoritative checkpoint has also been restored.
        auto restored=make();
        CHECK(restored->deserialize(sim->serialize(),&error));
        {
            LoopbackAdminEndpoint again(journalPath);
            const bool restarted=again.open(0,kEdit,lookup,reader,writer,&error);
            CHECK(restarted);
            if(restarted) {
                again.afterTick(*restored);
                CHECK(again.gateway().entries().size()==1);
                CHECK(again.gateway().entries().begin()->second.receipt.result==Result::Applied);
                CHECK(restored->world().resourceConst<FieldRuntimeState>()->revision==1);
            }
        } // release lock before deleting journal directory
    }
    std::filesystem::remove_all(dir);
}
#endif

#ifndef __EMSCRIPTEN__
// A second endpoint must refuse the same authoritative receipt journal
// until the first endpoint, including its listener, has been destroyed.
void endpointJournalOwnershipLifecycle() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journal=dir/"receipts";
    const auto lookup=[](const std::string& name)->std::optional<net::Key>{
        return name=="admin"?std::optional<net::Key>(key()):std::nullopt;
    };
    std::string error;
    {
        LoopbackAdminEndpoint first(journal);
        const bool started=first.open(0,kEdit,lookup,reader,writer,&error);
        CHECK(started);
        if(started) {
            LoopbackAdminEndpoint competitor(journal);
            CHECK(!competitor.open(0,kEdit,lookup,reader,writer,&error));
        }
    }
    {
        LoopbackAdminEndpoint restarted(journal);
        CHECK(restarted.open(0,kEdit,lookup,reader,writer,&error));
    }
    std::filesystem::remove_all(dir);
}
#endif

void authProof() {
    auto [client,server]=net::makeLoopbackPair();
    const auto k=key();
    ServerHandshake serverAuth([&](const std::string& user)->std::optional<net::Key>{
        return user=="admin"?std::optional<net::Key>(k):std::nullopt;
    });
    ClientHandshake clientAuth("admin",k);
    CHECK(clientAuth.begin(*client));
    net::Bytes b;
    CHECK(server->recv(b));
    CHECK(serverAuth.consume(*server,b));
    CHECK(client->recv(b));
    CHECK(clientAuth.consume(*client,b));
    CHECK(server->recv(b));
    CHECK(serverAuth.consume(*server,b));
    CHECK(client->recv(b));
    CHECK(clientAuth.consume(*client,b));
    CHECK(clientAuth.authenticated() && serverAuth.authenticated());
    CHECK(serverAuth.principal()=="admin");
    // A wrong client secret cannot establish a trusted principal.
    auto [badC,badS]=net::makeLoopbackPair();
    ServerHandshake badServer([&](const std::string& user)->std::optional<net::Key>{
        return user=="admin"?std::optional<net::Key>(k):std::nullopt;
    });
    net::Key wrong=k;wrong[0]^=12;
    ClientHandshake badClient("admin",wrong);
    CHECK(badClient.begin(*badC));
    CHECK(badS->recv(b));
    CHECK(badServer.consume(*badS,b));
    CHECK(badC->recv(b));
    CHECK(badClient.consume(*badC,b));
    CHECK(badS->recv(b));
    CHECK(!badServer.consume(*badS,b));
    CHECK(!badServer.authenticated());
    // Review fix: an unknown (well-formed) principal must receive the same
    // 32-byte Challenge as a known one and be denied only at Proof, so Hello
    // cannot be used to enumerate accounts.
    auto [unkC,unkS]=net::makeLoopbackPair();
    ServerHandshake unkServer([&](const std::string& user)->std::optional<net::Key>{
        return user=="admin"?std::optional<net::Key>(k):std::nullopt;
    });
    ClientHandshake unkClient("intruder",k);
    CHECK(unkClient.begin(*unkC));
    CHECK(unkS->recv(b));
    CHECK(unkServer.consume(*unkS,b));
    CHECK(!unkServer.rejected());
    CHECK(unkC->recv(b));
    {
        Kind kind{};
        net::Bytes payload;
        CHECK(decode(b,kind,payload));
        CHECK(kind==Kind::Challenge && payload.size()==32);
    }
    CHECK(unkClient.consume(*unkC,b));
    CHECK(unkS->recv(b));
    CHECK(!unkServer.consume(*unkS,b));
    CHECK(!unkServer.authenticated() && unkServer.rejected());
    CHECK(unkC->recv(b));
    CHECK(!unkClient.consume(*unkC,b));
    CHECK(!unkClient.authenticated());
}
#ifndef __EMSCRIPTEN__
void realLoopbackWebSocket() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    {
    auto sim=make();
    LoopbackAdminEndpoint service(dir/"journal");
    std::string error;
    CHECK(service.open(0,kEdit,
        [](const std::string& name)->std::optional<net::Key>{
            return name=="admin"?std::optional<net::Key>(key()):std::nullopt;
        },reader,writer,&error));
    CHECK(service.port()!=0);
    const auto url="ws://127.0.0.1:"+std::to_string(service.port());
    // Review fix: idle unauthenticated sockets get a separate small quota
    // and cannot take every admin slot.
    std::vector<std::unique_ptr<net::Conn>> idle;
    for(int i=0;i<6;++i)
        if(auto conn=net::connectWebSocket(url))idle.push_back(std::move(conn));
    CHECK(idle.size()==6);
    std::uint64_t idleClock=net::nowMs();
    for(int i=0;i<40;++i) {
        idleClock+=10;
        for(auto& conn:idle)conn->update(idleClock);
        service.poll(*sim,idleClock);
        CHECK(service.connections()<=LoopbackAdminEndpoint::kMaxUnauthenticatedPeers);
    }
    CHECK(service.connections()>=1);
    // Past the short handshake deadline the anonymous peers are dropped.
    idleClock+=LoopbackAdminEndpoint::kHandshakeTimeoutMs+10;
    service.poll(*sim,idleClock);
    service.poll(*sim,idleClock+1);
    CHECK(service.connections()==0);
    for(auto& conn:idle)conn->close();
    idle.clear();
    LoopbackAdminConnector connector;
    CHECK(connector.connect(url,"admin",key(),&error));
    const auto start=idleClock+2;
    std::uint64_t lastPollMs=start;
    bool established=false;
    for(int i=0;i<200;++i) {
        const auto now=start+std::uint64_t(i*20);
        connector.poll(now);
        service.poll(*sim,now);
        connector.poll(now+1);
        lastPollMs=now+1;
        if(connector.ready()){established=true;break;}
    }
    CHECK(established);
    if(established) {
        RemoteClient remote(334455);
        CHECK(remote.begin(desired(21),kEdit,4));
        auto c=connector.takeAuthenticated();
        // Both WS endpoints already observed synthetic handshake timestamps.
        // Starting this phase from a FRESH wall-clock nowMs() can go
        // *backwards*: the handshake loop advances time by 20ms per spin,
        // faster than wall time. Native WsConn then underflows its unsigned
        // now-lastHeard_ timeout and closes before any PHYSIS Submit.
        // Continue the SAME monotonic logical clock for socket I/O.
        const auto next=lastPollMs+1;
        for(int i=0;i<150;++i) {
            const auto now=next+std::uint64_t(i*30);
            remote.poll(*c,now);
            service.poll(*sim,now);
            sim->step();
            service.afterTick(*sim);
            remote.poll(*c,now+1);
            if(remote.status()==ClientStatus::Applied)break;
        }
        if(remote.status()!=ClientStatus::Applied) {
            std::printf("PHYSIS WS diagnostic: status=%u receipt=%u detail=%s "
                        "revision=%llu peers=%zu entries=%zu journal=%d conn=%d\n",
                        unsigned(remote.status()),unsigned(remote.receipt().result),
                        remote.receipt().detail.c_str(),
                        static_cast<unsigned long long>(
                            sim->world().resourceConst<FieldRuntimeState>()->revision),
                        service.connections(),service.gateway().entries().size(),
                        int(service.journalHealthy()),int(c->state()));
        }
        CHECK(remote.status()==ClientStatus::Applied);
        CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    }
    } // Destroy listener and release journal lock BEFORE deleting lock file.
    std::filesystem::remove_all(dir);
}
#endif
}
int main(){
    codecAndCorruption();
    durableIntentAndReload();
    journalWriteFailureNeverAcknowledgesDurableSuccess();
    queuedReceiptBeforeCrash();
    queuedReceiptAlreadyAppliedBeforeCrash();
    missingJournalFailsClosed();
    recoveredIntent();
    authProof();
    exclusiveJournalOwnership();
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    killedProcessReleasesJournalLock();
    unsafeJournalLockInodesRejected();
    unsafeJournalLockParentsRejected();
    replacedLiveLockSuspendsAdmin();
    liveParentPermissionChangeSuspendsAdmin();
    pinnedJournalSaveDoesNotFollowMovedParent();
    pinnedJournalLoadRejectsUnsafeInodes();
    pinnedEndpointRejectsUnsafeStartupJournal();
    killedEndpointRecoversQueuedJournal();
#endif
#ifndef __EMSCRIPTEN__
    endpointJournalOwnershipLifecycle();
#endif
#ifndef __EMSCRIPTEN__
    realLoopbackWebSocket();
#endif
    std::printf("PHYSIS admin/journal: %s\n",failed?"FAIL":"PASS");
    return failed?1:0;
}

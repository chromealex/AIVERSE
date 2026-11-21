// Stage 108 deferred: explicit device generation prevents false validity
// when a GPU Device address is reused after host-controlled shutdown.
#include "engine/render/RenderResourceOwnerGroup.h"
#include "engine/render/RenderDeviceLifetime.h"
#include <cstdio>
#include <cstdlib>
#if defined(__APPLE__) || defined(__unix__)
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

// Unlike assert(), CHECK is active in Release and RelWithDebInfo CTest runs.
[[noreturn]] static void testCheckFailed(const char* expr,const char* file,int line) {
    std::fprintf(stderr,"render_device_epoch CHECK failed: %s at %s:%d\n",
                 expr,file,line);
    std::abort();
}
#define CHECK(expr) do {if(!(expr))testCheckFailed(#expr,__FILE__,__LINE__);} while(false)
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <unordered_set>
#include <vector>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
using namespace aiverse;
class StubDevice final: public gfx::Device {
public:
    gfx::DeviceInfo d{};
    std::string error;
    const gfx::DeviceInfo& info() const override{return d;}
    const std::string& lastError() const override{return error;}
    bool waitForResourceIdle() override{return true;}
    bool throwOnBufferCreation=false;
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{
        if(throwOnBufferCreation)
            throw std::runtime_error("injected Model buffer allocation failure");
        return {}; // force geometry allocation failure in the CPU fixture
    }
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle) override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
    void destroyPipeline(gfx::PipelineHandle) override{}
    void beginFrame(int,int) override{}
    void endFrame() override{}
    void beginPass(const gfx::PassDesc&) override{}
    void endPass() override{}
    void setViewport(const gfx::Rect&) override{}
    void setScissor(const gfx::Rect&) override{}
    void setPipeline(gfx::PipelineHandle) override{}
    void setVertexBuffer(gfx::BufferHandle,size_t) override{}
    void setIndexBuffer(gfx::BufferHandle,size_t) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t) override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override{}
};
// CPU-only regressions for Stage 129-131 unique-owned Model cache leases.
// These do not assert GPU completion: StubDevice has no in-flight work.
static void verifyReadLeaseContracts() {
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    auto model=std::make_unique<render::Model>();
    model->path="models/hero.glb";
    const render::Model* resident=cache.insertIfAbsent(
        "models/hero.glb#geometry",std::move(model));
    CHECK(resident);
    CHECK(!cache.acquireModelRead("missing.glb"));
    CHECK(!cache.acquireModelRead(static_cast<const render::Model*>(nullptr)));
    render::Model foreign;
    CHECK(!cache.acquireModelRead(&foreign));

    auto narrow=cache.acquireModelRead(resident);
    auto byKey=cache.acquireModelRead("models/hero.glb#geometry");
    CHECK(narrow && narrow.get()==resident && byKey.get()==resident);
    auto moved=std::move(narrow);
    CHECK(!narrow && moved.get()==resident);
    auto frame=cache.acquireFrameReadScope();
    CHECK(frame.valid() && cache.activeModelReaders()==3);
    const auto first=cache.modelReadSafetyStats();
    CHECK(first.peakReaders>=3);

    // A foreign thread MUST be rejected before it even reads models_.
    bool threadDenied=false;
    std::thread foreignLookup([&]{
        try {auto candidate=cache.acquireModelRead("models/hero.glb#geometry");(void)candidate;}
        catch(const std::logic_error&){threadDenied=true;}
    });
    foreignLookup.join();
    CHECK(threadDenied);

    unsigned callbacks=0;
    auto detach=[&](const auto&){++callbacks;};
    auto release=[&](const render::Model*){++callbacks;};
    auto denied=[&](auto&& fn){
        bool thrown=false;
        try{fn();}
        catch(const std::logic_error&){thrown=true;}
        CHECK(thrown);
    };
    denied([&]{cache.replace(dev,"models/hero.glb#geometry",
        std::make_unique<render::Model>(),release);});
    denied([&]{cache.invalidateSources(dev,
        std::vector<std::string>{"models/hero.glb"},detach);});
    denied([&]{cache.trimUnused(dev,{},release);});
    denied([&]{cache.shutdown(dev);});
    CHECK(callbacks==0);
    CHECK(cache.find("models/hero.glb#geometry")==resident);
    CHECK(cache.activeModelReaders()==3);
    CHECK(cache.modelReadSafetyStats().blockedMutations==4);
    CHECK(cache.modelReadSafetyStats().peakReaders>=3);

    // Streaming publication of NEW keys is deliberately allowed under a
    // frame reader: std::map node insertion cannot move existing Model*.
    cache.insertIfAbsent("models/new.glb",
                         std::make_unique<render::Model>());
    render::ModelResourceCache::ModelMap staged;
    staged.emplace("models/streamed.glb",
                   std::make_unique<render::Model>());
    cache.publishStagedModels(dev,staged);
    CHECK(staged.empty() && cache.find("models/new.glb") &&
           cache.find("models/streamed.glb"));
    CHECK(cache.activeModelReaders()==3);
    // Reject any staged-key collision atomically BEFORE extracting the
    // first map node; both the resident and staged data survive unchanged.
    render::ModelResourceCache::ModelMap collisions;
    collisions.emplace("models/another.glb",std::make_unique<render::Model>());
    collisions.emplace("models/hero.glb#geometry",std::make_unique<render::Model>());
    const auto beforeCollisionGeneration=cache.generation();
    bool collisionDenied=false;
    try {cache.publishStagedModels(dev,collisions);}
    catch(const std::logic_error&){collisionDenied=true;}
    CHECK(collisionDenied);
    CHECK(collisions.size()==2);
    CHECK(cache.generation()==beforeCollisionGeneration);
    CHECK(cache.find("models/hero.glb#geometry")==resident);
    CHECK(cache.find("models/another.glb")==nullptr);
    // The cache's std::map may not be written from a different thread,
    // even for a new key and even while coarse frame readers are live.
    bool foreignInsertDenied=false;
    std::thread foreignInsert([&]{
        try {
            cache.insertIfAbsent("models/foreign.glb",
                                 std::make_unique<render::Model>());
        }catch(const std::logic_error&){foreignInsertDenied=true;}
    });
    foreignInsert.join();
    CHECK(foreignInsertDenied);
    CHECK(!cache.contains("models/foreign.glb"));
    // Replacements and evictions remain forbidden until the last reader.

    frame.reset();
    moved.reset();
    byKey.reset();
    CHECK(cache.activeModelReaders()==0);
    cache.shutdown(dev); // no GPU buffers; empty/idle StubDevice
    CHECK(cache.find("models/hero.glb#geometry")==nullptr);
}
static void verifyReadOwnerRebind() {
    // The same CPU cache can be rebound on a DIFFERENT owner thread after
    // successful shutdown. No real GPU backend is accessed concurrently.
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    {auto frame=cache.acquireFrameReadScope();CHECK(frame);}
    cache.shutdown(dev);
    std::thread newOwner([&]{
        cache.bindResourceDevice(dev);
        auto frame=cache.acquireFrameReadScope();
        CHECK(frame.valid());
        frame.reset();
        cache.shutdown(dev);
    });
    newOwner.join();
    CHECK(cache.activeModelReaders()==0);
}

// Stage 134: off-thread map reads are forbidden even when the caller
// does not request a ModelReadLease. Check before touching std::map.
static void verifyOwnerThreadMapReadContracts() {
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    auto* resident=cache.insertIfAbsent("models/read.glb",
                                        std::make_unique<render::Model>());
    CHECK(resident);
    const auto& constCache=static_cast<const render::ModelResourceCache&>(cache);
    size_t denied=0;
    std::thread worker([&]{
        const auto expectDenied=[&](auto&& fn) {
            bool rejected=false;
            try {fn();}
            catch(const std::logic_error&){rejected=true;}
            if(rejected)++denied;
        };
        expectDenied([&]{(void)cache.find("models/read.glb");});
        expectDenied([&]{(void)constCache.find("models/read.glb");});
        expectDenied([&]{(void)cache.contains("models/read.glb");});
        expectDenied([&]{(void)cache.models();});
        expectDenied([&]{(void)cache.modelFiles();});
        expectDenied([&]{(void)cache.affectedBySource("models/read.glb");});
        expectDenied([&]{(void)cache.stats();});
        expectDenied([&]{cache.forEachOwnedModel(
            [](const render::Model&,bool){});});
        expectDenied([&]{(void)cache.geometryPoolModelReferences();});
        expectDenied([&]{(void)cache.gpuOwnerEstimate();});
        expectDenied([&]{(void)cache.pendingRetirements();});
        expectDenied([&]{(void)cache.retirementStats();});
        expectDenied([&]{(void)cache.estimatedRetiredGpuBytes();});
        expectDenied([&]{(void)cache.publicationBackpressured();});
    });
    worker.join();
    CHECK(denied==14);
    CHECK(cache.find("models/read.glb")==resident);
    CHECK(constCache.find("models/read.glb")==resident);
    CHECK(cache.contains("models/read.glb"));
    CHECK(cache.models().size()==1);
    CHECK(cache.modelFiles().size()==1);
    CHECK(cache.affectedBySource("models/read.glb").size()==1);
    cache.shutdown(dev);
}

// Stage 136: a failed worker-prepared GPU upload must NEVER publish a
// partially resident Model. Allocation/throw paths must retain ownership
// until a verified GPU retirement barrier, not an ordinary destructor.
static void verifyPreparedModelUploadRollback() {
    struct PreparedForTest {
        std::unique_ptr<render::Model> model;
    };
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    const auto originalGeneration=cache.generation();

    PreparedForTest empty;
    empty.model=std::make_unique<render::Model>();
    CHECK(cache.publishPrepared(dev,"models/ready.glb",std::move(empty)));
    CHECK(!empty.model);
    CHECK(cache.contains("models/ready.glb"));
    CHECK(cache.preparedUploadFailures()==0);

    // A synchronous load wins an async race: avoid any GPU upload and
    // preserve the still-CPU-only prepared model owned by the caller.
    PreparedForTest duplicate;
    duplicate.model=std::make_unique<render::Model>();
    duplicate.model->vertices.resize(3);
    duplicate.model->indices={0,1,2};
    CHECK(!cache.publishPrepared(dev,"models/ready.glb",std::move(duplicate)));
    CHECK(duplicate.model);
    CHECK(cache.preparedUploadFailures()==0);
    CHECK(cache.pendingRetirements()==0);

    // The stub deliberately rejects all Buffer allocations. Previously
    // uploadModel(false) was ignored and this unusable model was admitted.
    PreparedForTest failed;
    failed.model=std::make_unique<render::Model>();
    failed.model->vertices.resize(3);
    failed.model->indices={0,1,2};
    CHECK(!cache.publishPrepared(dev,"models/fail.glb",std::move(failed)));
    CHECK(!failed.model); // ownership moved to GPU retirement queue
    CHECK(!cache.contains("models/fail.glb"));
    CHECK(cache.pendingRetirements()==1);
    CHECK(cache.preparedUploadFailures()==1);
    CHECK(cache.generation()==originalGeneration+1);

    // An actual allocation exception uses the SAME guarded rollback path
    // and must not leave the partially uploaded Model in a local unique_ptr.
    dev.throwOnBufferCreation=true;
    PreparedForTest throwing;
    throwing.model=std::make_unique<render::Model>();
    throwing.model->vertices.resize(3);
    throwing.model->indices={0,1,2};
    bool caught=false;
    try {
        (void)cache.publishPrepared(dev,"models/throw.glb",std::move(throwing));
    }catch(const std::runtime_error&){caught=true;}
    CHECK(caught);
    CHECK(!throwing.model);
    CHECK(!cache.contains("models/throw.glb"));
    CHECK(cache.pendingRetirements()==2);
    CHECK(cache.preparedUploadFailures()==2);
    dev.throwOnBufferCreation=false;
    cache.shutdown(dev); // StubDevice's idle barrier proves safe retirement
    CHECK(cache.pendingRetirements()==0);
}

static void verifyRetirementDiagnosticObserverIsolation() {
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    auto* original=cache.insertIfAbsent(
        "models/old.glb",std::make_unique<render::Model>());
    CHECK(original);
    unsigned notifications=0,blocked=0;
    cache.setRetirementDiagnosticObserver(
        [&](const render::RetirementDiagnosticEvent& event) {
            if(event.kind!=render::RetirementDiagnosticKind::Enqueued &&
               event.kind!=render::RetirementDiagnosticKind::RecoveredAfterIdle)
                return;
            ++notifications;
            CHECK(cache.models().size()==1);
            // An observer can inspect Model residency but never mutate the
            // resident map/retirement queue while the event is emitted.
            try {
                cache.insertIfAbsent("models/illegal.glb",
                                     std::make_unique<render::Model>());
            }catch(const std::logic_error&){++blocked;}
            try {
                (void)cache.collectRetired(dev);
            }catch(const std::logic_error&){++blocked;}
        });
    auto replacement=std::make_unique<render::Model>();
    CHECK(cache.replace(dev,"models/old.glb",std::move(replacement),
                        [](const render::Model*){}));
    CHECK(notifications==1);
    CHECK(blocked==2);
    CHECK(cache.find("models/illegal.glb")==nullptr);
    CHECK(cache.pendingRetirements()==1);
    CHECK(!cache.publicationBackpressured());
    CHECK(cache.collectRetired(dev)); // fallback emits RecoveredAfterIdle
    CHECK(notifications==2);
    CHECK(blocked==4);
    CHECK(cache.pendingRetirements()==0);
    CHECK(cache.find("models/old.glb")!=original);
    cache.shutdown(dev);
}

// Stage 135: a read-only owner enumeration callback must not invalidate
// its own map/retirement iterators. Recursive *read-only* enumeration remains
// valid and exceptions must restore the callback guard.
static void verifyModelOwnershipVisitorContracts() {
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    auto first=std::make_unique<render::Model>();
    first->path="models/first.glb";
    auto second=std::make_unique<render::Model>();
    second->path="models/second.glb";
    const auto* resident=cache.insertIfAbsent("models/first.glb",
                                              std::move(first));
    CHECK(resident);
    CHECK(cache.insertIfAbsent("models/second.glb",std::move(second)));
    const auto initialGeneration=cache.generation();
    unsigned visited=0, blocked=0;
    const auto expectBlocked=[&](auto&& callback) {
        bool thrown=false;
        try{callback();}
        catch(const std::logic_error&){thrown=true;}
        CHECK(thrown);
        ++blocked;
    };
    cache.forEachOwnedModel([&](const render::Model& model,bool retired) {
        CHECK(!retired);
        ++visited;
        CHECK(cache.models().size()==2);
        CHECK(cache.contains(model.path));
        CHECK(cache.retirementStats().pendingResources==0);
        CHECK(cache.pendingRetirements()==0);
        CHECK(!cache.publicationBackpressured());
        unsigned nested=0;
        cache.forEachOwnedModel([&](const render::Model&,bool) {++nested;});
        CHECK(nested==2);
        // Changing any cached membership or retirement state while the
        // owner enumerates would make the current borrowed Model unsafe.
        expectBlocked([&]{cache.insertIfAbsent("models/new.glb",
                           std::make_unique<render::Model>());});
        expectBlocked([&]{cache.replace(dev,"models/first.glb",
                           std::make_unique<render::Model>(),
                           [](const render::Model*){});});
        expectBlocked([&]{(void)cache.acquireModelRead(&model);});
        expectBlocked([&]{(void)cache.collectRetired(dev);});
        expectBlocked([&]{cache.setRetirementDiagnosticObserver(
            [](const render::RetirementDiagnosticEvent&){});});
        if(visited==1) {
            // retireUnpublished() is noexcept, so nested retirement cannot
            // throw. It must take the explicit idle fallback, never append
            // under an active retirement-queue enumeration callback.
            auto scratch=std::make_unique<render::Model>();
            const auto failures=cache.stagingQueueFailures();
            cache.retireUnpublished(dev,scratch);
            CHECK(!scratch);
            CHECK(cache.stagingQueueFailures()==failures+1);
            CHECK(cache.pendingRetirements()==0);
        }
    });
    CHECK(visited==2);
    CHECK(blocked==10);
    CHECK(cache.generation()==initialGeneration);
    CHECK(cache.find("models/first.glb")==resident);
    CHECK(cache.find("models/new.glb")==nullptr);
    bool escaped=false;
    try {
        cache.forEachOwnedModel([](const render::Model&,bool) {
            throw std::logic_error("simulated inspector exception");
        });
    }catch(const std::logic_error&){escaped=true;}
    CHECK(escaped);
    CHECK(cache.insertIfAbsent("models/new.glb",
                               std::make_unique<render::Model>()));
    CHECK(cache.generation()==initialGeneration+1);
    cache.shutdown(dev);
}

// Stage 133: a callback handed a resident Model* may inspect it, but
// must never change cache membership or create a new reader guard while
// the caller still owns its std::map iterator.
static void verifyDetachCallbackReentrancyContracts() {
    StubDevice dev;
    render::ModelResourceCache cache;
    cache.bindResourceDevice(dev);
    auto old=std::make_unique<render::Model>();
    old->path="models/old.glb";
    auto* resident=cache.insertIfAbsent("models/old.glb",std::move(old));
    CHECK(resident);
    const uint64_t initialGeneration=cache.generation();
    auto expectRejected=[](auto&& callback) {
        bool rejected=false;
        try{callback();}
        catch(const std::logic_error&){rejected=true;}
        CHECK(rejected);
    };

    expectRejected([&]{
        cache.replace(dev,"models/old.glb",
            std::make_unique<render::Model>(),[&](const render::Model* model) {
                CHECK(model==resident);
                CHECK(cache.find("models/old.glb")==resident);
                // A nested insertion would mutate the map while replace
                // still owns an iterator and is not an acceptable callback.
                cache.insertIfAbsent("models/other.glb",
                    std::make_unique<render::Model>());
            });
    });
    CHECK(cache.find("models/old.glb")==resident);
    CHECK(cache.find("models/other.glb")==nullptr);
    CHECK(cache.generation()==initialGeneration);

    render::ModelResourceCache::ModelMap staged;
    staged.emplace("models/new.glb",std::make_unique<render::Model>());
    expectRejected([&]{
        cache.invalidateSources(dev,{"models/old.glb"},
            [&](const auto& affected) {
                CHECK(affected.size()==1);
                CHECK(affected.front().model==resident);
                // A staged batch may be published later, but not during
                // the external detach callback.
                cache.publishStagedModels(dev,staged);
            });
    });
    CHECK(staged.size()==1);
    CHECK(cache.find("models/old.glb")==resident);
    CHECK(cache.find("models/new.glb")==nullptr);
    CHECK(cache.generation()==initialGeneration);

    expectRejected([&]{
        cache.trimUnused(dev,{},
            [&](const render::Model* model) {
                CHECK(model==resident);
                // Fresh leases created here could outlive the trim callback
                // and protect a pointer that trim immediately destroys.
                auto frame=cache.acquireFrameReadScope();
                (void)frame;
            });
    });
    CHECK(cache.find("models/old.glb")==resident);
    CHECK(cache.activeModelReaders()==0);
    CHECK(cache.generation()==initialGeneration);

    // A previously throwing callback must never poison the guard.
    const size_t invalidated=cache.invalidateSources(dev,{"models/old.glb"},
        [&](const auto& affected) {
            CHECK(affected.size()==1);
            CHECK(affected.front().model==resident);
            CHECK(cache.find("models/old.glb")==resident);
        });
    CHECK(invalidated==1);
    CHECK(cache.find("models/old.glb")==nullptr);
    cache.publishStagedModels(dev,staged);
    CHECK(staged.empty());
    CHECK(cache.find("models/new.glb")!=nullptr);
    CHECK(cache.activeModelReaders()==0);
    cache.shutdown(dev);
}
// Fatal owner-thread contracts must be verified in a separate process:
// they intentionally call std::terminate, never a catchable exception.
static int fatalChild(const std::string& mode) {
    render::ModelResourceCache cache;
    if(mode=="--fatal-lease") {
        auto* resident=cache.insertIfAbsent(
            "test",std::make_unique<render::Model>());
        auto token=cache.acquireModelRead(resident);
        std::thread foreign([guard=std::move(token)]() mutable {guard.reset();});
        foreign.join();
        return 0; // reaching this line means the required termination failed
    }
    if(mode=="--fatal-scope") {
        auto guard=cache.acquireFrameReadScope();
        std::thread foreign([guard=std::move(guard)]() mutable {guard.reset();});
        foreign.join();
        return 0;
    }
    if(mode=="--fatal-destroy") {
        auto holder=std::make_unique<render::ModelResourceCache>();
        auto guard=holder->acquireFrameReadScope();
        holder.reset(); // MUST terminate with a live reader
        guard.reset();
        return 0;
    }
    return 2;
}
#if defined(__APPLE__) || defined(__unix__)
static void verifyFatalReadContracts(const char* self) {
    for(const char* mode:{"--fatal-lease","--fatal-scope","--fatal-destroy"}) {
        pid_t pid=-1;
        char* args[]={const_cast<char*>(self),const_cast<char*>(mode),nullptr};
        // Preserve the host's runtime-loader environment for the child.
        // An empty envp may hide DYLD_LIBRARY_PATH / shared libraries.
#if defined(__APPLE__)
        char* const* childEnv=*_NSGetEnviron();
#else
        char* const* childEnv=::environ;
#endif
        const int spawned=posix_spawnp(&pid,self,nullptr,nullptr,args,childEnv);
        CHECK(spawned==0);
        int status=0;
        CHECK(waitpid(pid,&status,0)==pid);
        CHECK(WIFSIGNALED(status));
        CHECK(WTERMSIG(status)==SIGABRT);
    }
}
#endif

int main(int argc,char** argv) {
    if(argc==2)return fatalChild(argv[1]);
    CHECK(argc==1);
    verifyReadLeaseContracts();
    verifyReadOwnerRebind();
    verifyOwnerThreadMapReadContracts();
    verifyPreparedModelUploadRollback();
    verifyRetirementDiagnosticObserverIsolation();
    verifyModelOwnershipVisitorContracts();
    verifyDetachCallbackReentrancyContracts();
#if defined(__APPLE__) || defined(__unix__)
    verifyFatalReadContracts(argv[0]);
#endif
    StubDevice device;
    render::ModelResourceCache cache;
    render::ModelResourceCache hosted; // embedded GameClient shares editor epoch
    render::RenderDeviceLifetime first(device);
    const auto prior=first.lease();
    cache.bindResourceDevice(device,prior);
    hosted.bindResourceDevice(device,prior);
    CHECK(cache.resourceDeviceGeneration()==prior.generation());
    CHECK(hosted.resourceDeviceGeneration()==prior.generation());
    CHECK(prior.validFor(device));
    {
        render::RenderResourceOwnerGroup group;
        auto lease=group.registerScoped(prior,cache);
        auto hostedLease=group.registerScoped(prior,hosted);
        CHECK(group.snapshot(device).uniqueOwners==2);
        hostedLease.reset();
        lease.reset();
    }
    // Successful explicit GPU idle is required BEFORE retiring the
    // host's generation token or recreating the GPU device at this address.
    // Hosted GameClient destroys its resources first, then the owning
    // editor cache, before the ONE shared GPU generation is retired.
    hosted.shutdown(device);
    cache.shutdown(device);
    first.retire();
    CHECK(!prior.validFor(device));
    render::RenderDeviceLifetime next(device); // same pointer, new epoch
    const auto current=next.lease();
    CHECK(current.validFor(device));
    CHECK(current.generation()!=prior.generation());
    cache.bindResourceDevice(device,current);
    hosted.bindResourceDevice(device,current);
    CHECK(cache.resourceDeviceGeneration()==current.generation());
    CHECK(hosted.resourceDeviceGeneration()==current.generation());
    bool rejected=false;
    try {cache.bindResourceDevice(device,prior);}
    catch(const std::logic_error&) {rejected=true;}
    CHECK(rejected);
    hosted.shutdown(device);
    cache.shutdown(device);
}

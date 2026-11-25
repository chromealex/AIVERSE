// Opt-in runtime access validation for experimental ECS wave scheduling.
// This facade checks calls routed through GuardedWorld; it does NOT prevent
// raw World access, retained pointers/references, or external global mutations.
#pragma once
#include "EcsWaveExecutor.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace aiverse::ecs_experiment {

class AccessViolation : public std::logic_error {
public:
    explicit AccessViolation(const char* message):std::logic_error(message){}
};

class AccessScope {
    const SystemAccess* previous_;
    static inline thread_local const SystemAccess* current_ = nullptr;
    static inline thread_local Entity currentEntity_=kNullEntity;
    static inline thread_local bool entityConstrained_=false;
public:
    explicit AccessScope(const SystemAccess& access):previous_(current_) {current_=&access;}
    AccessScope(const AccessScope&)=delete;
    AccessScope& operator=(const AccessScope&)=delete;
    ~AccessScope(){current_=previous_;}
    static const SystemAccess* current(){return current_;}
    // Only the entity currently owned by the lane can be accessed for types
    // written by the job. Other read-only component types are freely readable.
    class EntityScope {
        Entity previous_;
        bool previousConstrained_;
    public:
        explicit EntityScope(Entity e)
            : previous_(currentEntity_),previousConstrained_(entityConstrained_) {
            currentEntity_=e;
            entityConstrained_=true;
        }
        EntityScope(const EntityScope&)=delete;
        ~EntityScope(){
            currentEntity_=previous_;
            entityConstrained_=previousConstrained_;
        }
    };
    static void requireOutsideEntityJob() {
        if(entityConstrained_)
            throw AccessViolation("shared mutation or nested traversal inside per-entity job");
    }
    static void requireEntityLocal(AccessId id,Entity e) {
        if(entityConstrained_ && current_ &&
           containsWrite(current_->writes,id) && !(e==currentEntity_))
            throw AccessViolation("cross-entity access to a component written by this job");
    }
private:
    static bool containsWrite(const std::vector<AccessId>& ids, AccessId id) {
        return std::find(ids.begin(),ids.end(),id)!=ids.end();
    }
};

inline bool containsAccess(const std::vector<AccessId>& ids, AccessId id) {
    return std::find(ids.begin(),ids.end(),id)!=ids.end();
}
inline void requireComponent(AccessId id,bool write) {
    const auto* scope=AccessScope::current();
    if(!scope)throw AccessViolation("component access outside system scope");
    if(write ? !containsAccess(scope->writes,id)
             : !containsAccess(scope->reads,id) && !containsAccess(scope->writes,id))
        throw AccessViolation("undeclared component access");
}
inline void requireResource(AccessId id,bool write) {
    const auto* scope=AccessScope::current();
    if(!scope)throw AccessViolation("resource access outside system scope");
    if(write ? !containsAccess(scope->resourceWrites,id)
             : !containsAccess(scope->resourceReads,id) && !containsAccess(scope->resourceWrites,id))
        throw AccessViolation("undeclared resource access");
}
inline void requireStructural() {
    const auto* scope=AccessScope::current();
    if(!scope || !scope->structural)
        throw AccessViolation("undeclared structural change");
}

template<class T> AccessId componentId() {return detail::componentTypeId<T>();}

class GuardedWorld {
    World& world_;
public:
    explicit GuardedWorld(World& world):world_(world){}
    template<class T> const T* read(Entity e) const {
        requireComponent(componentId<T>(),false);
        AccessScope::requireEntityLocal(componentId<T>(),e);
        return static_cast<const World&>(world_).tryGetConst<T>(e);
    }
    template<class T> T* write(Entity e) {
        requireComponent(componentId<T>(),true);
        AccessScope::requireEntityLocal(componentId<T>(),e);
        return world_.tryGet<T>(e);
    }
    template<class T> bool has(Entity e) const {
        requireComponent(componentId<T>(),false);
        AccessScope::requireEntityLocal(componentId<T>(),e);
        return world_.has<T>(e);
    }
    template<class T,class... Args> T& add(Entity e,Args&&... args) {
        AccessScope::requireOutsideEntityJob();
        requireStructural();
        requireComponent(componentId<T>(),true);
        return world_.add<T>(e,std::forward<Args>(args)...);
    }
    template<class T> void remove(Entity e) {
        AccessScope::requireOutsideEntityJob();
        requireStructural();
        requireComponent(componentId<T>(),true);
        world_.remove<T>(e);
    }
    Entity create() {
        AccessScope::requireOutsideEntityJob();
        requireStructural();
        return world_.create();
    }
    void destroyLater(Entity e) {
        AccessScope::requireOutsideEntityJob();
        requireStructural();
        world_.destroyLater(e);
    }
    template<class T> const T* readResource() const {
        requireResource(componentId<T>(),false);
        return static_cast<const World&>(world_).resourceConst<T>();
    }
    template<class T> T* writeResource() {
        AccessScope::requireOutsideEntityJob();
        requireResource(componentId<T>(),true);
        return world_.resource<T>();
    }
    // Only the declared access is allowed. Other types must be queried through
    // this facade. No direct World& escapes from this API.
    template<class T,class Fn> void eachRead(Fn&& fn) const {
        AccessScope::requireOutsideEntityJob();
        requireComponent(componentId<T>(),false);
        static_cast<const World&>(world_).eachExistingConst<T>([&](Entity e,const T& v){
            fn(e,v);
        });
    }
    template<class T,class Fn> void eachWrite(Fn&& fn) {
        AccessScope::requireOutsideEntityJob();
        requireComponent(componentId<T>(),true);
        world_.eachExisting<T>(std::forward<Fn>(fn));
    }
    // A checked per-entity job; each lane owns distinct dense positions of T.
    // Other components declared as writable cannot be accessed on foreign
    // entities through GuardedWorld, preventing a common cross-lane race.
    // The caller must still avoid retained aliases and unmanaged external state.
    template<class T,class Fn>
    void eachAdaptiveWrite(PersistentWorkers& workers,ExecutionMode mode,
                           ParallelPolicy policy,Fn&& fn) {
        AccessScope::requireOutsideEntityJob();
        requireComponent(componentId<T>(),true);
        const auto* declaration=AccessScope::current();
        world_.prepareExistingWriteIds(declaration->writes);
        auto* writePool=world_.prepareExistingWrite<T>();
        const size_t count=writePool?writePool->size():0;
        const size_t lanes=size_t(workers.workers())+1;
        const bool parallel=workers.workers()>0 &&
            (mode==ExecutionMode::Parallel ||
             (mode==ExecutionMode::Auto && count>=policy.minimumEntities &&
              count/lanes>=policy.minimumPerLane));
        auto range=[&](size_t begin,size_t end) {
            AccessScope scope(*declaration); // install permissions on each worker
            World::ComponentChangeBatch changes;
            auto publish=[&] {world_.mergeComponentChanges<T>(changes);};
            try {
            for(size_t i=begin;i<end;++i) {
                Entity e=writePool->entityAt(i);
                changes.record(e);
                AccessScope::EntityScope owner(e);
                fn(e,writePool->dataAt(i));
            }
            } catch(...) {publish();throw;}
            publish();
        };
        if(parallel)workers.parallelFor(count,range);
        else if(count>0)range(0,count);
    }
};

// Execute callbacks with thread-local declarations installed on the exact worker
// executing each system. Scope restoration also happens on exceptions.
inline void runChecked(WaveExecutor& executor,const DependencyPlan& plan,
    const std::vector<std::function<void()>>& jobs,
    const std::function<void(size_t)>& barrier={}) {
    if(jobs.size()!=plan.systems().size())
        throw std::invalid_argument("system/function count mismatch");
    std::vector<WaveExecutor::SystemFn> wrapped;
    wrapped.reserve(jobs.size());
    for(size_t i=0;i<jobs.size();++i) {
        wrapped.push_back([&,i] {
            AccessScope scope(plan.systems()[i]);
            jobs[i]();
        });
    }
    executor.run(plan,wrapped,barrier);
}
} // namespace aiverse::ecs_experiment

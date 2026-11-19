// AIVERSE LENS: CPU-only camera scheduling, stable ECS entity identities and
// render-target dependency ordering. No RHI resources, GPU state or genre logic.
// One scheduler instance is owned by one presentation World; call reset() when
// replacing a World at the same address, after load, or after rollback.
#pragma once
#include "EcsCameraRender.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aiverse::render {

enum class CameraPlanIssue : uint8_t {
    InvalidOutput, DuplicateOutput, InvalidDependency, MissingProducer,
    DependencyCycle, ScreenSuppressed
};
struct CameraPlanDiagnostic {
    Entity entity{};
    CameraPlanIssue reason = CameraPlanIssue::InvalidOutput;
    uint64_t targetAssetKey = 0;
};
struct CameraRenderPlan {
    std::vector<EcsCameraView> jobs; // ready to submit in dependency order
    std::vector<CameraPlanDiagnostic> diagnostics;
};

class CameraRenderScheduler {
    struct State {
        uint64_t lastFrame = 0;
        double lastSeconds = 0.0;
        bool completed = false;
        bool dirty = true;
        bool manual = false;
        CameraOutputComponent lastOutput{};
    };
    const World* world_ = nullptr;
    std::unordered_map<uint64_t, State> states_;
public:
    void reset() { world_ = nullptr; states_.clear(); }
    void requestCapture(Entity entity) {
        if (entity.valid()) states_[entity.packed()].manual = true;
    }
    void markDirty(Entity entity) {
        if (entity.valid()) states_[entity.packed()].dirty = true;
    }
    void forget(Entity entity) { states_.erase(entity.packed()); }
    // A successful render is acknowledged by the host *after* endPass(). Failed
    // output allocations must not consume a manual request or clear dirty.
    void acknowledge(Entity entity, const CameraOutputComponent& output,
                     uint64_t frameIndex, double seconds, bool succeeded) {
        if (!succeeded || !entity.valid()) return;
        auto& state = states_[entity.packed()];
        state.lastFrame = frameIndex;
        state.lastSeconds = seconds;
        state.completed = true;
        state.dirty = false;
        state.manual = false;
        state.lastOutput = output;
    }

    CameraRenderPlan buildPlan(const World& world, uint64_t frameIndex,
                               double seconds, Vec3 renderOrigin = {}) {
        CameraRenderPlan plan;
        if (!std::isfinite(seconds)) return plan;
        if (world_ && world_ != &world) states_.clear();
        world_ = &world;
        // A recycled ECS index never inherits the previous entity's clock.
        for (auto it = states_.begin(); it != states_.end();) {
            if (!world.alive(Entity::fromPacked(it->first))) it = states_.erase(it);
            else ++it;
        }
        std::vector<EcsCameraView> cameras;
        visitEcsCameras(world, [&](const EcsCameraView& camera) {
            cameras.push_back(camera);
        }, renderOrigin);
        const size_t n = cameras.size();
        if (!n) return plan;
        std::vector<bool> blocked(n, false);
        auto issue = [&](size_t index, CameraPlanIssue reason, uint64_t key=0) {
            plan.diagnostics.push_back({cameras[index].entity, reason, key});
            blocked[index] = true;
        };
        std::unordered_map<uint64_t, std::vector<size_t>> producerGroups;
        size_t screen = n;
        bool layeredScreens=false;
        for (size_t i=0;i<n;++i) {
            const auto& output = cameras[i].output;
            if ((output.width == 0) != (output.height == 0)) {
                issue(i,CameraPlanIssue::InvalidOutput,output.targetAssetKey);
                continue;
            }
            if (output.kind == CameraOutputKind::Texture) {
                if (!output.targetAssetKey) {
                    issue(i,CameraPlanIssue::InvalidOutput);
                    continue;
                }
                producerGroups[output.targetAssetKey].push_back(i);
            } else if (output.kind == CameraOutputKind::Screen) {
                layeredScreens=layeredScreens || cameras[i].viewport.enabled || cameras[i].stack.overlay;
                if (screen == n || cameras[i].priority > cameras[screen].priority ||
                    (cameras[i].priority == cameras[screen].priority &&
                     cameras[i].entity.packed() < cameras[screen].entity.packed()))
                    screen = i;
            } else issue(i,CameraPlanIssue::InvalidOutput);
        }
        // Without explicit viewport/stack authoring keep legacy single screen.
        // When composition is opted in all screen cameras render to isolated RTs.
        for (size_t i=0;i<n && !layeredScreens;++i) if (i!=screen &&
            cameras[i].output.kind==CameraOutputKind::Screen && !blocked[i])
            issue(i,CameraPlanIssue::ScreenSuppressed);
        std::unordered_map<uint64_t,size_t> producer;
        for (const auto& [key,owners] : producerGroups) {
            if (owners.size()!=1) {
                for (size_t index: owners) issue(index,CameraPlanIssue::DuplicateOutput,key);
            } else if (!blocked[owners[0]]) producer.emplace(key,owners[0]);
        }
        std::vector<std::vector<size_t>> edges(n);
        std::vector<size_t> indegree(n,0);
        for (size_t i=0;i<n;++i) {
            if (blocked[i]) continue;
            const auto& dependencies = cameras[i].dependencies;
            if (dependencies.count > dependencies.reads.size()) {
                issue(i,CameraPlanIssue::InvalidDependency);
                continue;
            }
            std::unordered_set<uint64_t> unique;
            for (size_t j=0;j<dependencies.count;++j) {
                const uint64_t key = dependencies.reads[j];
                if (!key || !unique.insert(key).second) {
                    issue(i,CameraPlanIssue::InvalidDependency,key);
                    break;
                }
                const auto found=producer.find(key);
                if (found==producer.end()) {
                    issue(i,CameraPlanIssue::MissingProducer,key);
                    break;
                }
                // An explicit declared dependency is needed even when the
                // producer is currently rate-limited or manual-only.
                edges[found->second].push_back(i);
                ++indegree[i];
            }
        }
        struct Ready { size_t index; int32_t priority; uint64_t entity; bool screen; };
        struct LowerPriority {
            bool operator()(const Ready& a,const Ready& b) const {
                return a.screen != b.screen ? a.screen :
                       (a.priority != b.priority ? a.priority < b.priority :
                        a.entity > b.entity);
            }
        };
        std::priority_queue<Ready,std::vector<Ready>,LowerPriority> ready;
        for (size_t i=0;i<n;++i) if (!blocked[i] && indegree[i]==0)
            ready.push({i,cameras[i].priority,cameras[i].entity.packed(),
                        cameras[i].output.kind==CameraOutputKind::Screen});
        size_t visited=0;
        while (!ready.empty()) {
            const size_t i=ready.top().index;
            ready.pop();
            ++visited;
            auto& state=states_[cameras[i].entity.packed()];
            const auto& view=cameras[i];
            // Recreated targets (size/format/key changes) need fresh contents.
            const bool outputChanged=state.completed && !(state.lastOutput==view.output);
            const bool anotherFrame=!state.completed || state.lastFrame!=frameIndex;
            bool due=false;
            switch(view.update.mode) {
                case CameraUpdateMode::EveryFrame: due=true; break;
                case CameraUpdateMode::EveryNFrames:
                    due=!state.completed || (frameIndex>=state.lastFrame &&
                         frameIndex-state.lastFrame>=std::max<uint64_t>(1,view.update.everyNFrames));
                    break;
                case CameraUpdateMode::FixedRate:
                    due=!state.completed || seconds<state.lastSeconds ||
                         seconds-state.lastSeconds+1e-9>=1.0/double(std::max<uint16_t>(1,view.update.rateHz));
                    break;
                case CameraUpdateMode::OnDirty: due=state.dirty || !state.completed; break;
                case CameraUpdateMode::Manual: due=state.manual; break;
                default: due=false; break;
            }
            if (anotherFrame && (due || state.manual || outputChanged))
                plan.jobs.push_back(view);
            for (size_t child: edges[i]) if (--indegree[child]==0 && !blocked[child])
                ready.push({child,cameras[child].priority,cameras[child].entity.packed(),
                            cameras[child].output.kind==CameraOutputKind::Screen});
        }
        (void)visited;
        // A node left with indegree > 0 is cyclic or downstream of a cycle.
        // Never submit an incomplete dependency chain.
        for (size_t i=0;i<n;++i) if (!blocked[i] && indegree[i]!=0)
            issue(i,CameraPlanIssue::DependencyCycle);
        // Remove any scheduled nodes that need a producer rejected by a
        // cycle/invalid input. Rejected subgraphs cannot sample stale data.
        bool changed=true;
        while (changed) {
            changed=false;
            for (size_t i=0;i<n;++i) if (!blocked[i]) {
                const auto& dep=cameras[i].dependencies;
                for(size_t j=0;j<dep.count && j<dep.reads.size();++j) {
                    const auto it=producer.find(dep.reads[j]);
                    if (it==producer.end() || blocked[it->second]) {
                        issue(i,CameraPlanIssue::MissingProducer,dep.reads[j]);
                        changed=true;
                        break;
                    }
                }
            }
        }
        plan.jobs.erase(std::remove_if(plan.jobs.begin(),plan.jobs.end(),[&](const auto& view) {
            for (size_t i=0;i<n;++i)
                if (cameras[i].entity==view.entity) return blocked[i];
            return true;
        }),plan.jobs.end());
        return plan;
    }
};

} // namespace aiverse::render

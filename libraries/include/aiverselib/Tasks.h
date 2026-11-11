#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
namespace aiverse::lib {
enum class TaskStatus : std::int64_t { Pending,Active,Done };
struct Task { std::int64_t id=0,priority=0,worker=0; TaskStatus status=TaskStatus::Pending; };
// Unique task IDs; worker IDs > 0. Caller stores/restores task fields in NativeState.
template<class Eligible>
std::optional<std::size_t> claim(std::span<Task> tasks,std::int64_t worker,Eligible eligible) {
    if (worker<=0) return std::nullopt;
    for (std::size_t i=0;i<tasks.size();++i) if (tasks[i].worker==worker && tasks[i].status==TaskStatus::Active) return i;
    std::optional<std::size_t> best;
    for (std::size_t i=0;i<tasks.size();++i) {
        const auto& t=tasks[i];
        if (t.status!=TaskStatus::Pending || !eligible(t)) continue;
        if (!best || t.priority>tasks[*best].priority || (t.priority==tasks[*best].priority && t.id<tasks[*best].id)) best=i;
    }
    if (best) { tasks[*best].worker=worker; tasks[*best].status=TaskStatus::Active; }
    return best;
}
inline std::optional<std::size_t> claim(std::span<Task> tasks,std::int64_t worker) { return claim(tasks,worker,[](const Task&){return true;}); }
inline void releaseWorker(std::span<Task> tasks,std::int64_t worker) {
    if (worker<=0) return;
    for (auto& t:tasks) if (t.worker==worker && t.status==TaskStatus::Active) { t.worker=0; t.status=TaskStatus::Pending; }
}
inline bool finish(Task& task,std::int64_t worker) {
    if (worker<=0 || task.status!=TaskStatus::Active || task.worker!=worker) return false;
    task.worker=0; task.status=TaskStatus::Done; return true;
}
}

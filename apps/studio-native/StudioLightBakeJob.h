// A job owns its captured document and I/O callbacks until the worker exits.
// Only the submission thread starts jobs, polls results and handles publication.
#pragma once
#include "engine/bake/CapturedBake.h"
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace aiverse::studio {
class StudioLightBakeJob {
public:
    struct Status { std::string stage; float fraction; };
    ~StudioLightBakeJob() {
        cancel();
        if (worker_.valid()) worker_.wait();
    }
    void start(bake::CapturedBake input, bake::ReadFn read, bake::WriteFn stageFile) {
        if (worker_.valid()) throw std::logic_error("A light bake is already running");
        cancelled_ = false;
        { std::lock_guard lock(mutex_); progress_ = {"scene",0}; }
        worker_ = std::async(std::launch::async,
            [this,input=std::move(input),read=std::move(read),stageFile=std::move(stageFile)] {
                auto progress = [this](const std::string& stage,float fraction) {
                    std::lock_guard lock(mutex_);
                    progress_ = {stage,fraction};
                    return !cancelled_.load();
                };
                return bake::runCapturedBake(input,read,stageFile,progress);
            });
    }
    void cancel() { cancelled_ = true; }
    Status progress() const { std::lock_guard lock(mutex_); return progress_; }
    std::optional<bake::CapturedBakeResult> poll() {
        if (!worker_.valid() || worker_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return {};
        auto result = worker_.get();
        if (cancelled_.load()) result.error="cancelled";
        return result;
    }
private:
    std::atomic<bool> cancelled_{false};
    mutable std::mutex mutex_;
    Status progress_{"idle",0};
    std::future<bake::CapturedBakeResult> worker_;
};
} // namespace aiverse::studio

#pragma once
#include <glib.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::studio {
class StudioBackgroundQueue {
public:
    StudioBackgroundQueue() {
        GError* error = nullptr;
        pool_ = g_thread_pool_new(run, nullptr, 4, FALSE, &error);
        if (!pool_) {
            std::string message = error->message;
            g_error_free(error);
            throw std::runtime_error(message);
        }
    }
    StudioBackgroundQueue(const StudioBackgroundQueue&) = delete;
    StudioBackgroundQueue& operator=(const StudioBackgroundQueue&) = delete;
    ~StudioBackgroundQueue() { stop(); }

    // Called on the UI thread before releasing the StudioHost and its session.
    void stop() {
        *active_ = false;
        if (pool_) g_thread_pool_free(std::exchange(pool_, nullptr), FALSE, TRUE);
    }
    void submit(std::function<void()> work, std::function<void()> completion) {
        if (!pool_) throw std::logic_error("Studio background queue is closed");
        auto* job = new Job{std::move(work), std::move(completion), active_, g_main_context_ref_thread_default()};
        // GLib queues the item even when starting a new worker fails.
        GError* error = nullptr;
        if (!g_thread_pool_push(pool_, job, &error)) {
            g_warning("Studio worker creation: %s", error->message);
            g_error_free(error);
        }
    }
private:
    struct Job {
        std::function<void()> work, completion;
        std::shared_ptr<bool> active;
        GMainContext* context;
    };
    static void run(gpointer data, gpointer) {
        auto* job = static_cast<Job*>(data);
        job->work();
        auto* source = g_idle_source_new();
        g_source_set_priority(source, G_PRIORITY_DEFAULT);
        g_source_set_callback(source, [](gpointer data)->gboolean {
            auto* job = static_cast<Job*>(data);
            if (*job->active) job->completion();
            return G_SOURCE_REMOVE;
        }, job, [](gpointer data) { delete static_cast<Job*>(data); });
        auto* context = job->context;
        g_source_attach(source, context);
        g_main_context_unref(context);
        g_source_unref(source);
    }
    GThreadPool* pool_ = nullptr;
    std::shared_ptr<bool> active_ = std::make_shared<bool>(true);
};
}

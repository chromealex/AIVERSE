#pragma once
#include <memory>
#include <mutex>

namespace aiverse {

// A coherent immutable snapshot shared by one or more threads. The mutex
// synchronizes the pointer publication, not work using the retained snapshot.
// This avoids deprecated atomic shared_ptr free functions and remains portable
// to C++20 Apple libc++ SDKs without atomic<shared_ptr>.
template<class T>
class PublishedSharedSnapshot {
public:
    [[nodiscard]] std::shared_ptr<const T> load() const {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

    void store(std::shared_ptr<const T> next) {
        {
            std::lock_guard lock(mutex_);
            snapshot_.swap(next);
        }
        // Drop the old generation outside the mutex.
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const T> snapshot_;
};

} // namespace aiverse

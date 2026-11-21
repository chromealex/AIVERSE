// Diagnostic only: validate CPU time clocks before attributing wall-time
// differences in the Metal rendering fixture to thread descheduling.
// Compile standalone on macOS: clang++ -std=c++17 -O2 -Wall -Wextra
//   -Werror tools/batching-thread-clock-sanity.cpp -o /tmp/aiverse-clock-sanity
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_info.h>
#endif

static double clockMs(clockid_t clockId) {
    timespec now{};
    if (clock_gettime(clockId, &now) != 0) return -1.0;
    return double(now.tv_sec) * 1000.0 + double(now.tv_nsec) * 1e-6;
}

#if defined(__APPLE__)
static double machThreadMs() {
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    const thread_t thread = mach_thread_self();
    const kern_return_t result =
        thread_info(thread, THREAD_BASIC_INFO,
                    reinterpret_cast<thread_info_t>(&info), &count);
    mach_port_deallocate(mach_task_self(), thread);
    if (result != KERN_SUCCESS) return -1.0;
    return 1000.0 * double(info.user_time.seconds + info.system_time.seconds)
        + 1e-3 * double(info.user_time.microseconds + info.system_time.microseconds);
}
#endif

int main() {
    using clock = std::chrono::steady_clock;
    const auto startWall = clock::now();
    const double startThread = clockMs(CLOCK_THREAD_CPUTIME_ID);
    const double startProcess = clockMs(CLOCK_PROCESS_CPUTIME_ID);
#if defined(__APPLE__)
    const double startMach = machThreadMs();
#endif
    // 30ms of deliberately non-blocking CPU arithmetic; no GPU, sleeps,
    // allocations or I/O in the timed loop.
    uint64_t state = 0x123456789abcdefull;
    uint64_t iterations = 0;
    do {
        for (int k = 0; k < 4096; ++k) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
        }
        ++iterations;
    } while (clock::now() - startWall < std::chrono::milliseconds(30));
    const double endThread = clockMs(CLOCK_THREAD_CPUTIME_ID);
    const double endProcess = clockMs(CLOCK_PROCESS_CPUTIME_ID);
#if defined(__APPLE__)
    const double endMach = machThreadMs();
#endif
    const auto endWall = clock::now();
    const double wall = std::chrono::duration<double, std::milli>(endWall - startWall).count();
    const double thread = endThread - startThread;
    const double process = endProcess - startProcess;
    std::printf("Clock sanity (CPU-bound, no Metal): wall_ms=%.6f "
                "thread_cpu_ms=%.6f process_cpu_ms=%.6f "
                "thread_to_wall=%.6f process_to_wall=%.6f "
                "iterations=%llu checksum=%llu\n",
                wall, thread, process,
                wall > 0 ? thread / wall : 0.0,
                wall > 0 ? process / wall : 0.0,
                static_cast<unsigned long long>(iterations),
                static_cast<unsigned long long>(state));
#if defined(__APPLE__)
    const double mach = endMach - startMach;
    std::printf("Mach THREAD_BASIC_INFO delta_ms=%.6f mach_to_wall=%.6f\n",
                mach, wall > 0 ? mach / wall : 0.0);
#endif
    if (startThread < 0 || endThread < 0 || startProcess < 0 || endProcess < 0) {
        std::fprintf(stderr, "Clock sanity FAIL: CPU clock unavailable\n");
        return 2;
    }
    if (wall < 20.0 || iterations == 0) {
        std::fprintf(stderr, "Clock sanity FAIL: invalid busy loop\n");
        return 3;
    }
    if (thread < wall * 0.5 || thread > wall * 1.2) {
        std::fprintf(stderr,
                     "Clock sanity INCONSISTENT: thread CPU clock differs greatly "
                     "from busy wall clock; Stage 95 offcpu is not trustworthy\n");
        return 4;
    }
    std::puts("Clock sanity PASS: busy-loop thread CPU and wall clocks agree");
    return 0;
}

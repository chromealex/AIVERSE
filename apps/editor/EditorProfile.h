// Per-phase timings of the editor's world tree (docs/editor-world-tree.md § Performance). Always collected
// (a steady_clock read per phase); AIVERSE_EDITOR_PROFILE=1 prints a line every 2 s to stdout:
//   [profile] frames 120 avg 8.10 max 14.2 ms | readWorld 10× avg 0.41 max 0.60 | ...
// previewBuild = the editor thread's part of a preview rebuild (map / rules to JSON), previewWorker = the setup
// on the background thread (not frame time). The Hierarchy's status tooltip shows the same line.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace aiverse::editor {

class EditorProfile {
public:
    enum Phase { ReadWorld, Snapshot, PreviewBuild, PreviewWorker, Hierarchy, Details, WorldUpdate, Count };
    static const char* name(Phase p) {
        static const char* n[] = {"readWorld", "prepareSnapshot", "previewBuild", "previewWorker", "hierarchy", "details", "worldUpdate"};
        return n[p];
    }

    struct Stat { double sum = 0, max = 0; int n = 0; };
    struct Scope {
        EditorProfile& p; Phase ph; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~Scope() { p.add(ph, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
    };

    EditorProfile() { const char* e = std::getenv("AIVERSE_EDITOR_PROFILE"); print_ = e && *e && *e != '0'; }
    Scope scope(Phase p) { return Scope{*this, p}; }
    void add(Phase p, double ms) { auto& s = cur_[p]; s.sum += ms; s.max = std::max(s.max, ms); ++s.n; }
    // Once per frame: total frame ms; rolls the window every 2 s.
    void frame(double ms, double now) {
        frames_.sum += ms; frames_.max = std::max(frames_.max, ms); ++frames_.n;
        if (ms > 16.0) ++spikes_;
        if (now - windowStart_ < 2.0) return;
        for (int i = 0; i < Count; ++i) last_[i] = cur_[i], cur_[i] = {};
        lastFrames_ = frames_; lastSpikes_ = spikes_; frames_ = {}; spikes_ = 0;
        windowStart_ = now;
        if (print_) std::printf("%s\n", line().c_str()), std::fflush(stdout);
    }
    // The last full window, e.g. "frames 120 avg 8.1 max 14.2 (>16 ms: 0) | readWorld 10× avg 0.41 max 0.60 (0.03/frame) | ..."
    std::string line() const {
        char buf[256];
        const int nf = std::max(1, lastFrames_.n);
        std::snprintf(buf, sizeof buf, "[profile] frames %d avg %.2f max %.1f ms (>16 ms: %d)", lastFrames_.n, lastFrames_.sum / nf, lastFrames_.max, lastSpikes_);
        std::string s = buf;
        for (int i = 0; i < Count; ++i) {
            const Stat& st = last_[i];
            if (!st.n) continue;
            std::snprintf(buf, sizeof buf, " | %s %d× avg %.2f max %.2f (%.3f/frame)", name(Phase(i)), st.n, st.sum / st.n, st.max, st.sum / nf);
            s += buf;
        }
        return s;
    }
    const Stat& last(Phase p) const { return last_[p]; }

private:
    Stat cur_[Count], last_[Count], frames_, lastFrames_;
    int spikes_ = 0, lastSpikes_ = 0;
    double windowStart_ = 0;
    bool print_ = false;
};

} // namespace aiverse::editor

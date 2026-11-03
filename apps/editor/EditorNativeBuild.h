// State of the "build the editor with the project's C++ modules" banner (EditorNativeBuild.cpp).
#pragma once
#include <string>
#include <vector>

#include "core/NativeBuildOps.h"

namespace rts::editor {

struct NativeBuildUi {
    enum class Stage { Hidden, Offer, Building, Done, Failed } stage = Stage::Hidden;
    std::vector<std::string> missing, compiled;
    NativeTools tools;
    NativeBuildPlan plan;
    NativeBuilt existing;            // the project's built editor, if any
    bool ready = false;              // up to date and has the modules (nativeBuildReady): switch to it
    std::string why;                 // why the existing build can't be used
    bool userStarted = false;        // the user asked for the build here: restart into it when it succeeds
    std::string projectDir;          // what the state was computed for
    int seenRuns = -1;
    bool cmakeInstalling = false;
    bool restartPending = false, restartAsked = false;
};

} // namespace rts::editor

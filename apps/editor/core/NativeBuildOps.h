// Per-project editor build over the Workspace runner (NativeBuild.h): toolchain lookup, start, status;
// MCP ops native_status / native_build. The editor GUI (EditorNativeBuild.cpp) uses the same functions.
#pragma once
#include "NativeBuild.h"
#include "Workspace.h"

namespace rts::editor {

constexpr const char* kNativeBuildLabel = "native-editor";   // Workspace::runLabel() of the build

// cmake / ninja for this host: rtsbuild --check-toolchain <host> (Homebrew, ~/.rtsengine/toolchains, ...),
// else PATH + the usual folders. cmake "" = not found (`problem` says why).
struct NativeTools { std::string cmake, ninja, problem; bool cmakeAutoInstall = false; };
NativeTools findNativeTools(const Workspace& ws);

// Plan for the open project with the found tools.
NativeBuildPlan nativePlanFor(const Workspace& ws, const NativeTools& tools);
// Checks the build dir, writes its .gitignore and starts the build in the Workspace runner.
// Returns "" when started, else a readable error (already written to the Console and editor.log).
std::string startNativeBuild(Workspace& ws, const NativeBuildPlan& plan);
// The project's built editor, whichever generator made it: editor binary ("" = none) and what restart launches.
struct NativeBuilt { std::string editor, launch, headless; };
NativeBuilt findNativeBuilt(const Workspace& ws);
// Up to date (NativeBuild.h) and holds every module the rules list: the editor may switch to it unasked.
bool nativeBuildReady(const Workspace& ws, const NativeBuilt& b, const std::string& engineExe, std::string* why);
// {required, compiled, missing, buildDir, editor, headless, built, upToDate, ready, why, building}
Json nativeStatus(Workspace& ws, const std::string& engineExe);

// engineExe: the binary whose mtime marks the engine version for the up-to-date check ("" = rtsbuild).
void registerNativeBuildOps(Workspace& ws, const std::string& engineExe = "");

} // namespace rts::editor

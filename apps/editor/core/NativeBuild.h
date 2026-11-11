// Per-project editor builds with the project's C++ modules (docs/native-modules.md § Редактор с
// модулями проекта). A project lists modules in rules.json "nativeModules"; a binary contains only
// what it was compiled with (compiledNativeModules()). When the running editor lacks some, it can
// configure + build the engine for that project into <project>/Builds/.temp/editor-<host> (nativeBuildDir) and restart
// there. Pure helpers here (no Workspace, no UI): the editor (EditorNativeBuild.cpp), the MCP ops
// (NativeBuildOps.cpp) and tests use them.
//
// The per-project build never touches the engine checkout: the build dir lives in the project's
// Builds/ (git-ignored at any depth by the engine's .gitignore, and by a .gitignore "*" written into
// the build dir itself for projects in their own repos), the native registry is generated into the
// build dir, and the result is never registered as the .rtsproj handler (isProjectBuildExe).
#pragma once
#include <string>
#include <vector>

#include "engine/data/Json.h"

namespace aiverse::editor {

std::string nativeHost();                                    // "linux" | "macos" | "windows"

// ids from rules["nativeModules"] (in order, duplicates kept once)
std::vector<std::string> requiredNativeModules(const Json& rules);
// ids of compiledNativeModules() of this binary
std::vector<std::string> compiledNativeIds();
// required ids that are not in `compiled`
std::vector<std::string> missingNativeModules(const Json& rules, const std::vector<std::string>& compiled);

struct NativeBuildPlan {
    std::string host, engineDir, projectDir, buildDir;
    std::string cmake;                          // executable (absolute when found by the toolchain check)
    std::vector<std::string> configureArgs;     // after the cmake executable
    std::vector<std::string> buildArgs;
    std::string editorBinary;                   // the executable that the build produces
    std::string launchTarget;                   // what restart launches: the .app bundle on macOS, else editorBinary
    std::string headlessBinary;
};

// <project>/Builds/.temp: hidden intermediates of every build of the project (aiversebuild's _build, _pack, ...
// and the per-project editor); Builds/ itself holds only final builds.
std::string projectBuildsTempDir(const std::string& projectDir);
// <project>/Builds/.temp/editor-<host>: the whole CMake tree of the per-project editor
std::string nativeBuildDir(const std::string& projectDir, const std::string& host);
// ninja: path of ninja ("" = cmake's default generator); cacheExists: the build dir was configured
// before (its generator is kept: passing another -G would fail); jobs <= 0: cmake's default.
NativeBuildPlan planNativeBuild(const std::string& engineDir, const std::string& projectDir, const std::string& host,
                                const std::string& cmake, const std::string& ninja, bool cacheExists, int jobs);
// One shell line: configure && build (stdout + stderr merged by the runner).
std::string nativeBuildCommand(const NativeBuildPlan& plan);
// Parallel jobs for the build: AIVERSE_NATIVE_BUILD_JOBS, else half the cores (linking the editor is memory hungry).
int nativeBuildJobs();

// The build dir must not end up in the engine repository: outside the engine tree, or inside it
// below a "Builds" folder (.gitignore: Builds/) and never in the engine's own build/ dirs.
// Returns "" when fine, else why not.
std::string nativeBuildDirProblem(const std::string& engineDir, const std::string& buildDir);
// git check-ignore of the build dir when it is inside a git work tree; "" = ignored / not in a repo / no git.
std::string nativeBuildGitProblem(const std::string& buildDir);

// Built binary newer than every file of <project>/native and than `engineExe` (the running editor:
// a newer engine means a rebuild). `why` gets the reason when it is not up to date.
bool nativeBuildUpToDate(const std::string& binary, const std::string& projectDir, const std::string& engineExe, std::string* why = nullptr);

// Module ids the build in `buildDir` was configured with (its generated registry gen/RtsNativeRegistry.cpp;
// empty when not configured).
std::vector<std::string> nativeBuildModules(const std::string& buildDir);
// The build holds every id of `required` (by its registry); `why` lists what it lacks.
bool nativeBuildHasModules(const std::string& buildDir, const std::vector<std::string>& required, std::string* why = nullptr);

// What the editor does on opening a project (or on Play):
//   None   - this binary has every module the rules list
//   Switch - it lacks some, and the project's build is up to date and has them: restart there without asking
//   Offer  - it lacks some and there is no such build (missing, stale, other modules), or this editor was itself
//            started by a switch (nativeChild: never switch again, no loops): offer to build
enum class NativeOpenAction { None, Switch, Offer };
NativeOpenAction nativeOpenAction(bool modulesMissing, bool buildReady, bool nativeChild);

// Shell line that starts the built editor detached on `projectArg` (.rtsproj or folder). toolsDir:
// where aiversebuild / aiversemcp / AiverseGame of the regular build are (the project build has only the editor).
// The child gets --native-editor (and AIVERSE_NATIVE_EDITOR=1 where the shell can set it): it never switches again.
std::string nativeRestartCommand(const std::string& host, const std::string& launchTarget, const std::string& projectArg, const std::string& toolsDir);

// The executable is a per-project build (<...>/Builds/.temp/editor-*/...): it must not take over the .rtsproj
// file association of the regular editor.
bool isProjectBuildExe(const std::string& exe);

} // namespace aiverse::editor

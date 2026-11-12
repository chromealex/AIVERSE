// Project file `<Title>.aiverse` at the root of a project folder: the file the user double-clicks
// to open the project in the editor (like Unity's / Godot's project files). game.json stays the
// runtime manifest (client, aiversebuild, packs read it); the project file only marks the folder as a
// project and carries editor-side facts. Projects made before the AIVERSE rename carry the same file
// as `<Title>.rtsproj`: it is still found, opened and rewritten in place (never renamed or duplicated).
// Format (JSON, comments allowed, identical for both extensions):
//
//   {"format": 1, "name": "My RTS", "engine": "0.2.0", "manifest": "game.json", "created": "2026-10-28"}
//
// Unknown keys are kept when the editor rewrites the file. aiversebuild never packs *.aiverse / *.rtsproj.
#pragma once
#include <string>

namespace aiverse::editor {

inline constexpr const char* kProjectExt = ".aiverse";        // new projects
inline constexpr const char* kLegacyProjectExt = ".rtsproj";  // read and kept for projects made before the rename
inline constexpr int kProjectFormat = 1;

struct ProjectFile {
    int format = kProjectFormat;
    std::string name;
    std::string engine;                  // engine version that created / last migrated the project
    std::string manifest = "game.json";  // runtime manifest, relative to the project folder
    std::string created;                 // YYYY-MM-DD (UTC)
};

std::string engineVersion();             // "0.2.0" (CMake project version)
bool isProjectFilePath(const std::string& path);         // extension .aiverse or legacy .rtsproj (any case)
bool isLegacyProjectFilePath(const std::string& path);   // extension .rtsproj (any case)

// `<dir>/<name>.aiverse` (first one by name), else a legacy `<dir>/<name>.rtsproj`, or "" when the
// folder has neither.
std::string findProjectFile(const std::string& dir);
bool readProjectFile(const std::string& path, ProjectFile& out, std::string* err);
bool writeProjectFile(const std::string& path, const ProjectFile& pf, std::string* err);
// File name for a project title: "Моя стратегия" -> "Моя стратегия.aiverse".
std::string projectFileName(const std::string& title);

// Anything the user may hand over as "a project": a .aiverse / .rtsproj file, a project folder, or its
// game.json. Returns the absolute project folder (with the manifest inside), or "" + a readable error.
std::string resolveProjectDir(const std::string& path, std::string* err);

// Makes sure the project folder has a project file (migration of projects made before it existed):
// writes `<name from game.json>.aiverse` when there is neither a .aiverse nor a legacy .rtsproj. Returns its path ("" if it can't be written,
// e.g. a read-only folder); `created` tells whether a new file was written.
std::string ensureProjectFile(const std::string& dir, bool* created = nullptr);

} // namespace aiverse::editor

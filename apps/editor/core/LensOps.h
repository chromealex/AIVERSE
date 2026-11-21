// LENS cameras.json authoring helpers shared by the MCP ops (LensOps.cpp) and
// the native LENS / Cameras editor window. One safe read / validate / write path:
// symlink refusal, 1 MiB bound, native loadCameraScene validation, optimistic
// concurrency by file revision, staged write + rename that never deletes the
// last-good file.
#pragma once
#include "engine/data/Json.h"
#include <string>

namespace aiverse::editor::lens {

// Revision of a missing cameras.json (an empty v1 scene is implied).
inline constexpr const char* kMissingRevision = "missing";

struct SceneFile {
    Json scene;            // parsed asset, or the empty v1 scene when missing
    std::string revision;  // opaque: FNV-1a 64 of the file bytes (hex), or kMissingRevision
    bool exists = false;
};

// Throws std::runtime_error on symlink / non-regular / oversized / malformed files.
SceneFile readSceneFile(const std::string& projectDir);

// Native validation (scratch World). {"ok", "error"?, "normalizedScene", "count", "clientCompatible"}.
Json validateScene(const Json& scene);

// Value comparison that ignores object member order and number spelling
// (1 == 1.0 == 1.000), so a scene that went through another JSON library
// still matches the file it was read from.
bool sameScene(const Json& a, const Json& b);

struct SaveResult {
    bool ok = false;
    bool conflict = false;   // the file changed since expectedRevision
    std::string error;
    std::string revision;    // new revision after a successful write
    Json validation;         // validateScene() result
};
// Validates and writes cameras.json. A non-empty expectedRevision must equal the
// current file revision, otherwise nothing is written (conflict=true). An empty
// expectedRevision skips the concurrency check (explicit overwrite).
SaveResult saveScene(const std::string& projectDir, const Json& scene,
                     const std::string& expectedRevision);

} // namespace aiverse::editor::lens

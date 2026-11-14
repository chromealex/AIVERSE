// Game data documents (rules.json) assembled from several files and with inheritance between entries
// (docs/game-format.md § Splitting rules.json). Everything here runs while loading, never per tick.
//
// include: the root file (and any included file) may list more files, relative to itself:
//     "include": ["rules/units.json", "rules/abilities.json"]
//   Top-level objects merge key by key, top-level arrays append, scalars may be set once. A key or an
//   array entry "id" defined twice is an error naming both files. Includes are read in order, depth first.
// extends: an entry of a top-level collection (an object of objects such as "types", or an array of
//   objects with "id" such as "playerAbilities") may say "extends": "base" (or ["a", "b"]): it starts as a
//   deep copy of those entries of the same collection (objects merge key by key, other values replace) and
//   its own keys go on top. Entries with "abstract": true are bases only and are removed.
#pragma once
#include "Json.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aiverse {

// Reads one file by the path it was asked for (relative paths are joined to the including file's folder).
using JsonReadText = std::function<bool(const std::string& path, std::string& out)>;
JsonReadText fileReader();   // std::ifstream on the path as given

// Where each piece of a composed document came from, so an editor can write changes back to the same files.
struct JsonOrigin {
    std::vector<std::string> files;               // root first, then includes in reading order (paths as read)
    std::map<std::string, size_t> where;          // piece key (see JsonCompose.cpp) -> index into files
    std::map<size_t, Json> includes;              // each file's own "include" list
    std::map<size_t, std::string> text;           // what each file held when read (unchanged files are not rewritten)
};

// The root file with its includes merged. Throws JsonError with the file names involved.
Json composeJson(const std::string& path, const JsonReadText& read, JsonOrigin* origin = nullptr);
// Only the list of files a composed document is made of (root first): hash them all for "same data" checks.
std::vector<std::string> composedFiles(const std::string& path, const JsonReadText& read);

// "extends" / "abstract" in any top-level collection?
bool needsExtends(const Json& doc);
// A copy with every "extends" applied and abstract entries removed. Throws JsonError (unknown base, cycle).
Json resolveExtends(const Json& doc);
// composeJson + resolveExtends: what every reader that is not an editor wants.
Json readComposedJson(const std::string& path, const JsonReadText& read);

// Splits an edited composed document back into its files: (path, content) for each file in origin.files,
// with its "include" list. New entries go to the file holding the previous entry of their collection.
std::vector<std::pair<std::string, Json>> splitComposed(const Json& doc, const JsonOrigin& origin);

} // namespace aiverse

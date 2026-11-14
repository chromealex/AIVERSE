// rules_lint and the parser-derived part of rules_schema (see SchemaOps.cpp).
#pragma once
#include "Workspace.h"

namespace aiverse::editor {
void registerSchemaOps(Workspace& ws);   // after the core ops: wraps rules_set / rules_merge / map_validate
void registerPruneOps(Workspace& ws);    // rules_prune (PruneOps.cpp)
// Keys of rules.json / the map that the game loader never reads (typos, misplaced blocks).
Json lintGameData(const Json& rules, const Json& map, std::string* loadError);
}

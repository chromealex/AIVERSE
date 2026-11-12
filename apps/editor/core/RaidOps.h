// Extraction raids in the editor (docs/raid.md): the block lives in rules.json "raid" (a map adds / overrides
// points and objectives in its own "raid" block; MapDoc keeps it verbatim in `extra`). MCP ops: raid_info,
// raid_set, raid_point, raid_run. The Raid window (windows/RaidWindow.cpp) calls the same functions.
#pragma once
#include "Workspace.h"

namespace aiverse::editor {

void registerRaidOps(Workspace& ws);

// Merged config of the open map (rules + map "raid") as JSON for tools; throws readable errors.
Json raidInfo(const Workspace& ws);
// Bot raid on the open map (headless, deterministic): outcome, receipts, phase timeline.
Json raidRun(const Workspace& ws, uint64_t seed, double seconds);

} // namespace aiverse::editor

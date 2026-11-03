// MCP / editor operations of the game wizard: game_make_options, game_make_plan, game_make (WizardOps.cpp).
#pragma once
#include <string>
#include "Workspace.h"

namespace rts::editor {
void registerWizardOps(Workspace& ws);
std::string wizardStagingDir();   // ~/.rtsengine/wizard/<time>: where an art link is downloaded
}

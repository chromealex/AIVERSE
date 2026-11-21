// VIVID Human Creator operations of the editor core: GPU-free authoring of
// digital humans saved as <project>/humans/<name>.json (the same documents
// the Studio "Human Creator" window loads, edits and previews). Works in
// aiversemcp without an editor. docs/vivid.md § Studio Human Creator.
#pragma once
#include "Workspace.h"

namespace aiverse::editor {

void registerHumanCreatorOps(Workspace& ws);

} // namespace aiverse::editor

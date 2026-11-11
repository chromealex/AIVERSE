// Project hub helpers shared by its pages (EditorProjects.cpp, EditorGameWizard.cpp).
#pragma once
#include <cstddef>
#include <string>

namespace aiverse::editor {
// Folder browser as a modal: true when a folder was chosen (in `out`). needProject: pick a project (.rtsproj / game.json).
bool hubFolderBrowser(bool& open, std::string& dir, char* newFolder, size_t newFolderSize, bool needProject, std::string& out);
}

#pragma once
#include "core/Workspace.h"
#include "engine/i18n/Localization.h"
#include "engine/platform/Platform.h"
namespace aiverse::editor {
inline std::string genreText(const Workspace& ws,const char* key) {
    static i18n::Localization localization;static std::string root;
    if(root!=ws.engineDir){localization.load([](const auto& path,auto& text){return platform::readContentFile(path,text);},"ru",{ws.engineDir+"/engine/assets/lang"});root=ws.engineDir;}
    return localization.tr(key);
}
}

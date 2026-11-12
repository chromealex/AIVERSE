#pragma once
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/i18n/Localization.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
namespace aiverse::editor {
struct ModelingChild {
    ModelingChild(const char *id, ImVec2 size, ImGuiChildFlags flags = 0) { ImGui::BeginChild(id, size, flags); }
    ~ModelingChild() { ImGui::EndChild(); }
};
struct ModelingLabels {
    i18n::Localization loc;
    bool loaded = false;
    void load(Editor &ed) {
        if (loaded)
            return;
        loaded = true;
        const char *configured = std::getenv("AIVERSE_LANG");
        std::string lang = configured && *configured ? configured : "ru";
        loc.load(
            [&](const std::string &p, std::string &out) {
                std::ifstream f(ed.ws().engineDir + "/engine/assets/" + p);
                if (!f)
                    return false;
                std::ostringstream s;
                s << f.rdbuf();
                out = s.str();
                return true;
            },
            lang, {"lang"});
    }
    std::string text(const char *key) { return loc.tr(std::string("modeling.") + key); }
};
inline Json modelParams(const std::string &name, int64_t revision = -1) {
    auto p = Json::object();
    p.set("name", Json::string(name));
    if (revision >= 0)
        p.set("expectedRevision", Json::number(revision));
    return p;
}
inline Json putModelItem(const std::string &collection, const std::string &id, const Json &value) {
    auto command = Json::object();
    command.set("op", Json::string("put"));
    command.set("collection", Json::string(collection));
    command.set("id", Json::string(id));
    command.set("value", value);
    auto commands = Json::array();
    commands.push(command);
    return commands;
}
} // namespace aiverse::editor

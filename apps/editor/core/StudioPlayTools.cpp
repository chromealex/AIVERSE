#include "StudioPlayTools.h"
#include <algorithm>
#include <cmath>

namespace aiverse::editor {

void registerStudioPlayTools(Workspace& workspace, const StudioPlayHandler& handler) {
    auto add = [&](const char* name, const char* method, const char* description, const char* inputSchema) {
        const auto schema = Json::parse(inputSchema);
        workspace.registerOp({name, description, schema,
            [handler, method, schema](const Json& args) {
                if (!handler) throw std::runtime_error("This operation requires a connected local Studio");
                for (const auto& key : schema["required"].items())
                    if (!args.has(key.str())) throw std::invalid_argument("Missing argument: " + key.str());
                if (args.getStr("expectedProject").empty()) throw std::invalid_argument("Expected a project path");
                for (const auto& [key, value] : args.members()) {
                    const auto& property = schema["properties"][key];
                    const auto type = property.getStr("type");
                    if (type.empty() || (type == "string" && !value.isString()) ||
                        (type == "boolean" && value.type() != Json::Type::Bool) ||
                        (type == "integer" && (!value.isNumber() || std::floor(value.toDouble()) != value.toDouble() ||
                          value.toDouble() < property["minimum"].toDouble() || value.toDouble() > property["maximum"].toDouble())))
                        throw std::invalid_argument("Invalid argument: " + key);
                    if (property.has("enum")) {
                        const auto& allowed = property["enum"].items();
                        if (std::find(allowed.begin(), allowed.end(), value) == allowed.end())
                            throw std::invalid_argument("Unsupported value for " + key);
                    }
                }
                return handler(method, args);
            }, true});
    };
    add("studio_runtime_entities", "studio.gameEntities",
        "List a page of entities from the local Studio match.",
        R"({"type":"object","properties":{"expectedProject":{"type":"string"},"offset":{"type":"integer","minimum":0,"maximum":10000000},"limit":{"type":"integer","minimum":1,"maximum":100},"query":{"type":"string"}},"required":["expectedProject","offset","limit","query"],"additionalProperties":false})");
    add("studio_runtime_inspect", "studio.gameInspect",
        "Inspect an exact entity reference at its current session epoch.",
        R"({"type":"object","properties":{"expectedProject":{"type":"string"},"epoch":{"type":"string"},"ref":{"type":"string"}},"required":["expectedProject","epoch","ref"],"additionalProperties":false})");
    add("studio_runtime_regions", "studio.gameRegions",
        "Inspect the local match region and cadence snapshot.",
        R"({"type":"object","properties":{"expectedProject":{"type":"string"}},"required":["expectedProject"],"additionalProperties":false})");
    add("studio_runtime_profile", "studio.gameProfile",
        "Inspect, start or stop a bounded capture of local simulation ticks.",
        R"({"type":"object","properties":{"expectedProject":{"type":"string"},"action":{"type":"string","enum":["status","start","stop"]},"steps":{"type":"integer","minimum":1,"maximum":120}},"required":["expectedProject","action"],"additionalProperties":false})");
    add("studio_runtime_edit", "studio.gameEdit",
        "Edit one supported vital in a paused local match using the inspected tick and raw value.",
        R"({"type":"object","properties":{"expectedProject":{"type":"string"},"epoch":{"type":"string"},"ref":{"type":"string"},"expectedTick":{"type":"integer","minimum":0,"maximum":4294967295},"field":{"type":"string","enum":["hp","energy"]},"expectedRaw":{"type":"string"},"value":{"type":"string"}},"required":["expectedProject","epoch","ref","expectedTick","field","expectedRaw","value"],"additionalProperties":false})");
    add("studio_play_review_preview", "studio.previewPlayChanges",
        "Preview selected changes from a paused local saved map and return a one-use token.",
        R"({"type":"object","properties":{"expectedProject":{"type":"string"},"positions":{"type":"boolean"},"amounts":{"type":"boolean"},"removeDead":{"type":"boolean"}},"required":["expectedProject","positions","amounts","removeDead"],"additionalProperties":false})");
    add("studio_play_review_apply", "studio.keepPlayChanges",
        "Apply the exact previously previewed local changes as one undoable map edit.",
        R"({"type":"object","properties":{"expectedProject":{"type":"string"},"token":{"type":"string"}},"required":["expectedProject","token"],"additionalProperties":false})");
}

} // namespace aiverse::editor

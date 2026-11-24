#pragma once
#include "Workspace.h"
#include <filesystem>
#include <stdexcept>

namespace aiverse::editor {
inline Json featureReadinessSnapshot(const Workspace& ws) {
    namespace fs = std::filesystem;
    if (ws.engineDir.empty()) throw std::runtime_error("Engine root is unavailable");
    const auto file = fs::path(ws.engineDir) / "logs" / "feature-readiness.json";
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) throw std::runtime_error("Feature report unavailable; generate it with tools/agents/feature_policy.py");
    if (size > 16u * 1024u * 1024u) throw std::runtime_error("Feature report exceeds 16 MiB");
    Json report = Json::parseFile(file.string());
    if (!report.isObject() || report.getInt("schema") != 1 || report.getStr("sha").size() != 40 ||
        !report["policy"].isObject() || !report["readiness"]["owners"].isArray() ||
        !report["release"]["blockers"].isArray())
        throw std::runtime_error("Invalid feature readiness report");
    report.set("snapshotOnly", Json::fromBool(true));
    report.set("freshness", Json::string("not_verified_by_editor"));
    return report;
}
} // namespace aiverse::editor

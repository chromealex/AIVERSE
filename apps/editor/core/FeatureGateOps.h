#pragma once
#include "Workspace.h"
#include <filesystem>
#include <stdexcept>
#include <algorithm>

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

inline Json featureReadinessQuery(const Workspace& ws, const Json& params) {
    const Json report = featureReadinessSnapshot(ws);
    const std::string section = params.getStr("section", "tasks");
    const std::string owner = params.getStr("owner");
    const std::string feature = params.getStr("feature");
    const std::string kind = params.getStr("kind");
    const int64_t offset = params.getInt("offset", 0), limit = params.getInt("limit", 100);
    if (offset < 0 || limit < 1 || limit > 200)
        throw std::runtime_error("Invalid report pagination");
    Json rows = Json::array();
    int64_t total = 0;
    auto append = [&](Json row) {
        if (total >= offset && total - offset < limit) rows.push(std::move(row));
        ++total;
    };
    if (section == "tasks" || section == "features") {
        for (const auto& bucket : report["readiness"]["owners"].items()) {
            const std::string name = bucket.getStr("owner");
            if (!owner.empty() && name != owner) continue;
            for (const auto& item : bucket[section == "tasks" ? "tasks" : "manifests"].items()) {
                if (!feature.empty() && (section == "tasks" ? item.getStr("feature") : item.getStr("id")) != feature) continue;
                if (!kind.empty() && item.getStr(section == "tasks" ? "kind" : "state") != kind) continue;
                Json row = item;
                row.set("owner", Json::string(name));
                append(std::move(row));
            }
        }
    } else if (section == "dependencies") {
        for (const auto& pair : report["policy"]["dependencies"]["features"].members()) {
            if (!feature.empty() && pair.first != feature) continue;
            Json row = pair.second;
            row.set("id", Json::string(pair.first));
            append(std::move(row));
        }
    } else if (section == "blockers") {
        for (const auto& row : report["release"]["blockers"].items())
            if (feature.empty() || row.getStr("feature") == feature) append(row);
    } else if (section == "schedule") {
        for (const auto& row : report["automation"]["schedule"].items())
            if (owner.empty() || row.getStr("owner") == owner) append(row);
    } else {
        throw std::runtime_error("Unknown readiness query section");
    }
    Json out = Json::object();
    out.set("sha", report["sha"]);
    out.set("snapshotOnly", Json::fromBool(true));
    out.set("freshness", report["freshness"]);
    out.set("section", Json::string(section));
    out.set("total", Json::number(total));
    out.set("offset", Json::number(offset));
    out.set("items", std::move(rows));
    return out;
}
} // namespace aiverse::editor

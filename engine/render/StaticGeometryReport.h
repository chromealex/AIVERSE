#pragma once
#include "GeometryScene.h"

namespace aiverse::render {
inline Json staticGeometryReport(const GeometryScene& scene) {
    const auto& stats = scene.stats();
    Json report = Json::object();
    report.set("enabled", Json::fromBool(scene.strataPortableRenderingEnabled()));
    report.set("instances", Json::number(int64_t(scene.instanceCount())));
    report.set("autoStrataSources", Json::number(int64_t(stats.strataAutoStrataSources)));
    report.set("autoLodSources", Json::number(int64_t(stats.strataAutoLodSources)));
    report.set("cookedSources", Json::number(int64_t(stats.strataCookedSources)));
    report.set("cookStaleSources", Json::number(int64_t(stats.strataCookStaleSources)));
    report.set("portableSources", Json::number(int64_t(stats.strataPortableSources)));
    report.set("portableDeclined", Json::number(int64_t(stats.strataPortableDeclined)));
    report.set("portableDraws", Json::number(int64_t(stats.strataPortableDraws)));
    report.set("portableFallbacks", Json::number(int64_t(stats.strataPortableFallbacks)));
    report.set("portableClusters", Json::number(int64_t(stats.strataPortableClusters)));
    report.set("nodes", Json::number(int64_t(stats.strataNodes)));
    report.set("failures", Json::number(int64_t(stats.strataFailures)));
    report.set("stagedSources", Json::number(int64_t(stats.strataSources)));
    report.set("skippedSources", Json::number(int64_t(stats.strataSkippedSources)));
    report.set("seamUnsafeDeclined", Json::number(int64_t(stats.strataSeamUnsafeDeclined)));
    report.set("globalBudgetDeclined", Json::number(int64_t(stats.strataGlobalBudgetDeclined)));
    report.set("sharedModels", Json::number(int64_t(stats.strataPortableSharedModels)));
    report.set("sharedReuses", Json::number(int64_t(stats.strataPortableSharedReuses)));
    report.set("stageMs", Json::number(double(stats.strataStageMs)));
    report.set("cookWrittenSources", Json::number(double(stats.strataCookWrittenSources)));
    report.set("cookReadMs", Json::number(double(stats.strataCookedLoadMs)));
    report.set("buildMs", Json::number(double(stats.strataBuildMs)));
    report.set("compactUploads", Json::number(int64_t(stats.strataCompactUploads)));
    report.set("compactReuses", Json::number(int64_t(stats.strataCompactReuses)));
    Json warnings = Json::array();
    for (const auto& warning : scene.warnings()) {
        if (warning.find("Strata") != std::string::npos || warning.find("STRATA") != std::string::npos)
            warnings.push(Json::string(warning.substr(0, 400)));
    }
    report.set("warnings", std::move(warnings));
    Json decisions = Json::array();
    for (const auto& line : scene.strataSelectDecisions()) decisions.push(Json::string(line));
    report.set("select", std::move(decisions));
    return report;
}
} // namespace aiverse::render

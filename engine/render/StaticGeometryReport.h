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
    Json decisions = Json::array();
    for (const auto& line : scene.strataSelectDecisions()) decisions.push(Json::string(line));
    report.set("select", std::move(decisions));
    return report;
}
} // namespace aiverse::render

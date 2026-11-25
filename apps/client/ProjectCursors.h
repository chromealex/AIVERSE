#pragma once
#include "engine/data/Json.h"
#include "engine/platform/Platform.h"
#include "engine/render/TextureCache.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace aiverse::client {
class ProjectCursors {
    struct State {
        std::vector<std::string> frames;
        double fps = 8;
        int x = 0, y = 0;
    };
    struct Image { int w = 0, h = 0; std::vector<uint8_t> pixels; };
    std::map<std::string, State> states_;
    std::map<std::string, Image> images_;
    std::string state_, installed_;
    double started_ = 0;
    int installedX_ = -1, installedY_ = -1;

    void restore() {
        if (!installed_.empty()) platform::setCursorImage(nullptr,0,0,0,0);
        installed_.clear();
    }
public:
    bool enabled() const { return !states_.empty(); }
    void clear() { restore(); states_.clear(); images_.clear(); state_.clear(); }
    void load(const Json& config) {
        clear();
        for (const auto& [name, value] : config.members()) {
            State s;
            if (value.isString()) s.frames.push_back(value.str());
            else {
                for (const auto& path : value["frames"].items())
                    if (path.isString()) s.frames.push_back(path.str());
                if (s.frames.empty() && value["image"].isString()) s.frames.push_back(value["image"].str());
                s.fps = std::clamp(value["fps"].toDouble(8), 0., 120.);
                s.x = int(std::clamp(value["hotspot"][0].toDouble(), 0., 255.));
                s.y = int(std::clamp(value["hotspot"][1].toDouble(), 0., 255.));
            }
            if (!s.frames.empty()) states_.emplace(name, std::move(s));
        }
    }
    void show(const std::string& requested, double now) {
        if (requested.empty()) { restore(); state_.clear(); return; }
        auto found = states_.find(requested);
        if (found == states_.end()) found = states_.find("normal");
        if (found == states_.end()) { restore(); state_.clear(); return; }
        if (state_ != found->first) { state_ = found->first; started_ = now; }
        const auto& s = found->second;
        const auto frame = size_t(std::fmod(std::max(0.,now-started_)*s.fps, double(s.frames.size())));
        const auto& path = s.frames[frame];
        auto [it, fresh] = images_.try_emplace(path);
        auto& image = it->second;
        if (fresh) {
            std::string bytes;
            if (!platform::readContentFile(path,bytes) || !render::decodeImage(bytes,image.w,image.h,image.pixels)
                || image.w < 1 || image.h < 1 || image.w > 256 || image.h > 256) {
                image.pixels.clear();
                std::fprintf(stderr, "[cursor] cannot load a 1..256 pixel cursor from %s\n", path.c_str());
            }
        }
        if (image.pixels.empty()) { restore(); return; }
        const int x = std::clamp(s.x,0,image.w-1), y = std::clamp(s.y,0,image.h-1);
        if (installed_ == path && installedX_ == x && installedY_ == y) return;
        if (!platform::setCursorImage(image.pixels.data(),image.w,image.h,x,y)) { restore(); return; }
        installed_ = path; installedX_ = x; installedY_ = y;
    }
};
} // namespace aiverse::client

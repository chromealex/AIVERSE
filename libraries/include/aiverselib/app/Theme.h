#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace aiverse::lib::app {
struct Insets { int left=0, top=0, right=0, bottom=0; };
struct Layout { int x=0, y=0, width=0, height=0, columns=1; };
struct LayoutConfig { int minimumColumn=280, gap=12, maximumColumns=4; float minimumScale=0.75f, maximumScale=2.0f; };
inline std::optional<Layout> layout(int width, int height, Insets safe, LayoutConfig c = {}) {
    if (width <= 0 || height <= 0 || safe.left < 0 || safe.right < 0 || safe.top < 0 || safe.bottom < 0 || c.minimumColumn <= 0 || c.gap < 0 || c.maximumColumns <= 0) return std::nullopt;
    const auto w = static_cast<long long>(width) - safe.left - safe.right;
    const auto h = static_cast<long long>(height) - safe.top - safe.bottom;
    if (w <= 0 || h <= 0) return std::nullopt;
    auto columns = std::clamp((w + c.gap) / (static_cast<long long>(c.minimumColumn) + c.gap), 1LL, static_cast<long long>(c.maximumColumns));
    return Layout{safe.left, safe.top, static_cast<int>(w), static_cast<int>(h), static_cast<int>(columns)};
}
inline std::optional<float> uiScale(float requested, LayoutConfig c = {}) {
    if (!std::isfinite(requested) || !std::isfinite(c.minimumScale) || !std::isfinite(c.maximumScale) || c.minimumScale <= 0 || c.maximumScale < c.minimumScale) return std::nullopt;
    return std::clamp(requested, c.minimumScale, c.maximumScale);
}
struct Theme { std::string background="#151c28", foreground="#eeeeee", accent="#64b5ff"; int spacing=12, fontSize=16; };
inline bool colorToken(const std::string& s) {
    return s.size()==7 && s[0]=='#' && std::all_of(s.begin()+1,s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});
}
inline std::optional<std::string> themeStyles(const Theme& t) {
    if (!colorToken(t.background)||!colorToken(t.foreground)||!colorToken(t.accent)||t.spacing<0||t.spacing>128||t.fontSize<8||t.fontSize>96) return std::nullopt;
    return "body { background-color: "+t.background+"; color: "+t.foreground+"; font-size: "+std::to_string(t.fontSize)+"dp; } .screen { padding: "+std::to_string(t.spacing)+"dp; } .row { display: block; margin-bottom: "+std::to_string(t.spacing)+"dp; } button { background-color: "+t.accent+"; }";
}
}

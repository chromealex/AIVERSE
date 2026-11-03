// Small ImGui helpers shared by editor windows: string inputs, help marks, and the editor's toolbar
// widgets (icon tool buttons, segmented groups, toolbar strips, empty states). Colours come from the
// current ImGui style, which EditorTheme writes, so they follow the dark / light theme and the accent.
#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include "imgui.h"
#include "imgui_internal.h" // MenuItemEx: icons in menus
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include "EditorIcons.h"
#include "EditorTheme.h"

namespace rts::editor::ui {

inline int stringResize(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(d->UserData);
        s->resize(size_t(d->BufTextLen));
        d->Buf = s->data();
    }
    return 0;
}
inline bool inputString(const char* label, std::string& s, ImGuiInputTextFlags f = 0) {
    return ImGui::InputText(label, s.data(), s.capacity() + 1, f | ImGuiInputTextFlags_CallbackResize, stringResize, &s);
}
inline bool inputMultiline(const char* label, std::string& s, ImVec2 size) {
    return ImGui::InputTextMultiline(label, s.data(), s.capacity() + 1, size, ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput, stringResize, &s);
}
inline void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30); ImGui::TextUnformatted(text); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
}

inline ImVec4 accentColor() { return ImGui::GetStyle().Colors[ImGuiCol_CheckMark]; } // the theme's accent
inline ImU32 rgbaU32(ImVec4 c, float a) { c.w *= a; return ImGui::GetColorU32(c); }

// The semibold heading font loaded by EditorTheme (falls back to the current font).
inline ImFont* headingFont() {
    for (ImFont* f : ImGui::GetIO().Fonts->Fonts)
        if (std::strncmp(f->GetDebugName(), "Inter SemiBold", 14) == 0) return f;
    return ImGui::GetFont();
}
// A section title in the heading font (bigger and semibold than SeparatorText).
inline void heading(const char* text) {
    ImGui::PushFont(headingFont(), 0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// Square icon button for toolbars. active = the current tool / mode (filled with the accent);
// the tooltip (Russian label) shows on hover, also when disabled.
inline bool toolButton(const char* icon, const char* tooltip, bool active = false, bool enabled = true) {
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(icon);
    ImGui::PushID(tooltip ? tooltip : "");
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton("##tool", ImVec2(h, h));
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (tooltip && *tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + h, p.y + h);
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec4 a = accentColor();
    if (active) dl->AddRectFilled(p, q, rgbaU32(a, held ? 0.8f : hovered ? 0.9f : 1.0f), r);
    else if (hovered && enabled) dl->AddRectFilled(p, q, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), r);
    const ImVec2 ts = ImGui::CalcTextSize(icon);
    const ImU32 tc = active ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled, hovered ? 1.0f : 0.8f);
    dl->AddText(ImVec2(p.x + (h - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), tc, icon);
    return pressed && enabled;
}

// Segmented group of tool buttons on one raised background: beginSegment(); toolButton(); SameLine();
// toolButton(); endSegment(). Not nestable (uses draw list channels).
inline void beginSegment() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
}
inline void endSegment() {
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    a.x -= 2; a.y -= 2; b.x += 2; b.y += 2;
    const float r = ImGui::GetStyle().FrameRounding + 2;
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg), r);
    if (ImGui::GetStyle().FrameBorderSize > 0) dl->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border), r);
    dl->ChannelsMerge();
}
// Thin vertical divider between toolbar groups.
inline void toolbarDivider() {
    ImGui::SameLine(0, 8);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 4), ImVec2(p.x, p.y + h - 4), ImGui::GetColorU32(ImGuiCol_Separator), 1);
    ImGui::Dummy(ImVec2(1, h));
    ImGui::SameLine(0, 8);
}

// Toolbar strip across the top of a window drawn with zeroPadding(): background + bottom border.
//   float y = ui::beginToolbar(); ...buttons on one line... ui::endToolbar(y);
inline float beginToolbar(float pad = 5) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() + pad * 2 + 2;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_WindowBg));
    dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + w, p.y + h - 0.5f), ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad + 3, p.y + pad + 2)); // + room for the segment backgrounds
    return p.y + h;
}
inline void endToolbar(float bottom) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, bottom));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, bottom));
}

// Button with an icon and a label; primary = filled with the accent (the main action of a window).
inline bool button(const char* icon, const char* label, bool primary = false, ImVec2 size = ImVec2(0, 0)) {
    std::string text = icon && *icon ? std::string(icon) + "  " + label : std::string(label);
    if (primary) {
        const ImVec4 a = accentColor();
        ImGui::PushStyleColor(ImGuiCol_Button, a);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.x * 1.1f + 0.04f, a.y * 1.1f + 0.04f, a.z * 1.1f + 0.04f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x * 0.85f, a.y * 0.85f, a.z * 0.85f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    }
    bool r = ImGui::Button(text.c_str(), size);
    if (primary) ImGui::PopStyleColor(5);
    return r;
}

// Empty-state message centred in [a, b]: a large dim icon, a title and a hint line.
inline void emptyState(ImVec2 a, ImVec2 b, const char* icon, const char* title, const char* hint = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetStyle().FontSizeBase;
    ImFont* hf = headingFont();
    const float iconSize = fs * 2.6f, titleSize = fs * 1.1f;
    const ImVec2 is = hf->CalcTextSizeA(iconSize, 1e9f, 0, icon);
    const ImVec2 ts = hf->CalcTextSizeA(titleSize, 1e9f, 0, title);
    const float wrap = std::max(120.0f, std::min(b.x - a.x - 40, fs * 30));
    const ImVec2 hs = hint ? ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), 1e9f, wrap, hint) : ImVec2(0, 0);
    const float total = is.y + 10 + ts.y + (hint ? 6 + hs.y : 0);
    float y = a.y + (b.y - a.y - total) * 0.5f;
    const float cx = (a.x + b.x) * 0.5f;
    dl->AddText(hf, iconSize, ImVec2(cx - is.x * 0.5f, y), rgbaU32(accentColor(), 0.75f), icon);
    y += is.y + 10;
    dl->AddText(hf, titleSize, ImVec2(cx - ts.x * 0.5f, y), ImGui::GetColorU32(ImGuiCol_Text), title);
    y += ts.y + 6;
    if (hint) dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(cx - hs.x * 0.5f, y), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint, nullptr, wrap);
}
// The same in the rest of the current window (reserves the space).
inline void emptyStateHere(const char* icon, const char* title, const char* hint = nullptr, float height = 0) {
    const ImVec2 p = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    const float h = height > 0 ? height : std::max(avail.y, 160.0f);
    emptyState(p, ImVec2(p.x + avail.x, p.y + h), icon, title, hint);
    ImGui::Dummy(ImVec2(avail.x, h));
}


// ============================================================ shared look (UX pass)
// Semantic colours of the current theme: use these instead of hard-coded RGB so both themes read well.
inline const EditorTheme::Palette& pal() { return EditorTheme::current(); }
enum class Tone { Normal, Dim, Accent, Ok, Warn, Error };
inline ImVec4 toneColor(Tone t) {
    const auto& p = pal();
    switch (t) {
        case Tone::Dim: return p.textDim;
        case Tone::Accent: return p.accent;
        case Tone::Ok: return p.success;
        case Tone::Warn: return p.warning;
        case Tone::Error: return p.danger;
        default: return p.text;
    }
}
inline const char* toneIcon(Tone t) {
    switch (t) {
        case Tone::Ok: return ICON_CIRCLE_CHECK;
        case Tone::Warn: return ICON_WARNING;
        case Tone::Error: return ICON_ALERT;
        default: return ICON_INFO;
    }
}
// Text in a tone colour (no wrapping), printf-style.
inline void toneText(Tone t, const char* fmt, ...) {
    char buf[1024];
    va_list a; va_start(a, fmt); std::vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    ImGui::PushStyleColor(ImGuiCol_Text, toneColor(t));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
}
// A guess of the tone from a result message ("Ошибка…", "Сохранено…").
inline Tone toneOf(const std::string& m) {
    for (const char* k : {"Ошибка", "ошибка", "Нельзя", "Не удалось", "не удалось", "JSON:", "error"}) if (m.find(k) != std::string::npos) return Tone::Error;
    for (const char* k : {"Внимание", "предупрежд", "Сначала"}) if (m.find(k) != std::string::npos) return Tone::Warn;
    return Tone::Ok;
}
// Callout: a tinted rounded box with an icon and wrapped text (results, hints, warnings).
inline void callout(Tone t, const char* text) {
    if (!text || !*text) return;
    const ImVec4 c = t == Tone::Normal || t == Tone::Dim ? pal().textDim : toneColor(t);
    const char* icon = toneIcon(t);
    const float pad = 8, iconW = ImGui::CalcTextSize(icon).x + 8;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(60.0f, ImGui::GetContentRegionAvail().x);
    const float wrap = w - pad * 2 - iconW;
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const float h = ts.y + pad * 2 - 2;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), rgbaU32(c, 0.12f), r);
    dl->AddRectFilled(p, ImVec2(p.x + 3, p.y + h), rgbaU32(c, 0.9f), r, ImDrawFlags_RoundCornersLeft);
    dl->AddText(ImVec2(p.x + pad + 2, p.y + pad - 1), ImGui::GetColorU32(c), icon);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + pad + iconW, p.y + pad - 1), ImGui::GetColorU32(ImGuiCol_Text), text, nullptr, wrap);
    ImGui::Dummy(ImVec2(w, h));
}
inline void callout(Tone t, const std::string& text) { callout(t, text.c_str()); }
// Result message of the last action of a window: tone guessed from the text.
inline void resultMessage(const std::string& m) { if (!m.empty()) callout(toneOf(m), m); }

// Search field across the available width: hint inside, a clear button when not empty.
inline bool searchField(const char* id, std::string& s, const char* hint = "Поиск", float width = -FLT_MIN) {
    ImGui::PushID(id);
    const float clearW = s.empty() ? 0 : ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(width < 0 ? ImGui::GetContentRegionAvail().x - clearW - (clearW > 0 ? ImGui::GetStyle().ItemInnerSpacing.x : 0) : width);
    const std::string h = std::string(ICON_SEARCH "  ") + hint;
    bool ch = ImGui::InputTextWithHint("##search", h.c_str(), s.data(), s.capacity() + 1, ImGuiInputTextFlags_CallbackResize, stringResize, &s);
    if (!s.empty()) {
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button(ICON_X, ImVec2(clearW, 0))) { s.clear(); ch = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Очистить");
    }
    ImGui::PopID();
    return ch;
}
// Case-insensitive (ASCII) substring test for filters.
inline bool contains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto lo = [](unsigned char c) { return char(c < 128 ? std::tolower(c) : c); };
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [&](char a, char b) { return lo(static_cast<unsigned char>(a)) == lo(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

// Property grid: dim labels on the left, full-width widgets on the right, rows aligned.
//   if (ui::beginProps("##p")) { ui::prop("Владелец"); ImGui::Combo("##owner", ...); ...; ui::endProps(); }
// Widgets in the grid need "##" labels (the grid draws the label).
inline bool beginProps(const char* id, float labelWidth = 0) {
    const float avail = ImGui::GetContentRegionAvail().x;
    if (labelWidth <= 0) labelWidth = std::clamp(avail * 0.36f, ImGui::GetFontSize() * 5.0f, ImGui::GetFontSize() * 11.0f);
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}
inline void prop(const char* label, const char* tip = nullptr) {
    if (!ImGui::GetCurrentTable()) { ImGui::SetNextItemWidth(-FLT_MIN); return; }   // the grid was skipped (collapsed window)
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, pal().textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (tip && *tip && ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28); ImGui::TextUnformatted(tip); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}
// A row spanning both columns (checkbox with its own label, buttons, notes).
inline void propWide() {
    if (!ImGui::GetCurrentTable()) return;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
}
inline void endProps() { ImGui::EndTable(); }

// Section title: semibold text with an optional icon and a hairline to the right edge.
inline void section(const char* icon, const char* title) {
    ImGui::Spacing();
    ImGui::PushFont(headingFont(), 0.0f);
    if (icon && *icon) { ImGui::PushStyleColor(ImGuiCol_Text, accentColor()); ImGui::TextUnformatted(icon); ImGui::PopStyleColor(); ImGui::SameLine(); }
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    const ImVec2 a = ImGui::GetItemRectMax();
    const float x1 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float y = (ImGui::GetItemRectMin().y + a.y) * 0.5f;
    if (x1 > a.x + 12) ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x + 10, y), ImVec2(x1, y), ImGui::GetColorU32(ImGuiCol_Separator));
}

// Small rounded label ("3", "изменено", "ошибка") at the cursor, on the same line.
inline void badge(const char* text, ImVec4 c) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + 2, w = ts.x + 12;
    const float y = p.y + (ImGui::GetFrameHeight() - h) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), rgbaU32(c, 0.18f), h * 0.5f);
    dl->AddText(ImVec2(p.x + 6, y + 1), ImGui::GetColorU32(c), text);
    ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
}

// Segmented control of text options filling the width (2..6 choices). Returns true when changed.
inline bool segmented(const char* id, int& value, const char* const* labels, int count, float width = 0) {
    ImGui::PushID(id);
    const float avail = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    const float gap = 2, w = (avail - gap * float(count - 1)) / float(count);
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine();
        const bool on = value == i;
        if (on) {
            const ImVec4 a = accentColor();
            ImGui::PushStyleColor(ImGuiCol_Button, a);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, a);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        ImGui::PushID(i);
        if (ImGui::Button(labels[i], ImVec2(w, 0)) && !on) { value = i; changed = true; }
        ImGui::PopID();
        if (on) ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// Grid of choice tiles (wraps to the window width): like segmented, for 5+ options or long labels.
inline bool choiceGrid(const char* id, int& value, const char* const* labels, int count, float minW = 92) {
    ImGui::PushID(id);
    const float avail = ImGui::GetContentRegionAvail().x, gap = 4;
    const int cols = std::clamp(int((avail + gap) / (minW + gap)), 1, count);
    const float w = (avail - gap * float(cols - 1)) / float(cols);
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    for (int i = 0; i < count; ++i) {
        if (i % cols) ImGui::SameLine();
        const bool on = value == i;
        if (on) {
            const ImVec4 a = accentColor();
            ImGui::PushStyleColor(ImGuiCol_Button, a);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, a);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        ImGui::PushID(i);
        if (ImGui::Button(labels[i], ImVec2(w, 0))) { changed = value != i; value = i; }
        ImGui::PopID();
        if (on) ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

// Header card at the top of a panel: big icon, title, dim subtitle (Inspector, window headers).
inline void headerCard(const char* icon, const std::string& title, const std::string& subtitle = {}, ImVec4 iconColor = ImVec4(0, 0, 0, 0)) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImFont* hf = headingFont();
    const float fs = ImGui::GetFontSize();
    const float iconBox = fs * 2.3f;
    const float h = std::max(iconBox, fs * (subtitle.empty() ? 1.4f : 2.6f)) + 12;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 2;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), r);
    const ImVec4 ic = iconColor.w > 0 ? iconColor : accentColor();
    const ImVec2 ib(p.x + 6, p.y + (h - iconBox) * 0.5f);
    dl->AddRectFilled(ib, ImVec2(ib.x + iconBox, ib.y + iconBox), rgbaU32(ic, 0.18f), r);
    const float isz = fs * 1.3f;
    const ImVec2 is = hf->CalcTextSizeA(isz, 1e9f, 0, icon);
    dl->AddText(hf, isz, ImVec2(ib.x + (iconBox - is.x) * 0.5f, ib.y + (iconBox - is.y) * 0.5f), ImGui::GetColorU32(ic), icon);
    const float tx = ib.x + iconBox + 10, maxW = p.x + w - tx - 6;
    const float th = fs * 1.1f;
    float ty = p.y + (h - (subtitle.empty() ? th : th + fs + 2)) * 0.5f;
    dl->PushClipRect(ImVec2(tx, p.y), ImVec2(tx + maxW, p.y + h), true);
    dl->AddText(hf, th, ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    if (!subtitle.empty()) dl->AddText(ImVec2(tx, ty + th + 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), subtitle.c_str());
    dl->PopClipRect();
    ImGui::Dummy(ImVec2(w, h));
    ImGui::Spacing();
}

// Buttons that split the width evenly on one row: if (ui::wideButton(ICON_TRASH, "Удалить", 0, 2)) ...
inline bool wideButton(const char* icon, const char* label, int index = 0, int count = 1, bool primary = false) {
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    if (index > 0) ImGui::SameLine();
    const float w = (ImGui::GetContentRegionAvail().x - gap * float(count - 1 - index)) / float(count - index);
    return button(icon, label, primary, ImVec2(w, 0));
}

// Dim one-line hint (key bindings, what a click does).
inline void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, pal().textDim);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// Menu item with an icon in the icon column.
inline bool menuItem(const char* icon, const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true) {
    return ImGui::MenuItemEx(label, icon, shortcut, selected, enabled);
}

// Readable text on top of a 3D viewport: a translucent rounded plate behind the text.
inline void viewportLabel(ImDrawList* dl, ImVec2 pos, const char* text, ImVec4 color = ImVec4(1, 1, 1, 0.92f)) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(ImVec2(pos.x - 6, pos.y - 3), ImVec2(pos.x + ts.x + 6, pos.y + ts.y + 3), IM_COL32(12, 13, 16, 170), 5);
    dl->AddText(pos, ImGui::GetColorU32(color), text);
}

} // namespace rts::editor::ui

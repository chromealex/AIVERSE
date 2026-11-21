#pragma once
#include "engine/platform/Platform.h"
#include <string>
#include <utility>

namespace aiverse::studio {
// Shared host key names, independent of Cocoa hardware key codes.
inline platform::Key studioGameKey(const std::string& key) {
    using platform::Key;
    if (key.size()==1) {
        const char c=key[0];
        if (c>='a' && c<='z') return Key(int(Key::A)+c-'a');
        if (c>='A' && c<='Z') return Key(int(Key::A)+c-'A');
        if (c>='0' && c<='9') return Key(int(Key::Num0)+c-'0');
    }
    static const std::pair<const char*,Key> names[]={
        {" ",Key::Space},{"Escape",Key::Escape},{"Enter",Key::Enter},{"Tab",Key::Tab},
        {"Backspace",Key::Backspace},{"Delete",Key::Delete},{"ArrowLeft",Key::Left},
        {"ArrowRight",Key::Right},{"ArrowUp",Key::Up},{"ArrowDown",Key::Down},
        {"Home",Key::Home},{"End",Key::End},{"PageUp",Key::PageUp},{"PageDown",Key::PageDown},
        {"-",Key::Minus},{"=",Key::Equal},{"[",Key::LeftBracket},{"]",Key::RightBracket},
        {";",Key::Semicolon},{"'",Key::Apostrophe},{",",Key::Comma},{".",Key::Period},
        {"/",Key::Slash},{"\\",Key::Backslash},{"`",Key::GraveAccent}
    };
    for (const auto& item:names) if(key==item.first)return item.second;
    return Key::Unknown;
}
}

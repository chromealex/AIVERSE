#pragma once
#include "../Platform.h"
#include <X11/keysym.h>

namespace aiverse::platform {
inline Key linuxKeyFromKeysym(unsigned long k) {
    if (k >= XK_a && k <= XK_z) return Key(int(Key::A) + int(k - XK_a));
    if (k >= XK_A && k <= XK_Z) return Key(int(Key::A) + int(k - XK_A));
    if (k >= XK_0 && k <= XK_9) return Key(int(Key::Num0) + int(k - XK_0));
    if (k >= XK_F1 && k <= XK_F12) return Key(int(Key::F1) + int(k - XK_F1));
    switch (k) {
        case XK_Escape: return Key::Escape; case XK_Return: return Key::Enter; case XK_Tab: return Key::Tab;
        case XK_BackSpace: return Key::Backspace; case XK_Delete: return Key::Delete; case XK_Insert: return Key::Insert;
        case XK_space: return Key::Space; case XK_Left: return Key::Left; case XK_Right: return Key::Right;
        case XK_Up: return Key::Up; case XK_Down: return Key::Down; case XK_Home: return Key::Home; case XK_End: return Key::End;
        case XK_Page_Up: return Key::PageUp; case XK_Page_Down: return Key::PageDown;
        case XK_Shift_L: return Key::LeftShift; case XK_Shift_R: return Key::RightShift;
        case XK_Control_L: return Key::LeftCtrl; case XK_Control_R: return Key::RightCtrl;
        case XK_Alt_L: return Key::LeftAlt; case XK_Alt_R: return Key::RightAlt;
        case XK_Super_L: return Key::LeftSuper; case XK_Super_R: return Key::RightSuper;
        case XK_minus: return Key::Minus; case XK_equal: return Key::Equal; case XK_comma: return Key::Comma;
        case XK_period: return Key::Period; case XK_slash: return Key::Slash; case XK_semicolon: return Key::Semicolon;
        default: return Key::Unknown;
    }
}
}

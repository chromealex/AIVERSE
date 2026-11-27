#pragma once
#include "engine/platform/linux/LinuxKey.h"
#include <set>
#include <gtk/gtk.h>
#include <functional>
#include <utility>

namespace aiverse::studio {
class StudioGamePointer {
public:
    StudioGamePointer(GtkWidget* area, std::function<void(const platform::Event&)> send)
        : area_(area), send_(std::move(send)) {
        gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
            GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
        connection_ = g_signal_connect(area_, "event", G_CALLBACK(+[](GtkWidget*, GdkEvent* event, gpointer data)->gboolean {
            return static_cast<StudioGamePointer*>(data)->handle(event);
        }), this);
    }
    ~StudioGamePointer() { g_signal_handler_disconnect(area_, connection_); }
    StudioGamePointer(const StudioGamePointer&) = delete;
    StudioGamePointer& operator=(const StudioGamePointer&) = delete;
private:
    bool handle(GdkEvent* input) {
        using namespace platform;
        Event event;
        GdkModifierType state{};
        gdk_event_get_state(input, &state);
        if (state & GDK_SHIFT_MASK) event.mods |= kModShift;
        if (state & GDK_CONTROL_MASK) event.mods |= kModCtrl;
        if (state & GDK_MOD1_MASK) event.mods |= kModAlt;
        if (state & GDK_SUPER_MASK) event.mods |= kModSuper;
        switch (input->type) {
        case GDK_KEY_PRESS:
        case GDK_KEY_RELEASE: {
            const bool down = input->type == GDK_KEY_PRESS;
            if (input->key.keyval == GDK_KEY_F4 && (event.mods & kModAlt)) return false;
            const guint code = input->key.hardware_keycode;
            GdkKeymapKey base{code, 0, 0};
            const auto symbol = gdk_keymap_lookup_key(gdk_keymap_get_for_display(gtk_widget_get_display(area_)), &base);
            event.key = linuxKeyFromKeysym(symbol ? symbol : input->key.keyval);
            event.type = down ? EventType::KeyDown : EventType::KeyUp;
            if (down) event.repeat = !pressed_.insert(code).second;
            else pressed_.erase(code);
            send_(event);
            const auto character = gdk_keyval_to_unicode(input->key.keyval);
            if (down && g_unichar_isprint(character) && !(event.mods & (kModCtrl | kModSuper))) {
                event.type = EventType::Char;
                event.codepoint = character;
                send_(event);
            }
            return true;
        }
        case GDK_BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
            if (input->button.button < 1 || input->button.button > 3) return false;
            event.button = input->button.button == 3 ? 1 : input->button.button == 2 ? 2 : 0;
            event.type = input->type == GDK_BUTTON_PRESS ? EventType::MouseDown : EventType::MouseUp;
            if (event.type == EventType::MouseDown) gtk_widget_grab_focus(area_);
            break;
        case GDK_MOTION_NOTIFY: event.type = EventType::MouseMove; break;
        case GDK_SCROLL:
            event.type = EventType::MouseWheel;
            switch (input->scroll.direction) {
            case GDK_SCROLL_UP: event.wheelY = 1; break;
            case GDK_SCROLL_DOWN: event.wheelY = -1; break;
            case GDK_SCROLL_RIGHT: event.wheelX = 1; break;
            case GDK_SCROLL_LEFT: event.wheelX = -1; break;
            case GDK_SCROLL_SMOOTH: event.wheelX = input->scroll.delta_x; event.wheelY = -input->scroll.delta_y; break;
            }
            break;
        case GDK_FOCUS_CHANGE:
            pressed_.clear();
            positioned_ = false;
            event.type = input->focus_change.in ? EventType::FocusGained : EventType::FocusLost;
            send_(event);
            return false;
        case GDK_LEAVE_NOTIFY: positioned_ = false; return false;
        default: return false;
        }
        double x = 0, y = 0;
        gdk_event_get_coords(input, &x, &y);
        event.x = x; event.y = y;
        if (positioned_) { event.deltaX = x-lastX_; event.deltaY = y-lastY_; }
        lastX_ = x; lastY_ = y; positioned_ = true;
        send_(event);
        return true;
    }
    GtkWidget* area_;
    std::function<void(const platform::Event&)> send_;
    gulong connection_;
    std::set<guint> pressed_;
    bool positioned_ = false;
    double lastX_ = 0, lastY_ = 0;
};
}

#pragma once
#include "../StudioHost.h"
#include <gtk/gtk.h>

namespace aiverse::studio {
class StudioScenePointer {
public:
    StudioScenePointer(GtkWidget* area, StudioHost& host) : area_(area), host_(host) {
        gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
            GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
        connection_ = g_signal_connect(area_, "event", G_CALLBACK(+[](GtkWidget*, GdkEvent* event, gpointer data)->gboolean {
            return static_cast<StudioScenePointer*>(data)->handle(event);
        }), this);
    }
    ~StudioScenePointer() { g_signal_handler_disconnect(area_, connection_); }
    StudioScenePointer(const StudioScenePointer&) = delete;
    StudioScenePointer& operator=(const StudioScenePointer&) = delete;
private:
    static StudioSceneKey sceneKey(guint symbol) {
        using K = StudioSceneKey;
        switch (gdk_keyval_to_lower(symbol)) {
        case GDK_KEY_a: return K::A;
        case GDK_KEY_d: return K::D;
        case GDK_KEY_e: return K::E;
        case GDK_KEY_f: return K::F;
        case GDK_KEY_q: return K::Q;
        case GDK_KEY_r: return K::R;
        case GDK_KEY_s: return K::S;
        case GDK_KEY_v: return K::V;
        case GDK_KEY_w: return K::W;
        case GDK_KEY_Left: return K::Left;
        case GDK_KEY_Right: return K::Right;
        case GDK_KEY_Up: return K::Up;
        case GDK_KEY_Down: return K::Down;
        case GDK_KEY_Home: return K::Home;
        case GDK_KEY_Delete: return K::Delete;
        case GDK_KEY_Escape: return K::Escape;
        case GDK_KEY_BackSpace: return K::Delete;
        case GDK_KEY_bracketleft: return K::BracketLeft;
        case GDK_KEY_bracketright: return K::BracketRight;
        default: return K::Other;
        }
    }
    bool handle(GdkEvent* event) {
        const int width = gtk_widget_get_allocated_width(area_), height = gtk_widget_get_allocated_height(area_);
        GdkModifierType flags{};
        gdk_event_get_state(event, &flags);
        StudioSceneMods mods;
        mods.shift = flags & GDK_SHIFT_MASK;
        mods.alt = flags & GDK_MOD1_MASK;
        mods.ctrl = mods.command = flags & GDK_CONTROL_MASK;
        double x = lastX_, y = lastY_;
        int phase = 1, button = held_ < 0 ? 0 : held_;
        switch (event->type) {
        case GDK_KEY_PRESS:
        case GDK_KEY_RELEASE: {
            auto key = sceneKey(event->key.keyval);
            if (key == StudioSceneKey::Other) {
                // A secondary layout or shifted symbol may hide the shortcut's base key.
                auto* map = gdk_keymap_get_for_display(gtk_widget_get_display(area_));
                GdkKeymapKey* keys = nullptr;
                guint* symbols = nullptr;
                gint count = 0;
                if (gdk_keymap_get_entries_for_keycode(map, event->key.hardware_keycode, &keys, &symbols, &count)) {
                    for (int i = 0; i < count && key == StudioSceneKey::Other; ++i)
                        if (keys[i].level == 0) key = sceneKey(symbols[i]);
                    g_free(keys);
                    g_free(symbols);
                }
            }
            return key != StudioSceneKey::Other && host_.sceneKey(key, event->type == GDK_KEY_PRESS, mods, held_ == 1);
        }
        case GDK_FOCUS_CHANGE:
            if (!event->focus_change.in) host_.setPivotSnap(false);
            return false;
        case GDK_BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
            switch (event->button.button) {
            case 1: button = 0; break;
            case 3: button = 1; break;
            case 2: button = 2; break;
            default: return false;
            }
            phase = event->type == GDK_BUTTON_PRESS ? 0 : 2;
            if ((phase == 0 && held_ >= 0) || (phase == 2 && button != held_)) return true;
            gtk_widget_grab_focus(area_);
            held_ = phase == 0 ? button : -1;
            break;
        case GDK_MOTION_NOTIFY: break;
        case GDK_LEAVE_NOTIFY:
            if (held_ < 0) host_.hover(-1, -1, width, height);
            return false;
        case GDK_UNMAP:
        case GDK_GRAB_BROKEN:
            if (held_ >= 0) host_.sceneMouse(2, held_, x, y, 0, 0, width, height, mods);
            held_ = -1;
            return false;
        case GDK_SCROLL: {
            double amount = 0;
            if (event->scroll.direction == GDK_SCROLL_UP) amount = 1;
            else if (event->scroll.direction == GDK_SCROLL_DOWN) amount = -1;
            else if (event->scroll.direction == GDK_SCROLL_SMOOTH) amount = -event->scroll.delta_y;
            if (amount) host_.sceneWheel(amount);
            return true;
        }
        default: return false;
        }
        gdk_event_get_coords(event, &x, &y);
        host_.sceneMouse(phase, button, x, y, phase ? x-lastX_ : 0, phase ? y-lastY_ : 0, width, height, mods);
        lastX_ = x;
        lastY_ = y;
        return true;
    }
    GtkWidget* area_;
    StudioHost& host_;
    gulong connection_;
    int held_ = -1;
    double lastX_ = 0, lastY_ = 0;
};
}

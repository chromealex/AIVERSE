#pragma once
#include "../StudioHost.h"
#include <gtk/gtk.h>

namespace aiverse::studio {
class StudioScenePointer {
public:
    StudioScenePointer(GtkWidget* area, StudioHost& host) : area_(area), host_(host) {
        gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
            GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
        connection_ = g_signal_connect(area_, "event", G_CALLBACK(+[](GtkWidget*, GdkEvent* event, gpointer data)->gboolean {
            return static_cast<StudioScenePointer*>(data)->handle(event);
        }), this);
    }
    ~StudioScenePointer() { g_signal_handler_disconnect(area_, connection_); }
    StudioScenePointer(const StudioScenePointer&) = delete;
    StudioScenePointer& operator=(const StudioScenePointer&) = delete;
private:
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

#pragma once
#include "../StudioHost.h"
#include <gtk/gtk.h>
#include <cmath>

namespace aiverse::studio {
class StudioPreviewPointer {
public:
    StudioPreviewPointer(GtkWidget* area, StudioSurface surface, StudioHost& host)
        : area_(area), surface_(surface), host_(host) {
        gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
            GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
        connection_ = g_signal_connect(area_, "event", G_CALLBACK(+[](GtkWidget*, GdkEvent* event, gpointer data)->gboolean {
            return static_cast<StudioPreviewPointer*>(data)->handle(event);
        }), this);
    }
    ~StudioPreviewPointer() { g_signal_handler_disconnect(area_, connection_); }
    StudioPreviewPointer(const StudioPreviewPointer&) = delete;
    StudioPreviewPointer& operator=(const StudioPreviewPointer&) = delete;
private:
    bool handle(GdkEvent* event) {
        const bool human = surface_ == StudioSurface::Character || surface_ == StudioSurface::Appearance;
        const int w = gtk_widget_get_allocated_width(area_), h = gtk_widget_get_allocated_height(area_);
        double x = 0, y = 0;
        gdk_event_get_coords(event, &x, &y);
        switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (button_ || (event->button.button != 1 && event->button.button != 3)) return false;
            button_ = event->button.button;
            gtk_widget_grab_focus(area_);
            lastX_ = x;
            lastY_ = y;
            natureClick_ = surface_ == StudioSurface::Nature && button_ == 1;
            if (human && button_ == 1) host_.humanPick(surface_, x, y, w, h);
            return true;
        case GDK_MOTION_NOTIFY: {
            if (!button_) return false;
            const double dx = x-lastX_, dy = y-lastY_;
            if (natureClick_ && std::abs(dx) < 3 && std::abs(dy) < 3) return true;
            natureClick_ = false;
            if (human) {
                if (button_ != 1 || !host_.humanPick(surface_, x, y, w, h)) host_.humanOrbit(surface_, dx, dy);
            } else host_.fxCameraOrbit(surface_, dx, dy);
            lastX_ = x;
            lastY_ = y;
            return true;
        }
        case GDK_BUTTON_RELEASE:
            if (button_ != event->button.button) return false;
            if (natureClick_ && std::abs(x-lastX_) < 3 && std::abs(y-lastY_) < 3) host_.natureClick(x, y, w, h);
            button_ = 0;
            natureClick_ = false;
            return true;
        case GDK_SCROLL: {
            double steps = 0;
            if (event->scroll.direction == GDK_SCROLL_UP) steps = 1;
            else if (event->scroll.direction == GDK_SCROLL_DOWN) steps = -1;
            else if (event->scroll.direction == GDK_SCROLL_SMOOTH) steps = -event->scroll.delta_y;
            if (steps && human) host_.humanZoom(surface_, steps*2);
            else if (steps) host_.fxCameraZoom(surface_, steps*2);
            return true;
        }
        case GDK_GRAB_BROKEN:
        case GDK_UNMAP:
            button_ = 0;
            natureClick_ = false;
            return false;
        default: return false;
        }
    }
    GtkWidget* area_;
    StudioSurface surface_;
    StudioHost& host_;
    gulong connection_;
    guint button_ = 0;
    double lastX_ = 0, lastY_ = 0;
    bool natureClick_ = false;
};
}

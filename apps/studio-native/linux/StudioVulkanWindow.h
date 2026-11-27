#pragma once
#include "../../../engine/gfx/vulkan/Vulkan.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <optional>

namespace aiverse::studio {
// Capture on the GTK thread. The owner keeps the realized widget alive until its
// render thread has released the swapchain. A windowless widget shares its parent.
inline std::optional<gfx::VulkanWindow> studioVulkanWindow(GtkWidget* widget) {
    if (!gtk_widget_get_realized(widget) || !gtk_widget_get_has_window(widget))
        return std::nullopt;
    GdkWindow* window = gtk_widget_get_window(widget);
    if (!GDK_IS_X11_WINDOW(window) || !gdk_window_ensure_native(window))
        return std::nullopt;
    gfx::VulkanWindow target;
    target.kind = gfx::VulkanWindow::Kind::Xlib;
    target.display = gdk_x11_display_get_xdisplay(gdk_window_get_display(window));
    target.window = gdk_x11_window_get_xid(window);
    return target;
}
}

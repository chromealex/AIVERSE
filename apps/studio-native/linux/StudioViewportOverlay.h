#pragma once
#include "../StudioViewportLayout.h"
#include <gtk/gtk.h>

namespace aiverse::studio {
// GtkOverlay measures only its main child (the WebView), so an old panel
// rectangle never prevents the user from shrinking the workspace.
inline NativeViewportPlacement gtkViewportPlacement(GtkOverlay* overlay, const ViewportRect& rect) {
    auto* widget = GTK_WIDGET(overlay);
    const int height = gtk_widget_get_allocated_height(widget);
    const int scale = gtk_widget_get_scale_factor(widget);
    const auto clipped = viewportPlacement(rect, gtk_widget_get_allocated_width(widget), height, scale);
    if (!clipped.visible) return {};
    const int left = int(std::floor(clipped.x));
    const int right = int(std::ceil(clipped.x + clipped.width));
    const int bottom = int(std::floor(clipped.y));
    const int top = int(std::ceil(clipped.y + clipped.height));
    return {double(left), double(bottom), double(right-left), double(top-bottom),
        (right-left)*scale, (top-bottom)*scale, true};
}

inline GtkWidget* addStudioViewport(GtkOverlay* overlay) {
    auto* area = gtk_drawing_area_new();
    gtk_widget_set_no_show_all(area, TRUE);
    auto* rect = new ViewportRect;
    g_object_set_data_full(G_OBJECT(area), "studio-viewport-rect", rect,
        [](gpointer data){ delete static_cast<ViewportRect*>(data); });
    gtk_overlay_add_overlay(overlay, area);
    g_signal_connect_object(overlay, "get-child-position", G_CALLBACK(+[](GtkOverlay* host,
        GtkWidget* child, GdkRectangle* bounds, GtkWidget* target)->gboolean {
        if (child != target) return FALSE;
        const auto& rect = *static_cast<ViewportRect*>(g_object_get_data(G_OBJECT(child), "studio-viewport-rect"));
        const auto placement = gtkViewportPlacement(host, rect);
        bounds->x = int(placement.x);
        bounds->y = int(gtk_widget_get_allocated_height(GTK_WIDGET(host)) - placement.y - placement.height);
        bounds->width = int(placement.width);
        bounds->height = int(placement.height);
        return TRUE;
    }), area, G_CONNECT_DEFAULT);
    return area;
}

inline NativeViewportPlacement placeStudioViewport(GtkWidget* area, const ViewportRect& rect) {
    auto* host = GTK_OVERLAY(gtk_widget_get_parent(area));
    auto placement = gtkViewportPlacement(host, rect);
    *static_cast<ViewportRect*>(g_object_get_data(G_OBJECT(area), "studio-viewport-rect")) = rect;
    gtk_widget_set_visible(area, placement.visible);
    gtk_widget_queue_allocate(GTK_WIDGET(host));
    return placement;
}
}

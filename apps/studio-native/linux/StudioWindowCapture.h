#pragma once
#include "../../../engine/gfx/Gfx.h"
#include <gtk/gtk.h>
#include <cstring>
#include <memory>

namespace aiverse::studio {
inline void captureStudioGtkWindow(GtkWindow* top, gfx::Device::CaptureFn done) {
    auto* widget = GTK_WIDGET(top);
    auto* window = gtk_widget_get_window(widget);
    if (!window || !gtk_widget_get_mapped(widget) || (gdk_window_get_state(window) & GDK_WINDOW_STATE_ICONIFIED)) {
        done(0, 0, {});
        return;
    }
    const int width = gdk_window_get_width(window), height = gdk_window_get_height(window);
    const int scale = gdk_window_get_scale_factor(window);
    constexpr int64_t maxPixels = 32 * 1024 * 1024;
    if (width < 1 || height < 1 || scale < 1 || int64_t(width) * height > maxPixels / scale / scale) {
        done(0, 0, {});
        return;
    }
    using Pixbuf = std::unique_ptr<GdkPixbuf, decltype(&g_object_unref)>;
    Pixbuf image(gdk_pixbuf_get_from_window(window, 0, 0, width, height), g_object_unref);
    if (!image) { done(0, 0, {}); return; }
    const int w = gdk_pixbuf_get_width(image.get()), h = gdk_pixbuf_get_height(image.get());
    const int channels = gdk_pixbuf_get_n_channels(image.get());
    if (w < 1 || h < 1 || int64_t(w) * h > maxPixels || (channels != 3 && channels != 4)) {
        done(0, 0, {});
        return;
    }
    const auto* pixels = gdk_pixbuf_read_pixels(image.get());
    const int stride = gdk_pixbuf_get_rowstride(image.get());
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        auto* out = rgba.data() + size_t(y) * w * 4;
        const auto* row = pixels + size_t(y) * stride;
        if (channels == 4) std::memcpy(out, row, size_t(w) * 4);
        else for (int x = 0; x < w; ++x) {
            std::memcpy(out + x * 4, row + x * 3, 3);
            out[x * 4 + 3] = 255;
        }
    }
    done(w, h, std::move(rgba));
}
}

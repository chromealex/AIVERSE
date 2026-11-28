#pragma once
#include "../StudioHost.h"
#include "../StudioNetworkPlay.h"
#include "StudioChildProcess.h"
#include "StudioFileManager.h"
#include "StudioWindowCapture.h"
#include "StudioBackgroundQueue.h"
#include "StudioViewportOverlay.h"
#include "StudioFileDialog.h"
#include "StudioUserPaths.h"
#include "StudioVulkanWindow.h"
#include <array>
#include <stdexcept>

namespace aiverse::studio {
// Construct on the GTK thread after realizing the main overlay. Destroy the
// StudioHost (and join its render thread) before destroying the overlay.
class StudioSurfacePlatform : public StudioHostPlatform {
public:
    explicit StudioSurfacePlatform(GtkOverlay* overlay) : mainOverlay_(overlay) {
        if (!gtk_widget_get_realized(GTK_WIDGET(overlay)))
            throw std::invalid_argument("Studio viewport overlay is not realized");
        try {
            for (auto& entry : surfaces_) {
                entry.widget = addStudioViewport(overlay);
                gtk_widget_set_can_focus(entry.widget, TRUE);
                gtk_widget_realize(entry.widget);
                auto handle = studioVulkanWindow(entry.widget);
                if (!handle) throw std::runtime_error("Studio Vulkan panels require an X11 display");
                entry.window = *handle;
            }
        } catch (...) {
            destroySurfaces();
            throw;
        }
    }
    ~StudioSurfacePlatform() override { destroySurfaces(); }
    StudioSurfacePlatform(const StudioSurfacePlatform&) = delete;
    StudioSurfacePlatform& operator=(const StudioSurfacePlatform&) = delete;

    std::unique_ptr<gfx::Device> createDevice(StudioSurface surface) override {
        const auto& window = surfaces_.at(static_cast<size_t>(surface)).window;
        std::string error;
        auto device = gfx::createVulkanDevice(window, &error);
        if (!device) throw std::runtime_error("Studio Vulkan panel: " + error);
        return device;
    }

    bool placeSurface(StudioSurface surface, const ViewportRect& rect, NativeViewportPlacement& out) override {
        out = placeStudioViewport(widget(surface), rect);
        return true;
    }

    bool surfaceDrawable(StudioSurface surface) override {
        auto* area = widget(surface);
        if (!gtk_widget_get_mapped(area)) return false;
        auto* top = gtk_widget_get_window(gtk_widget_get_toplevel(area));
        return top && !(gdk_window_get_state(top) & GDK_WINDOW_STATE_ICONIFIED);
    }

    bool gameFocused() override { return gtk_widget_has_focus(widget(StudioSurface::Game)); }

    bool confirmDiscardChanges(const std::string& title, const std::string& message,
                               const std::string& discardLabel) override {
        return confirmStudioDiscard(parentWindow(), title, message, discardLabel);
    }

    void chooseImportSources(const std::string& startDir, StudioPathSelection done) override {
        chooseStudioPaths(parentWindow(), GTK_FILE_CHOOSER_ACTION_OPEN, "Import into content",
                          startDir, {}, true, std::move(done));
    }

    std::filesystem::path cacheDir() override { return g_get_user_cache_dir(); }
    void rememberLastProject(const std::string& dir) override { storeStudioLastProject(dir); }

    PlayerExitStatus launchGamePlayer(const std::vector<std::string>& args) override {
        const auto directory = std::filesystem::read_symlink("/proc/self/exe").parent_path();
        return launchStudioChild(studioNetworkPlayer(directory / "aiverse_client"), args);
    }

    void openProjectStudio(const std::filesystem::path& executable, const std::string& project) override {
        launchStudioChild(executable, {"--project", project});
    }

    void revealInFileManager(const std::filesystem::path& path, bool openFolder) override {
        revealStudioFile(path, openFolder);
    }

    void captureStudioWindow(gfx::Device::CaptureFn done) override {
        captureStudioGtkWindow(parentWindow(), std::move(done));
    }

    double now() override { return double(g_get_monotonic_time()) / G_USEC_PER_SEC; }
    void log(const std::string& message) override { g_message("%s", message.c_str()); }
    void runInBackground(std::function<void()> work, std::function<void()> thenOnMain) override {
        background_.submit(std::move(work), std::move(thenOnMain));
    }
    void stopBackgroundWork() { background_.stop(); }

    void chooseProject(std::function<void(std::string)> done) override {
        chooseFolder("Open Project", {}, std::move(done));
    }

    void chooseFolder(const std::string& title, const std::string& startDir,
                      std::function<void(std::string)> done) override {
        chooseStudioPath(parentWindow(), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, title, startDir, {}, std::move(done));
    }

    void chooseSaveFile(const std::string& startDir, const std::string& defaultName,
                        std::function<void(std::string)> done) override {
        chooseStudioPath(parentWindow(), GTK_FILE_CHOOSER_ACTION_SAVE, "Save Map", startDir, defaultName, std::move(done));
    }

protected:
    GtkWidget* widget(StudioSurface surface) const {
        return surfaces_.at(static_cast<size_t>(surface)).widget;
    }
private:
    GtkOverlay* mainOverlay_;
    GtkWindow* parentWindow() const { return GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(mainOverlay_))); }
    StudioBackgroundQueue background_;
    struct Surface { GtkWidget* widget = nullptr; gfx::VulkanWindow window; };
    std::array<Surface, static_cast<size_t>(StudioSurface::Count)> surfaces_{};
    void destroySurfaces() {
        for (auto& entry : surfaces_)
            if (entry.widget) gtk_widget_destroy(entry.widget);
    }
};
}

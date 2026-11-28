// AIVERSE Studio: per-project Scene camera bookmarks.
// Editor-only user settings; no game or simulation data is modified.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/platform/Platform.h"

#include "../core/LegacyCameraBookmarks.h"

namespace aiverse::editor {
using legacy_camera::SavedCamera;

class StudioCameraBookmarksWindow final : public EditorWindow {
public:
    const char* id() const override { return "studio_camera_bookmarks"; }
    const char* title() const override { return "Ракурсы сцены"; }
    const char* group() const override { return "scene"; }
    const char* dockArea() const override { return "right-bottom"; }
    bool openByDefault() const override { return false; }
    int order() const override { return 90; }

    void draw(Editor& ed) override {
        const std::string& project = ed.ws().project.dir;
        if (project.empty()) {
            ui::emptyStateHere(ICON_SCENE, "Нет открытого проекта", "Откройте проект, чтобы сохранять ракурсы камеры.");
            return;
        }
        if (project != project_) { project_ = project; load(); }
        ImGui::TextDisabled("Ракурсы Scene — настройки пользователя, не данные игры");
        ImGui::Separator();
        for (int i = 0; i < int(slots_.size()); ++i) {
            ImGui::PushID(i);
            const auto& slot = slots_[size_t(i)];
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);
            ImGui::SameLine(32);
            ImGui::BeginDisabled(!slot.valid);
            if (ImGui::Button("Перейти")) {
                auto& cam = ed.sceneCam;
                cam.target = slot.target;
                cam.yaw = slot.yaw;
                cam.pitch = slot.pitch;
                cam.distance = std::clamp(slot.distance, cam.minDistance, cam.maxDistance);
                cam.fovY = slot.fov;
                cam.ortho = slot.ortho;
                ed.focusWindow("scene");
                ed.status = "Ракурс сцены восстановлен";
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Запомнить")) {
                auto& s = slots_[size_t(i)];
                auto& cam = ed.sceneCam;
                s.target = cam.target;
                s.yaw = cam.yaw;
                s.pitch = cam.pitch;
                s.distance = cam.distance;
                s.fov = cam.fovY;
                s.ortho = cam.ortho;
                s.valid = finite(s);
                if (!s.valid) ed.status = "Некорректные параметры камеры";
                else if (!save()) ed.status = "Не удалось сохранить ракурсы";
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!slot.valid);
            if (ImGui::SmallButton(ICON_TRASH)) { slots_[size_t(i)] = {}; if (!save()) ed.status = "Ошибка сохранения ракурсов"; }
            ImGui::EndDisabled();
            if (slot.valid) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%.1f, %.1f, %.1f)", slot.target.x, slot.target.y, slot.target.z);
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("Вся карта (F)")) { ed.fitScene(); ed.focusWindow("scene"); }
        ImGui::SameLine();
        if (ImGui::Button("К выбранному")) {
            float x = 0, z = 0;
            if (ed.selectionPos(ed.selection, x, z)) { ed.focusScene(x, z, 18); ed.focusWindow("scene"); }
        }
        ImGui::TextDisabled("ПКМ — поворот, СКМ — панорамирование, колесо — масштаб");
    }

private:
    std::array<SavedCamera, 6> slots_{};
    std::string project_;

    std::filesystem::path path() const {
        return legacy_camera::bookmarkFile(platform::userDataDir(), project_);
    }
    void load() { slots_ = legacy_camera::read(path()); }
    bool save() const {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dest = path();
        fs::create_directories(dest.parent_path(), ec);
        if (ec) return false;
        const fs::path tmp = dest.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out) return false;
            out << "AIVERSE_CAMERA_BOOKMARKS_V1\n" << std::setprecision(9);
            for (const auto& s : slots_)
                out << int(s.valid) << ' ' << s.target.x << ' ' << s.target.y << ' ' << s.target.z << ' '
                    << s.yaw << ' ' << s.pitch << ' ' << s.distance << ' ' << s.fov << ' ' << int(s.ortho) << '\n';
            out.flush();
            if (!out) return false;
        }
        fs::rename(tmp, dest, ec);
        if (!ec) return true;
        // Windows rename doesn't replace existing files. Delete only the user-settings destination.
        fs::remove(dest, ec);
        ec.clear(); fs::rename(tmp, dest, ec);
        return !ec;
    }
};
AIVERSE_EDITOR_WINDOW(StudioCameraBookmarksWindow);
} // namespace aiverse::editor

#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/PortOps.h"
#include "engine/i18n/Localization.h"
#include <chrono>
#include <filesystem>
#include <future>

namespace aiverse::editor {
class ImportProjectWindow : public EditorWindow {
public:
    const char* id() const override { return "project_import"; }
    const char* title() const override { return title_.empty() ? "Project import" : title_.c_str(); }
    const char* dockArea() const override { return "center"; }
    bool openByDefault() const override { return false; }
    void update(Editor& ed, double) override {
        if (!loaded_) {
            loaded_ = true;
            try {
                for (const char* lang : {"en", "ru"}) {
                    const auto path = std::filesystem::path(ed.ws().engineDir) / "engine/assets/lang" / (std::string(lang) + ".json");
                    loc_.addTable(lang, Json::parseFile(path.string()));
                }
                loc_.setLanguage(i18n::negotiate(i18n::systemLanguages(), {"en", "ru"}, "en"));
            } catch (const std::exception&) { loc_.setLanguage("en"); }
            title_ = tr("title");
        }
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                report_ = job_.get();
                Json overview = Json::object();
                for (const char* key : {"engine", "version", "serialization", "bytes", "counts", "summary", "errors", "error"})
                    if (report_.has(key)) overview.set(key, report_[key]);
                preview_ = overview.dump(2);
                const std::string details = report_.dump(2);
                preview_ += "\n\n" + details.substr(0, 65536);
                if (details.size() > 65536) preview_ += "\n" + tr("truncated");
                scanned_ = report_.getBool("ok", true);
                if (operation_ == "import" && scanned_) imported_ = true;
            } catch (const std::exception& e) { error_ = e.what(); scanned_ = false; }
        }
    }
    void draw(Editor& ed) override {
        const bool busy = job_.valid();
        ImGui::TextWrapped("%s", tr("intro").c_str());
        ImGui::BeginDisabled(busy);
        if (ui::inputString(tr("source").c_str(), source_)) { scanned_ = false; imported_ = false; }
        if (ui::inputString(tr("destination").c_str(), destination_)) imported_ = false;
        const std::string autoLabel = tr("auto");
        const char* modes[] = {autoLabel.c_str(), "2D", "3D"};
        ImGui::Combo(tr("mode").c_str(), &mode_, modes, 3);
        ImGui::InputFloat(tr("scale").c_str(), &scale_, 0.1f, 1.0f, "%.4f");
        if (ImGui::Button(tr("scan").c_str())) start(ed, "scan");
        ImGui::SameLine();
        ImGui::BeginDisabled(!scanned_ || destination_.empty() || !(scale_ > 0));
        if (ImGui::Button(tr("import").c_str())) start(ed, "import");
        ImGui::EndDisabled();
        if (imported_) {
            if (ImGui::Button(tr("open").c_str())) ed.requestOpenProject(destination_);
            ImGui::SameLine();
            if (ImGui::Button(tr("ai").c_str())) {
                Json p = Json::object(); p.set("path", Json::string(destination_));
                const std::string prompt = tr("prompt") + "\n" + p.dump();
                ImGui::SetClipboardText(prompt.c_str());
                ed.focusWindow("agent");
                ed.status = tr("copied");
            }
        }
        ImGui::EndDisabled();
        if (busy) ImGui::TextUnformatted(tr("busy").c_str());
        if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
        if (!report_.isNull()) {
            ImGui::BeginChild("report", ImVec2(0, 0));
            ImGui::TextUnformatted(preview_.c_str());
            ImGui::EndChild();
        }
    }
private:
    std::string tr(const char* key) const { return loc_.tr(std::string("editor.port.") + key); }
    void start(Editor& ed, const std::string& op) {
        Json p = Json::object(); p.set("path", Json::string(source_));
        if (op == "import") { p.set("mode", Json::string(mode_ == 1 ? "2d" : mode_ == 2 ? "3d" : "auto")); p.set("out", Json::string(destination_)); p.set("scale", Json::number(double(scale_), 6)); }
        error_.clear(); operation_ = op;
        const std::string engine = ed.ws().engineDir;
        job_ = std::async(std::launch::async, [engine, op, p] { return runProjectPort(engine, op, p); });
    }
    i18n::Localization loc_;
    bool loaded_ = false, scanned_ = false, imported_ = false;
    int mode_ = 0;
    float scale_ = 1;
    std::string source_, destination_, operation_, error_, title_, preview_;
    Json report_;
    std::future<Json> job_;
};
AIVERSE_EDITOR_WINDOW(ImportProjectWindow);
}

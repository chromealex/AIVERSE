// PRISMA post effects: the "postfx" chain of game.json and of the open map (bloom, colour grading, LUT,
// vignette, tilt-shift, ...), with the frame cost per device class and warnings for heavy effects,
// plus a preview of the quality tiers. Keeps Editor::prisma() in sync with the data.
// Data: engine/render/PrismaData.h; MCP: apps/editor/core/PrismaOps.cpp; docs/postfx.md.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/PrismaOps.h"
#include "engine/render/PrismaData.h"
#include "engine/render/PrismaVolumeData.h"
#include "engine/render/TextureCache.h"

namespace aiverse::editor {

using namespace aiverse::render;

namespace {
ImVec4 costColor(PrismaCostClass c) {
    return ui::toneColor(c == PrismaCostClass::Cheap ? ui::Tone::Ok : c == PrismaCostClass::Medium ? ui::Tone::Warn : ui::Tone::Error);
}
// 1..3 dots in the cost colour (cheap / medium / heavy)
void costDots(PrismaCostClass c) {
    const float r = 3.5f, step = 9.0f, h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const int n = int(c) + 1;
    for (int i = 0; i < 3; ++i)
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r + float(i) * step, p.y + h * 0.5f), r,
                                                    i < n ? ImGui::GetColorU32(costColor(c)) : ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImGui::Dummy(ImVec2(step * 3, h));
}
const char* costRu(PrismaCostClass c) { return c == PrismaCostClass::Cheap ? "лёгкий" : c == PrismaCostClass::Medium ? "средний" : "тяжёлый"; }
const char* kTiersRu[] = {"выкл", "низкое (слабые телефоны)", "среднее (телефоны)", "высокое (ПК, мощные)"};
const char* kDeviceRu[] = {"Слабый телефон", "Средний телефон", "ПК"};
const char* kDeviceTip[] = {"Mali-T720 / Adreno 306, 1280x720, уровень «низкое»", "Adreno 610 / Mali-G52, 1920x864, уровень «среднее»", "2560x1440, уровень «высокое»"};
} // namespace

class PrismaWindow : public EditorWindow {
public:
    const char* id() const override { return "postfx"; }
    const char* title() const override { return "PRISMA — Постобработка"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 7; }
    bool openByDefault() const override { return false; }

    void update(Editor& ed, double) override {
        if (!registered_) { registerOps(ed); registered_ = true; }
        Prisma& pf = ed.prisma();
        if (ed.ws().project.dir != projectDir_) {
            projectDir_ = ed.ws().project.dir;
            const std::string dir = projectDir_;
            pf.setImageLoader([dir](const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba) {
                std::ifstream f(dir + "/" + path, std::ios::binary);
                if (!f) return false;
                std::string bytes((std::istreambuf_iterator<char>(f)), {});
                return decodeImage(bytes, w, h, rgba);
            });
            lastKey_.clear();
        }
        // follow the data (agents change it over MCP, the window while dragging)
        const std::string key = ed.ws().project.manifest["postfx"].dump(-1) + "|" + ed.doc().data.extra["postfx"].dump(-1);
        if (key != lastKey_) {
            issues_.clear();
            pf.configure(ed.ws().project.manifest["postfx"], ed.doc().data.extra["postfx"], &issues_);
            lastKey_ = key;
        }
        pf.setQuality(tier_);
        ed.postFxInScene = inScene_;
    }

    void draw(Editor& ed) override {
        Prisma& pf = ed.prisma();
        if (!ImGui::IsAnyItemActive() && dirty_) save(ed);
        ui::section(nullptr, "Предпросмотр");
        int t = int(tier_);
        if (ui::beginProps("##pv")) {
            ui::prop("Качество", "Только для просмотра: в игре уровень выбирается по видеокарте (слабые Android — низкое, телефоны — среднее, ПК — высокое), "
                                 "игрок может снизить его, а при медленных кадрах он снижается сам.");
            if (ImGui::Combo("##tier", &t, kTiersRu, 4)) tier_ = PrismaQuality(t);
            ui::endProps();
        }
        ui::checkbox("Показывать в «Сцене»", &inScene_);
        ImGui::TextDisabled("Последний кадр: проходов %d, эффектов %d", pf.stats().passes, pf.stats().effects);
        ImGui::TextDisabled("Temporal HDR: %s  |  jitter: %s (%.3f, %.3f) px",
                            pf.stats().temporalResolved ? "resolved" : "off / fallback",
                            pf.stats().temporalJittered ? "on" : "off",
                            double(pf.stats().jitterPixelX),
                            double(pf.stats().jitterPixelY));
        ImGui::TextDisabled("Prisma render: %dx%d -> output %dx%d  |  temporal upscale: %s",
                            pf.stats().width,pf.stats().height,
                            pf.stats().outputWidth,pf.stats().outputHeight,
                            pf.stats().temporalUpscaled ? "yes" : "off / fallback");
        ImGui::TextDisabled("View priority %u / 255 | HDR history admission: %s",
            unsigned(pf.stats().adaptiveViewPriority),
            pf.stats().adaptiveHistoryAdmissionRejected ?
                "rejected by per-camera quota" : "allowed / not requested");
        const auto capability=pf.runtimeCapabilities();
        ImGui::TextDisabled("GPU capability: %s | RGBA16F: %s | HDR authored: %s | usable: %s",
            capability.deviceHealthy ? "ready" : "unavailable / disabled",
            capability.hdrFormatSupported ? "supported" : "unsupported",
            capability.hdrAuthoredRequested ? "yes" : "no",
            capability.hdrRequested ? "yes" : "no");
        ImGui::TextDisabled("Temporal: requested %s, shader %s, last resolve %s",
            capability.temporalRequested ? "yes" : "no",
            capability.temporalPipelinePrepared ? "ready" : "not ready",
            capability.temporalResolvedLastFrame ? "yes" : "no");
        ImGui::TextDisabled("Subpixel: requested %s, applied %s | upscale: requested %s, resolved %s",
            capability.jitterRequested ? "yes" : "no",
            capability.jitterAppliedLastFrame ? "yes" : "no",
            capability.upscaleRequested ? "yes" : "no",
            capability.upscaleAppliedLastFrame ? "yes" : "no");
        ImGui::TextDisabled("Motion vectors: %s | reactive last frame: %s | adaptive: %s",
            capability.velocityPipelinePrepared ? "ready" : "unavailable",
            capability.reactiveLastFrame ? "valid" : "not supplied",
            capability.adaptiveRequested ? "requested" : "off");
        if (!capability.fallbackReason.empty())
            ImGui::TextDisabled("GPU fallback reason: %s",capability.fallbackReason.c_str());
        if (ImGui::CollapsingHeader("Диагностика эффектов по камерам")) {
            ImGui::TextDisabled("Encoded = отправлен draw, НЕ GPU completion или проверенные пиксели.");
            const auto ids = pf.effectReportViewIds();
            if (ids.empty()) ImGui::TextDisabled("Нет завершённых Prisma resolve для камер.");
            for (const uint64_t id : ids) {
                const auto* report = pf.effectReport(id);
                if (!report) continue;
                const std::string label = std::string("View ") + std::to_string(id) +
                    " | resolve " + std::to_string(report->sequence) + "##prisma-view-" +
                    std::to_string(id);
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::TextDisabled("%dx%d | tier %s | HDR %s | Scene %s",
                        report->width, report->height,
                        prismaQualityName(report->quality),
                        report->hdr ? "yes" : "no",
                        report->sceneReady ? "ready" : "unavailable");
                    if (report->manualExposureEncoded)
                        ImGui::TextDisabled("Manual HDR exposure: %+.2f EV (fixed, no AE history)",
                            double(report->manualExposureEV));
                    if (report->shaderGraphFallback)
                        ui::toneText(ui::Tone::Warn,
                            "Shader Graph пропущен: сохранены доступные обычные эффекты PRISMA.");
                    if (report->shaderGraphTextures > 0 ||
                        report->autoExposureTextureSlot >= 0) {
                        ImGui::TextDisabled("Shader Graph textures: %d | HDR exposure slot: %d (-1=inactive)",
                            report->shaderGraphTextures,
                            report->autoExposureTextureSlot);
                        if (report->autoExposureTextureSlot >= 0) {
                            ImGui::TextDisabled("HDR highlight rejection: %.2f (0=legacy)",
                                double(report->autoExposureHighlightRejection));
                            ImGui::TextDisabled("Exposure: %s | region metered this frame: %s",
                                report->autoExposureFrozen ? "frozen (history reused)" :
                                                             "adaptation encoded",
                                report->autoExposureRegionMetered ?
                                    (report->autoExposureBoxMetered ? "rectangle" : "circle") : "no");
                        }
                    }
                    if (report->reactiveInputSupplied) {
                        ImGui::TextDisabled("Reactive mask: %s | MotionBlur reactive edges: %s",
                            report->reactiveInputAccepted ?
                                "metadata accepted" : "rejected (size/format)",
                            report->motionBlurReactiveEdgesEnabled ?
                                "encoded" : "off");
                        if (report->reactiveInputAccepted)
                            ImGui::TextDisabled("Object velocity: %s | Current-frame reactive: %s",
                                report->objectVelocityInputAccepted ?
                                    "accepted" : "absent (seed/cut/unsupported)",
                                report->reactiveWithoutObjectVelocity ?
                                    "mask-only pass" : "with object motion");
                    }
                    if (report->effects.empty())
                        ImGui::TextDisabled("Для последнего кадра нет authored эффектов.");
                    for (const auto& state : report->effects) {
                        const auto& def = prismaType(state.type);
                        ImGui::BulletText("%s | req %s | pipe %s | encoded %s%s",
                            def.label,
                            state.requested ? "yes" : "no",
                            state.pipelinePrepared ? "ready" : "not ready",
                            state.encoded ? "yes" : "no",
                            state.degraded ? " | degraded fallback" : "");
                        if (!state.reason.empty()) {
                            ImGui::Indent();
                            ImGui::TextDisabled("Reason: %s", state.reason.c_str());
                            ImGui::Unindent();
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::TextDisabled("Jitter: отдельный opt-in AIVERSE_PRISMA_JITTER=1; без него обычный Temporal Resolve остаётся доступен.");
        ImGui::TextDisabled("GPU adaptive: %s  |  scale: %.3f  |  stable fallback: %s",
            pf.stats().adaptiveResolution ? "on" : "off",
            double(pf.stats().requestedRenderScale),
            pf.stats().temporalFallback ? "used" : "not needed");
        if (pf.stats().adaptiveResolution) {
            constexpr double kMiB=1024.0*1024.0;
            ImGui::TextDisabled("Prisma + renderer + geometry + owned assets (known minimum): %.1f / %.1f MiB (%s)",
                double(pf.stats().adaptiveKnownGpuBytes)/kMiB,
                double(pf.stats().adaptiveMemoryLimitBytes)/kMiB,
                pf.stats().adaptiveMemoryPressure ? "pressure" : "within budget");
            if (std::isfinite(pf.stats().adaptiveGpuMs) &&
                pf.stats().adaptiveGpuMs>0.0)
                ImGui::TextDisabled("Adaptive timing: %.3f ms (%s)",
                    pf.stats().adaptiveGpuMs,
                    pf.stats().adaptiveScopedGpuTiming ?
                        "completed Metal/GL33 viewport counter" : "completed whole GPU frame");
            else
                ImGui::TextDisabled("Adaptive timing: unavailable; no CPU/FPS substitute");
        }
        const double gpuFrame=ed.device().gpuFrameMs();
        if (std::isfinite(gpuFrame) && gpuFrame>0.0)
            ImGui::TextDisabled("GPU: %.2f ms (last completed, WHOLE frame; not this viewport)",
                gpuFrame);
        else
            ImGui::TextDisabled("GPU frame timing unavailable; adaptive resolution remains at authored scale");

        ui::section(nullptr, "Где");
        static const char* scopes[] = {"Вся игра (game.json)", "Эта карта"};
        ui::segmented("##scope", scope_, scopes, 2);
        ui::help("Эффект карты заменяет эффект того же типа из game.json (только для этой карты); \"enabled\": false на карте выключает его.");
        const std::string scope = scope_ ? "map" : "game";

        drawPresets(ed, scope);
        drawEffects(ed, scope);
        drawAdd(ed, scope);
        drawVolumes(ed, scope);
        drawCost(ed);
        if (!pf.lastError().empty()) ui::toneText(ui::Tone::Error, "%s", pf.lastError().c_str());
    }

private:
    Json block(Editor& ed, const std::string& scope) { return prismaBlock(ed.ws(), scope); }
    Json& blockMut(Editor& ed, const std::string& scope) {
        Json& owner = scope == "map" ? ed.doc().data.extra : ed.ws().project.manifest;
        if (!owner["postfx"].isObject()) {
            Json b = Json::object();
            b.set("effects", owner["postfx"].isArray() ? owner["postfx"] : Json::array());
            owner.set("postfx", b);
        }
        Json& b = *owner.find("postfx");
        if (!b["effects"].isArray()) b.set("effects", Json::array());
        return b;
    }
    // In-memory edit (one undo step / one save per drag), written by save() when the mouse is released.
    template <class F> void edit(Editor& ed, const std::string& scope, F&& f) {
        if (scope == "map" && !dirty_) ed.doc().beginEdit();
        f(*blockMut(ed, scope).find("effects"));
        if (scope == "map") ed.doc().touched();
        dirty_ = true;
        dirtyScope_ = scope;
    }
    void save(Editor& ed) {
        dirty_ = false;
        if (dirtyScope_ == "game") {
            std::string err;
            if (!ed.ws().project.saveManifest(&err)) ed.ws().log("postfx", err, true);
        }
    }

    void drawPresets(Editor& ed, const std::string& scope) {
        ui::section(nullptr, "Готовые наборы");
        const auto& ps = prismaPresets();
        preset_ = std::clamp(preset_, 0, int(ps.size()) - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##preset", ps[size_t(preset_)].label)) {
            for (int i = 0; i < int(ps.size()); ++i) {
                if (ImGui::Selectable(ps[size_t(i)].label, i == preset_)) preset_ = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ps[size_t(i)].help);
            }
            ImGui::EndCombo();
        }
        if (ui::wideButton(ICON_REFRESH, "Заменить", 0, 2)) applyPreset(ed, scope, "replace");
        ImGui::SetItemTooltip("Убрать текущие эффекты и поставить набор");
        if (ui::wideButton(ICON_PLUS, "Добавить", 1, 2)) applyPreset(ed, scope, "merge");
        ImGui::SetItemTooltip("Добавить эффекты набора к текущим");
    }
    void applyPreset(Editor& ed, const std::string& scope, const char* mode) {
        Json p = Json::object();
        p.set("preset", Json::string(prismaPresets()[size_t(preset_)].id));
        p.set("mode", Json::string(mode));
        p.set("scope", Json::string(scope));
        lastAnswer_ = ed.ws().call("postfx_preset", p);
    }

    void drawEffects(Editor& ed, const std::string& scope) {
        ui::section(ICON_SPARKLES, scope == "map" ? "Эффекты карты" : "Эффекты игры");
        const Json effects = block(ed, scope).isArray() ? block(ed, scope) : block(ed, scope)["effects"];
        if (effects.size() == 0) { ui::hint("Нет эффектов: картинка идёт в экран без постобработки (ноль затрат)."); return; }
        int removeAt = -1;
        for (size_t i = 0; i < effects.size(); ++i) {
            const Json& ej = effects[i];
            const PrismaTypeDef* d = prismaType(ej.getStr("type"));
            ImGui::PushID(int(i));
            if (!d) { ui::toneText(ui::Tone::Error, "неизвестный тип '%s'", ej.getStr("type").c_str()); ImGui::PopID(); continue; }
            PrismaEffect e;
            try { e = parsePrismaEffect(ej, false); } catch (...) { ImGui::PopID(); continue; }
            bool en = e.enabled;
            if (ui::checkbox("##en", &en)) edit(ed, scope, [&](Json& arr) { arr.at(i).set("enabled", Json::fromBool(en)); });
            ImGui::SameLine();
            costDots(d->cost);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s эффект", costRu(d->cost));
            ImGui::SameLine();
            const bool open = ui::collapsing((std::string(d->label) + "###h").c_str());
            if (open) {
                drawParams(ed, scope, i, *d, e);
                ImGui::Indent();
                if (ImGui::SmallButton("Удалить эффект")) removeAt = int(i);
                ImGui::Unindent();
                ImGui::Spacing();
            }
            ImGui::PopID();
        }
        if (removeAt >= 0) edit(ed, scope, [&](Json& arr) { arr.eraseAt(size_t(removeAt)); });
    }

    void drawParams(Editor& ed, const std::string& scope, size_t i, const PrismaTypeDef& d, const PrismaEffect& e) {
        ImGui::Indent();
        ImGui::TextWrapped("%s", d.help);
        int mq = int(e.minQuality) - 1;
        const char* mqs[] = {"низкое", "среднее", "высокое"};
        if (ImGui::Combo("Минимальное качество", &mq, mqs, 3))
            edit(ed, scope, [&](Json& arr) { arr.at(i).set("minQuality", Json::string(prismaQualityName(PrismaQuality(mq + 1)))); });
        ui::help("На более низких уровнях эффект пропускается: так тяжёлые эффекты не попадают на слабые телефоны.");
        for (auto& w : warningsRu(e)) ui::toneText(ui::Tone::Warn, "%s", w.c_str());
        if (d.type == PrismaType::DepthOfField)
            ui::help("Старые focusDepth/focusRange: аппаратная глубина 0..1. Включите metricFocus для фокуса по расстоянию вдоль камеры (единицы мира); focusDistance/focusRangeMeters задают плоскость и диапазон, occlusionToleranceMeters ограничивает смешивание на границах объектов. Требуется AIVERSE_PRISMA_DOF=1; стандартный minQuality — High, но его можно переопределить. При некорректной near/far автоматически используется старый режим.");
        if (d.type == PrismaType::MotionBlur)
            ui::help("reactiveTapRejection=0 сохраняет прежнее размытие движения. При значении >0 и валидной RGBA8 reactive mask фильтр дополнительно ослабляет цветовые пробы с отличающимся покрытием прозрачных объектов/частиц. Не меняет ECS и не добавляет историю; выходной HDR alpha сохраняется. Для диагностики маски проверяются размеры/формат по metadata и доступной RHI информации, но не содержимое пикселей.");
        if (d.type == PrismaType::AutoExposure)
            ui::help("Ручной EV: включите manualExposure, задайте manualEV (-12..+12). Он заменяет Auto Exposure, применяется к линейному HDR ДО tonemapping и работает без AIVERSE_PRISMA_AUTO_EXPOSURE=1. В отличие от freezeExposure, ручной EV не требует предварительного замера. При выключении ручного режима история Auto Exposure инициализируется заново. Автоматика: HDR High + AIVERSE_PRISMA_AUTO_EXPOSURE=1; highlightRejection=0 сохраняет прежний замер, highlightStartEV/Softness задают подавление ярких выбросов. freezeExposure удерживает валидную 1x1 историю; camera cut делает новый замер. meterRegionEnabled включает круг с meterCenterX/Y, meterRadius/Feather и ненулевым meterOutsideWeight. meterBox переключает круг на прямоугольник, meterHalfWidth/Height задают его полуширину/полувысоту как доли размеров viewport. Native-камеры могут переопределить режимы по viewId.");
        int off = 0;
        for (auto& p : d.params) {
            switch (p.kind) {
            case PrismaParamKind::Number: {
                float v = e.values[size_t(off)];
                if (ImGui::SliderFloat(p.label, &v, p.lo, p.hi, "%.2f"))
                    edit(ed, scope, [&](Json& arr) { arr.at(i).set(p.name, Json::number(double(v), 3)); });
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s", p.name, p.help);
                off += 1;
                break;
            }
            case PrismaParamKind::Bool: {
                bool v = e.values[size_t(off)] > 0.5f;
                if (ui::checkbox(p.label, &v)) edit(ed, scope, [&](Json& arr) { arr.at(i).set(p.name, Json::fromBool(v)); });
                off += 1;
                break;
            }
            case PrismaParamKind::Color: {
                float c[3] = {e.values[size_t(off)], e.values[size_t(off + 1)], e.values[size_t(off + 2)]};
                const bool hdr = p.hi > 1.0f || p.lo < 0.0f;
                if (hdr ? ImGui::DragFloat3(p.label, c, 0.01f, p.lo, p.hi, "%.2f") : ImGui::ColorEdit3(p.label, c))
                    edit(ed, scope, [&](Json& arr) {
                        Json a = Json::array();
                        for (float x : c) a.push(Json::number(double(std::clamp(x, p.lo, p.hi)), 3));
                        arr.at(i).set(p.name, a);
                    });
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s", p.name, p.help);
                off += 3;
                break;
            }
            case PrismaParamKind::Text: {
                std::string s = e.text;
                if (ui::inputString(p.label, s, ImGuiInputTextFlags_EnterReturnsTrue))
                    edit(ed, scope, [&](Json& arr) { arr.at(i).set(p.name, Json::string(s)); });
                ui::help("Путь в папке игры, Enter — применить. Нейтральная таблица для правки: кнопка ниже (или MCP postfx_lut_neutral).");
                if (ImGui::SmallButton("Создать нейтральную LUT")) {
                    Json r = ed.ws().call("postfx_lut_neutral", Json::object());
                    if (r.has("path")) edit(ed, scope, [&](Json& arr) { arr.at(i).set(p.name, r["path"]); });
                }
                break;
            }
            }
        }
        ImGui::Unindent();
    }

    void drawAdd(Editor& ed, const std::string& scope) {
        ImGui::Spacing();
        if (ImGui::BeginCombo("##add", "Добавить эффект…")) {
            for (auto& d : prismaCatalog()) {
                char label[160];
                std::snprintf(label, sizeof label, "%s  (%s)", d.label, costRu(d.cost));
                if (ImGui::Selectable(label)) {
                    Json p = Json::object();
                    p.set("type", Json::string(d.id));
                    p.set("scope", Json::string(scope));
                    if (d.type == PrismaType::Lut) {
                        Json r = ed.ws().call("postfx_lut_neutral", Json::object());
                        p.set("texture", r.has("path") ? r["path"] : Json::string("postfx/neutral_lut.png"));
                    }
                    lastAnswer_ = ed.ws().call("postfx_add", p);
                }
                if (ImGui::IsItemHovered()) {
                    const PrismaEffect e = defaultPrismaEffect(d.type);
                    ImGui::SetTooltip("%s\nслабый телефон ~%.1f мс, средний ~%.1f мс, ПК ~%.2f мс за кадр", d.help,
                                      double(prismaMs(prismaEffectWork(e, PrismaQuality::Low, true), prismaProfiles()[0])),
                                      double(prismaMs(prismaEffectWork(e, PrismaQuality::Medium, true), prismaProfiles()[1])),
                                      double(prismaMs(prismaEffectWork(e, PrismaQuality::High, true), prismaProfiles()[2])));
                }
            }
            ImGui::EndCombo();
        }
        if (lastAnswer_.has("error")) ui::toneText(ui::Tone::Error, "%s", lastAnswer_.getStr("error").c_str());
    }

    // PRISMA camera volumes share the game's/map's normal save and undo semantics.
    Json& volumeBlockMut(Editor& ed, const std::string& scope) {
        Json& owner = scope == "map" ? ed.doc().data.extra : ed.ws().project.manifest;
        if (!owner["prismaVolumes"].isObject()) owner.set("prismaVolumes", Json::object());
        Json& block = *owner.find("prismaVolumes");
        if (!block["volumes"].isArray()) block.set("volumes", Json::array());
        return block;
    }
    template <class F> void editVolumes(Editor& ed, const std::string& scope, F&& fn) {
        if (scope == "map" && !dirty_) ed.doc().beginEdit();
        fn(volumeBlockMut(ed, scope));
        if (scope == "map") ed.doc().touched();
        dirty_ = true;
        dirtyScope_ = scope;
    }
    template <class F> void editVolumeEffect(Editor& ed, const std::string& scope,
                                             size_t volume, size_t effect, F&& fn) {
        editVolumes(ed, scope, [&](Json& block) {
            Json& item = block.member("volumes").at(volume);
            Json& profile = item.member("profile");
            fn(profile.member("effects").at(effect));
        });
    }
    static Json vec3Json(const float* p) {
        Json a = Json::array();
        for (int i = 0; i < 3; ++i) a.push(Json::number(double(p[i]), 3));
        return a;
    }
    static Json vec3Json(float3 p) {
        const float xyz[3]={p.x,p.y,p.z};
        return vec3Json(xyz);
    }
    static void readVec3(const Json& obj, const char* key, float* p, float defaultValue) {
        for (int i = 0; i < 3; ++i)
            p[i] = obj[key].isArray() && obj[key].size() == 3 ?
                float(obj[key][size_t(i)].toDouble(defaultValue)) : defaultValue;
    }
    void drawVolumeEffects(Editor& ed, const std::string& scope, size_t vi, const Json& volume) {
        const Json& profile = volume["profile"];
        const Json& effects = profile["effects"];
        int removed = -1;
        if (effects.isArray()) for (size_t ei = 0; ei < effects.size(); ++ei) {
            const Json& raw = effects[ei];
            const PrismaTypeDef* def = prismaType(raw.getStr("type"));
            ImGui::PushID(int(ei));
            if (!def) {
                ImGui::TextDisabled("Неизвестный эффект: %s", raw.getStr("type").c_str());
                ImGui::PopID();
                continue;
            }
            if (ImGui::TreeNode("volume-effect", "%s", def->label)) {
                bool enabled = raw.getBool("enabled", true);
                if (ui::checkbox("Включен", &enabled))
                    editVolumeEffect(ed, scope, vi, ei, [&](Json& item) {
                        item.set("enabled", Json::fromBool(enabled));
                    });
                PrismaEffect value = parsePrismaEffect(raw, false);
                size_t at = 0;
                for (const auto& p : def->params) {
                    ImGui::PushID(p.name);
                    const bool authored = raw.has(p.name);
                    bool use = authored;
                    if (ui::checkbox("##override", &use)) {
                        editVolumeEffect(ed, scope, vi, ei, [&](Json& item) {
                            if (use) {
                                if (p.kind == PrismaParamKind::Color) item.set(p.name, vec3Json(&value.values[at]));
                                else if (p.kind == PrismaParamKind::Text) item.set(p.name, Json::string(value.text));
                                else if (p.kind == PrismaParamKind::Bool)
                                    item.set(p.name, Json::fromBool(value.values[at] > .5f));
                                else item.set(p.name, Json::number(double(value.values[at]), 3));
                            } else item.erase(p.name);
                        });
                    }
                    ImGui::SameLine();
                    if (!use) ImGui::BeginDisabled();
                    if (p.kind == PrismaParamKind::Number) {
                        float v = value.values[at];
                        if (ImGui::SliderFloat(p.label, &v, p.lo, p.hi, "%.3f"))
                            editVolumeEffect(ed,scope,vi,ei,[&](Json& item) {
                                item.set(p.name,Json::number(double(v),3));
                            });
                        ++at;
                    } else if (p.kind == PrismaParamKind::Color) {
                        float rgb[3]={value.values[at],value.values[at+1],value.values[at+2]};
                        if ((p.lo < 0.f || p.hi > 1.f) ?
                            ImGui::DragFloat3(p.label,rgb,.01f,p.lo,p.hi) :
                            ImGui::ColorEdit3(p.label,rgb))
                            editVolumeEffect(ed,scope,vi,ei,[&](Json& item) {
                                item.set(p.name,vec3Json(rgb));
                            });
                        at+=3;
                    } else if (p.kind == PrismaParamKind::Bool) {
                        bool v=value.values[at]>.5f;
                        if (ui::checkbox(p.label,&v))
                            editVolumeEffect(ed,scope,vi,ei,[&](Json& item) {
                                item.set(p.name,Json::fromBool(v));
                            });
                        ++at;
                    } else {
                        std::string v = value.text;
                        if (ui::inputString(p.label,v,ImGuiInputTextFlags_EnterReturnsTrue))
                            editVolumeEffect(ed,scope,vi,ei,[&](Json& item) {
                                item.set(p.name,Json::string(v));
                            });
                    }
                    if (!use) ImGui::EndDisabled();
                    ImGui::PopID();
                }
                if (ImGui::SmallButton("Удалить эффект")) removed = int(ei);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removed >= 0) editVolumes(ed,scope,[&](Json& b) {
            b.member("volumes").at(vi).member("profile").member("effects").eraseAt(size_t(removed));
        });
        if (ImGui::BeginCombo("Добавить эффект в объём##volAdd","Выбрать тип…")) {
            for (const auto& def : prismaCatalog()) {
                bool exists = false;
                if (effects.isArray()) for (const Json& e : effects.items())
                    if (e.getStr("type") == def.id) { exists = true; break; }
                if (exists) continue;
                if (ImGui::Selectable(def.label))
                    editVolumes(ed,scope,[&](Json& b) {
                        Json effect = Json::object();
                        effect.set("type",Json::string(def.id));
                        Json& p=b.member("volumes").at(vi).member("profile");
                        if (!p["effects"].isArray()) p.set("effects",Json::array());
                        p.member("effects").push(std::move(effect));
                    });
            }
            ImGui::EndCombo();
        }
    }
    void drawVolumes(Editor& ed, const std::string& scope) {
        ui::section(nullptr, "PRISMA Volumes");
        const Json& owner=scope=="map" ? ed.doc().data.extra : ed.ws().project.manifest;
        const Json block = owner["prismaVolumes"];
        const Json entries=block["volumes"];
        if (scope=="map") {
            bool replace=block.getBool("replace",false);
            if (ui::checkbox("Заменить объёмы игры",&replace))
                editVolumes(ed,scope,[&](Json& b){b.set("replace",Json::fromBool(replace));});
        }
        std::vector<std::string> warnings;
        const auto stack=parsePrismaVolumes(ed.ws().project.manifest["prismaVolumes"],
                                            ed.doc().data.extra["prismaVolumes"],&warnings);
        const auto eye=ed.sceneCam.eye();
        const auto s=stack.resolve(ed.prisma().baseSettings(),eye);
        ImGui::TextDisabled("Активных в Scene: %zu эффектов | объёмов: %zu",
                            s.activeAt(tier_).size(),stack.volumes.size());
        for(const auto& issue:warnings) ui::toneText(ui::Tone::Warn,"%s",issue.c_str());
        int remove=-1;
        if (entries.isArray()) for(size_t i=0;i<entries.size();++i) {
            const Json& v=entries[i];
            ImGui::PushID(int(i));
            const std::string label=v.getStr("id","volume "+std::to_string(i));
            if (ui::collapsing((label+"###volume").c_str())) {
                std::string id=label;
                if(ui::inputString("ID",id,ImGuiInputTextFlags_EnterReturnsTrue))
                    editVolumes(ed,scope,[&](Json& b){
                        b.member("volumes").at(i).set("id",Json::string(id));
                    });
                bool enabled=v.getBool("enabled",true);
                if(ui::checkbox("Активен",&enabled))
                    editVolumes(ed,scope,[&](Json& b){
                        b.member("volumes").at(i).set("enabled",Json::fromBool(enabled));
                    });
                static const char* shapes[]={"Глобальный","Сфера","Коробка"};
                const std::string shape=v.getStr("shape","global");
                int shapeIndex=shape=="sphere"?1:shape=="box"?2:0;
                if(ImGui::Combo("Форма",&shapeIndex,shapes,3))
                    editVolumes(ed,scope,[&](Json& b){
                        Json& item=b.member("volumes").at(i);
                        item.set("shape",Json::string(shapeIndex==0?"global":shapeIndex==1?"sphere":"box"));
                        if (shapeIndex!=0 && !item["center"].isArray()) {
                            auto p=ed.sceneCam.eye(); item.set("center",vec3Json(p));
                        }
                        if(shapeIndex==1 && !item.has("radius"))
                            item.set("radius",Json::number(8.0));
                        if(shapeIndex==2 && !item["halfExtents"].isArray()) {
                            const float e[3]={8,8,8};item.set("halfExtents",vec3Json(e));
                        }
                    });
                if(shapeIndex!=0) {
                    float center[3];readVec3(v,"center",center,0);
                    if(ImGui::DragFloat3("Центр",center,.1f))
                        editVolumes(ed,scope,[&](Json& b){
                            b.member("volumes").at(i).set("center",vec3Json(center));
                        });
                    if(ImGui::SmallButton("В центр камеры Scene")) {
                        const auto p=ed.sceneCam.eye();
                        editVolumes(ed,scope,[&](Json& b){
                            b.member("volumes").at(i).set("center",vec3Json(p));
                        });
                    }
                    if(shapeIndex==1) {
                        float radius=float(v["radius"].toDouble(8));
                        if(ImGui::DragFloat("Радиус",&radius,.1f,0.001f,100000.f))
                            editVolumes(ed,scope,[&](Json& b){
                                b.member("volumes").at(i).set("radius",Json::number(double(std::max(.001f,radius)),3));
                            });
                    } else {
                        float extents[3];readVec3(v,"halfExtents",extents,8);
                        if(ImGui::DragFloat3("Полуразмер",extents,.1f,.001f,100000.f))
                            editVolumes(ed,scope,[&](Json& b){
                                for(auto& x:extents)x=std::max(.001f,x);
                                b.member("volumes").at(i).set("halfExtents",vec3Json(extents));
                            });
                    }
                }
                float weight=float(v["weight"].toDouble(1));
                if(ImGui::SliderFloat("Вес",&weight,0,1))
                    editVolumes(ed,scope,[&](Json& b){
                        b.member("volumes").at(i).set("weight",Json::number(double(weight),3));
                    });
                float blend=float(v["blendDistance"].toDouble(0));
                if(ImGui::DragFloat("Дистанция смешивания",&blend,.1f,0,100000))
                    editVolumes(ed,scope,[&](Json& b){
                        b.member("volumes").at(i).set("blendDistance",Json::number(double(std::max(0.f,blend)),3));
                    });
                int priority=int(v.getInt("priority"));
                if(ImGui::DragInt("Приоритет",&priority,1,-100000,100000))
                    editVolumes(ed,scope,[&](Json& b){
                        b.member("volumes").at(i).set("priority",Json::number(int64_t(priority)));
                    });
                // Json::integer() is Fixed-based; use the authored literal for full 0xffffffff.
                uint32_t mask=0xffffffffu;
                if(v.has("layerMask")) {
                    try {mask=uint32_t(std::stoull(v["layerMask"].numberLiteral()));}
                    catch (...) { mask=0xffffffffu; }
                }
                if(ImGui::InputScalar("Маска камеры",ImGuiDataType_U32,&mask))
                    editVolumes(ed,scope,[&](Json& b){
                        b.member("volumes").at(i).set("layerMask",Json::numberText(std::to_string(mask)));
                    });
                drawVolumeEffects(ed,scope,i,v);
                if(ImGui::SmallButton("Удалить объём"))remove=int(i);
            }
            ImGui::PopID();
        }
        if(remove>=0)editVolumes(ed,scope,[&](Json& b){
            b.member("volumes").eraseAt(size_t(remove));
        });
        const char* types[]={"Глобальный","Сфера у камеры","Коробка у камеры"};
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##new-volume-shape",&volumeShape_,types,3);
        if(ImGui::Button("Добавить PRISMA Volume",ImVec2(-FLT_MIN,0))) {
            const auto pos=ed.sceneCam.eye();
            editVolumes(ed,scope,[&](Json& b){
                Json& arr=b.member("volumes");
                int next=1;
                std::string id;
                do {
                    id=(scope=="map"?"map_volume_":"game_volume_")+std::to_string(next++);
                    bool found=false;
                    for(const auto& item:arr.items())
                        if(item.getStr("id")==id){found=true;break;}
                    if(!found)break;
                }while(next<100000);
                arr.push(makePrismaVolumeJson(id,
                    volumeShape_ == 0 ? PrismaVolumeShape::Global :
                    volumeShape_ == 1 ? PrismaVolumeShape::Sphere : PrismaVolumeShape::Box, pos));
            });
        }
    }

    void drawCost(Editor& ed) {
        ui::section(nullptr, "Стоимость кадра (игра + карта)");
        const PrismaSettings s = parsePrisma(ed.ws().project.manifest["postfx"], ed.doc().data.extra["postfx"]);
        if (ImGui::BeginTable("cost", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Устройство", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Эфф.");
            ImGui::TableSetupColumn("мс");
            ImGui::TableSetupColumn("из 60 fps");
            ImGui::TableHeadersRow();
            for (size_t k = 0; k < prismaProfiles().size(); ++k) {
                const auto& p = prismaProfiles()[k];
                PrismaWork sum;
                const auto active = s.activeAt(p.tier);
                if (!active.empty()) {
                    sum = prismaBaseWork();
                    for (auto* e : active) { const PrismaWork w = prismaEffectWork(*e, p.tier); sum.units += w.units; sum.passes += w.passes; }
                }
                const float ms = prismaMs(sum, p);
                const float budget = k == 2 ? 1.5f : 3.0f;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(kDeviceRu[k]);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kDeviceTip[k]);
                ImGui::TableNextColumn(); ImGui::Text("%zu", active.size());
                ImGui::TableNextColumn(); ui::toneText(ms > budget ? ui::Tone::Error : ui::Tone::Ok, "%.2f", double(ms));
                ImGui::TableNextColumn(); ImGui::Text("%.0f fps", double(1000.0f / (1000.0f / 60.0f + ms)));
            }
            ImGui::EndTable();
        }
        ui::help("Оценка по модели заполнения (engine/render/PrismaData.cpp): один полноэкранный проход с одной выборкой ~0.8 мс на слабом "
                 "телефоне в 720p. Лёгкие эффекты собраны в один общий проход, тяжёлые добавляют свои.");
        for (auto& is : issues_) ui::toneText(ui::Tone::Warn, "%s", is.c_str());
    }

    static std::vector<std::string> warningsRu(const PrismaEffect& e) {
        std::vector<std::string> out;
        const PrismaTypeDef& d = prismaType(e.type);
        const float weak = prismaMs(prismaEffectWork(e, PrismaQuality::Low, true), prismaProfiles()[0]);
        const float mid = prismaMs(prismaEffectWork(e, PrismaQuality::Medium, true), prismaProfiles()[1]);
        char b[320];
        if (d.cost == PrismaCostClass::Heavy) {
            std::snprintf(b, sizeof b, "Тяжёлый эффект: ~%.1f мс за кадр на среднем телефоне, ~%.1f мс на слабом.", double(mid), double(weak));
            out.push_back(b);
            if (e.minQuality == PrismaQuality::Low) {
                std::snprintf(b, sizeof b, "Минимальное качество «низкое» включает его и на слабых телефонах (%.0f%% кадра при 60 fps). Лучше «среднее» или «высокое».",
                              double(weak / 16.7f * 100.0f));
                out.push_back(b);
            }
        } else if (d.cost == PrismaCostClass::Medium && e.minQuality == PrismaQuality::Low && weak >= 1.0f) {
            std::snprintf(b, sizeof b, "~%.1f мс на слабом телефоне; минимальное качество «среднее» его там пропустит.", double(weak));
            out.push_back(b);
        }
        return out;
    }

    void registerOps(Editor& ed) {
        // the core registers a stub (aiversemcp lists it); the window replaces it with the real preview
        ed.ws().registerOp({"view_postfx", "Editor preview of post-processing (does not change the game): quality off | low | medium | high shows what "
                            "weak phones (low) or strong devices (high) get; scene=false hides effects in the Scene view (Game view keeps them). "
                            "Combine with view_screenshot to compare tiers.",
                            Json::parse(R"({"properties": {"quality": {"type": "string", "enum": ["off", "low", "medium", "high"]}, "scene": {"type": "boolean"}}})"),
                            [this](const Json& p) {
                                PrismaQuality q;
                                if (p.has("quality")) {
                                    if (!parsePrismaQuality(p.getStr("quality"), q)) throw std::runtime_error("quality must be off, low, medium or high");
                                    tier_ = q;
                                }
                                if (p.has("scene")) inScene_ = p.getBool("scene", true);
                                Json r = Json::object();
                                r.set("ok", Json::fromBool(true));
                                r.set("quality", Json::string(prismaQualityName(tier_)));
                                return r;
                            }, true});
    }

    bool registered_ = false, inScene_ = true, dirty_ = false;
    int scope_ = 0, preset_ = 0, volumeShape_ = 1;
    PrismaQuality tier_ = PrismaQuality::High;
    std::string projectDir_, lastKey_, dirtyScope_;
    std::vector<std::string> issues_;
    Json lastAnswer_;
};

AIVERSE_EDITOR_WINDOW(PrismaWindow);

} // namespace aiverse::editor

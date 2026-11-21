// PHYSIS debug view — opt-in read-only nature fields in the universal editor.
// Live game ECS is NEVER mutated here. The offline demo owns an independent
// World and can be stepped explicitly, even when the current project has no PHYSIS.
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../PhysisSceneOverlay.h"
#include "../PhysisFieldEditor.h"
#include "../PhysisLiveEdit.h"
#include "../PhysisRemoteEdit.h"
#include "engine/physis/PhysisDebugPalette.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <limits>

namespace aiverse::editor {
namespace {

using namespace aiverse::physis;

constexpr const char* kLayers[] = {
    "Температура, °C", "Влажность", "Давление, hPa", "Дождь",
    "Снег", "Влажность грунта", "Снежный покров", "Лёд",
    "Скорость ветра, м/с", "Вертикальный ветер", "Гравитация, м/с²",
    "Пожар", "Землетрясение", "Локальное затопление PHYSIS"
};
constexpr const char* kPalettes[] = {"Тепловая", "Контрастная", "Вода"};

ImU32 colorToImU32(DebugColor c) { return IM_COL32(c.r, c.g, c.b, c.a); }

} // namespace

class PhysisWindow final : public EditorWindow {
public:
    const char* id() const override { return "physis"; }
    const char* title() const override { return "PHYSIS · Природа"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 16; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        const aiverse::World* live = nullptr;
        if (auto* sim = ed.sim()) {
            const auto& w = sim->world();
            if (w.resourceConst<State>()) live = &w;
        }

        ui::heading("PHYSIS · диагностика природы");
        ImGui::TextDisabled("Состояние из ECS · без изменения игры");
        if (!live && !demo_)
            ui::hint("В текущей игре нет активной PHYSIS. Создайте независимое демо для просмотра.");
        if (ImGui::Button("Создать / сбросить демо"))
            makeDemo();

        if (demo_) {
            if (live) {
                ImGui::SameLine();
                if (ImGui::Checkbox("Показывать демо", &showDemo_)) dirty_ = true;
            } else {
                showDemo_ = true;
            }
        }

        drawNaturePreview(ed);

        // Project-authored wind/storm/gravity sources are always editable,
        // including edit mode where no running PHYSIS ECS World exists.
        drawPhysisFieldsPanel(ed);
        drawLiveEdit(ed);
        drawRemoteEdit(ed);

        const aiverse::World* world = showDemo_ && demo_ ? demo_.get() : live;
        if (!world) {
            ui::hint("Для отображения реальных данных PHYSIS необходимо подключить её к GameModule::setup.");
            return;
        }
        const bool demonstration = world == demo_.get();
        ImGui::TextColored(demonstration ? ImVec4(0.92f, 0.76f, 0.35f, 1) :
                                           ImVec4(0.35f, 0.78f, 0.73f, 1),
                            "%s", demonstration ? "Изолированная демонстрация" :
                                                   "Живое состояние игрового мира (только чтение)");
        const auto* state = world->resourceConst<State>();
        if (!state) return;

        if (demonstration) {
            if (ImGui::Button("Следующий тик")) {
                advanceEnvironmentWithStormsAndDisasters(*demo_,
                                                         FloodMode::ExternalAuthority);
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+60 тиков")) {
                for (int i = 0; i < 60; ++i)
                    advanceEnvironmentWithStormsAndDisasters(*demo_,
                                                             FloodMode::ExternalAuthority);
                dirty_ = true;
            }
            ImGui::TextDisabled("Демо не пишет в карту, World проекта или Box3D");
        }

        int layer = static_cast<int>(layer_);
        if (ImGui::Combo("Поле", &layer, kLayers, IM_ARRAYSIZE(kLayers))) {
            layer_ = static_cast<DiagnosticLayer>(layer);
            dirty_ = true;
        }
        if (ImGui::Combo("Палитра", &palette_, kPalettes, IM_ARRAYSIZE(kPalettes))) {
            dirty_ = true;
        }
        if (ImGui::SliderInt("Масштаб сетки", &stride_, 1, 8)) dirty_ = true;
        if (ImGui::SliderInt("Размер окна (ячейки)", &viewSize_, 8, 64)) dirty_ = true;
        if (ImGui::InputInt("Центр X", &centerX_)) dirty_ = true;
        if (ImGui::InputInt("Центр Z", &centerZ_)) dirty_ = true;
        ImGui::Checkbox("Контуры ветра, штормов и гравитации", &showVolumes_);
        if (ImGui::Button("Обновить данные")) {
            dirty_ = true;
            physisSceneOverlayState().dirty = true;
        }
        ui::section(ICON_SCENE, "PHYSIS в окне «Сцена»");
        auto& sceneOverlay = physisSceneOverlayState();
        const bool overlaySource = live || ed.physisNaturePreview().previewWorld();
        ImGui::BeginDisabled(!overlaySource);
        if (ImGui::Checkbox("Показать 3D-оверлей в «Сцене»", &sceneOverlay.enabled))
            sceneOverlay.dirty = true;
        if (sceneOverlay.enabled) {
            if (ImGui::SliderInt("Радиус в ячейках##physisScene", &sceneOverlay.radiusCells,
                                 6, 24)) sceneOverlay.dirty = true;
            if (ImGui::SliderInt("Шаг##physisScene", &sceneOverlay.stride, 1, 4))
                sceneOverlay.dirty = true;
            if (ImGui::SliderFloat("Прозрачность##physisScene", &sceneOverlay.alpha,
                                   0.1f, 0.8f)) sceneOverlay.dirty = true;
            ImGui::Checkbox("Поля / контуры##physisScene", &sceneOverlay.drawVolumes);
        }
        ImGui::EndDisabled();
        if (!live)
            ui::hint(overlaySource ? "3D-оверлей показывает предпросмотр «Живой природы»."
                                   : "3D-оверлей: включите «Живую природу» или запустите игру с PHYSIS.");
        if (sceneOverlay.layer != layer_ ||
            sceneOverlay.palette != static_cast<DebugPalette>(palette_)) {
            sceneOverlay.layer = layer_;
            sceneOverlay.palette = static_cast<DebugPalette>(palette_);
            sceneOverlay.dirty = true;
        }
        if (ImGui::Button("Настройки PHYSIS в Инспекторе")) {
            ed.select({Selection::MapProps, -1, {}});
            ed.focusWindow("inspector");
        }

        // Refresh only when input, World identity or committed tick changed.
        // Mutations without a new tick (editor edits or rollback-to-same-tick)
        // require explicit refresh. No pointer into mutable ECS is cached.
        if (cachedSource_ != world || cachedTick_ != state->tick)
            dirty_ = true;
        if (dirty_) {
            DiagnosticRequest request;
            // Clamp user-entered centres before narrowing (INT_MIN/INT_MAX).
            const auto safeStart = [&](int center) {
                return static_cast<std::int32_t>(std::clamp<std::int64_t>(
                    std::int64_t(center) - viewSize_ / 2,
                    std::numeric_limits<std::int32_t>::min(),
                    std::int64_t(std::numeric_limits<std::int32_t>::max()) - viewSize_));
            };
            request.originX = safeStart(centerX_);
            request.originZ = safeStart(centerZ_);
            request.width = viewSize_;
            request.height = viewSize_;
            request.stride = stride_;
            request.layer = layer_;
            // Build a transient COW snapshot for accelerated vector layers.
            // Scalar layers already read ECS cells directly (Stage 15).
            if (diagnosticNeeds3DField(layer_)) {
                FieldSpatialIndex index(*world);
                valid_ = sampleDiagnosticRasterIndexed(index, request, raster_);
            } else {
                valid_ = sampleDiagnosticRaster(*world, request, raster_);
            }
            cachedSource_ = world;
            cachedTick_ = state->tick;
            dirty_ = false;
        }
        if (!valid_) {
            ui::hint("Невозможно прочитать поле: проверьте границы и размер.");
            return;
        }
        ImGui::TextDisabled("Tick %llu · доступно %u/%zu · диапазон %.3f .. %.3f",
                            static_cast<unsigned long long>(raster_.tick),
                            raster_.sampleCount, raster_.valid.size(),
                            Fixed::fromRaw(raster_.minRaw).toDouble(),
                            Fixed::fromRaw(raster_.maxRaw).toDouble());
        drawMap(*world);
        if (raster_.sampleCount == 0)
            ui::hint("В выбранной области нет ячеек PHYSIS.");
        if (selected_ >= 0 && std::size_t(selected_) < raster_.valid.size() &&
            raster_.valid[std::size_t(selected_)]) {
            const auto col = selected_ % raster_.width;
            const auto row = selected_ / raster_.width;
            const auto x = raster_.originX + col * raster_.stride;
            const auto z = raster_.originZ + row * raster_.stride;
            ImGui::Separator();
            ImGui::Text("Ячейка X %d · Z %d", x, z);
            ImGui::Text("Значение: %s", Fixed::fromRaw(
                raster_.rawValues[std::size_t(selected_)]).toString().c_str());
            const auto entity = cellAt(*world, x, z);
            if (const auto* cell = world->tryGetConst<Cell>(entity)) {
                const std::int64_t meters = state->settings.cellSizeMeters;
                const std::int64_t mx = std::int64_t(x) * meters + meters / 2;
                const std::int64_t mz = std::int64_t(z) * meters + meters / 2;
                const auto maxMeters = std::numeric_limits<std::int64_t>::max() /
                                       Fixed::kOne / 2;
                if (mx >= -maxMeters && mx <= maxMeters &&
                    mz >= -maxMeters && mz <= maxMeters) {
                    const Vec3 position{Fixed::fromInt(mx), cell->elevation,
                                        Fixed::fromInt(mz)};
                    NatureFieldSample detail;
                    if (sampleNatureField(*world, position, detail)) {
                    ImGui::TextDisabled("Ветер: %.2f, %.2f, %.2f м/с",
                        detail.windVelocity.x.toDouble(), detail.windVelocity.y.toDouble(),
                        detail.windVelocity.z.toDouble());
                    ImGui::TextDisabled("Гравитация: %.2f, %.2f, %.2f м/с²",
                        detail.gravityAcceleration.x.toDouble(),
                        detail.gravityAcceleration.y.toDouble(),
                        detail.gravityAcceleration.z.toDouble());
                    }
                }
            }
        }
        ImGui::Spacing();
        ui::hint("PHYSIS не заменяет NEREID: «локальное затопление» — только отдельная модель PHYSIS.");
    }

private:
    // Live edits go through a GameModule-opted-in Simulation command, never
    // from the Scene overlay into the authoritative World directly.
    void drawRemoteEdit(Editor& ed) {
        ui::section(ICON_GLOBE,"PHYSIS · Remote Editing (только API)");
        auto& remote=physis_remote::link();
        // Review 2026-11-21: nothing in the shipped editor/engine calls
        // link().attachAuthenticated(); be explicit instead of implying a
        // connection may appear by itself.
        if(!remote.attached() && !remote.authenticating())
            ui::hint("Только API: в этой сборке редактор не подключает канал "
                     "PHYSIS. Нужен хост проекта, вызывающий "
                     "physis_remote::link().attachAuthenticated().");
        // Poll the handshake even before EditorLink is fully authenticated.
        // Otherwise a host-started loopback connector could never finish.
        remote.poll(::aiverse::net::nowMs());
        if(!remote.attached()) {
            ui::hint(remote.authenticating() ?
                "Проверяем полномочия и защищённый канал PHYSIS..." :
                "Нет авторизованного соединения с сервером PHYSIS.");
            ui::hint("Сетевой хост подключает канал после проверки доступа.");
            const auto explanation=remote.message();
            if(!explanation.empty())
                ImGui::TextUnformatted(explanation.data(),
                                       explanation.data()+explanation.size());
            return;
        }
        // PHYSIS has its own admin protocol, not normal InputFrame.
        const auto* client=remote.client();
        if(!client)return;
        const auto status=client->status();
        const char* statusLabel="Ожидает";
        using ClientStatus=physis::remote::ClientStatus;
        switch(status) {
            case ClientStatus::Idle: statusLabel="Готово";break;
            case ClientStatus::Offline: statusLabel="Нет соединения";break;
            case ClientStatus::AwaitingReceipt: statusLabel="Ждём ACK";break;
            case ClientStatus::Queued: statusLabel="В очереди сервера";break;
            case ClientStatus::Applied: statusLabel="Применено сервером";break;
            case ClientStatus::Rejected: statusLabel="Отклонено";break;
            case ClientStatus::Conflict: statusLabel="Конфликт ревизии";break;
            case ClientStatus::Denied: statusLabel="Недостаточно прав";break;
            case ClientStatus::Uncertain: statusLabel="Ответ не получен";break;
        }
        ImGui::Text("Соединение: %s",remote.connected()?"подключено":"отключено");
        ImGui::Text("Статус: %s",statusLabel);
        if(client->hasRemoteRevision())
            ImGui::TextDisabled("Ревизия сервера: %llu",
                static_cast<unsigned long long>(client->remoteRevision()));
        else ImGui::TextDisabled("Ревизия сервера ещё не подтверждена.");
        if(client->requestId())
            ImGui::TextDisabled("ID запроса: %llu",
                static_cast<unsigned long long>(client->requestId()));
        if(!client->receipt().detail.empty())
            ImGui::TextWrapped("%s",client->receipt().detail.c_str());
        const auto msg=remote.message();
        if(!msg.empty())
            ImGui::TextUnformatted(msg.data(),msg.data()+msg.size());
        ImGui::BeginDisabled(!remote.connected() || client->waiting());
        if(ImGui::SmallButton("Запросить ревизию##physisRemote"))
            remote.refreshRevision();
        ImGui::EndDisabled();
        const bool canApply=remote.connected() &&
                            client->hasRemoteRevision() &&
                            !client->waiting() &&
                            status!=ClientStatus::Uncertain;
        ImGui::BeginDisabled(!canApply);
        if(ImGui::Button("Отправить поля на сервер##physisRemote")) {
            std::string error;
            (void)remote.submit(ed.doc().toJson(),&error);
        }
        ImGui::EndDisabled();
        if(status==ClientStatus::Uncertain &&
            ImGui::Button("Повторить с тем же ID##physisRemote"))
            remote.retryOutstanding();
        ui::hint("Queued не означает Applied. Повторы сохраняют ID и содержимое.");
        ui::hint("PHYSIS не подключается к серверу без авторизованного хоста.");
    }

    void drawLiveEdit(Editor& ed) {
        ui::section(ICON_PENCIL, "PHYSIS · Live Editing (только API)");
        // Review 2026-11-21: no engine GameModule implements FieldEditHost,
        // so this panel only works for a game's own native module.
        ui::hint("Только API: встроенные GameModule движка не реализуют "
                 "FieldEditHost; правки доступны лишь нативному модулю игры, "
                 "который явно разрешил их.");
        auto* sim=ed.sim();
        liveEdit_.observe(sim);
        std::string reason;
        const bool available=physis_live::canSubmit(sim,&reason);
        if (sim && sim->world().resourceConst<FieldRuntimeState>()) {
            const auto& current=*sim->world().resourceConst<FieldRuntimeState>();
            ImGui::TextDisabled("Текущая ECS ревизия: %llu · тик: %u",
                                static_cast<unsigned long long>(current.revision),sim->tick());
            if (liveEdit_.status==physis_live::EditStatus::Queued)
                ImGui::TextDisabled("Ожидаем тик: %u · исходная ревизия: %llu",
                    liveEdit_.scheduledTick,
                    static_cast<unsigned long long>(liveEdit_.expectedRevision));
        }
        const bool pending=liveEdit_.status==physis_live::EditStatus::Queued;
        ImGui::BeginDisabled(!available || pending);
        if (ImGui::Button("Применить поля в игровую симуляцию##physisLive")) {
            std::string error;
            if (liveEdit_.submit(*sim,ed.doc().toJson(),&error)) {
                dirty_=true;
                physisSceneOverlayState().dirty=true;
            }
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Статус: %s",
            physis_live::statusName(liveEdit_.status));
        if (!liveEdit_.message.empty())
            ImGui::TextWrapped("%s",liveEdit_.message.c_str());
        if (!available) {
            ui::hint(reason.c_str());
            ui::hint("Необходим GameModule с явным разрешением на локальные правки.");
        } else if (pending) {
            ui::hint("Отправлено не означает применено. Статус обновится после нужного тика.");
        }
        ui::hint("Только поле ветра/шторма/гравитации; климат и вода здесь не обновляются.");
    }

    // «Живая природа»: real fire/wind/grass/smoke/sound in the Scene view
    // without a play-test. See apps/editor/PhysisNaturePreview.h.
    void drawNaturePreview(Editor& ed) {
        auto& nature = ed.physisNaturePreview();
        auto& st = nature.settings;
        ui::section(ICON_FLAME, "Живая природа в «Сцене»");
        if (ImGui::Checkbox("Показать огонь, ветер и траву##physisNature", &st.enabled) && st.enabled)
            nature.requestReset();
        if (!st.enabled) {
            ui::hint("Пожар по траве, который гонит ветер, дым, угли и звук — прямо в редакторе, "
                     "без запуска игры. Используется PHYSIS-блок карты (климат и поля ветра).");
            return;
        }
        ImGui::TextColored(nature.usingLive() ? ImVec4(0.35f, 0.78f, 0.73f, 1) : ImVec4(0.92f, 0.76f, 0.35f, 1),
            "%s", nature.usingLive() ? "Игровая PHYSIS (только чтение)"
                                     : "Предпросмотр: отдельный World, в игру не попадает");
        if (!nature.usingLive()) {
            ImGui::Checkbox("Идёт##physisNature", &st.playing);
            ImGui::SameLine();
            ImGui::Checkbox("Поджигать кликом (ЛКМ)##physisNature", &st.igniteOnClick);
            ImGui::SameLine();
            if (ImGui::Button("Сброс##physisNature")) nature.requestReset();
            ImGui::SliderFloat("Скорость##physisNature", &st.speed, 0.1f, 6.f, "%.1fx");
            bool wind = ImGui::SliderFloat("Ветер, м/с##physisNature", &st.windSpeed, 0.f, 30.f, "%.1f");
            wind |= ImGui::SliderFloat("Куда дует, °##physisNature", &st.windDirectionDeg, -180.f, 180.f, "%.0f");
            if (wind) nature.applyWindSettings();
            ImGui::SliderFloat("Влажность##physisNature", &st.humidity, 0.f, 1.f, "%.2f");
            ImGui::SliderInt("Детализация, м/ячейка##physisNature", &st.previewCellMeters, 0, 16,
                             st.previewCellMeters == 0 ? "сетка карты" : "%d м");
            if (ImGui::IsItemDeactivatedAfterEdit()) nature.requestReset();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 — точная сетка PHYSIS карты; 2–8 м — подробный пожар для просмотра.");
        }
        ImGui::Checkbox("Трава##physisNature", &st.grass);
        ImGui::SameLine();
        ImGui::Checkbox("Пламя и дым##physisNature", &st.flames);
        ImGui::SameLine();
        ImGui::Checkbox("Звук##physisNature", &st.sound);
        static const char* kGrassQuality[] = {"низкое", "среднее", "высокое"};
        ImGui::Combo("Качество травы##physisNature", &st.grassQuality, kGrassQuality, 3);
        if (st.sound) ImGui::SliderFloat("Громкость##physisNature", &st.volume, 0.f, 1.f, "%.2f");
        ImGui::TextDisabled("тик %llu · ячейка %.0f м · очагов %zu · дым %zu · звуков %zu · патчей травы %zu · горит ≈%.0f м²",
            (unsigned long long)nature.tick(), nature.cellMeters(), nature.flames(), nature.smoke(),
            nature.voices(), nature.grassPatches(), nature.burningArea());
        if (!nature.status().empty()) ui::hint(nature.status().c_str());
    }

    void makeDemo() {
        demo_ = std::make_unique<aiverse::World>();
        Settings config;
        config.originX = -16;
        config.originZ = -16;
        config.width = 32;
        config.height = 32;
        config.cellSizeMeters = 100;
        config.seed = 307;
        initialize(*demo_, config);
        initializeDisasters(*demo_);

        // Gradient provides a useful heatmap even without a weather tick.
        for (const auto entity : demo_->resourceConst<State>()->cells) {
            const auto* cell = demo_->tryGetConst<Cell>(entity);
            if (!cell) continue;
            ClimateZone zone;
            zone.temperatureOffset = Fixed::fromInt(cell->x);
            zone.humidityOffset = Fixed::fromRatio(cell->z, 64);
            demo_->add<ClimateZone>(entity, zone);
        }
        StormEvent storm;
        storm.kind = StormKind::Tornado;
        storm.center = {150_fx, 0_fx, 50_fx};
        storm.eyeRadiusMeters = 100_fx;
        storm.outerRadiusMeters = 900_fx;
        storm.heightMeters = 600_fx;
        storm.tangentialWind = 55_fx;
        storm.updraft = 35_fx;
        storm.pressureDrop = 45_fx;
        storm.precipitationBoost = Fixed::fromRatio(3, 4);
        demo_->add<StormEvent>(demo_->create(), storm);
        WindZone zone;
        zone.center = {-600_fx, 0_fx, -350_fx};
        zone.radiusMeters = 1100_fx;
        zone.velocity = {25_fx, 1_fx, -5_fx};
        zone.gustAmplitude = 6_fx;
        demo_->add<WindZone>(demo_->create(), zone);
        GravityField gravity;
        gravity.center = {-200_fx, 0_fx, 350_fx};
        gravity.radiusMeters = 600_fx;
        gravity.mode = GravityMode::Uniform;
        gravity.acceleration = {};
        gravity.overrideBaseline = true;
        demo_->add<GravityField>(demo_->create(), gravity);
        (void)ignite(*demo_, 2, 2);
        for (int i = 0; i < 25; ++i)
            advanceEnvironmentWithStormsAndDisasters(*demo_, FloodMode::ExternalAuthority);
        showDemo_ = true;
        centerX_ = centerZ_ = 0;
        selected_ = -1;
        dirty_ = true;
    }

    void drawMap(const aiverse::World& world) {
        const float available = ImGui::GetContentRegionAvail().x;
        if (available < 32 || raster_.width <= 0 || raster_.height <= 0) return;
        const float w = available;
        const float h = w * float(raster_.height) / float(raster_.width);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##physisHeatmap", ImVec2(w, h));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h),
                            IM_COL32(24, 28, 38, 255), 3);
        const float cw = w / float(raster_.width);
        const float ch = h / float(raster_.height);
        for (int y = 0; y < raster_.height; ++y)
            for (int x = 0; x < raster_.width; ++x) {
                const std::size_t index = std::size_t(y) *
                                          std::size_t(raster_.width) + std::size_t(x);
                const DebugColor c = diagnosticColor(raster_, index,
                                                     static_cast<DebugPalette>(palette_));
                if (!c.a) continue;
                draw->AddRectFilled(ImVec2(origin.x + x * cw, origin.y + y * ch),
                                    ImVec2(origin.x + (x + 1) * cw, origin.y + (y + 1) * ch),
                                    colorToImU32(c));
            }
        // Optional zone outlines use read-only ECS components. The overlay is
        // drawn exclusively in this panel (not in the Scene/Inspector pipeline).
        if (showVolumes_) {
            const auto* state = world.resourceConst<State>();
            if (state) {
                const double metresPerPixelCell = double(state->settings.cellSizeMeters) *
                                                  double(raster_.stride);
                const auto showCircle = [&](Vec3 center, Fixed radius, ImU32 color,
                                            const char* mark) {
                    if (radius <= Fixed{} || metresPerPixelCell <= 0) return;
                    const double cx = (center.x.toDouble() /
                                       double(state->settings.cellSizeMeters) -
                                       double(raster_.originX)) / double(raster_.stride);
                    const double cy = (center.z.toDouble() /
                                       double(state->settings.cellSizeMeters) -
                                       double(raster_.originZ)) / double(raster_.stride);
                    const float px = origin.x + float(cx) * cw;
                    const float py = origin.y + float(cy) * ch;
                    const float sx = float(radius.toDouble() / metresPerPixelCell) * cw;
                    const float sy = float(radius.toDouble() / metresPerPixelCell) * ch;
                    if (sx <= 0 || sy <= 0 || sx > 200000 || sy > 200000) return;
                    // Only circular outlines with isotropic scales are drawn;
                    // the map cells are rendered with a proportional aspect ratio.
                    draw->AddCircle(ImVec2(px, py), sx, color, 48, 1.6f);
                    draw->AddText(ImVec2(px + 3, py + 3), color, mark);
                };
                draw->PushClipRect(origin, ImVec2(origin.x + w, origin.y + h), true);
                world.eachExistingConst<WindZone>([&](Entity, const WindZone& zone) {
                    showCircle(zone.center, zone.radiusMeters, IM_COL32(95, 210, 230, 240), "W");
                });
                world.eachExistingConst<StormEvent>([&](Entity, const StormEvent& storm) {
                    if (!stormActive(storm, state->tick)) return;
                    showCircle(storm.center, storm.outerRadiusMeters,
                               IM_COL32(250, 185, 72, 240),
                               storm.kind == StormKind::Tornado ? "T" : "H");
                });
                world.eachExistingConst<GravityField>([&](Entity, const GravityField& field) {
                    if (field.enabled) showCircle(field.center, field.radiusMeters,
                                                  IM_COL32(183, 133, 249, 240), "G");
                });
                draw->PopClipRect();
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int x = std::clamp(int((mouse.x - origin.x) / cw), 0, raster_.width - 1);
            const int y = std::clamp(int((mouse.y - origin.y) / ch), 0, raster_.height - 1);
            selected_ = y * raster_.width + x;
        }
        if (selected_ >= 0 && std::size_t(selected_) < raster_.valid.size()) {
            const int x = selected_ % raster_.width;
            const int y = selected_ / raster_.width;
            const ImVec2 a(origin.x + x * cw, origin.y + y * ch);
            const ImVec2 b(a.x + cw, a.y + ch);
            draw->AddRect(a, b, IM_COL32(255, 255, 255, 235), 0, 0, 2.0f);
        }
        draw->AddRect(origin, ImVec2(origin.x + w, origin.y + h),
                      ImGui::GetColorU32(ImGuiCol_Border), 3);
    }

    physis_live::FieldEditSession liveEdit_;
    std::unique_ptr<aiverse::World> demo_;
    DiagnosticRaster raster_{};
    DiagnosticLayer layer_ = DiagnosticLayer::TemperatureCelsius;
    const aiverse::World* cachedSource_ = nullptr;
    std::uint64_t cachedTick_ = ~std::uint64_t(0);
    bool showDemo_ = false, dirty_ = true, valid_ = false, showVolumes_ = true;
    int viewSize_ = 32, stride_ = 1, centerX_ = 0, centerZ_ = 0;
    int palette_ = 0, selected_ = -1;
};

AIVERSE_EDITOR_WINDOW(PhysisWindow);

} // namespace aiverse::editor

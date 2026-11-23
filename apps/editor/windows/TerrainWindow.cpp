// Terrain sculpting and ground materials (heightfield maps): brushes raise / lower / smooth / flatten
// / noise / set on the corner heights, painting the material layer, 16-bit PNG heightmap import /
// export. Strokes happen in the Scene view while a brush here is active (the same "external tool"
// convention as the Tiles window: ed.tool == Tool::Count). Agents use the same operations:
// terrain_sculpt, terrain_material, terrain_heights, heightmap_import / heightmap_export
// (core/TerrainOps.cpp).
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/PlanetOps.h"
#include "../core/TerrainOps.h"
#include "engine/world/TerraWeaveCavernsRaycast.h"

namespace aiverse::editor {

class TerrainWindow : public EditorWindow {
public:
    const char* id() const override { return "terrain"; }
    const char* title() const override { return "Рельеф"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 5; }
    bool openByDefault() const override { return false; }

    void draw(Editor& ed) override {
        Workspace& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_TERRAIN, "Игра не открыта", "Файл → Открыть проект"); return; }
        MapData& d = ws.map.data;
        const bool hf = !d.corners.empty();
        // what the map has: one dim line + hints
        {
            std::string info;
            if (hf) { char b[96]; std::snprintf(b, sizeof b, "Высоты %d×%d углов", d.w + 1, d.h + 1); info = b; info += ws.project.rules.terrain.heightfield() ? " · влияют на игру" : " · только вид"; }
            else info = "Карты высот ещё нет — первый мазок создаст её из текущего рельефа";
            if (!ed.planetMap() && d.detailScale > 0) { char b[64]; std::snprintf(b, sizeof b, " · детально ×%d", d.detailScale); info += b; }
            ui::hint(info.c_str());
            ui::help("Высоты влияют на игру, только если в rules.json terrain.model = \"heightfield\". Иначе рельеф только визуальный.");
        }
        if (ed.planetMap()) ui::callout(ui::Tone::Accent, "Карта с планетами: кисти работают на теле под курсором в «Сцене» (переходят через рёбра граней).");

        // TerraWeave is opt-in and uses the same Workspace operation as MCP.
        // It never rewrites an existing authored heightfield without consent.
        if (!ed.planetMap() && !ws.map.data.worldSpace) {
            ui::section(ICON_TERRAIN, "TerraWeave · процедурный мир");
            ui::hint("Предпросмотр показывает диапазон высот (9×9 точек). Все режимы генерации доступны через Undo.");
            const Json& savedRecipe=d.extra["terraweave"];
            const bool canPreserve=savedRecipe.isObject() &&
                savedRecipe.getInt("widthCells",-1)==d.w &&
                savedRecipe.getInt("heightCells",-1)==d.h &&
                savedRecipe["baseFingerprint"].isString();
            if(!canPreserve)twPreserve_=false;
            if(savedRecipe.isObject() && ui::wideButton(ICON_DOWNLOAD, "Параметры из карты", 0, 2)) {
                // Load the EXACT integer centimetre values, not rounded
                // metres. Otherwise a regen could silently change a seed.
                const int64_t loadedSeed=savedRecipe.getInt("seed",1);
                if(loadedSeed<0 || loadedSeed>INT32_MAX) {
                    message_="Seed сохранённой карты не помещается в поле редактора; используйте MCP";
                } else {
                    twSeed_=int(loadedSeed);
                    twBaseCm_=int(savedRecipe.getInt("baseHeightCm",0));
                    twReliefCm_=int(savedRecipe.getInt("reliefCm",6000));
                    twPeriodCm_=int(savedRecipe.getInt("basePeriodCm",6400));
                    twOctaves_=int(savedRecipe.getInt("octaves",5));
                    twOriginXcm_=int(savedRecipe.getInt("originXcm",0));
                    twOriginZcm_=int(savedRecipe.getInt("originZcm",0));
                    twCellSizeCm_=int(savedRecipe.getInt("cellSizeCm",100));
                }
            }
            if (ui::beginProps("##terraweave")) {
                ui::prop("Seed");
                ImGui::InputInt("##twseed", &twSeed_);
                ui::prop("Средняя высота (см)");
                ImGui::InputInt("##twbase", &twBaseCm_);
                ui::prop("Перепад высот (см)");
                ImGui::InputInt("##twrelief", &twReliefCm_);
                ui::prop("Масштаб шума (см)");
                ImGui::InputInt("##twperiod", &twPeriodCm_);
                ui::prop("Октавы");
                ImGui::SliderInt("##twoctaves", &twOctaves_, 1, 12);
                ui::prop("Начало X (см)");
                ImGui::InputInt("##tworiginx", &twOriginXcm_);
                ui::prop("Начало Z (см)");
                ImGui::InputInt("##tworiginz", &twOriginZcm_);
                ui::prop("Размер ячейки (см)");
                ImGui::InputInt("##twcellsize", &twCellSizeCm_);
                ui::endProps();
            }
            twSeed_ = std::max(0, twSeed_);
            twBaseCm_ = std::clamp(twBaseCm_, -200000, 200000);
            twReliefCm_ = std::clamp(twReliefCm_, 0, 200000);
            twPeriodCm_ = std::clamp(twPeriodCm_, 100, 1000000);
            twCellSizeCm_ = std::clamp(twCellSizeCm_, 1, 10000);
            twOriginXcm_ = std::clamp(twOriginXcm_, -1000000000, 1000000000);
            twOriginZcm_ = std::clamp(twOriginZcm_, -1000000000, 1000000000);
            if(ui::checkbox("Заменить старый рельеф целиком", &twOverwrite_) && twOverwrite_)
                twPreserve_=false;
            ImGui::BeginDisabled(!canPreserve);
            if(ui::checkbox("Сохранить ручные изменения при регенерации", &twPreserve_) && twPreserve_)
                twOverwrite_=false;
            ImGui::EndDisabled();
            if(!canPreserve && savedRecipe.isObject())
                ui::hint("Старая карта без контрольной суммы или изменён размер: сначала явная генерация создаст безопасный рецепт.");
            if(twPreserve_)
                ui::hint("Ручные правки высот и детального слоя будут перенесены поверх нового рельефа. Материалы сохраняются.");
            if (ui::wideButton(ICON_IMAGE, "Предпросмотр", 0, 2)) {
                Json result = ws.call("terraweave_preview", terraweaveParams());
                report(ed, result);
            }
            if (ui::wideButton(ICON_BRUSH, "Сгенерировать", 1, 2)) {
                Json result = ws.call("terraweave_generate", terraweaveParams());
                report(ed, result);
            }
            ui::help("Сохранить правки: разница между текущими высотами и прежней процедурной основой переносится на новую. При полной замене детальный слой удаляется. Все изменения можно отменить через Undo.");
            ImGui::BeginDisabled(!d.extra["terraweave"].isObject());
            bool forest = ed.terraWeaveForestPreviewEnabled();
            if (ui::checkbox("Показывать процедурный лес в «Сцене»", &forest))
                ed.setTerraWeaveForestPreviewEnabled(forest);
            ImGui::EndDisabled();
            if (forest)
                ui::hint(("TerraWeave · растений в кадре: " +
                          std::to_string(ed.terraWeaveForestPreviewCount()) +
                          " (редактор, без изменений игрового мира)").c_str());
            else if (!d.extra["terraweave"].isObject())
                ui::hint("Для показа леса сначала сгенерируйте terrain TerraWeave.");
        }

        if (!ed.planetMap()) {
            ui::section(ICON_TERRAIN, "TerraWeave · открытый мир / Sparse");
            if(ui::checkbox("Кисть с автоматическим созданием чанков", &sparse_) && sparse_) {
                caverns_=false;setActive(ed,true);
            }
            ui::hint("Можно нажимать за пределами старой карты. Созданные чанки и высоты сохраняются в map.terraweaveSparse, доступны агентам и Undo.");
            if(sparse_) {
                static const char* sparseActions[] = {
                    "Создать чанк", "Поднять грунт", "Опустить грунт"};
                ImGui::Combo("Операция##sparse", &sparseAction_,
                             sparseActions,3);
                ImGui::SliderInt("Размер чанка (м)##sparse",
                                 &sparseChunkMetres_,16,1024);
                ui::hint("Плоский чанк: ровно 1 квад. Рельефные чанки: LOD от расстояния до камеры (33/17/9/5).");
                const Json& state=ed.doc().data.extra["terraweaveSparse"];
                const size_t count=state["chunks"].size();
                ui::hint(("Сохранённых sparse-чанков: " +
                          std::to_string(count)).c_str());
                if(ui::wideButton(ICON_TERRAIN,
                        "Создать чанк в центре камеры",0,1)){
                    Json p=Json::object();
                    p.set("xCm",Json::number(int64_t(
                        std::llround(double(ed.sceneCam.target.x)*100.0))));
                    p.set("zCm",Json::number(int64_t(
                        std::llround(double(ed.sceneCam.target.z)*100.0))));
                    p.set("mode",Json::string("create"));
                    p.set("chunkMetres",Json::number(
                        int64_t(sparseChunkMetres_)));
                    report(ed,ed.ws().call("terraweave_sparse_paint",p));
                }
            }
        }

        if (!ed.planetMap()) {
            ui::section(ICON_TERRAIN, "TerraWeave · Caverns / SDF");
            if(ui::checkbox("Кисть пещер в 3D-сцене", &caverns_)) {
                if(caverns_) { sparse_=false;setActive(ed,true); }
            }
            if(caverns_) {
                static const char* caveModes[]={"Вырезать (Carve)","Заполнить (Fill)"};
                ImGui::Combo("Операция##caverns", &caveAction_, caveModes,2);
                ImGui::SliderInt("Радиус (см)##caverns", &caveRadiusCm_,50,10000);
                ImGui::SliderInt("Глубина под плоскостью сцены (см)##caverns",
                                 &caveDepthCm_,0,50000);
                ui::hint("ЛКМ: пересечение луча камеры с поверхностью SDF пещеры; "
                         "если попадания нет, кисть не рисует. Shift меняет Carve ↔ Fill. "
                         "Undo/Redo и сохранение через MapDoc.");
                ui::hint(caveHoverValid_ ? ("Курсор: "+std::to_string(caveHoverCm_.x)+", "+
                    std::to_string(caveHoverCm_.y)+", "+std::to_string(caveHoverCm_.z)+" см").c_str():
                    "Курсор: поверхность пещеры не найдена");
                const Json& saved=ed.doc().data.extra["terraweaveCaverns"];
                ui::hint(("SDF-операций: "+std::to_string(saved["stamps"].size())).c_str());
                const auto& jobs=ed.cavernsBuildStats;
                ui::hint(("CPU mesh: ожидание "+std::to_string(jobs.pending)+
                          ", готово "+std::to_string(jobs.prepared)+
                          ", выполняется "+std::to_string(jobs.inFlight)).c_str());
                ImGui::Checkbox("3D GPU-предпросмотр пещер (только визуально)##caverns",
                                &ed.cavernsVisualPreviewEnabled);
                ui::hint("Предпросмотр GPU рисует GeometryScene без STRATA. "
                         "Box3D, SpatialWorld и NavMesh НЕ изменяются; "
                         "в игре правки пока не опубликованы.");
                ui::hint(("GPU preview: активных групп "+std::to_string(jobs.gpuActive)+
                          ", подготовлено "+std::to_string(jobs.gpuStaged)+
                          ", осталось загрузить "+std::to_string(jobs.gpuRemaining)+
                          ", ожидают GPU idle "+std::to_string(jobs.gpuRetired)).c_str());
                ui::hint(("GPU revision "+std::to_string(jobs.gpuPreviewRevision)+
                          ", failures "+std::to_string(jobs.gpuUploadFailures)+
                          ", retry frame "+std::to_string(jobs.gpuRetryAfterFrame)).c_str());
                ui::hint(("GPU frame source bytes "+std::to_string(jobs.gpuFrameBytes)+
                          ", upload rejections "+std::to_string(jobs.gpuRejectedBytes)+
                          ", owner pressure "+std::to_string(jobs.gpuRejectedPressure)+
                          ", STRATA idle pending "+std::to_string(jobs.gpuRetainedStrata)).c_str());
                if(!jobs.diagnostic.empty())
                    ui::hint(("Caverns: "+jobs.diagnostic).c_str());
                if(ui::wideButton(ICON_TERRAIN,"Штамп в центре камеры",0,1)){
                    Json p=Json::object();
                    p.set("mode",Json::string(caveAction_==0?"carve":"fill"));
                    p.set("xCm",Json::number(int64_t(std::llround(double(ed.sceneCam.target.x)*100.))));
                    p.set("yCm",Json::number(int64_t(std::llround(double(ed.sceneCam.target.y)*100.))-caveDepthCm_));
                    p.set("zCm",Json::number(int64_t(std::llround(double(ed.sceneCam.target.z)*100.))));
                    p.set("radiusCm",Json::number(int64_t(caveRadiusCm_)));
                    report(ed,ed.ws().call("terraweave_caverns_paint",p));
                }
            }
        }

        bool active = ed.tool == Tool::Count && owner_;
        ImGui::PushStyleColor(ImGuiCol_Text, active ? ui::toneColor(ui::Tone::Accent) : ImGui::GetStyle().Colors[ImGuiCol_Text]);
        if (ui::checkbox(ICON_BRUSH "  Кисть рельефа в «Сцене»", &active)) {
            if(active){caverns_=false;sparse_=false;}
            setActive(ed, active);
        }
        ImGui::PopStyleColor();
        if (!ed.planetMap()) {
            ui::checkbox("Детальная карта высот", &detail_);
            ImGui::SetItemTooltip("Кисти работают по детальной карте высот (k точек на клетку, мягкий край), а не по углам клеток. "
                                  "Первый мазок создаёт её из того, что нарисовано сейчас. На картах heightfield углы клеток следуют за ней.");
        }
        static const char* opsLabels[] = {"Поднять", "Опустить", "Сгладить", "Выровнять", "Шум", "Задать высоту", "Материал", "Эрозия", "Штамп"};
        const int nOps = detail_ && !ed.planetMap() ? 9 : 7;
        if (mode_ >= nOps) mode_ = 0;
        if (ui::choiceGrid("##ops", mode_, opsLabels, nOps, 96)) setActive(ed, true);
        if (ui::beginProps("##brush")) {
            if (mode_ == 8) {
                static const char* stamps[] = {"Кратер", "Плато", "Дюны", "Скалы"};
                ui::prop("Штамп");
                ImGui::Combo("##stamp", &stamp_, stamps, 4);
                ui::prop("Поворот");
                ImGui::SliderFloat("##angle", &angle_, 0.0f, 360.0f, "%.0f°");
            }
            ui::prop("Радиус");
            ImGui::SliderFloat("##radius", &radius_, detail_ ? 0.25f : 0.5f, 32.0f, "%.2f");
            if (detail_) { ui::prop("Жёсткость", "0 — мягкий край, ближе к 1 — резкий"); ImGui::SliderFloat("##hard", &hardness_, 0.0f, 0.95f, "%.2f"); }
            if (mode_ <= 1 || mode_ == 4) { ui::prop("Сила"); ImGui::SliderFloat("##str", &strength_, 0.02f, 2.0f, "%.2f"); }
            else if (mode_ <= 3 || mode_ == 7) { ui::prop("Сила"); ImGui::SliderFloat("##blend", &blend_, 0.05f, 1.0f, "%.2f"); }
            else if (mode_ == 8) { ui::prop("Высота штампа"); ImGui::SliderFloat("##sh", &strength_, -4.0f, 4.0f, "%.2f"); }
            else if (mode_ == 5) {
                ui::prop("Высота", "Пипетка: следующий щелчок в «Сцене» берёт высоту оттуда.");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
                ImGui::InputFloat("##target", &target_, 0.25f, 1.0f, "%.2f");
                ImGui::SameLine(0, 4);
                if (ui::toolButton(ICON_PIPETTE, "Пипетка: взять высоту щелчком в «Сцене»", pick_)) pick_ = !pick_;
            } else {
                ui::prop("Слой", "Индекс слоя из terrain.layers (rules.json / game.json). -1 стирает: материал по типу клетки.");
                ImGui::SliderInt("##layer", &layer_, -1, 3, layer_ < 0 ? "стереть" : "%d");
            }
            ui::endProps();
        }
        ui::hint("Shift — обратное действие (поднять ↔ опустить, стереть материал).");

        ui::section(ICON_IMAGE, "Карта высот PNG (16 бит)");
        if (ui::beginProps("##png")) {
            ui::prop("Файл");
            ui::inputString("##file", file_);
            ui::prop("Масштаб", "высота = смещение + значение / 65535 × масштаб (в клетках мира). Экспорт пишет масштаб и смещение в ответе и в PNG (tEXt).");
            ImGui::InputFloat("##scale", &scale_, 0.5f, 2.0f, "%.2f");
            ui::prop("Смещение");
            ImGui::InputFloat("##offset", &offset_, 0.25f, 1.0f, "%.2f");
            if (!ed.planetMap()) { ui::propWide(); ui::checkbox("Детальная (k точек на клетку)", &pngDetail_); }
            ui::endProps();
        }
        const int n = hf ? 3 : 2;
        if (ui::wideButton(ICON_DOWNLOAD, "Импорт", 0, n)) {
            Json p = Json::object();
            p.set("file", Json::string(file_)); p.set("scale", Json::number(double(scale_), 3)); p.set("offset", Json::number(double(offset_), 3));
            p.set("detail", Json::fromBool(pngDetail_));
            report(ed, ws.call("heightmap_import", p));
        }
        ImGui::BeginDisabled(!(hf || (pngDetail_ && d.detailScale > 0)));
        if (ui::wideButton(ICON_SAVE, "Экспорт", 1, n)) {
            Json p = Json::object();
            p.set("file", Json::string(file_));
            p.set("detail", Json::fromBool(pngDetail_));
            Json r = ws.call("heightmap_export", p);
            if (!r.has("error")) { scale_ = float(r["scale"].toDouble(scale_)); offset_ = float(r["offset"].toDouble(offset_)); }
            report(ed, r);
        }
        ImGui::EndDisabled();
        if (!(hf || (pngDetail_ && d.detailScale > 0)))
            ImGui::SetItemTooltip("Нечего экспортировать: сначала нарисуйте рельеф кистью или импортируйте PNG");
        if (hf && ui::wideButton(ICON_TRASH, "Удалить рельеф", 2, n)) {
            Json p = Json::object(); p.set("enabled", Json::fromBool(false));
            report(ed, ws.call("map_heightfield", p));
        }
        ui::resultMessage(message_);
    }

    void update(Editor& ed, double) override {
        if (ed.tool != Tool::Count) owner_ = false;
        if (!owner_ || ed.playing() || !ed.sceneVisible()) {
            painting_ = false;
            ed.cavernsBrushPreview.visible=false;
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        Viewport& vp = *ed.sceneViewport;
        if(caverns_ && vp.hovered) {
            render::float3 origin{},direction{};
            caveHoverValid_=false;
            if(ed.sceneCam.screenRay(io.MousePos.x-vp.originX,io.MousePos.y-vp.originY,
                                     vp.pointsW,vp.pointsH,origin,direction)) {
                const auto& saved=ed.doc().data.extra["terraweaveCaverns"];
                try {
                    if(!caveCacheValid_ || saved!=caveCacheJson_) {
                        auto snapshot=std::make_shared<terraweave::caverns::CaveVolumeSnapshot>(
                            terraweave::caverns::caveSnapshotFromJson(saved));
                        auto index=std::make_shared<const terraweave::caverns::CaveSnapshotSpatialIndex>(
                            snapshot);
                        caveCacheJson_=saved;
                        caveCacheIndex_=std::move(index);
                        caveCacheValid_=true;
                    }
                    const auto& index=*caveCacheIndex_;
                    constexpr double scale=double(1<<20);
                    const double dlen=std::sqrt(double(direction.x)*direction.x+
                        double(direction.y)*direction.y+double(direction.z)*direction.z);
                    if(dlen>0.00001) {
                        terraweave::caverns::CaveRayCm ray;
                        ray.origin={int64_t(std::llround(double(origin.x)*100.)),
                                    int64_t(std::llround(double(origin.y)*100.)),
                                    int64_t(std::llround(double(origin.z)*100.))};
                        ray.dxQ20=int32_t(std::llround(double(direction.x)/dlen*scale));
                        ray.dyQ20=int32_t(std::llround(double(direction.y)/dlen*scale));
                        ray.dzQ20=int32_t(std::llround(double(direction.z)/dlen*scale));
                        auto hit=terraweave::caverns::raycastCaveSurface(index,ray,50000,100);
                        if(hit){caveHoverCm_=hit->point;caveHoverValid_=true;}
                    }
                }catch(const std::exception&){}
            }
        }else caveHoverValid_=false;
        ed.cavernsBrushPreview.visible=caverns_ && caveHoverValid_;
        if(ed.cavernsBrushPreview.visible){
            ed.cavernsBrushPreview.position={float(double(caveHoverCm_.x)/100.),
                float(double(caveHoverCm_.y)/100.),float(double(caveHoverCm_.z)/100.)};
            ed.cavernsBrushPreview.radiusMetres=float(caveRadiusCm_)*.01f;
            ed.cavernsBrushPreview.fill=(io.KeyShift?1-caveAction_:caveAction_)==1;
        }
        if (!io.MouseDown[0]) { painting_ = false; stamped_ = false; return; }
        if (stamped_) return;
        if (!painting_ && !(io.MouseClicked[0] && vp.hovered)) return;
        if (ed.planetMap()) { planetStroke(ed, vp, io); return; }
        if(caverns_) {
            if(!caveHoverValid_)return;
            const double x=double(caveHoverCm_.x)/100.;
            const double z=double(caveHoverCm_.z)/100.;
            const double now=ed.time();
            if(painting_ && std::hypot(x-lastX_,z-lastZ_)<
                double(caveRadiusCm_)*0.0025 && now-lastT_<0.033)return;
            const int action=io.KeyShift?1-caveAction_:caveAction_;
            Json p=Json::object();
            p.set("mode",Json::string(action==0?"carve":"fill"));
            p.set("xCm",Json::number(caveHoverCm_.x));
            p.set("yCm",Json::number(caveHoverCm_.y));
            p.set("zCm",Json::number(caveHoverCm_.z));
            p.set("radiusCm",Json::number(int64_t(caveRadiusCm_)));
            p.set("withinStroke",Json::fromBool(painting_));
            const Json result=ed.ws().call("terraweave_caverns_paint",p);
            report(ed,result);
            if(!result.has("error")){
                painting_=true;lastX_=float(x);lastZ_=float(z);lastT_=now;
            }
            return;
        }
        float x, z;
        if (!ed.groundAt(vp, ed.sceneCam, io.MousePos.x, io.MousePos.y, x, z)) return;
        if(sparse_) {
            // Camera ray/plane projection is world-wide; unlike MapDoc
            // fixed corners, this tool never clamps to the old RTS map size.
            if(!std::isfinite(x) || !std::isfinite(z))return;
            const double now=ed.time();
            if(painting_ &&
               std::hypot(x-lastX_,z-lastZ_)<radius_*0.25f &&
               now-lastT_<0.033)return;
            const bool inv=io.KeyShift;
            const int action=sparseAction_==0?0:
                (sparseAction_==1?(inv?2:1):(inv?1:2));
            Json p=Json::object();
            p.set("xCm",Json::number(int64_t(
                std::llround(double(x)*100.0))));
            p.set("zCm",Json::number(int64_t(
                std::llround(double(z)*100.0))));
            p.set("mode",Json::string(action==0?"create":
                                      action==1?"raise":"lower"));
            p.set("chunkMetres",Json::number(
                int64_t(sparseChunkMetres_)));
            p.set("radiusCm",Json::number(int64_t(
                std::clamp(std::llround(double(radius_)*100.0),
                           1ll,50000ll))));
            p.set("depthCm",Json::number(int64_t(
                std::clamp(std::llround(double(strength_)*25.0),
                           1ll,20000ll))));
            p.set("withinStroke",Json::fromBool(painting_));
            const Json result=ed.ws().call("terraweave_sparse_paint",p);
            report(ed,result);
            if(!result.has("error")) {
                painting_=true;
                lastX_=x;lastZ_=z;lastT_=now;
            }
            return;
        }
        if (pick_) { target_ = ed.view().heightAt(ed.editGrid(), x, z); pick_ = false; return; }
        // dabs every ~1/4 radius along the stroke, at most ~30 per second
        const double now = ed.time();
        if (painting_ && (std::hypot(x - lastX_, z - lastZ_) < radius_ * 0.25f && now - lastT_ < 0.033)) return;
        if (!painting_) ed.doc().beginEdit();
        painting_ = true;
        lastX_ = x; lastZ_ = z; lastT_ = now;
        const bool inv = io.KeyShift;
        const float lh = float(ed.rules().levelHeight.toDouble());
        if (detail_) { // sub-cell brushes (TerrainDetailOps; one undo step per stroke, begun above)
            if (mode_ == 6) { paintDetailMaterial(ed.ws(), x, z, radius_, std::max(0, layer_), inv ? 0.0f : 0.35f, hardness_); return; }
            Sculpt op = mode_ == 0 ? (inv ? Sculpt::Lower : Sculpt::Raise) : mode_ == 1 ? (inv ? Sculpt::Raise : Sculpt::Lower)
                      : mode_ == 2 ? Sculpt::Smooth : mode_ == 3 ? Sculpt::Flatten : mode_ == 4 ? Sculpt::Noise : mode_ == 5 ? Sculpt::Set
                      : mode_ == 7 ? Sculpt::Erode : Sculpt::Stamp;
            const float s = mode_ <= 1 || mode_ == 4 ? strength_ * 0.25f : mode_ == 5 ? target_ : mode_ == 8 ? (inv ? -strength_ : strength_) : blend_ * 0.5f;
            static const char* stamps[] = {"crater", "mesa", "dunes", "rocks"};
            int x0, y0, x1, y1;
            std::string err;
            if (!detailSculpt(ed.ws(), op, {{x, z}}, radius_, s, hardness_, seed_, stamps[stamp_], angle_ * 0.0174533f, x0, y0, x1, y1, &err)) ed.status = err;
            if (mode_ == 8) stamped_ = true;   // one stamp per click
            return;
        }
        if (mode_ == 6) { paintMaterial(ed.doc(), int(std::floor(x)), int(std::floor(z)), int(radius_), inv ? -1 : layer_); return; }
        Sculpt op = mode_ == 0 ? (inv ? Sculpt::Lower : Sculpt::Raise) : mode_ == 1 ? (inv ? Sculpt::Raise : Sculpt::Lower)
                  : mode_ == 2 ? Sculpt::Smooth : mode_ == 3 ? Sculpt::Flatten : mode_ == 4 ? Sculpt::Noise : Sculpt::Set;
        float s = mode_ <= 1 || mode_ == 4 ? strength_ * 0.25f : mode_ == 5 ? target_ : blend_ * 0.5f; // per dab
        sculpt(ed.doc(), lh, op, x, z, radius_, s, seed_);
    }

private:
    // planets: the same brushes as 3D dabs on the body under the cursor (PlanetOps, crosses face seams)
    void planetStroke(Editor& ed, Viewport& vp, ImGuiIO& io) {
        int body; render::float3 local;
        if (!ed.planetAt(vp, io.MousePos.x, io.MousePos.y, body, local)) return;
        const render::float3 dir = normalize(local);
        const game::PlanetSurface* s = ed.view().planets().surface(body);
        if (pick_) { target_ = s ? s->heightAt(dir) : 0; pick_ = false; return; }
        const double now = ed.time();
        const float cellAngle = s ? 1.5708f / float(s->faceSize()) : 0.05f;
        if (painting_ && std::acos(std::clamp(dot(dir, lastDir_), -1.0f, 1.0f)) < radius_ * 0.25f * cellAngle && now - lastT_ < 0.033) return;
        if (!painting_) ed.doc().beginEdit();
        painting_ = true;
        lastDir_ = dir; lastT_ = now;
        const bool inv = io.KeyShift;
        const std::string id = ed.planetBodyId(body);
        if (mode_ == 6) { planetMaterial(ed.doc(), id, dir, radius_, inv ? -1 : layer_); return; }
        if (mode_ >= 7) { ed.status = "Эрозия / штамп: только на плоских картах"; return; }
        Sculpt op = mode_ == 0 ? (inv ? Sculpt::Lower : Sculpt::Raise) : mode_ == 1 ? (inv ? Sculpt::Raise : Sculpt::Lower)
                  : mode_ == 2 ? Sculpt::Smooth : mode_ == 3 ? Sculpt::Flatten : mode_ == 4 ? Sculpt::Noise : Sculpt::Set;
        const float st = mode_ <= 1 || mode_ == 4 ? strength_ * 0.25f : mode_ == 5 ? target_ : blend_ * 0.5f;
        if (detail_) { // the body's detail grid (sub-cell, created on first use; the corners follow it)
            std::string err;
            if (planetDetailSculpt(ed.ws(), id, op, dir, radius_, st, hardness_, seed_, &err) < 0) ed.status = err;
            return;
        }
        planetSculpt(ed.doc(), id, op, dir, radius_, st, seed_);
    }

    Json terraweaveParams() const {
        Json p = Json::object();
        p.set("seed", Json::number(int64_t(twSeed_)));
        p.set("baseHeightCm", Json::number(int64_t(twBaseCm_)));
        p.set("reliefCm", Json::number(int64_t(twReliefCm_)));
        p.set("basePeriodCm", Json::number(int64_t(twPeriodCm_)));
        p.set("octaves", Json::number(int64_t(twOctaves_)));
        p.set("originXcm", Json::number(int64_t(twOriginXcm_)));
        p.set("originZcm", Json::number(int64_t(twOriginZcm_)));
        p.set("cellSizeCm", Json::number(int64_t(twCellSizeCm_)));
        p.set("overwrite", Json::fromBool(twOverwrite_));
        p.set("preserveEdits", Json::fromBool(twPreserve_));
        return p;
    }
    void setActive(Editor& ed, bool on) {
        owner_ = on;
        ed.tool = on ? Tool::Count : Tool::Select;
        if (on) ++seed_;
    }
    void report(Editor& ed, const Json& r) { message_ = r.has("error") ? "Ошибка: " + r.getStr("error") : r.getStr("summary"); ed.status = message_; }

    int twSeed_ = 1, twBaseCm_ = 0, twReliefCm_ = 6000, twPeriodCm_ = 6400, twOctaves_ = 5;
    int twOriginXcm_ = 0, twOriginZcm_ = 0, twCellSizeCm_ = 100;
    bool twOverwrite_ = false, twPreserve_ = false;
    int mode_ = 0, layer_ = 0, stamp_ = 0;
    // Shared Workspace sparse authoring API, independent of RTS/FPS map sizes.
    bool sparse_=false;
    bool caverns_=false, caveHoverValid_=false;
    terraweave::caverns::PointCm caveHoverCm_{};
    Json caveCacheJson_{};
    std::shared_ptr<const terraweave::caverns::CaveSnapshotSpatialIndex> caveCacheIndex_;
    bool caveCacheValid_=false;
    int caveAction_=0,caveRadiusCm_=400,caveDepthCm_=1000;
    int sparseAction_=0,sparseChunkMetres_=256;
    float radius_ = 4, strength_ = 0.5f, blend_ = 0.5f, target_ = 0, scale_ = 8, offset_ = 0, hardness_ = 0.2f, angle_ = 0;
    bool detail_ = true, pngDetail_ = false, stamped_ = false;
    std::string file_ = "maps/heightmap.png", message_;
    bool owner_ = false, painting_ = false, pick_ = false;
    float lastX_ = -1e9f, lastZ_ = -1e9f;
    render::float3 lastDir_;
    double lastT_ = 0;
    uint32_t seed_ = 1;
};

AIVERSE_EDITOR_WINDOW(TerrainWindow);

} // namespace aiverse::editor

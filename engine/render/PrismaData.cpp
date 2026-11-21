#include "PrismaData.h"
#include "graph/ShaderGraph.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>

namespace aiverse::render {

namespace {
using K = PrismaParamKind;
constexpr PrismaQuality kLow = PrismaQuality::Low, kMed = PrismaQuality::Medium, kHigh = PrismaQuality::High;

PrismaParamDef num(const char* n, float d, float lo, float hi, const char* help, const char* label) { return {n, K::Number, {d, 0, 0}, lo, hi, help, label}; }
PrismaParamDef col(const char* n, float r, float g, float b, float lo, float hi, const char* help, const char* label) { return {n, K::Color, {r, g, b}, lo, hi, help, label}; }
PrismaParamDef flag(const char* n, bool d, const char* help, const char* label) { return {n, K::Bool, {d ? 1.0f : 0.0f, 0, 0}, 0, 1, help, label}; }

std::vector<PrismaTypeDef> buildCatalog() {
    using T = PrismaType;
    using C = PrismaCostClass;
    std::vector<PrismaTypeDef> c;
    c.push_back({T::Fxaa, "fxaa", "Сглаживание FXAA", "Anti-aliasing of edges (FXAA) as a separate full-screen pass.", C::Heavy, kMed, {
        num("subpixel", 0.75f, 0, 1, "how much thin details are softened", "Мелкие детали")}});
    c.push_back({T::Pixelate, "pixelate", "Пикселизация", "Big square pixels (retro look).", C::Cheap, kLow, {
        num("size", 4, 1, 32, "pixel size in screen pixels", "Размер пикселя")}});
    c.push_back({T::ChromaticAberration, "chromaticAberration", "Хроматическая аберрация", "Colour fringes towards the screen edges (lens look).", C::Medium, kLow, {
        num("intensity", 0.3f, 0, 1, "fringe width", "Сила")}});
    c.push_back({T::Sharpen, "sharpen", "Резкость", "Sharpens the picture (helps on upscaled / blurry renders).", C::Medium, kMed, {
        num("amount", 0.5f, 0, 2, "sharpening strength", "Сила")}});
    c.push_back({T::TiltShift, "tiltShift", "Тилт-шифт (миниатюра)", "Blurs the top and bottom of the screen, keeps a sharp band: the 'miniature' RTS look.", C::Medium, kMed, {
        num("focus", 0.45f, 0, 1, "height of the sharp band centre (0 bottom, 1 top)", "Центр резкости"),
        num("width", 0.3f, 0, 1, "height of the sharp band (screen share); 0 + falloff 0 = whole screen blurred", "Ширина полосы"),
        num("falloff", 0.25f, 0, 1, "soft transition from sharp to blurred", "Переход"),
        num("blur", 0.8f, 0, 1, "blur radius", "Размытие")}});
    c.push_back({T::Bloom, "bloom", "Свечение (bloom)", "Bright areas glow (fire, lights, sun on water).", C::Heavy, kMed, {
        num("threshold", 0.7f, 0, 64, "brightness where glow starts (linear radiance in HDR; display brightness in LDR)", "Порог"),
        num("knee", 0.5f, 0, 1, "soft start of the threshold", "Мягкость порога"),
        num("intensity", 0.8f, 0, 4, "glow strength", "Сила"),
        num("radius", 0.6f, 0, 1, "glow spread", "Радиус"),
        col("tint", 1, 1, 1, 0, 2, "glow colour multiplier", "Цвет")}});
    c.push_back({T::SunShafts, "sunShafts", "Лучи солнца", "Light shafts from the sun through bright areas (radial blur towards the sun on screen).", C::Heavy, kHigh, {
        num("intensity", 0.6f, 0, 3, "strength", "Сила"),
        num("threshold", 0.75f, 0, 1, "brightness that casts shafts", "Порог"),
        num("length", 0.8f, 0, 1, "shaft length", "Длина"),
        col("color", 1, 0.9f, 0.7f, 0, 2, "shaft colour", "Цвет")}});
    c.push_back({T::ColorGrade, "colorGrade", "Цветокоррекция", "Exposure, contrast, saturation, white balance, lift / gamma / gain, colour filter.", C::Cheap, kLow, {
        num("exposure", 0, -3, 3, "stops (+1 = twice as bright)", "Экспозиция"),
        num("contrast", 1, 0, 2, "1 = unchanged", "Контраст"),
        num("saturation", 1, 0, 2, "0 = black and white, 1 = unchanged", "Насыщенность"),
        num("temperature", 0, -1, 1, "white balance: - cold (blue), + warm (orange)", "Температура"),
        num("tint", 0, -1, 1, "white balance: - green, + magenta", "Оттенок"),
        col("lift", 0, 0, 0, -0.5f, 0.5f, "added to shadows", "Тени (lift)"),
        col("gamma", 1, 1, 1, 0.2f, 3, "midtones (>1 brighter)", "Полутона (gamma)"),
        col("gain", 1, 1, 1, 0, 2, "highlights multiplier", "Света (gain)"),
        col("filter", 1, 1, 1, 0, 2, "colour multiplier", "Цветовой фильтр")}});
    c.push_back({T::Lut, "lut", "LUT (таблица цвета)", "Colour lookup table from an image: a strip of N squares N x N (256x16, 1024x32), red left to right inside a square, green top to bottom, blue square by square (Unreal layout; flipY for Unity). Make one with postfx_lut_neutral and grade it in any image editor.", C::Cheap, kLow, {
        {"texture", K::Text, {0, 0, 0}, 0, 0, "image path in the game folder (PNG)", "Картинка"},
        num("intensity", 1, 0, 1, "blend with the original", "Сила"),
        flag("flipY", false, "green grows upwards (Unity LUT images)", "Зелёный снизу вверх (Unity)")}});
    c.push_back({T::Posterize, "posterize", "Постеризация", "Fewer colour levels (comic / retro look).", C::Cheap, kLow, {
        num("levels", 8, 2, 64, "levels per channel", "Уровней")}});
    c.push_back({T::Vignette, "vignette", "Виньетка", "Darkens (or tints) the screen corners.", C::Cheap, kLow, {
        num("intensity", 0.35f, 0, 1, "strength", "Сила"),
        num("softness", 0.5f, 0, 1, "how far the darkening reaches inwards", "Мягкость"),
        col("color", 0, 0, 0, 0, 1, "corner colour", "Цвет")}});
    c.push_back({T::Grain, "grain", "Зерно плёнки", "Animated film grain.", C::Cheap, kLow, {
        num("intensity", 0.08f, 0, 1, "strength", "Сила"),
        num("size", 1.5f, 1, 4, "grain size in pixels", "Размер"),
        flag("animated", true, "changes every frame", "Анимировано")}});
    c.push_back({T::Fade, "fade", "Затемнение / заливка", "Blends the whole picture towards a colour (fade to black, flash, underwater tint).", C::Cheap, kLow, {
        col("color", 0, 0, 0, 0, 1, "colour", "Цвет"),
        num("amount", 0.5f, 0, 1, "0 = none, 1 = only the colour", "Сила")}});
    c.push_back({T::Letterbox, "letterbox", "Кинорамка", "Bars at the top and bottom (cinematic).", C::Cheap, kLow, {
        num("amount", 0.12f, 0, 0.5f, "share of the screen height covered by both bars", "Высота полос"),
        col("color", 0, 0, 0, 0, 1, "bar colour", "Цвет")}});
    c.push_back({T::DepthOfField, "depthOfField", "Глубина резкости", "Depth-driven defocus. Default uses device depth [0,1]; opt-in metricFocus uses camera-linear distance with near/far projection metadata and depth-edge rejection.", C::Heavy, kHigh, {
        num("focusDepth", 0.5f, 0, 1, "legacy focus point in device depth (0..1)", "Глубина фокуса (0..1)"),
        num("focusRange", 0.08f, 0.001f, 1, "legacy sharp interval in device depth", "Диапазон (0..1)"),
        num("maxBlur", 8.0f, 0, 16, "maximum blur radius in internal render pixels", "Радиус размытия"),
        num("intensity", 1.0f, 0, 1, "defocus blend strength", "Сила размытия"),
        flag("metricFocus", false, "enable camera-linear focus distance instead of legacy hardware depth", "Фокус по расстоянию (м)"),
        num("focusDistance", 12.0f, 0.05f, 2000.0f, "focus plane along the camera view direction in world units (metres for metre-scaled worlds)", "Расстояние фокуса"),
        num("focusRangeMeters", 4.0f, 0.01f, 500.0f, "sharp depth interval around the view-space focus plane", "Глубина резкости, м"),
        num("occlusionToleranceMeters", 0.35f, 0.001f, 20.0f, "reject color samples across depth silhouette discontinuities", "Порог границ, м")}});
    c.push_back({T::AutoExposure, "autoExposure", "Автоэкспозиция", "HDR-only GPU log-luminance measurement and temporal eye adaptation. Uses per-viewport exposure history.", C::Heavy, kHigh, {
        num("keyValue", 0.18f, 0.01f, 1.0f, "target middle-grey luminance in linear HDR", "Целевая яркость"),
        num("minEV", -6.0f, -12.0f, 0.0f, "minimum automatic exposure in EV stops", "Минимум EV"),
        num("maxEV", 6.0f, 0.0f, 12.0f, "maximum automatic exposure in EV stops", "Максимум EV"),
        num("speedUp", 3.0f, 0.01f, 20.0f, "adaptation speed when exposure increases (1/seconds)", "Скорость осветления"),
        num("speedDown", 1.0f, 0.01f, 20.0f, "adaptation speed when exposure decreases (1/seconds)", "Скорость затемнения"),
        num("compensation", 0.0f, -5.0f, 5.0f, "exposure compensation in EV stops", "Коррекция EV"),
        num("centerWeight", 0.0f, 0.0f, 1.0f, "0=uniform metering; 1=center-weighted", "Приоритет центра"),
        num("centerRadius", 0.35f, 0.05f, 1.0f, "normalized center-metering radius", "Радиус замера"),
        num("minMeterLogL", -10.0f, -16.0f, 0.0f, "lower log2 luminance clamp for metering", "Нижняя граница замера"),
        num("maxMeterLogL", 10.0f, 0.0f, 16.0f, "upper log2 luminance clamp for metering", "Верхняя граница замера"),
        // Stage 57: opt-in robust highlight metering. Zero preserves the
        // existing weighted log-luminance estimator for all old profiles.
        num("highlightRejection", 0.0f, 0.0f, 0.95f, "fraction of contribution removed from bright outliers; zero preserves legacy metering", "Подавление ярких выбросов"),
        num("highlightStartEV", 3.0f, -10.0f, 16.0f, "log2 luminance where bright samples begin to be downweighted", "Порог выбросов, EV"),
        num("highlightSoftnessEV", 2.0f, 0.1f, 8.0f, "smooth log2 luminance transition before full highlight rejection", "Мягкость выбросов, EV"),
        // Stage 58: camera-local cinematic lock. A valid exposure history
        // is held without GPU metering; a cold/cut camera seeds once first.
        flag("freezeExposure", false, "hold last valid per-camera exposure; initialize after camera cuts", "Зафиксировать экспозицию"),
        // Optional circular screen-space metering region. These authored
        // settings are resolved by the existing camera volume/profile stack.
        flag("meterRegionEnabled", false, "prefer a normalized screen-space circle for HDR luminance metering", "Замер по области кадра"),
        num("meterCenterX", 0.5f, 0.0f, 1.0f, "meter region center horizontal [0,1] left to right", "Центр замера X"),
        num("meterCenterY", 0.5f, 0.0f, 1.0f, "meter region center vertical [0,1] bottom to top", "Центр замера Y"),
        num("meterRadius", 0.25f, 0.01f, 1.0f, "meter circle radius as fraction of frame height", "Радиус замера области"),
        num("meterFeather", 0.10f, 0.001f, 0.5f, "soft edge width of the meter circle in frame-height units", "Переход по краю замера"),
        num("meterOutsideWeight", 0.05f, 0.02f, 1.0f, "minimum weight outside the metering circle; never zero", "Вес вне области"),
        // Stage 59: manually authored fixed exposure replaces the AE meter.
        // It is independent of AIVERSE_PRISMA_AUTO_EXPOSURE but still requires HDR.
        flag("manualExposure", false, "use a fixed camera exposure instead of GPU automatic metering; requires HDR", "Ручная экспозиция"),
        num("manualEV", 0.0f, -12.0f, 12.0f, "fixed exposure multiplier in stops (2^EV), before tone mapping; no metering history", "Ручная экспозиция, EV"),
        // Stage 59 metering shapes. The old circular region remains default.
        flag("meterBox", false, "use a feathered rectangular metering region instead of the default circle", "Прямоугольная область замера"),
        num("meterHalfWidth", 0.25f, 0.01f, 0.5f, "meter rectangle half width as a fraction of viewport width", "Полуширина области"),
        num("meterHalfHeight", 0.25f, 0.01f, 0.5f, "meter rectangle half height as a fraction of viewport height", "Полувысота области")}});
    c.push_back({T::MotionBlur, "motionBlur", "Размытие движения", "Camera reprojection velocity and optional native object velocity texture; screen-space shutter blur.", C::Heavy, kHigh, {
        num("intensity", 0.65f, 0, 1, "shutter blend strength", "Сила"),
        num("maxPixels", 20.0f, 0, 64, "maximum motion displacement in screen pixels", "Максимальное смещение"),
        num("samples", 8.0f, 2, 16, "blur sample count, rounded to integer", "Число проб"),
        num("shutterScale", 1.0f, 0, 2, "velocity multiplier per frame, 1=one frame displacement", "Выдержка"),
        num("depthThreshold", 0.012f, 0.0005f, 0.05f, "motion blur depth-edge rejection threshold in device-depth units", "Порог глубины"),
        num("reactiveStrength", 0.85f, 0.0f, 1.0f, "reduce motion blur on uncertain translucent surfaces covered by a reactive mask", "Подавление на прозрачности"),
        num("velocityDilation", 1.0f, 0.0f, 2.0f, "neighbor search radius in pixels for depth-matched velocity silhouettes, 0 disables", "Расширение векторов"),
        flag("linearDepth", false, "compare depth layers using camera-linearized view distance instead of device-depth", "Линейная глубина"),
        num("depthToleranceMeters", 0.35f, 0.01f, 10.0f, "linear-depth rejection tolerance in world units/metres", "Порог глубины, м"),
        // Optional Stage 61 transparency-safe sampling; zero preserves the
        // historical camera/object shutter blur output across all projects.
        num("reactiveTapRejection", 0.0f, 0.0f, 1.0f, "reject blur taps with differing translucent reactive coverage; 0 preserves legacy behavior", "Защита проб на прозрачных краях")}});
    // Explicit opt-in only: this effect is inert without AIVERSE_PRISMA_TEMPORAL=1.
    // Native/volume users share the same data definition; no genre coupling.
    c.push_back({T::TemporalResolve, "temporalResolve", "Временная стабилизация",
        "HDR temporal color resolve with camera and object motion reprojection, neighborhood clipping, depth rejection and reactive coverage.",
        C::Heavy, kHigh, {
        num("historyWeight", 0.86f, 0.0f, 0.98f, "maximum history contribution on a stable receiver", "Вес истории"),
        num("depthThreshold", 0.0075f, 0.0001f, 0.05f, "previous-frame hardware depth disocclusion tolerance", "Порог рассогласования глубины"),
        num("motionScale", 0.04f, 0.0f, 0.25f, "history weight falloff per pixel of motion", "Подавление при движении"),
        num("reactiveStrength", 0.95f, 0.0f, 1.0f, "reject history on translucent/particle reactive coverage", "Подавление на прозрачности"),
        num("jitterPixels", 0.5f, 0.0f, 0.5f, "subpixel sample jitter amplitude; additionally requires AIVERSE_PRISMA_JITTER=1", "Субпиксельное смещение"),
        num("historyConfidence", 0.65f, 0.0f, 1.0f, "reduce stale history near color/depth disagreements", "Проверка достоверности истории"),
        num("renderScale", 1.0f, 0.5f, 1.0f, "fraction of output width/height used for 3D render; AIVERSE_PRISMA_UPSCALE=1 required", "Масштаб 3D-рендера"),
        num("reconstructionSharpness", 0.15f, 0.0f, 0.5f, "mild HDR current-sample reconstruction before temporal confidence blending", "Резкость реконструкции"),
        flag("adaptiveResolution", false, "automatically adjust renderScale only from completed GPU frame timings; AIVERSE_PRISMA_ADAPTIVE=1 required", "Адаптивное разрешение"),
        num("targetGpuMs", 16.6f, 4.0f, 50.0f, "target completed GPU frame milliseconds; whole frame, not viewport-only", "Целевое время GPU, мс"),
        num("minRenderScale", 0.5f, 0.5f, 1.0f, "minimum adaptive internal render scale; never exceed authored renderScale", "Минимальный масштаб"),
        num("maxPrismaMemoryMiB", 256.0f, 32.0f, 4096.0f, "known Prisma live + GPU-pending bytes pressure threshold, NOT actual total VRAM", "Бюджет Prisma, МиБ")}});
    c.push_back({T::LensWater,"lensWater","Вода на линзе",
        "Water on the camera lens: draining droplets with trails and glints after the camera exits water, and the "
        "split waterline when the camera is at the surface (underwater optics only below the line).",C::Medium,kMed,{
        num("amount",.75f,0,1,"wet-lens strength; zero disables and clears wetness","Сила"),
        num("dropSize",16,2,64,"drop radius in output pixels","Размер капель, пикс."),
        num("density",.55f,0,1,"fraction of cells containing droplets","Количество капель"),
        num("distortion",12,0,48,"maximum refraction offset in pixels","Искажение, пикс."),
        num("flowSpeed",24,0,240,"downward lens drainage in pixels per second","Скорость стекания"),
        num("dryTime",7,.1f,30,"seconds to dry completely after leaving water","Время высыхания, с"),
        num("film",.2f,0,1,"strength of thin draining water film","Водяная плёнка"),
        num("streaks",.5f,0,1,"meandering rivulets with a head drop, left behind by the draining film","Стекающие ручейки"),
        num("sheet",.9f,0,1,"just-emerged lens: a refracting water film that drains down to the water level","Плёнка после выныривания"),
        num("sheetTime",2.8f,.05f,8,"seconds for the water film to drain off the lens","Время стекания плёнки, с"),
        num("glint",.5f,0,1,"sun glints in droplets","Блики в каплях"),
        flag("waterline",true,"camera at the water surface: frame split along a wavy waterline, underwater optics below it","Линия воды"),
        num("meniscus",6,0,32,"bright refracting meniscus band along the waterline, output pixels","Мениск, пикс."),
        num("waveHeight",.06f,0,.5f,"waterline wave amplitude at the lens, metres","Высота волн, м"),
        num("waveLength",.8f,.1f,10,"waterline wave length, metres","Длина волн, м"),
        num("waveSpeed",1.2f,0,10,"waterline wave speed, metres per second","Скорость волн, м/с"),
        num("lensDistance",.25f,.02f,2,"distance from the camera to the lens surface the waterline is tested at, metres","Расстояние до линзы, м")}});
    c.push_back({T::ShaderGraph, "shaderGraph", "Shader Graph", "Custom bounded fragment graph.", C::Medium, PrismaQuality::Medium, {}});
    return c;
}

int floatsOf(const PrismaParamDef& p) { return p.kind == K::Color ? 3 : p.kind == K::Text ? 0 : 1; }
int offsetOf(const PrismaTypeDef& t, const char* name) {
    int off = 0;
    for (auto& p : t.params) {
        if (std::string(p.name) == name) return off;
        off += floatsOf(p);
    }
    return -1;
}

Json n3(double v) { return Json::number(v, 3); }
std::string fmt(float v) { char b[32]; std::snprintf(b, sizeof b, v < 10 ? "%.1f" : "%.0f", double(v)); return b; }
} // namespace

const std::vector<PrismaTypeDef>& prismaCatalog() { static const std::vector<PrismaTypeDef> c = buildCatalog(); return c; }
const PrismaTypeDef* prismaType(const std::string& id) {
    for (auto& t : prismaCatalog()) if (id == t.id) return &t;
    return nullptr;
}
const PrismaTypeDef& prismaType(PrismaType t) {
    for (auto& d : prismaCatalog()) if (d.type == t) return d;
    return prismaCatalog().front();
}
const char* prismaQualityName(PrismaQuality q) {
    switch (q) { case PrismaQuality::Off: return "off"; case PrismaQuality::Low: return "low"; case PrismaQuality::Medium: return "medium"; default: return "high"; }
}
bool parsePrismaQuality(const std::string& s, PrismaQuality& out) {
    if (s == "off") out = PrismaQuality::Off;
    else if (s == "low") out = PrismaQuality::Low;
    else if (s == "medium") out = PrismaQuality::Medium;
    else if (s == "high") out = PrismaQuality::High;
    else return false;
    return true;
}
const char* prismaCostName(PrismaCostClass c) { return c == PrismaCostClass::Cheap ? "cheap" : c == PrismaCostClass::Medium ? "medium" : "heavy"; }

float PrismaEffect::get(const char* name) const {
    const int o = offsetOf(prismaType(type), name);
    return o >= 0 && size_t(o) < values.size() ? values[size_t(o)] : 0.0f;
}
const float* PrismaEffect::color(const char* name) const {
    static const float kZero[3] = {0, 0, 0};
    const int o = offsetOf(prismaType(type), name);
    return o >= 0 && size_t(o) + 3 <= values.size() ? &values[size_t(o)] : kZero;
}

PrismaEffect defaultPrismaEffect(PrismaType t) {
    const PrismaTypeDef& d = prismaType(t);
    PrismaEffect e;
    if(t == PrismaType::ShaderGraph) e.shaderGraph = graph::makeDefault("postfx");
    e.type = t;
    e.minQuality = d.defaultMinQuality;
    for (auto& p : d.params) for (int i = 0; i < floatsOf(p); ++i) e.values.push_back(p.def[i]);
    return e;
}

void sanitizePrismaSettings(PrismaSettings& settings) {
    // GPU-free native path uses the same limits and defaults as the
    // strict/non-strict JSON parser. Direct C++ or ECS-backed profiles
    // may have truncated vectors or NaN/Inf from scripted math.
    for (auto& effect : settings.effects) {
        const PrismaTypeDef& def = prismaType(effect.type);
        const PrismaEffect defaults = defaultPrismaEffect(effect.type);
        if (effect.values.size() != defaults.values.size()) {
            std::vector<float> normalized = defaults.values;
            const size_t count = std::min(normalized.size(), effect.values.size());
            std::copy_n(effect.values.begin(), count, normalized.begin());
            effect.values = std::move(normalized);
        }
        size_t offset = 0;
        for (const auto& param : def.params) {
            const int width = floatsOf(param); // 0 for Text, 3 for Color
            for (int i = 0; i < width; ++i) {
                float& value = effect.values[offset++];
                if (!std::isfinite(value))
                    value = param.def[i];
                else if (param.kind == PrismaParamKind::Bool)
                    value = value > 0.5f ? 1.0f : 0.0f;
                else
                    value = std::clamp(value,param.lo,param.hi);
            }
        }
    }
}

PrismaEffect parsePrismaEffect(const Json& j, bool strict, std::vector<std::string>* issues) {
    auto problem = [&](const std::string& m) {
        if (strict) throw std::runtime_error(m);
        if (issues) issues->push_back(m);
    };
    if (!j.isObject()) { problem("postfx: an effect must be an object {\"type\": ...}"); return {}; }
    const std::string id = j.getStr("type");
    const PrismaTypeDef* d = prismaType(id);
    if (!d) {
        std::string known;
        for (auto& t : prismaCatalog()) known += std::string(known.empty() ? "" : ", ") + t.id;
        throw std::runtime_error("postfx: unknown effect type '" + id + "' (known: " + known + ")");
    }
    PrismaEffect e = defaultPrismaEffect(d->type);
    if (d->type == PrismaType::ShaderGraph) {
        try { const Json g = j.has("graph") ? j["graph"] : e.shaderGraph; graph::compile(g, "postfx"); e.shaderGraph = g; }
        catch (const std::exception& ex) { e.enabled = false; e.shaderGraph = {}; problem(ex.what()); }
    }
    for (auto& [k, v] : j.members()) {
        if (k == "type" || (k == "graph" && d->type == PrismaType::ShaderGraph)) continue;
        if (k == "enabled") { e.enabled = v.boolean(true); continue; }
        if (k == "minQuality") {
            PrismaQuality q;
            if (!parsePrismaQuality(v.str(), q) || q == PrismaQuality::Off) problem("postfx '" + id + "': minQuality must be low, medium or high");
            else e.minQuality = q;
            continue;
        }
        const int off = offsetOf(*d, k.c_str());
        const PrismaParamDef* p = nullptr;
        for (auto& pd : d->params) if (k == pd.name) p = &pd;
        if (!p) {
            std::string known;
            for (auto& pd : d->params) known += std::string(known.empty() ? "" : ", ") + pd.name;
            problem("postfx '" + id + "': unknown parameter '" + k + "' (parameters: " + known + ", enabled, minQuality)");
            continue;
        }
        // Stage 65: configuration can be created through game.json, MCP,
        // the editor, or native Json builders. Do not let NaN/Inf or a
        // non-numeric RGB element enter a GPU uniform via std::clamp:
        // std::clamp(NaN,lo,hi) returns NaN on conventional implementations.
        // Range-clamp in double precision BEFORE converting to float, so
        // large but finite native JSON numbers cannot overflow to Inf.
        auto clampIn = [&](const Json& number) -> std::optional<float> {
            if (!number.isNumber()) {
                problem("postfx '" + id + "': " + k + " must contain finite numeric values");
                return std::nullopt;
            }
            const double x = number.toDouble();
            if (!std::isfinite(x)) {
                problem("postfx '" + id + "': " + k + " must be finite");
                return std::nullopt;
            }
            const double bounded = std::clamp(x,double(p->lo),double(p->hi));
            const float safe = float(bounded);
            if (bounded != x && issues)
                issues->push_back("postfx '" + id + "': " + k +
                    " clamped to " + fmt(safe) + " (range " +
                    fmt(p->lo) + ".." + fmt(p->hi) + ")");
            return safe;
        };
        switch (p->kind) {
        case K::Text:
            if (!v.isString()) problem("postfx '" + id + "': " + k + " must be a string");
            else e.text = v.str();
            break;
        case K::Bool:
            if (v.type() != Json::Type::Bool) problem("postfx '" + id + "': " + k + " must be true or false");
            else e.values[size_t(off)] = v.boolean() ? 1.0f : 0.0f;
            break;
        case K::Number:
            if (auto value = clampIn(v)) e.values[size_t(off)] = *value;
            break;
        case K::Color:
            if (v.isNumber()) {
                if (auto value = clampIn(v))
                    for (int i = 0; i < 3; ++i) e.values[size_t(off + i)] = *value;
            } else if (!v.isArray() || v.size() < 3) {
                problem("postfx '" + id + "': " + k + " must be [r, g, b]");
            } else {
                // Apply an RGB triplet atomically. If one component is
                // invalid, keep all three defaults rather than mixing
                // user-supplied and default channels unpredictably.
                std::optional<float> rgb[3];
                bool allValid = true;
                for (int i = 0; i < 3; ++i) {
                    rgb[i] = clampIn(v[size_t(i)]);
                    allValid &= rgb[i].has_value();
                }
                if (allValid)
                    for (int i = 0; i < 3; ++i)
                        e.values[size_t(off + i)] = *rgb[i];
            }
            break;
        }
    }
    if (d->type == PrismaType::Lut && e.text.empty()) problem("postfx 'lut': needs \"texture\" (an image in the game folder)");
    if (d->type == PrismaType::ShaderGraph && e.shaderGraph.isNull()) e.enabled = false;
    return e;
}

Json prismaEffectJson(const PrismaEffect& e) {
    const PrismaTypeDef& d = prismaType(e.type);
    Json j = Json::object();
    j.set("type", Json::string(d.id));
    if (e.type == PrismaType::ShaderGraph) j.set("graph", e.shaderGraph);
    if (!e.enabled) j.set("enabled", Json::fromBool(false));
    if (e.minQuality != d.defaultMinQuality) j.set("minQuality", Json::string(prismaQualityName(e.minQuality)));
    int off = 0;
    for (auto& p : d.params) {
        const int n = floatsOf(p);
        bool same = true;
        for (int i = 0; i < n; ++i) if (std::fabs(e.values[size_t(off + i)] - p.def[i]) > 1e-4f) same = false;
        if (p.kind == K::Text) { if (!e.text.empty()) j.set(p.name, Json::string(e.text)); }
        else if (!same) {
            if (p.kind == K::Bool) j.set(p.name, Json::fromBool(e.values[size_t(off)] > 0.5f));
            else if (p.kind == K::Number) j.set(p.name, n3(e.values[size_t(off)]));
            else { Json a = Json::array(); for (int i = 0; i < 3; ++i) a.push(n3(e.values[size_t(off + i)])); j.set(p.name, a); }
        }
        off += n;
    }
    return j;
}

const PrismaEffect* PrismaSettings::find(PrismaType t) const {
    for (auto& e : effects) if (e.type == t) return &e;
    return nullptr;
}
std::vector<const PrismaEffect*> PrismaSettings::activeAt(PrismaQuality q) const {
    std::vector<const PrismaEffect*> out;
    if (!enabled || q == PrismaQuality::Off) return out;
    for (auto& e : effects) if (e.enabled && int(q) >= int(e.minQuality)) out.push_back(&e);
    return out;
}

PrismaSettings parsePrisma(const Json& game, const Json& map, std::vector<std::string>* issues) {
    PrismaSettings s;
    std::map<std::string, Json> byType;     // merged effect objects
    std::vector<std::string> order;
    auto effectsOf = [](const Json& b) -> const Json& { return b.isArray() ? b : b["effects"]; };
    auto addFrom = [&](const Json& block, bool overlay) {
        const Json& list = effectsOf(block);
        for (auto& e : list.items()) {
            if (!e.isObject()) { if (issues) issues->push_back("postfx: an effect must be an object"); continue; }
            const std::string t = e.getStr("type");
            auto it = byType.find(t);
            if (it == byType.end()) { byType[t] = e; order.push_back(t); continue; }
            if (overlay) { for (auto& [k, v] : e.members()) it->second.set(k, v); }
            else it->second = e;
        }
    };
    if (game.isObject() || game.isArray()) {
        if (game.isObject() && !game.getBool("enabled", true)) s.enabled = false;
        addFrom(game, false);
    }
    if (map.isObject() || map.isArray()) {
        if (map.isObject() && map.has("enabled")) s.enabled = map.getBool("enabled", true);
        if (map.isObject() && map.getBool("replace")) { byType.clear(); order.clear(); }
        addFrom(map, true);
    }
    for (const Json* block : {&game, &map}) {
        if (!block->isObject() || !block->has("hdr")) continue;
        const auto v = block->getStr("hdr");
        if (v == "auto") s.hdr = HdrMode::Auto;
        else if (v == "off") s.hdr = HdrMode::Off;
        else if (v == "on") s.hdr = HdrMode::On;
        else if (issues) issues->push_back("postfx.hdr: expected auto, off or on");
    }
    for (auto& d : prismaCatalog()) {          // catalog order = application order
        auto it = byType.find(d.id);
        if (it == byType.end()) continue;
        try { s.effects.push_back(parsePrismaEffect(it->second, false, issues)); }
        catch (const std::exception& ex) { if (issues) issues->push_back(ex.what()); }
    }
    for (auto& t : order) if (!prismaType(t) && issues) issues->push_back("postfx: unknown effect type '" + t + "'");
    return s;
}

// ---------------------------------------------------------------- presets
const std::vector<PrismaPreset>& prismaPresets() {
    static const std::vector<PrismaPreset> p = {
        {"cinematic", "Кино", "soft glow, slightly desaturated contrast, vignette, letterbox, light grain",
         R"([{"type":"bloom","intensity":0.6,"threshold":0.72},{"type":"colorGrade","contrast":1.12,"saturation":0.9,"temperature":0.06},
             {"type":"vignette","intensity":0.35},{"type":"letterbox","amount":0.1},{"type":"grain","intensity":0.04}])"},
        {"vivid", "Сочный", "saturated and crisp: colour boost, a little glow and sharpening",
         R"([{"type":"colorGrade","saturation":1.25,"contrast":1.08},{"type":"bloom","intensity":0.45,"threshold":0.8},{"type":"sharpen","amount":0.3}])"},
        {"mobile", "Лёгкий для телефонов", "cheap look for weak phones: colour grade + vignette only (one composite pass)",
         R"([{"type":"colorGrade","saturation":1.12,"contrast":1.06},{"type":"vignette","intensity":0.25}])"},
        {"noir", "Нуар", "black and white, hard contrast, heavy vignette, grain",
         R"([{"type":"colorGrade","saturation":0,"contrast":1.3},{"type":"vignette","intensity":0.55,"softness":0.6},{"type":"grain","intensity":0.08}])"},
        {"retro", "Ретро", "big pixels and few colours",
         R"([{"type":"pixelate","size":3},{"type":"posterize","levels":12},{"type":"vignette","intensity":0.2}])"},
        {"miniature", "Миниатюра", "tilt-shift: the battlefield looks like a tabletop model",
         R"([{"type":"tiltShift","focus":0.45,"width":0.25,"falloff":0.3},{"type":"colorGrade","saturation":1.3,"contrast":1.1}])"},
        {"sunset", "Закат", "warm light, sun shafts and glow",
         R"([{"type":"colorGrade","temperature":0.35,"tint":0.05,"contrast":1.05},{"type":"sunShafts","intensity":0.7},{"type":"bloom","intensity":0.6},{"type":"vignette","intensity":0.25}])"},
        {"winter", "Зима", "cold white balance, muted colours, soft glow",
         R"([{"type":"colorGrade","temperature":-0.3,"saturation":0.85,"exposure":0.15},{"type":"bloom","intensity":0.4,"threshold":0.8},{"type":"vignette","intensity":0.2}])"},
        {"nightVision", "Ночное видение", "green monochrome, bright, noisy",
         R"([{"type":"colorGrade","saturation":0,"exposure":0.8,"filter":[0.35,1.0,0.35]},{"type":"grain","intensity":0.22},{"type":"vignette","intensity":0.6,"softness":0.3}])"},
        {"clean", "Чистая картинка", "anti-aliasing and light sharpening",
         R"([{"type":"fxaa"},{"type":"sharpen","amount":0.25}])"},
    };
    return p;
}
const PrismaPreset* prismaPreset(const std::string& id) {
    for (auto& p : prismaPresets()) if (id == p.id) return &p;
    return nullptr;
}

// ---------------------------------------------------------------- cost
const std::vector<PrismaDeviceProfile>& prismaProfiles() {
    // Rough fill-rate figures: a full-screen texture read per pixel at 720p takes ~0.8 ms on a
    // Mali-T720 MP2 / Adreno 306 class GPU, ~0.15 ms of overhead per extra render target (tile
    // store + load). Mid phones render ~1.7 Mpix at ~3x the rate; desktops are ~40x faster.
    static const std::vector<PrismaDeviceProfile> p = {
        {"weakPhone", "weak phone (Mali-T720 / Adreno 306 class, 1280x720)", 1280, 720, 0.9f, 0.15f, PrismaQuality::Low},
        {"midPhone", "mid phone (Adreno 610 / Mali-G52 class, 1920x864)", 1920, 864, 0.3f, 0.05f, PrismaQuality::Medium},
        {"desktop", "desktop / laptop GPU (2560x1440)", 2560, 1440, 0.02f, 0.01f, PrismaQuality::High},
    };
    return p;
}

BloomPlan bloomPlan(PrismaQuality t) {
    if (t == PrismaQuality::High) return {2, 5};
    if (t == PrismaQuality::Medium) return {4, 4};
    return {4, 3};
}
BlurPlan tiltBlurPlan(PrismaQuality t) { return t == PrismaQuality::High ? BlurPlan{2, 2} : BlurPlan{4, 2}; }
ShaftsPlan shaftsPlan(PrismaQuality t) {
    if (t == PrismaQuality::High) return {2, 32};
    if (t == PrismaQuality::Medium) return {4, 24};
    return {4, 16};
}

PrismaWork prismaBaseWork() { return {1.6f, 1}; }

PrismaWork prismaEffectWork(const PrismaEffect& e, PrismaQuality tier, bool ignoreMin) {
    if (tier == PrismaQuality::Off || (!ignoreMin && (int(tier) < int(e.minQuality) || !e.enabled))) return {};
    using T = PrismaType;
    switch (e.type) {
    case T::ShaderGraph: return {1.0f + float(e.shaderGraph["nodes"].size()) * 0.08f, 0};
    case T::ColorGrade: return {0.15f, 0};
    case T::Vignette: return {0.05f, 0};
    case T::Grain: return {0.1f, 0};
    case T::LensWater: return {2.5f,0}; // estimate only, not measured GPU time
    case T::Fade: case T::Letterbox: return {0.02f, 0};
    case T::Pixelate: case T::Posterize: return {0.05f, 0};
    case T::Lut: return {1.0f, 0};
    case T::ChromaticAberration: return {2.0f, 0};
    case T::Sharpen: return {2.0f, 0};
    case T::Fxaa: return {6.0f, 1};
    case T::DepthOfField:
        // The opt-in metric path additionally reads depth at eight color
        // tap locations to reject foreground/background silhouette bleed.
        // Conservative, hardware-independent estimate only; not GPU time.
        return {e.get("metricFocus") > 0.5f ? 18.0f : 10.0f, 1};
    case T::AutoExposure: return {5.0f, 12};
    case T::MotionBlur: return {17.0f + (e.get("velocityDilation") > 0 ? 8.0f : 0.0f), 2}; // upper-bound pyramid: size-dependent passes
    case T::TemporalResolve: {
        // Heuristic only: temporal history/shader execute at OUTPUT resolution,
        // while the shared velocity pass and scene execute at INTERNAL size.
        const float scale=std::clamp(e.get("renderScale"),0.5f,1.0f);
        return {12.0f+4.0f*scale*scale,2};
    }
    case T::Bloom: {
        const BloomPlan b = bloomPlan(tier);
        float a = 1.0f / float(b.startDiv * b.startDiv), u = 4 * a;   // prefilter: 4 taps per output pixel
        for (int i = 1; i < b.levels; ++i) u += 5 * a / float(1 << (2 * i));
        for (int i = 0; i < b.levels - 1; ++i) u += 9 * a / float(1 << (2 * i));
        return {u + 1.0f, 1 + (b.levels - 1) * 2};
    }
    case T::TiltShift: {
        const BlurPlan b = tiltBlurPlan(tier);
        const float a = 1.0f / float(b.div * b.div);
        return {a * (1 + 5.0f * float(b.passes)) + 1.1f, 1 + b.passes};
    }
    case T::SunShafts: {
        const ShaftsPlan s = shaftsPlan(tier);
        const float a = 1.0f / float(s.div * s.div);
        return {a * (4 + float(s.samples)) + 1.0f, 2};
    }
    default: return {};
    }
}

float prismaMs(const PrismaWork& w, const PrismaDeviceProfile& p) {
    const float mpix = float(p.width) * float(p.height) / 1e6f;
    return w.units * mpix * p.msPerMpixUnit + float(w.passes) * p.msPerPass;
}

namespace {
float round2(float v) { return std::round(v * 100.0f) / 100.0f; }
const PrismaDeviceProfile& profile(const char* id) {
    for (auto& p : prismaProfiles()) if (std::string(p.id) == id) return p;
    return prismaProfiles().front();
}
} // namespace

std::vector<std::string> prismaEffectWarnings(const PrismaEffect& e) {
    std::vector<std::string> w;
    const PrismaTypeDef& d = prismaType(e.type);
    const auto& weak = profile("weakPhone");
    const auto& mid = profile("midPhone");
    const float weakForced = prismaMs(prismaEffectWork(e, PrismaQuality::Low, true), weak);
    const float midMs = prismaMs(prismaEffectWork(e, PrismaQuality::Medium, true), mid);
    const float hiMs = prismaMs(prismaEffectWork(e, PrismaQuality::High, true), mid);
    if (d.cost == PrismaCostClass::Heavy) {
        std::string s = std::string("HEAVY effect '") + d.id + "': about " + fmt(midMs) + " ms per frame on a mid phone (" +
                        fmt(hiMs) + " ms at quality high there), " + fmt(weakForced) + " ms on a weak phone.";
        if (e.minQuality == PrismaQuality::Low)
            s += " minQuality 'low' forces it onto weak phones too: at 60 fps that is " + fmt(weakForced / 16.7f * 100.0f) +
                 "% of the frame there. Prefer minQuality 'medium' (weak phones skip it) or 'high' (strong devices only).";
        else
            s += std::string(" It runs from quality '") + prismaQualityName(e.minQuality) + "' up, weak phones skip it.";
        w.push_back(s);
    } else if (d.cost == PrismaCostClass::Medium && e.minQuality == PrismaQuality::Low && weakForced >= 1.0f) {
        w.push_back(std::string("'") + d.id + "' costs about " + fmt(weakForced) + " ms per frame on a weak phone; minQuality 'medium' skips it there.");
    }
    return w;
}

Json prismaCostReport(const PrismaSettings& s) {
    Json r = Json::object(), effects = Json::array(), totals = Json::object(), warnings = Json::array();
    for (auto& e : s.effects) {
        const PrismaTypeDef& d = prismaType(e.type);
        Json j = Json::object();
        j.set("type", Json::string(d.id));
    if (e.type == PrismaType::ShaderGraph) j.set("graph", e.shaderGraph);
        j.set("class", Json::string(prismaCostName(d.cost)));
        j.set("enabled", Json::fromBool(e.enabled));
        j.set("minQuality", Json::string(prismaQualityName(e.minQuality)));
        Json per = Json::object();
        for (auto& p : prismaProfiles()) {
            const PrismaWork w = prismaEffectWork(e, p.tier);
            Json pj = Json::object();
            pj.set("tier", Json::string(prismaQualityName(p.tier)));
            pj.set("runs", Json::fromBool(w.units > 0));
            pj.set("ms", n3(round2(prismaMs(w, p))));
            if (w.units > 0) pj.set("passes", Json::number(int64_t(w.passes)));
            per.set(p.id, pj);
        }
        j.set("cost", per);
        effects.push(j);
        if (e.enabled) for (auto& m : prismaEffectWarnings(e)) warnings.push(Json::string(m));
    }
    for (auto& p : prismaProfiles()) {
        PrismaWork sum;
        const auto active = s.activeAt(p.tier);
        const bool hdr = s.enabled && hdrAtQuality(s.hdr, p.tier, true);
        if (!active.empty() || hdr) {
            sum = prismaBaseWork();
            for (auto* e : active) { const PrismaWork w = prismaEffectWork(*e, p.tier); sum.units += w.units; sum.passes += w.passes; }
        }
        const float ms = prismaMs(sum, p);
        Json t = Json::object();
        t.set("device", Json::string(p.label));
        t.set("tier", Json::string(prismaQualityName(p.tier)));
        t.set("hdrIfSupported", Json::fromBool(hdr));
        t.set("colorBytesPerPixel", Json::number(int64_t(hdr ? 8 : 4)));
        t.set("effects", Json::number(int64_t(active.size())));
        t.set("passes", Json::number(int64_t(sum.passes)));
        t.set("ms", n3(round2(ms)));
        t.set("fpsAt60", n3(round2(1000.0f / (1000.0f / 60.0f + ms))));
        totals.set(p.id, t);
        const float budget = std::string(p.id) == "desktop" ? 1.5f : 3.0f;
        if (ms > budget)
            warnings.push(Json::string("post chain over budget on a " + std::string(p.label) + ": " + fmt(ms) + " ms per frame (budget " +
                                       fmt(budget) + " ms); 60 fps becomes about " + fmt(1000.0f / (1000.0f / 60.0f + ms)) +
                                       " fps. Raise minQuality of heavy effects or remove some."));
    }
    r.set("effects", effects);
    r.set("total", totals);
    r.set("note", Json::string("LDR bandwidth estimates; HDR colour targets double colour storage/bandwidth, actual GPU cost must be measured. Estimates from the fill-rate model in engine/render/PrismaData.cpp (scene to texture + composite counted once). "
                               "Tiers: weak phones get 'low', mid phones 'medium', desktops 'high' by default; players can lower it."));
    r.set("warnings", warnings);
    return r;
}

} // namespace aiverse::render

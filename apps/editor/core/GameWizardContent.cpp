// Game wizard, the user's content: scan the art folder (or the downloaded link), copy what the engine can use into
// the project, put models on the types whose names they carry, music into playlists and sounds onto events by their
// file names, the key art into the splash and the menu, an icon, UI colours taken from the art.
#include "GameWizardImpl.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include "engine/pack/FileCopy.h"
#include "tools/branding/BrandImage.h"

namespace fs = std::filesystem;

namespace rts::editor::wiz {

namespace {

enum class Kind { Image, Model, ModelPart, Audio, Font, Text, Other };

Kind kindOf(const std::string& ext) {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return Kind::Image;
    if (ext == ".glb" || ext == ".gltf" || ext == ".obj" || ext == ".fbx") return Kind::Model;
    if (ext == ".bin" || ext == ".mtl") return Kind::ModelPart;
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") return Kind::Audio;
    if (ext == ".ttf" || ext == ".otf") return Kind::Font;
    if (ext == ".md" || ext == ".txt") return Kind::Text;
    return Kind::Other;
}

bool hasWord(const std::vector<std::string>& ws, std::initializer_list<const char*> keys) {
    for (auto& w : ws)
        for (const char* k : keys) if (w.rfind(k, 0) == 0) return true;
    return false;
}

// source file -> project-relative destination, unique names
std::string copyInto(GameMaker::Ctx& c, const fs::path& src, const std::string& relDir, const std::string& relName) {
    fs::path to = fs::path(c.dir) / relDir / relName;
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    rts::pack::copyFileReplace(src, to, ec);
    if (ec) { c.warn("не скопирован " + src.string() + ": " + ec.message()); return ""; }
    return (fs::path(relDir) / relName).generic_string();
}

std::string asciiName(const fs::path& p) {   // file names inside the game: ASCII (web / Android assets), keep the extension
    std::string stem, ext = lower(p.extension().string());
    for (unsigned char ch : p.stem().string()) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') stem += char(std::tolower(ch));
        else if (!stem.empty() && stem.back() != '_') stem += '_';
    }
    if (stem.empty()) stem = "file" + std::to_string(hashText(p.string()) % 100000);
    return stem + ext;
}

struct Found { fs::path path, rel; Kind kind; uintmax_t size; };

std::vector<Found> listFiles(const std::string& dir) {
    std::vector<Found> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const std::string fname = it->path().filename().string();
        if (!fname.empty() && (fname[0] == '.' || fname == "__MACOSX")) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const uintmax_t size = it->file_size(ec);
        if (size > 300ull * 1024 * 1024) continue;
        out.push_back({it->path(), fs::relative(it->path(), dir, ec), kindOf(lower(it->path().extension().string())), size});
        if (out.size() > 5000) break;
    }
    std::sort(out.begin(), out.end(), [](const Found& a, const Found& b) { return a.rel < b.rel; });
    return out;
}

void assign(GameMaker::Ctx& c, const std::string& file, const std::string& to, const std::string& why) {
    Json a = Json::object();
    a.set("file", Json::string(file)); a.set("to", Json::string(to)); a.set("why", Json::string(why));
    c.assigned.push(a);
}

// type ids / names a file stem could mean: "knight_red.glb" -> footman when its name is "Knight"
std::string typeForStem(GameMaker::Ctx& c, const std::string& stem, bool wantStructure, bool any) {
    const auto sw = words(stem);
    std::string best;
    int bestScore = 0;
    for (auto& t : c.info.types) {
        if (!any && t.structure != wantStructure) continue;
        int score = 0;
        std::vector<std::string> names = {t.id, t.name, c.typeName(t.id, "ru"), c.typeName(t.id, "en")};
        for (auto& n : names) {
            for (auto& nw : words(n)) {
                if (nw.size() < 3) continue;
                for (auto& w : sw) {
                    if (w == nw) score = std::max(score, 10);
                    else if (w.size() >= 4 && nw.size() >= 4 && (w.rfind(nw.substr(0, 4), 0) == 0)) score = std::max(score, 6);
                }
            }
        }
        if (score > bestScore) { bestScore = score; best = t.id; }
    }
    return best;
}

} // namespace

void scanSources(GameMaker::Ctx& c) {
    c.idea = c.spec.idea;
    if (c.sourceDir.empty()) { c.did("контент: папка не задана — арт и звук шаблона"); return; }
    std::error_code ec;
    if (!fs::is_directory(c.sourceDir, ec)) throw std::runtime_error("папка с контентом не найдена: " + c.sourceDir);
    int counts[7] = {};
    for (auto& f : listFiles(c.sourceDir)) {
        counts[int(f.kind)]++;
        if (f.kind == Kind::Text && f.size < 512 * 1024) c.idea += "\n" + readText(f.path.string());
    }
    c.did("контент: картинок " + std::to_string(counts[0]) + ", моделей " + std::to_string(counts[1]) + ", звуков " +
          std::to_string(counts[3]) + ", шрифтов " + std::to_string(counts[4]) + ", текстов " + std::to_string(counts[5]));
    if (counts[6] > 0) c.warn(std::to_string(counts[6]) + " файлов неизвестного типа пропущено (поддерживаются png/jpg/tga, glb/gltf/obj/fbx, wav/ogg/mp3/flac, ttf/otf, md/txt)");
}

void importContent(GameMaker::Ctx& c) {
    if (c.sourceDir.empty()) return;
    const auto files = listFiles(c.sourceDir);
    // folders that hold a model: their textures / buffers go with the model (relative references keep working)
    std::set<std::string> modelDirs;
    for (auto& f : files) if (f.kind == Kind::Model) modelDirs.insert(f.rel.parent_path().generic_string());
    int bestKey = -1000000, bestIcon = -1000000;
    bool fontDone = false;
    for (auto& f : files) {
        const std::string dirRel = f.rel.parent_path().generic_string();
        const auto ws = words(f.rel.generic_string());
        if (f.kind == Kind::Model || ((f.kind == Kind::ModelPart || f.kind == Kind::Image) && modelDirs.count(dirRel))) {
            std::string to = copyInto(c, f.path, "models", f.rel.generic_string());
            if (to.empty()) continue;
            if (f.kind == Kind::Model) {
                c.models.push_back(to);
                const std::string type = typeForStem(c, f.path.stem().string(), false, true);
                if (!type.empty() && !c.rules["types"][type]["view"].has("model")) {
                    Json& view = c.rules.member("types").member(type).member("view");
                    view.set("model", Json::string(to));
                    const auto* ti = c.info.find(type);
                    if (ti && ti->structure) {
                        const Json& fp = c.rules["types"][type]["structure"]["footprint"];
                        const int64_t w = fp.isArray() && fp.size() ? std::max<int64_t>(1, fp[0].integer(2)) : 2;
                        view.set("height", Json::number(w * 8 / 10 == 0 ? int64_t(1) : w * 8 / 10));
                    } else {
                        view.set("height", Json::number(int64_t(1)));
                    }
                    assign(c, f.rel.generic_string(), "types." + type + ".view.model", "имя файла совпало с юнитом «" + c.typeName(type, c.mainLang) + "»");
                } else {
                    assign(c, f.rel.generic_string(), to, "модель скопирована (юнит по имени не найден — назначьте в Инспекторе)");
                }
            }
            continue;
        }
        if (f.kind == Kind::Image) {
            std::string to = copyInto(c, f.path, "art", (f.rel.parent_path() / asciiName(f.path)).generic_string());
            if (to.empty()) continue;
            c.images.push_back(to);
            branding::Image img;
            const bool ok = branding::loadImageFile(f.path.string(), img);
            const int w = ok ? img.width : 0, h = ok ? img.height : 0;
            const bool iconWord = hasWord(ws, {"icon", "logo", "app", "иконк", "лого", "эмблем", "emblem"});
            const bool keyWord = hasWord(ws, {"splash", "key", "cover", "background", "bg", "menu", "title", "poster", "wallpaper",
                                             "фон", "заставк", "обложк", "постер", "меню", "art"});
            // 2D games: a sprite named like a type becomes its sheet (one frame, the whole image)
            if (c.info.sprites2d && !iconWord && !keyWord) {
                const std::string type = typeForStem(c, f.path.stem().string(), false, true);
                if (!type.empty() && !c.rules["types"][type].has("sprite")) {
                    Json sp = Json::object();
                    sp.set("sheet", Json::string(to));
                    Json fr = Json::array(); fr.push(Json::number(int64_t(w))); fr.push(Json::number(int64_t(h)));
                    sp.set("frame", fr);
                    c.rules.member("types").member(type).set("sprite", sp);
                    assign(c, f.rel.generic_string(), "types." + type + ".sprite", "спрайт по имени юнита");
                    continue;
                }
            }
            const int area = int(std::min<int64_t>(int64_t(w) * h / 1000, 100000));
            const int squareness = (w > 0 && h > 0) ? 100 - std::abs(w - h) * 100 / std::max(w, h) : 0;
            int keyScore = (keyWord ? 4000 : 0) + area + (w > h ? 500 : 0) - (iconWord ? 3000 : 0);
            int iconScore = (iconWord ? 4000 : 0) + squareness * 10 + std::min(area, 2000) - (w < 256 ? 3000 : 0);
            if (ok && keyScore > bestKey) { bestKey = keyScore; c.keyArt = to; }
            if (ok && iconScore > bestIcon) { bestIcon = iconScore; c.iconArt = to; }
            if (keyWord && hasWord(ws, {"menu", "меню", "background", "bg", "фон"})) c.menuArt = to;
            continue;
        }
        if (f.kind == Kind::Audio) {
            const bool music = f.size > 1200 * 1024 || hasWord(ws, {"music", "theme", "ost", "soundtrack", "track", "song", "loop", "музык", "тем", "саундтрек"});
            std::string to = copyInto(c, f.path, music ? "audio/music" : "audio/sfx", asciiName(f.path));
            if (to.empty()) continue;
            (music ? c.music : c.sounds).push_back(to);
            continue;
        }
        if (f.kind == Kind::Font) {
            std::string to = copyInto(c, f.path, "fonts", asciiName(f.path));
            if (to.empty()) continue;
            c.fonts.push_back(to);
            if (!fontDone && f.path.extension() != ".otf") {   // the first TrueType font becomes the title font of the screens
                std::error_code ec;
                rts::pack::copyFileReplace(f.path, fs::path(c.dir) / "fonts" / "title.ttf", ec);
                writeText(c.path("fonts/SOURCE.txt"), "fonts/title.ttf: " + f.rel.generic_string() +
                          " from the game's content folder (the game wizard). Check its license before release.\n");
                fontDone = true;
                assign(c, f.rel.generic_string(), "fonts/title.ttf", "шрифт заголовков");
            }
            continue;
        }
        if (f.kind == Kind::Text) {
            copyInto(c, f.path, "docs", asciiName(f.path));
            assign(c, f.rel.generic_string(), "docs/" + asciiName(f.path), "текст (прочитан как идея / ГДД)");
        }
    }
    if (!c.keyArt.empty()) assign(c, c.keyArt, "заставка и меню", "главный арт (по имени и размеру)");
    if (!c.iconArt.empty()) assign(c, c.iconArt, "иконка", "самая квадратная / с «icon», «logo» в имени");
    c.saveRules();
    c.did("перенесено: картинок " + std::to_string(c.images.size()) + ", моделей " + std::to_string(c.models.size()) +
          ", музыки " + std::to_string(c.music.size()) + ", звуков " + std::to_string(c.sounds.size()));
}

void applyAudio(GameMaker::Ctx& c) {
    if (c.music.empty() && c.sounds.empty()) return;
    const std::string file = c.path("audio.json");
    Json a = readJsonFile(file);
    if (!a.isObject()) a = Json::object();
    // music: by name into playlists; the rest plays during matches
    Json& music = a.member("music");
    std::map<std::string, std::vector<std::string>> lists;
    for (auto& m : c.music) {
        const auto ws = words(fs::path(m).stem().string());
        const char* list = hasWord(ws, {"menu", "main", "title", "theme", "меню", "главн"}) ? "menu"
                         : hasWord(ws, {"battle", "fight", "combat", "war", "action", "бой", "битв", "сраж"}) ? "battle"
                         : hasWord(ws, {"victory", "win", "triumph", "побед"}) ? "victory"
                         : hasWord(ws, {"defeat", "lose", "loss", "gameover", "пораж", "проигр"}) ? "defeat" : "match";
        lists[list].push_back(m);
    }
    if (!lists.count("menu") && lists.count("match")) lists["menu"].push_back(lists["match"].front());
    for (auto& [name, files] : lists) {
        Json pl = Json::object();
        Json fl = Json::array();
        for (auto& f : files) fl.push(Json::string(f));
        pl.set("files", fl);
        pl.set("loop", Json::fromBool(name != "victory" && name != "defeat"));
        if (files.size() > 1) pl.set("shuffle", Json::fromBool(true));
        music.set(name, pl);
        c.did("музыка «" + name + "»: " + std::to_string(files.size()) + " трек(ов)");
    }
    // sounds: one bank entry per file, events by name
    Json& sounds = a.member("sounds");
    Json& events = a.member("events");
    for (auto& s : c.sounds) {
        const std::string id = "wiz_" + fs::path(s).stem().string();
        Json e = Json::object();
        e.set("file", Json::string(s));
        const auto ws = words(fs::path(s).stem().string());
        const char* ev = hasWord(ws, {"click", "button", "tap", "ui", "клик", "кнопк"}) ? "click"
                       : hasWord(ws, {"select", "выбор", "выдел"}) ? "select"
                       : hasWord(ws, {"move", "order", "go", "ack", "yes", "приказ", "идём", "иду"}) ? "move"
                       : hasWord(ws, {"death", "die", "dead", "explo", "boom", "смерт", "взрыв"}) ? "died"
                       : hasWord(ws, {"attack", "alarm", "alert", "тревог", "атак"}) ? "underAttack"
                       : hasWord(ws, {"victory", "win", "побед"}) ? "victory"
                       : hasWord(ws, {"defeat", "lose", "пораж"}) ? "defeat"
                       : hasWord(ws, {"build", "construct", "постро", "стро"}) ? "built"
                       : hasWord(ws, {"coin", "buy", "purchase", "монет", "покуп"}) ? "" : "";
        if (std::string(ev) == "click" || std::string(ev) == "select") e.set("bus", Json::string("ui"));
        sounds.set(id, e);
        if (*ev && !events.has(ev)) { events.set(ev, Json::string(id)); assign(c, s, "audio.json events." + std::string(ev), "звук по имени файла"); }
        if (hasWord(ws, {"coin", "buy", "purchase", "монет", "покуп"})) {   // the upgrades screen plays "wiz_buy"
            Json b = e; b.set("bus", Json::string("ui"));
            sounds.set("wiz_buy", b);
        }
    }
    std::string err;
    if (!writeText(file, a.dump() + "\n", &err)) c.warn(err);
}

void applyBranding(Workspace& ws, GameMaker::Ctx& c) {
    if (!c.spec.brandingFromArt) return;
    Json p = Json::object();
    if (c.keyArt.empty() && c.iconArt.empty()) {
        // the template's icon and splash carry the template's studio logos: the engine generates neutral ones instead
        p.set("icons", Json());
        Json sp = Json::object();
        sp.set("background", Json::string("#0d1117"));
        sp.set("minSeconds", Json::number(int64_t(1)));
        p.set("splash", sp);
        Json r = ws.call("branding_set", p, "wizard");
        if (r.has("error")) c.warn("иконка/заставка: " + r.getStr("error"));
        else c.did("иконка и заставка: созданы движком (арта нет)");
        return;
    }
    // icon: a 1024 square from the best candidate (cover crop when it isn't square)
    std::string icon;
    branding::Image img;
    if (!c.iconArt.empty() && branding::loadImageFile(c.path(c.iconArt), img)) {
        branding::Image sq = branding::fitCover(img, 1024, 1024);
        std::string err;
        if (branding::writePng(c.path("branding/wiz_icon.png"), sq, &err)) icon = "branding/wiz_icon.png";
        else c.warn("иконка: " + err);
    }
    branding::Image key;
    std::string bg = "#101418";
    if (!c.keyArt.empty() && branding::loadImageFile(c.path(c.keyArt), key)) {
        const auto col = branding::edgeColor(key);
        char b[16];
        std::snprintf(b, sizeof b, "#%02x%02x%02x", int(col.r * 255), int(col.g * 255), int(col.b * 255));
        bg = b;
    }
    if (!icon.empty()) {
        Json ic = Json::object();
        ic.set("default", Json::string(icon));
        ic.set("background", Json::string(bg));
        p.set("icons", ic);
    }
    Json sp = Json::object();
    sp.set("background", Json::string(bg));
    if (!c.keyArt.empty()) { sp.set("image", Json::string(c.keyArt)); sp.set("fit", Json::string("cover")); }
    if (!icon.empty() && icon != c.keyArt) {
        Json logos = Json::array();
        Json l = Json::object();
        l.set("image", Json::string(icon));
        l.set("start", Json::number(int64_t(0)));
        l.set("duration", Json::numberText("2.2"));
        l.set("scale", Json::numberText("0.3"));
        Json an = Json::object(); an.set("fadeIn", Json::numberText("0.4")); an.set("fadeOut", Json::numberText("0.4"));
        l.set("animation", an);
        logos.push(l);
        sp.set("logos", logos);
    }
    sp.set("minSeconds", Json::numberText("2.5"));
    sp.set("skippable", Json::fromBool(true));
    p.set("splash", sp);
    Json r = ws.call("branding_set", p, "wizard");
    if (r.has("error")) c.warn("иконка/заставка: " + r.getStr("error"));
    else c.did("иконка " + (icon.empty() ? std::string("шаблона") : icon) + ", заставка " + (c.keyArt.empty() ? std::string("цветом") : c.keyArt));
}

void applyPalette(GameMaker::Ctx& c) {
    if (!c.spec.paletteFromArt || c.keyArt.empty()) return;
    branding::Image img;
    if (!branding::loadImageFile(c.path(c.keyArt), img)) return;
    branding::Image s = branding::resize(img, 48, 48);
    // accent: the most common saturated, not too dark hue (12 bins); panel: the image's dark average
    double bins[12][4] = {};
    double dark[3] = {}; int darkN = 0;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            const uint8_t* p = s.px(x, y);
            const float r = p[0] / 255.f, g = p[1] / 255.f, b = p[2] / 255.f;
            const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
            if (mx < 0.35f) { dark[0] += r; dark[1] += g; dark[2] += b; ++darkN; }
            if (d < 0.25f || mx < 0.4f) continue;
            float h = mx == r ? std::fmod((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
            if (h < 0) h += 6;
            const int bin = std::clamp(int(h * 2), 0, 11);
            const double w = d * d * mx * mx;   // vivid colours count more than large dull areas
            bins[bin][0] += w; bins[bin][1] += r * w; bins[bin][2] += g * w; bins[bin][3] += b * w;
        }
    int best = -1;
    for (int i = 0; i < 12; ++i) if (bins[i][0] > 0 && (best < 0 || bins[i][0] > bins[best][0])) best = i;
    if (best < 0) return;
    auto hex = [](double r, double g, double b) {
        char o[16];
        std::snprintf(o, sizeof o, "#%02x%02x%02x", std::clamp(int(r * 255), 0, 255), std::clamp(int(g * 255), 0, 255), std::clamp(int(b * 255), 0, 255));
        return std::string(o);
    };
    const double W = bins[best][0];
    double ar = bins[best][1] / W, ag = bins[best][2] / W, ab = bins[best][3] / W;
    const double am = std::max({ar, ag, ab});
    if (am < 0.75) { ar *= 0.85 / am; ag *= 0.85 / am; ab *= 0.85 / am; }   // bright enough to read on dark panels
    const std::string accent = hex(ar, ag, ab);
    const std::string panel = darkN ? hex(dark[0] / darkN * 0.8, dark[1] / darkN * 0.8, dark[2] / darkN * 0.8) + "e8" : "#141820e8";
    // the menu backdrop: the art under a dimmed screen (RmlUi resolves the image next to ui/skin.rcss)
    const std::string menu = c.menuArt.empty() ? c.keyArt : c.menuArt;
    const std::string bgName = "wiz_bg" + fs::path(menu).extension().string();
    std::error_code ec;
    rts::pack::copyFileReplace(c.path(menu), c.path("ui/" + bgName), ec);
    const std::string screenBg = darkN ? hex(dark[0] / darkN * 0.6, dark[1] / darkN * 0.6, dark[2] / darkN * 0.6) + "b8" : "#0c0f14b8";
    std::string css = "\n/* game wizard: colours from the key art (" + c.keyArt + "); edit or delete freely */\n"
        "h1, .screen-title { color: " + accent + "; }\n"
        ".panel { background-color: " + panel + "; border-color: " + accent + "; }\n"
        "button.primary { border-color: " + accent + "; }\n"
        "button:hover { border-color: " + accent + "; }\n"
        ".progress-fill, progress fill { background-color: " + accent + "; }\n"
        ".badge { border-color: " + accent + "; }\n"
        + (ec ? std::string() : ".screen { decorator: none; background-color: " + screenBg + "; }\n"
                                "#menu-bg { decorator: image(" + bgName + " cover); }\n");
    const std::string skin = c.path("ui/skin.rcss");
    writeText(skin, readText(skin) + css);
    c.did("цвета интерфейса из арта: акцент " + accent);
}

} // namespace rts::editor::wiz

#include "AtlasFormat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/data/Json.h"

namespace aiverse::atlas {

namespace {
void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(char(uint8_t(v >> (8 * i)))); }
uint32_t get32(const std::string& s, size_t o) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | uint8_t(s[o + size_t(i)]);
    return v;
}
bool fail(std::string* e, const std::string& m) { if (e) *e = m; return false; }
} // namespace

std::string sheetFramePath(std::string_view sheet, int frame) { return std::string(sheet) + "#" + std::to_string(frame); }

void meshRows(const std::vector<float>& poly, int w, int h, std::vector<int>& rows) {
    rows.assign(size_t(std::max(0, h)) * 2, 0);
    const size_t n = poly.size() / 2;
    if (n < 3) return;
    std::vector<double> a, b;
    for (int y = 0; y < h; ++y) {
        // clip to the slab y <= Y <= y + 1 (Sutherland-Hodgman, two planes), then take the x extent
        a.assign(poly.begin(), poly.begin() + std::ptrdiff_t(n * 2));
        for (int side = 0; side < 2 && !a.empty(); ++side) {
            const double lim = side == 0 ? double(y) : double(y + 1);
            auto in = [&](double py) { return side == 0 ? py >= lim : py <= lim; };
            b.clear();
            const size_t m = a.size() / 2;
            for (size_t i = 0; i < m; ++i) {
                const double x0 = a[2 * i], y0 = a[2 * i + 1], x1 = a[2 * ((i + 1) % m)], y1 = a[2 * ((i + 1) % m) + 1];
                const bool i0 = in(y0), i1 = in(y1);
                if (i0) { b.push_back(x0); b.push_back(y0); }
                if (i0 != i1) { const double t = (lim - y0) / (y1 - y0); b.push_back(x0 + (x1 - x0) * t); b.push_back(lim); }
            }
            a.swap(b);
        }
        if (a.empty()) continue;
        double lo = a[0], hi = a[0];
        for (size_t i = 0; i < a.size(); i += 2) { lo = std::min(lo, a[i]); hi = std::max(hi, a[i]); }
        int x0 = std::clamp(int(std::floor(lo)), 0, w), x1 = std::clamp(int(std::ceil(hi)), 0, w);
        if (x1 == x0 && x0 < w && hi >= lo) x1 = x0 + 1;   // a sliver still touches its column
        rows[size_t(y) * 2] = x0;
        rows[size_t(y) * 2 + 1] = x1;
    }
}

// ------------------------------------------------------------------ manifest
// "textures": { "<path>": [page, x, y, w, h, offsetX, offsetY, sourceW, sourceH, rotated] }
// "meshes":   { "<path>": [x0, y0, x1, y1, ...] }  content pixels * kMeshUnits (optional)
// "sheets":   { "<path>": [cols, rows, sourceW, sourceH] }  frames are "<path>#<i>" (optional)
bool Manifest::parse(const std::string& text, std::string* error) {
    pages_.clear();
    regions_.clear();
    sheets_.clear();
    Json j;
    try { j = Json::parse(text); } catch (const std::exception& e) { return fail(error, std::string("atlas manifest: ") + e.what()); }
    if (j.getInt("version", 0) != kManifestVersion) return fail(error, "atlas manifest: unsupported version");
    for (const Json& p : j["pages"].items()) {
        Page pg;
        pg.file = p.getStr("file");
        pg.group = p.getStr("group");
        pg.width = int(p.getInt("w"));
        pg.height = int(p.getInt("h"));
        pg.alpha = p.getBool("alpha", true);
        pg.mips = p.getBool("mips", false);
        if (pg.file.empty() || pg.width <= 0 || pg.height <= 0) return fail(error, "atlas manifest: bad page entry");
        pages_.push_back(std::move(pg));
    }
    for (const auto& [path, a] : j["textures"].members()) {
        if (!a.isArray() || a.size() < 10) return fail(error, "atlas manifest: bad entry '" + path + "'");
        Region r;
        r.page = int(a[0].integer());
        r.x = int(a[1].integer()); r.y = int(a[2].integer());
        r.w = int(a[3].integer()); r.h = int(a[4].integer());
        r.offsetX = int(a[5].integer()); r.offsetY = int(a[6].integer());
        r.sourceW = int(a[7].integer()); r.sourceH = int(a[8].integer());
        r.rotated = a[9].integer() != 0;
        if (r.page < 0 || size_t(r.page) >= pages_.size()) return fail(error, "atlas manifest: '" + path + "' refers to a missing page");
        const Page& pg = pages_[size_t(r.page)];
        int pw = r.rotated ? r.h : r.w, ph = r.rotated ? r.w : r.h;
        if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 || r.x + pw > pg.width || r.y + ph > pg.height)
            return fail(error, "atlas manifest: '" + path + "' is outside its page");
        regions_.emplace(path, r);
    }
    for (const auto& [path, a] : j["meshes"].members()) {
        auto it = regions_.find(path);
        if (it == regions_.end() || !a.isArray() || a.size() < 6 || a.size() % 2) return fail(error, "atlas manifest: bad mesh '" + path + "'");
        Region& r = it->second;
        for (size_t i = 0; i < a.size(); ++i) {
            int64_t q = a[i].integer();
            if (q < 0 || q > int64_t(i % 2 ? r.h : r.w) * kMeshUnits) return fail(error, "atlas manifest: mesh '" + path + "' is outside its content");
            r.mesh.push_back(float(q) / float(kMeshUnits));
        }
    }
    for (const auto& [path, a] : j["sheets"].members()) {
        if (!a.isArray() || a.size() < 4) return fail(error, "atlas manifest: bad sheet '" + path + "'");
        Sheet sh;
        sh.cols = int(a[0].integer()); sh.rows = int(a[1].integer());
        sh.sourceW = int(a[2].integer()); sh.sourceH = int(a[3].integer());
        if (sh.cols <= 0 || sh.rows <= 0 || sh.sourceW % sh.cols || sh.sourceH % sh.rows || sh.sourceW <= 0 || sh.sourceH <= 0)
            return fail(error, "atlas manifest: bad sheet '" + path + "'");
        for (int i = 0; i < sh.cols * sh.rows; ++i) {
            const Region* f = find(sheetFramePath(path, i));
            if (!f || f->sourceW != sh.frameW() || f->sourceH != sh.frameH()) return fail(error, "atlas manifest: sheet '" + path + "' misses frame " + std::to_string(i));
        }
        sheets_.emplace(path, sh);
    }
    return true;
}

std::string Manifest::serialize() const {
    Json j = Json::object();
    j.set("version", Json::number(int64_t(kManifestVersion)));
    Json pages = Json::array();
    for (const Page& p : pages_) {
        Json o = Json::object();
        o.set("file", Json::string(p.file));
        o.set("group", Json::string(p.group));
        o.set("w", Json::number(int64_t(p.width)));
        o.set("h", Json::number(int64_t(p.height)));
        o.set("alpha", Json::fromBool(p.alpha));
        o.set("mips", Json::fromBool(p.mips));
        pages.push(std::move(o));
    }
    j.set("pages", std::move(pages));
    Json tex = Json::object();
    for (const auto& [path, r] : regions_) { // std::map: sorted, deterministic
        Json a = Json::array();
        for (int v : {r.page, r.x, r.y, r.w, r.h, r.offsetX, r.offsetY, r.sourceW, r.sourceH, int(r.rotated)}) a.push(Json::number(int64_t(v)));
        tex.set(path, std::move(a));
    }
    j.set("textures", std::move(tex));
    Json meshes = Json::object();
    for (const auto& [path, r] : regions_) {
        if (r.mesh.empty()) continue;
        Json a = Json::array();
        for (float v : r.mesh) a.push(Json::number(int64_t(std::lround(double(v) * kMeshUnits))));
        meshes.set(path, std::move(a));
    }
    if (meshes.size()) j.set("meshes", std::move(meshes));
    Json sheets = Json::object();
    for (const auto& [path, sh] : sheets_) {
        Json a = Json::array();
        for (int v : {sh.cols, sh.rows, sh.sourceW, sh.sourceH}) a.push(Json::number(int64_t(v)));
        sheets.set(path, std::move(a));
    }
    if (sheets.size()) j.set("sheets", std::move(sheets));
    return j.dump(1) + "\n";
}

const Region* Manifest::find(std::string_view path) const {
    auto it = regions_.find(path);
    return it == regions_.end() ? nullptr : &it->second;
}

const Sheet* Manifest::findSheet(std::string_view path) const {
    auto it = sheets_.find(path);
    return it == sheets_.end() ? nullptr : &it->second;
}

void regionUVs(const Region& r, int pageW, int pageH, float out[8]) {
    float iw = 1.0f / float(pageW), ih = 1.0f / float(pageH);
    float x0 = float(r.x) * iw, y0 = float(r.y) * ih;
    if (!r.rotated) {
        float x1 = float(r.x + r.w) * iw, y1 = float(r.y + r.h) * ih;
        const float v[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
        std::memcpy(out, v, sizeof(v));
    } else { // stored turned 90 degrees clockwise: occupies h x w page pixels
        float x1 = float(r.x + r.h) * iw, y1 = float(r.y + r.w) * ih;
        const float v[8] = {x1, y0, x1, y1, x0, y1, x0, y0};
        std::memcpy(out, v, sizeof(v));
    }
}

// ------------------------------------------------------------------ .rtex
bool isRtex(const std::string& b) { return b.size() >= 24 && std::memcmp(b.data(), "RTEX", 4) == 0; }

std::string encodeRtex(const Image& img, bool alpha, bool mips, bool deltaFilter) {
    std::string s = "RTEX";
    put32(s, 1);
    put32(s, uint32_t(img.width));
    put32(s, uint32_t(img.height));
    put32(s, (alpha ? 1u : 0u) | (mips ? 2u : 0u) | (deltaFilter ? 4u : 0u));
    put32(s, 0);
    size_t row = size_t(img.width) * 4, base = s.size();
    s.resize(base + row * size_t(img.height));
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* src = img.rgba.data() + row * size_t(y);
        uint8_t* dst = reinterpret_cast<uint8_t*>(s.data() + base + row * size_t(y));
        for (size_t i = 0; i < row; ++i) dst[i] = deltaFilter && i >= 4 ? uint8_t(src[i] - src[i - 4]) : src[i];
    }
    return s;
}

bool decodeRtex(const std::string& b, Image& out, uint32_t* flags) {
    if (!isRtex(b) || get32(b, 4) != 1) return false;
    uint32_t w = get32(b, 8), h = get32(b, 12), f = get32(b, 16);
    if (w == 0 || h == 0 || w > 16384 || h > 16384 || b.size() != 24 + size_t(w) * h * 4) return false;
    out.width = int(w);
    out.height = int(h);
    out.rgba.assign(b.begin() + 24, b.end());
    if (f & 4u) {
        size_t row = size_t(w) * 4;
        for (size_t y = 0; y < h; ++y) {
            uint8_t* p = out.rgba.data() + row * y;
            for (size_t i = 4; i < row; ++i) p[i] = uint8_t(p[i] + p[i - 4]);
        }
    }
    if (flags) *flags = f;
    return true;
}

bool extractRegionInto(const Image& page, const Region& r, Image& out, int dx, int dy) {
    int pw = r.rotated ? r.h : r.w, ph = r.rotated ? r.w : r.h;
    if (r.x < 0 || r.y < 0 || r.x + pw > page.width || r.y + ph > page.height) return false;
    if (r.offsetX < 0 || r.offsetY < 0 || r.offsetX + r.w > r.sourceW || r.offsetY + r.h > r.sourceH) return false;
    if (dx < 0 || dy < 0 || dx + r.sourceW > out.width || dy + r.sourceH > out.height) return false;
    std::vector<int> rows;
    if (!r.mesh.empty()) meshRows(r.mesh, r.w, r.h, rows);
    for (int sy = 0; sy < r.h; ++sy) {
        // texels outside the mesh may belong to a neighbour (tight packing): leave them transparent
        const int x0 = rows.empty() ? 0 : rows[size_t(sy) * 2], x1 = rows.empty() ? r.w : rows[size_t(sy) * 2 + 1];
        for (int sx = x0; sx < x1; ++sx) {
            // rotated clockwise: source (sx, sy) -> page (h-1-sy, sx)
            const uint8_t* s = r.rotated ? page.px(r.x + r.h - 1 - sy, r.y + sx) : page.px(r.x + sx, r.y + sy);
            std::memcpy(out.px(dx + r.offsetX + sx, dy + r.offsetY + sy), s, 4);
        }
    }
    return true;
}

bool extractRegion(const Image& page, const Region& r, Image& out) {
    if (r.sourceW <= 0 || r.sourceH <= 0) return false;
    out.width = r.sourceW;
    out.height = r.sourceH;
    out.rgba.assign(size_t(r.sourceW) * size_t(r.sourceH) * 4, 0);
    return extractRegionInto(page, r, out, 0, 0);
}

// ------------------------------------------------------------------ CPU image source
bool loadPageImage(const std::string& bytes, const ImageSource::DecodeFn& decodePng, Image& out) {
    if (isRtex(bytes)) return decodeRtex(bytes, out);
    return decodePng && decodePng(bytes, out);
}

ImageSource::ImageSource(ReadFn read, DecodeFn decodePng) : read_(std::move(read)), decodePng_(std::move(decodePng)) {
    std::string text;
    if (read_ && read_(kManifestPath, text)) manifest_.parse(text); // a broken manifest = no atlas
}

bool ImageSource::page(int index) {
    if (cachedPage_ == index) return true;
    std::string bytes;
    cachedPage_ = -1;
    if (index < 0 || size_t(index) >= manifest_.pages().size()) return false;
    if (!read_(manifest_.pages()[size_t(index)].file, bytes) || !loadPageImage(bytes, decodePng_, cache_)) return false;
    cachedPage_ = index;
    return true;
}

bool ImageSource::loadImage(const std::string& path, Image& out) {
    Image decoded;
    if (atlased(path)) {
        if (!load(path, decoded)) return false;
    } else {
        std::string bytes;
        if (!read_ || !read_(path, bytes) || !loadPageImage(bytes, decodePng_, decoded)) return false;
    }
    out = std::move(decoded);
    return true;
}

bool ImageSource::load(const std::string& path, Image& out) {
    if (const Region* r = manifest_.find(path)) return page(r->page) && extractRegion(cache_, *r, out);
    const Sheet* sh = manifest_.findSheet(path);
    if (!sh) return false;
    out.width = sh->sourceW;
    out.height = sh->sourceH;
    out.rgba.assign(size_t(out.width) * size_t(out.height) * 4, 0);
    // frames in page order: each page decoded once
    std::vector<std::pair<int, int>> order;
    for (int i = 0; i < sh->cols * sh->rows; ++i) order.push_back({manifest_.frame(path, i)->page, i});
    std::sort(order.begin(), order.end());
    for (auto [pg, i] : order) {
        const Region* f = manifest_.frame(path, i);
        if (!page(pg) || !extractRegionInto(cache_, *f, out, (i % sh->cols) * sh->frameW(), (i / sh->cols) * sh->frameH())) return false;
    }
    return true;
}

} // namespace aiverse::atlas

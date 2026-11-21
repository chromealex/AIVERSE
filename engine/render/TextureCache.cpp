#include "TextureCache.h"
#include "GpuResourceRetirement.h"
#include "../platform/Platform.h"

// Private copy of stb_image (static functions) so this file links next to the UI's copy.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdio>
#include <cstring>

#include "engine/atlas/AtlasFormat.h"

namespace aiverse::render {

bool decodeImage(const std::string& bytes, int& w, int& h, std::vector<uint8_t>& rgba) {
    int n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), int(bytes.size()), &w, &h, &n, 4);
    if (!px) return false;
    rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    return true;
}

void TextureCache::shutdown() {
    auto isPage = [&](gfx::TextureHandle h) {
        for (const auto& [k, p] : atlasPages_) if (p == h) return true;
        return false;
    };
    std::vector<gfx::TextureHandle> gone;
    auto alreadyReleased = [&](gfx::TextureHandle h) {
        for (auto old : gone) if (old == h) return true;
        return false;
    };
    auto releaseOwned = [&](gfx::TextureHandle h) {
        if (!dev_ || !h.valid() || alreadyReleased(h)) return;
        // A texture may still be sampled by the old particle pipeline or
        // current ImGui preview. Never destroy before its GPU fence.
        gfx::TextureHandle owned = h;
        if (gpuRetirement_) gpuRetirement_->retire(*dev_,owned);
        else dev_->destroyTexture(h);
        gone.push_back(h);
    };
    for (const auto& [key, texture] : cache_)
        if (texture.tex.valid() && !isPage(texture.tex))
            releaseOwned(texture.tex);
    for (const auto& [key, texture] : atlasPages_)
        releaseOwned(texture);
    cache_.clear();
    atlasPages_.clear();
    sprites_.clear();
    atlas_.reset();
}

void TextureCache::useAtlas(const std::string& base) {
    if (atlas_ && atlasBase_ == base) return;
    atlasBase_ = base;
    atlas_ = std::make_shared<atlas::ImageSource>([base](const std::string& p, std::string& o) { return platform::readContentFile(base + p, o); },
                                                   [](const std::string& b, atlas::Image& img) { return decodeImage(b, img.width, img.height, img.rgba); });
}

gfx::TextureHandle TextureCache::atlasPage(int index, bool nearest, const std::string& base) {
    const atlas::Page& pg = atlas_->manifest().pages()[size_t(index)];
    std::string key = base + pg.file + (nearest ? "#n" : "#l");
    gfx::TextureHandle& page = atlasPages_[key];
    if (!page.valid()) {
        std::string bytes;
        atlas::Image img;
        if (!platform::readContentFile(base + pg.file, bytes) || !atlas::loadPageImage(bytes, [](const std::string& b, atlas::Image& i) { return decodeImage(b, i.width, i.height, i.rgba); }, img))
            return {};
        gfx::TextureDesc td;
        td.filter = nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
        td.width = img.width; td.height = img.height; td.data = img.rgba.data(); td.label = "atlas-page";
        page = dev_->createTexture(td);
    }
    return page;
}

bool TextureCache::resolveAtlased(const std::string& path, bool nearest, const std::string& base, TextureRef& r) {
    useAtlas(base);
    const atlas::Region* reg = atlas_->manifest().find(path);
    if (!reg && !atlas_->manifest().findSheet(path)) return false;
    if (!reg || !reg->plain()) { // callers map uvs linearly over the whole image: give them the original pixels
        atlas::Image img;
        if (!atlas_->load(path, img)) return false;
        gfx::TextureDesc td;
        td.filter = nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
        td.width = img.width; td.height = img.height; td.data = img.rgba.data(); td.label = path.c_str();
        r.tex = dev_->createTexture(td);
        r.width = img.width; r.height = img.height;
        return true;
    }
    const atlas::Page& pg = atlas_->manifest().pages()[size_t(reg->page)];
    r.tex = atlasPage(reg->page, nearest, base);
    if (!r.tex.valid()) return false;
    r.u0 = float(reg->x) / float(pg.width); r.v0 = float(reg->y) / float(pg.height);
    r.u1 = float(reg->x + reg->w) / float(pg.width); r.v1 = float(reg->y + reg->h) / float(pg.height);
    r.width = reg->sourceW; r.height = reg->sourceH;
    return true;
}

TextureRef TextureCache::resolveSprite(const std::string& path, bool nearest, const std::string& base, int frameW, int frameH) {
    const std::string key = base + path + (nearest ? "#n" : "#l") + "#sprite" + std::to_string(frameW) + "x" + std::to_string(frameH);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    TextureRef r;
    if (dev_) {
        useAtlas(base);
        const atlas::Manifest& m = atlas_->manifest();
        auto sprite = std::make_unique<AtlasSprite>();
        int W = 0, H = 0;
        if (const atlas::Sheet* sh = m.findSheet(path)) {
            const int fw = frameW > 0 ? frameW : sh->sourceW, fh = frameH > 0 ? frameH : fw;
            if (fw == sh->frameW() && fh == sh->frameH()) {   // the renderer's grid is the build's grid
                sprite->cols = sh->cols; sprite->rows = sh->rows;
                for (int i = 0; i < sh->cols * sh->rows; ++i) sprite->frames.push_back({{}, *m.frame(path, i), 1, 1});
                W = sh->sourceW; H = sh->sourceH;
            }
        } else if (const atlas::Region* reg = m.find(path); reg && !reg->plain()) {
            sprite->frames.push_back({{}, *reg, 1, 1});
            W = reg->sourceW; H = reg->sourceH;
        }
        bool ok = !sprite->frames.empty();
        for (AtlasFrame& f : sprite->frames) {
            if (!ok) break;
            const atlas::Page& pg = m.pages()[size_t(f.region.page)];
            f.tex = atlasPage(f.region.page, nearest, base);
            f.pageW = pg.width; f.pageH = pg.height;
            ok = f.tex.valid();
        }
        if (ok) {
            r.tex = sprite->frames[0].tex;
            r.width = W; r.height = H;
            r.sprite = sprite.get();
            sprites_[key] = std::move(sprite);
            cache_[key] = r;
            return r;
        }
    }
    r = resolve(path, nearest, base);
    cache_[key] = r;
    return r;
}

TextureRef TextureCache::resolve(const std::string& path, bool nearest, const std::string& base, bool tiling) {
    std::string key = base + path + (nearest ? "#n" : "#l") + (tiling ? "#t" : "");
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    TextureRef r;
    std::string bytes;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (dev_ && !tiling && resolveAtlased(path, nearest, base, r)) {
        // packed into a texture atlas at build time
    } else if (!dev_ || !platform::readContentFile(base + path, bytes)) {
        error_ = "texture not found: " + path;
        std::fprintf(stderr, "[sprites] %s\n", error_.c_str());
    } else if (!decodeImage(bytes, w, h, rgba)) {
        error_ = "texture: cannot decode " + path;
        std::fprintf(stderr, "[sprites] %s\n", error_.c_str());
    } else {
        gfx::TextureDesc td;
        td.width = w; td.height = h;
        td.filter = nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
        td.data = rgba.data();
        td.label = path.c_str();
        td.repeat = tiling; td.mipmaps = tiling;
        r.tex = dev_->createTexture(td);
        r.width = w; r.height = h;
    }
    cache_[key] = r; // failures are cached too: one message, no retry every frame
    return r;
}

TextureRef TextureCache::addGenerated(const std::string& name, int w, int h, const std::vector<uint32_t>& px, bool nearest) {
    if (auto it = cache_.find(name); it != cache_.end()) return it->second;
    TextureRef r;
    if (!dev_ || px.size() < size_t(w) * size_t(h)) return r;
    gfx::TextureDesc td;
    td.width = w; td.height = h;
    td.filter = nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
    td.data = px.data();
    td.label = name.c_str();
    r.tex = dev_->createTexture(td);
    r.width = w; r.height = h;
    cache_[name] = r;
    return r;
}

} // namespace aiverse::render

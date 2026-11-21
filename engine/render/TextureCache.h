// Image textures for sprites and tiles (PNG/JPEG/TGA via stb_image, read with
// platform::readContentFile so packs, Android assets and the web preload work).
//
// `resolve(path)` is the single indirection between a *logical* texture path used by game data
// ("sprites/tank.png") and the GPU: it returns the texture plus the uv rect the image occupies.
// Today every image is its own texture (rect = 0..1); a build-time atlas packer can return a
// sub-rect of a shared page instead without touching the renderers, as long as callers map their
// frame uvs through TextureRef::u()/v() and never rely on wrap/repeat.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include "../gfx/Gfx.h"
#include "GpuOwnedMemory.h"

#include "engine/atlas/AtlasFormat.h"

namespace aiverse::render {
class GpuResourceRetirement;


// An atlased image drawn straight from its pages (TextureCache::resolveSprite): per frame of the
// frame grid (cols x rows over the logical image; 1 x 1 for a whole image) its region (trim,
// rotation, mesh) and page texture. Only renderers that draw shapes (SpriteBatch::spriteShape)
// can use it: the region's rectangle may hold other images.
struct AtlasFrame {
    gfx::TextureHandle tex;
    atlas::Region region;
    int pageW = 1, pageH = 1;
};
struct AtlasSprite {
    int cols = 1, rows = 1;
    std::vector<AtlasFrame> frames;   // row-major
};

struct TextureRef {
    gfx::TextureHandle tex;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;  // rect of the logical image inside `tex`
    int width = 0, height = 0;             // logical image size in pixels (before any atlas trim)
    const AtlasSprite* sprite = nullptr;   // resolveSprite: draw through the frames, u0..v1 are unused
    bool valid() const { return tex.valid() && width > 0 && height > 0; }
    // logical uv (0..1 over the image) -> texture uv
    float u(float lu) const { return u0 + (u1 - u0) * lu; }
    float v(float lv) const { return v0 + (v1 - v0) * lv; }
};

class TextureCache {
public:
    void init(gfx::Device& dev) { dev_ = &dev; }
    // Atomic owner handoff for staged GPU icon caches. AtlasSprite addresses
    // remain valid because their heap objects stay inside unique_ptr nodes.
    void swap(TextureCache& other) noexcept {
        using std::swap;
        swap(atlas_,other.atlas_);
        atlasBase_.swap(other.atlasBase_);
        atlasPages_.swap(other.atlasPages_);
        sprites_.swap(other.sprites_);
        cache_.swap(other.cache_);
        error_.swap(other.error_);
        swap(dev_,other.dev_);
        swap(gpuRetirement_,other.gpuRetirement_);
    }
    // Optional editor hot-reload owner; retained by Editor across TextureCache
    // asset reloads. Doesn't own borrowed atlased sprite references.
    void setGpuResourceRetirement(GpuResourceRetirement* owner) {
        gpuRetirement_ = owner;
    }
    void shutdown();
    // `base` is prefixed to the path when reading (editor: "<project dir>/", game: "").
    // `tiling`: a repeating world-space texture (terrain splat layers): never taken from an atlas
    // page (wrap needs its own texture), created with repeat + mipmaps.
    TextureRef resolve(const std::string& path, bool nearest = true, const std::string& base = "", bool tiling = false);
    // For shape-drawing renderers (sprites through SpriteBatch): an image the build trimmed,
    // rotated, meshed or split into frames is drawn from its atlas page (TextureRef::sprite) instead
    // of being rebuilt as its own texture. frameW / frameH: the renderer's frame grid (0 = image
    // width / frameW, as the sprite blocks read "frame"); a framed sheet whose frames differ from it
    // comes back as resolve() does. Images on a page as they are resolve() unchanged.
    TextureRef resolveSprite(const std::string& path, bool nearest, const std::string& base, int frameW, int frameH);
    // Procedural texture registered under a logical name (e.g. "builtin:shadow").
    TextureRef addGenerated(const std::string& name, int w, int h, const std::vector<uint32_t>& rgba, bool nearest);
    const std::string& lastError() const { return error_; }
    // Owned sprite/atlas textures, not their borrowed AtlasFrame aliases.
    // Render-thread only; GPU retirement owns quarantined old textures.
    // Shared atlas pages are de-duplicated against cache references.
    GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        if (!dev_) return {};
        GpuOwnedMemoryCounter counter(*dev_);
        for (const auto& [key, page] : atlasPages_) {
            (void)key; counter.texture(page);
        }
        for (const auto& [key, ref] : cache_) {
            (void)key; counter.texture(ref.tex);
        }
        return counter.stats();
    }

private:
    // Build-time texture atlases (engine/atlas): images packed into a page resolve to the page +
    // their rect; trimmed/rotated ones (never sprite sheets) are rebuilt as standalone textures.
    bool resolveAtlased(const std::string& path, bool nearest, const std::string& base, TextureRef& out);
    void useAtlas(const std::string& base);
    gfx::TextureHandle atlasPage(int page, bool nearest, const std::string& base);
    std::shared_ptr<atlas::ImageSource> atlas_;
    std::map<std::string, std::unique_ptr<AtlasSprite>> sprites_;
    std::string atlasBase_;             // base the manifest was loaded for
    std::map<std::string, gfx::TextureHandle> atlasPages_;
    gfx::Device* dev_ = nullptr;
    GpuResourceRetirement* gpuRetirement_ = nullptr;
    std::map<std::string, TextureRef> cache_;
    std::string error_;
};

// Decodes an image file in memory to RGBA8 (top row first). Returns false on bad data.
bool decodeImage(const std::string& bytes, int& w, int& h, std::vector<uint8_t>& rgba);

} // namespace aiverse::render

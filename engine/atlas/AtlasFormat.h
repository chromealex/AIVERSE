// Texture atlases: what the build produces and the runtime reads (CPU side, no GPU).
//
// At build time tools/atlas packs small 2D images (UI, unit sprites, tiles, effects) into atlas
// pages. The pack then holds, instead of the original image files:
//   _atlas/atlas.json        manifest: pages + logical texture path -> region (see docs/game-format.md)
//   _atlas/<group>-<n>.rtex  page images (or .png when "pageFormat": "png")
//
// Every coordinate in the manifest is an integer (pixels; mesh vertices in 1/kMeshUnits pixel), so
// the JSON reader's fixed-point numbers stay exact; UVs are derived from the page size at runtime.
//
// Meshes: a region may carry a convex polygon (content-local pixels, original orientation) that
// covers every non-transparent pixel of its content. Only the texels the polygon touches (plus the
// extrusion ring) are guaranteed to belong to the region: "tight" groups pack other images into
// the rest of its rectangle, so a region with a mesh must be drawn as that polygon, never as its
// rectangle (extractRegion / ImageSource rebuild the image through the polygon).
//
// Framed sprite sheets ("frames" in the atlas rules): a frame-grid image is split into its frames,
// each packed (trimmed, rotated, meshed) on its own as "<sheet path>#<frame index>" (row-major);
// "sheets" keeps the grid so the whole image can still be rebuilt (Manifest::findSheet).
//
// .rtex = raw RGBA8, rows top to bottom, straight (non-premultiplied) alpha. The pack's zstd
// does the compression, so loading a page costs one zstd decode + an optional delta unfilter
// (no inflate, no PNG parsing). Layout, little-endian:
//   0 char[4] "RTEX"   4 u32 version (1)   8 u32 width   12 u32 height
//  16 u32 flags (bit0 has alpha, bit1 wants mips, bit2 rows stored with per-byte left-delta filter)
//  20 u32 reserved (0)  24 pixels (width*height*4 bytes)
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::atlas {

constexpr const char* kManifestPath = "_atlas/atlas.json";
constexpr const char* kAtlasDir = "_atlas/";
constexpr int kManifestVersion = 1;
constexpr int kMeshUnits = 16;   // mesh vertices are stored in 1/16 pixel

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4, rows top to bottom
    bool empty() const { return width <= 0 || height <= 0; }
    uint8_t* px(int x, int y) { return rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4; }
    const uint8_t* px(int x, int y) const { return rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4; }
};

struct Page {
    std::string file;      // pack path of the page image
    std::string group;     // atlas group it belongs to (ui, units, tiles, effects, custom)
    int width = 0, height = 0;
    bool alpha = true;     // false: every pixel opaque (can be drawn without blending)
    bool mips = false;     // the group wants mipmaps (padding/alignment were made mip-safe)
};

// Where a logical texture lives. (x, y, w, h) is the trimmed content in the page; when `rotated`
// the content occupies h x w page pixels and is turned 90 degrees clockwise. (offsetX, offsetY)
// is the position of the trimmed content inside the original sourceW x sourceH image, so
// pivots/anchors given in original-image pixels stay valid.
struct Region {
    int page = 0;
    int x = 0, y = 0, w = 0, h = 0;
    int offsetX = 0, offsetY = 0;
    int sourceW = 0, sourceH = 0;
    bool rotated = false;
    std::vector<float> mesh;   // convex polygon x0 y0 x1 y1 ... in content pixels (0..w, 0..h); empty = the rectangle
    // the page rect holds exactly the original image: linear uv over the rect is correct
    bool plain() const { return !rotated && offsetX == 0 && offsetY == 0 && w == sourceW && h == sourceH && mesh.empty(); }
};

// A frame-grid image split into frames by the build: frame i (row-major) is the region
// "<path>#<i>", each frame's sourceW x sourceH = sourceW / cols x sourceH / rows.
struct Sheet {
    int cols = 1, rows = 1;
    int sourceW = 0, sourceH = 0;
    int frameW() const { return cols > 0 ? sourceW / cols : 0; }
    int frameH() const { return rows > 0 ? sourceH / rows : 0; }
};
std::string sheetFramePath(std::string_view sheet, int frame);

// Page uv (0..1, v down) of a point given in content pixels of `r` (original orientation).
inline void regionPointUV(const Region& r, float cx, float cy, int pageW, int pageH, float& u, float& v) {
    float px = r.rotated ? float(r.x + r.h) - cy : float(r.x) + cx, py = r.rotated ? float(r.y) + cx : float(r.y) + cy;
    u = px / float(pageW);
    v = py / float(pageH);
}

// Pixels of a w x h content touched by a convex polygon (x y pairs, content pixels): for each row y,
// [rows[2y], rows[2y+1]) (empty when equal). Conservative: a pixel whose square meets the polygon is in.
// Build and runtime use this one function, so they agree on which texels a mesh owns.
void meshRows(const std::vector<float>& poly, int w, int h, std::vector<int>& rows);

class Manifest {
public:
    bool parse(const std::string& json, std::string* error = nullptr);
    std::string serialize() const;   // deterministic (paths sorted), compact

    const Region* find(std::string_view path) const;
    const Sheet* findSheet(std::string_view path) const;
    const Region* frame(std::string_view sheet, int index) const { return find(sheetFramePath(sheet, index)); }
    bool contains(std::string_view path) const { return find(path) || findSheet(path); }
    const std::vector<Page>& pages() const { return pages_; }
    const std::map<std::string, Region, std::less<>>& regions() const { return regions_; }
    const std::map<std::string, Sheet, std::less<>>& sheets() const { return sheets_; }
    bool empty() const { return regions_.empty(); }

    // builders (host tools)
    std::vector<Page>& pagesMut() { return pages_; }
    void add(const std::string& path, const Region& r) { regions_[path] = r; }
    void addSheet(const std::string& path, const Sheet& s) { sheets_[path] = s; }

private:
    std::vector<Page> pages_;
    std::map<std::string, Region, std::less<>> regions_;
    std::map<std::string, Sheet, std::less<>> sheets_;
};

// UVs (0..1, v down) of the four corners of the ORIGINAL image orientation, order: top-left,
// top-right, bottom-right, bottom-left. Covers only the trimmed content; place the quad at
// (offsetX, offsetY) with size (w, h) in source pixels.
void regionUVs(const Region& r, int pageW, int pageH, float out[8]);

// .rtex
std::string encodeRtex(const Image& img, bool alpha, bool mips, bool deltaFilter);
bool decodeRtex(const std::string& bytes, Image& out, uint32_t* flags = nullptr);
bool isRtex(const std::string& bytes);

// Rebuilds the original image (untrimmed, unrotated, transparent where trimmed or outside the
// mesh) from a page.
bool extractRegion(const Image& page, const Region& r, Image& out);
// The same into `out` (already sized) with the original image's top-left at (dx, dy).
bool extractRegionInto(const Image& page, const Region& r, Image& out, int dx, int dy);

// CPU access to atlased images by their original path (e.g. the UI, which needs standalone
// textures). Keeps the last decoded page cached; `decodePng` handles pages stored as PNG.
class ImageSource {
public:
    using ReadFn = std::function<bool(const std::string& path, std::string& out)>;
    using DecodeFn = std::function<bool(const std::string& bytes, Image& out)>;
    ImageSource(ReadFn read, DecodeFn decodePng = {});
    // the original image: a region, or a framed sheet put back together from its frames
    bool atlased(std::string_view path) const { return manifest_.contains(path); }
    bool load(const std::string& path, Image& out);
    // Logical CPU image access: atlas reconstruction or standalone file decoding.
    bool loadImage(const std::string& path, Image& out);
    const Manifest& manifest() const { return manifest_; }
    void dropCache() { cachedPage_ = -1; cache_ = {}; }

private:
    bool page(int index);
    ReadFn read_;
    DecodeFn decodePng_;
    Manifest manifest_;
    int cachedPage_ = -1;
    Image cache_;
};

// Decodes PNG/JPG/TGA (stb_image, private copy) into straight-alpha RGBA8.
bool decodeImageFile(const std::string& bytes, Image& out);

bool loadPageImage(const std::string& bytes, const ImageSource::DecodeFn& decodePng, Image& out);

} // namespace aiverse::atlas

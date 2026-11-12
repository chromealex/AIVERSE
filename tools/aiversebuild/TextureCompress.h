// ASTC texture compression for packaging (game.json "packaging" rules with "compress": "astc4x4" | "astc6x6").
//
// Encoding runs ARM's astcenc (external tool, Apache-2.0, not vendored): AIVERSE_ASTCENC, then
// ~/.aiverse/toolchains/astcenc/, then PATH (astcenc, astcenc-avx2, astcenc-sse4.1, astcenc-sse2, astcenc-neon).
// Every mip level is encoded separately and wrapped into a KTX 2.0 file (vkFormat ASTC_4x4 / 6x6,
// UNORM or SRGB, no supercompression: the resource pack compresses it with zstd), which the runtime
// detects by content (engine/render/TextureCodec.h), so the pack keeps the original file name.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::build {

struct AstcOptions {
    int block = 6;            // 4 or 6
    bool srgb = true;         // colour data (base colour, emission, UI); false for normal / data maps
    bool mipmaps = true;      // full chain down to 1x1
    bool alpha = true;        // false: alpha forced to 1 (cheaper blocks)
    std::string quality = "medium";   // astcenc preset: fastest | fast | medium | thorough
};

// Path of the astcenc executable, or "" when none is found.
std::string findAstcenc();

// Raw blocks of a .astc file (16-byte header + blocks); false when malformed.
bool readAstcFile(const std::string& bytes, int& blockX, int& blockY, int& width, int& height, std::string& blocks);

// KTX 2.0 bytes with a data format descriptor for ASTC; levels[0] is the largest mip.
std::string makeAstcKtx2(int block, bool srgb, int width, int height, const std::vector<std::string>& levels);

// Encodes RGBA8 pixels (width*height*4) into KTX2 bytes. tmpDir: scratch folder for the tool's files.
bool encodeAstcKtx2(const unsigned char* rgba, int width, int height, const AstcOptions& opt, const std::string& encoder,
                    const std::string& tmpDir, std::string& ktx2, std::string& err);

} // namespace aiverse::build

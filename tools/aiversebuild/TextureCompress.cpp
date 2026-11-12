#include "TextureCompress.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace aiverse::build {
namespace fs = std::filesystem;

namespace {
std::string shellQuote(const std::string& s) {
#if defined(_WIN32)
    return "\"" + s + "\"";
#else
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
#endif
}

bool executable(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xff); }
void set64(std::string& s, size_t at, uint64_t v) { for (int i = 0; i < 8; ++i) s[at + size_t(i)] = char((v >> (8 * i)) & 0xff); }

// Uncompressed 32-bit TGA, top-left origin: read by astcenc without extra libraries.
std::string tga(const unsigned char* rgba, int w, int h, bool alpha) {
    std::string s(18, '\0');
    s[2] = 2;
    s[12] = char(w & 0xff); s[13] = char(w >> 8); s[14] = char(h & 0xff); s[15] = char(h >> 8);
    s[16] = 32; s[17] = 0x28;
    s.reserve(18 + size_t(w) * size_t(h) * 4);
    for (size_t i = 0, n = size_t(w) * size_t(h); i < n; ++i) {
        const unsigned char* p = rgba + i * 4;
        s += char(p[2]); s += char(p[1]); s += char(p[0]); s += char(alpha ? p[3] : 255);
    }
    return s;
}

// Next mip: 2x2 box average (odd edges clamp), alpha-weighted colour so cut-outs keep their edge colour.
std::vector<unsigned char> half(const std::vector<unsigned char>& src, int w, int h, int& nw, int& nh) {
    nw = std::max(1, w / 2); nh = std::max(1, h / 2);
    std::vector<unsigned char> dst(size_t(nw) * size_t(nh) * 4);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            double c[3] = {0, 0, 0}, a = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                    const unsigned char* p = &src[(size_t(sy) * size_t(w) + size_t(sx)) * 4];
                    const double al = (p[3] + 1) / 256.0;
                    for (int k = 0; k < 3; ++k) c[k] += p[k] * al;
                    a += al;
                }
            unsigned char* d = &dst[(size_t(y) * size_t(nw) + size_t(x)) * 4];
            for (int k = 0; k < 3; ++k) d[k] = (unsigned char)std::clamp<long>(std::lround(c[k] / a), 0, 255);
            long sum = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) sum += src[(size_t(std::min(h - 1, y * 2 + dy)) * size_t(w) + size_t(std::min(w - 1, x * 2 + dx))) * 4 + 3];
            d[3] = (unsigned char)((sum + 2) / 4);
        }
    return dst;
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}
} // namespace

std::string findAstcenc() {
    if (const char* e = std::getenv("AIVERSE_ASTCENC"); e && *e && executable(e)) return e;
    static const char* names[] = {"astcenc", "astcenc-avx2", "astcenc-sse4.1", "astcenc-sse2", "astcenc-neon"};
    std::vector<fs::path> dirs;
    if (const char* home = std::getenv(
#if defined(_WIN32)
            "USERPROFILE"
#else
            "HOME"
#endif
            ); home && *home) {
        dirs.push_back(fs::path(home) / ".aiverse" / "toolchains" / "astcenc" / "bin");
        dirs.push_back(fs::path(home) / ".aiverse" / "toolchains" / "astcenc");
    }
    if (const char* path = std::getenv("PATH")) {
        std::string p = path;
#if defined(_WIN32)
        const char sep = ';';
#else
        const char sep = ':';
#endif
        size_t a = 0;
        while (a <= p.size()) {
            size_t b = p.find(sep, a);
            if (b == std::string::npos) b = p.size();
            if (b > a) dirs.emplace_back(p.substr(a, b - a));
            a = b + 1;
        }
    }
    for (const auto& d : dirs)
        for (const char* n : names) {
            fs::path c = d / n;
#if defined(_WIN32)
            c += ".exe";
#endif
            if (executable(c)) return c.string();
        }
    return "";
}

bool readAstcFile(const std::string& bytes, int& bx, int& by, int& w, int& h, std::string& blocks) {
    if (bytes.size() < 16) return false;
    const auto* d = reinterpret_cast<const unsigned char*>(bytes.data());
    if (!(d[0] == 0x13 && d[1] == 0xAB && d[2] == 0xA1 && d[3] == 0x5C)) return false;
    bx = d[4]; by = d[5];
    w = int(d[7] | (d[8] << 8) | (d[9] << 16));
    h = int(d[10] | (d[11] << 8) | (d[12] << 16));
    if (bx <= 0 || by <= 0 || w <= 0 || h <= 0 || d[6] != 1) return false;
    const size_t need = size_t((w + bx - 1) / bx) * size_t((h + by - 1) / by) * 16;
    if (bytes.size() < 16 + need) return false;
    blocks.assign(bytes, 16, need);
    return true;
}

std::string makeAstcKtx2(int block, bool srgb, int width, int height, const std::vector<std::string>& levels) {
    static const unsigned char id[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
    const uint32_t vk = block == 6 ? (srgb ? 166u : 165u) : (srgb ? 158u : 157u);
    std::string s(reinterpret_cast<const char*>(id), 12);
    put32(s, vk); put32(s, 1);                               // vkFormat, typeSize
    put32(s, uint32_t(width)); put32(s, uint32_t(height)); put32(s, 0); put32(s, 0); put32(s, 1);
    put32(s, uint32_t(levels.size())); put32(s, 0);          // levelCount, no supercompression
    const size_t index = s.size();                            // dfd/kvd/sgd offsets, filled below
    s.append(4 * 4 + 8 * 2, '\0');
    const size_t levelIndex = s.size();
    s.append(levels.size() * 24, '\0');
    // Data format descriptor (KDF 1.3): one basic block, colour model ASTC, one 128-bit sample.
    const uint32_t dfdOffset = uint32_t(s.size());
    std::string dfd;
    put32(dfd, 44);
    put32(dfd, 0);                                           // vendor 0 (Khronos), descriptor type 0 (basic)
    dfd += char(2); dfd += char(0); dfd += char(40); dfd += char(0);   // version 2, block size 24 + 16
    dfd += char(162);                                        // KHR_DF_MODEL_ASTC
    dfd += char(1);                                          // primaries BT.709
    dfd += char(srgb ? 2 : 1);                               // transfer sRGB / linear
    dfd += char(0);                                          // straight alpha
    dfd += char(block - 1); dfd += char(block - 1); dfd += char(0); dfd += char(0);
    dfd += char(16); dfd.append(7, '\0');                    // bytesPlane
    dfd += char(0); dfd += char(0); dfd += char(127); dfd += char(0);   // bitOffset 0, bitLength 128 - 1, channel ASTC data
    dfd.append(4, '\0');                                     // sample position
    put32(dfd, 0); put32(dfd, 0xFFFFFFFFu);
    s += dfd;
    // levels are stored smallest first, each aligned to 16 bytes (lcm of the block size and 4)
    std::vector<uint64_t> offsets(levels.size());
    for (size_t i = levels.size(); i-- > 0;) {
        while (s.size() % 16) s += '\0';
        offsets[i] = s.size();
        s += levels[i];
    }
    for (size_t i = 0; i < levels.size(); ++i) {
        set64(s, levelIndex + i * 24, offsets[i]);
        set64(s, levelIndex + i * 24 + 8, levels[i].size());
        set64(s, levelIndex + i * 24 + 16, levels[i].size());
    }
    for (int i = 0; i < 4; ++i) s[index + size_t(i)] = char((dfdOffset >> (8 * i)) & 0xff);
    for (int i = 0; i < 4; ++i) s[index + 4 + size_t(i)] = char((44u >> (8 * i)) & 0xff);
    return s;
}

bool encodeAstcKtx2(const unsigned char* rgba, int width, int height, const AstcOptions& opt, const std::string& encoder,
                    const std::string& tmpDir, std::string& ktx2, std::string& err) {
    if (encoder.empty()) { err = "astcenc not found (AIVERSE_ASTCENC, ~/.aiverse/toolchains/astcenc or PATH)"; return false; }
    if (opt.block != 4 && opt.block != 6) { err = "ASTC block must be 4x4 or 6x6"; return false; }
    if (width <= 0 || height <= 0) { err = "empty image"; return false; }
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    std::vector<unsigned char> level(rgba, rgba + size_t(width) * size_t(height) * 4);
    int w = width, h = height;
    std::vector<std::string> levels;
    const std::string blockArg = opt.block == 6 ? "6x6" : "4x4";
    const std::string preset = opt.quality == "fastest" || opt.quality == "fast" || opt.quality == "thorough" ? opt.quality : "medium";
    for (int l = 0;; ++l) {
        const fs::path in = fs::path(tmpDir) / ("level" + std::to_string(l) + ".tga"), out = fs::path(tmpDir) / ("level" + std::to_string(l) + ".astc");
        {
            std::ofstream f(in, std::ios::binary);
            const std::string t = tga(level.data(), w, h, opt.alpha);
            f.write(t.data(), std::streamsize(t.size()));
            if (!f) { err = "cannot write " + in.string(); return false; }
        }
        std::string cmd = shellQuote(encoder) + (opt.srgb ? " -cs " : " -cl ") + shellQuote(in.string()) + " " + shellQuote(out.string()) + " " +
                          blockArg + " -" + preset + " -silent" + (opt.alpha ? "" : " -esw rgb1");
#if defined(_WIN32)
        cmd = "\"" + cmd + " >NUL 2>&1\"";
#else
        cmd += " >/dev/null 2>&1";
#endif
        int rc = std::system(cmd.c_str());
#if !defined(_WIN32)
        if (rc != -1 && WIFEXITED(rc)) rc = WEXITSTATUS(rc);
#endif
        std::string bytes, blocks;
        int bx = 0, by = 0, aw = 0, ah = 0;
        if (rc != 0 || !readFile(out, bytes) || !readAstcFile(bytes, bx, by, aw, ah, blocks) || bx != opt.block || by != opt.block || aw != w || ah != h) {
            err = "astcenc failed on mip " + std::to_string(l) + " (" + std::to_string(w) + "x" + std::to_string(h) + ", exit " + std::to_string(rc) + ")";
            fs::remove(in, ec); fs::remove(out, ec);
            return false;
        }
        fs::remove(in, ec); fs::remove(out, ec);
        levels.push_back(std::move(blocks));
        if (!opt.mipmaps || (w == 1 && h == 1)) break;
        int nw = 0, nh = 0;
        level = half(level, w, h, nw, nh);
        w = nw; h = nh;
    }
    ktx2 = makeAstcKtx2(opt.block, opt.srgb, width, height, levels);
    return true;
}

} // namespace aiverse::build

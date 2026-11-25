// Portable Radiance RGBE (.hdr) decoder and native HDR texture uploader.
// Header-only so native game projects can import HDR images without linking
// the editor or changing deterministic ECS state.
#pragma once
#include "engine/gfx/Gfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::render {

struct RadianceImage {
    int width = 0, height = 0;
    // Linear RGBA16F pixels, four IEEE-754 binary16 values per pixel.
    std::vector<uint16_t> rgba16f;
};

namespace hdr_detail {
// Positive, finite RGBE radiance -> IEEE binary16, saturating above 65504.
// No alpha channel in RGBE; the importer writes alpha=1 exactly (0x3c00).
inline uint16_t toHalf(float value) {
    if (!(value > 0.0f)) return 0;
    if (!std::isfinite(value) || value >= 65504.0f) return 0x7bffu;
    int exponent = 0;
    const float mantissa = std::frexp(value, &exponent); // 0.5 .. 1
    if (exponent < -13) {
        // Binary16 subnormal: units of 2^-24; ties rounded to nearest.
        const long rounded = std::lround(std::ldexp(double(value), 24));
        // 1024 is the lowest normal half (0x0400), not a subnormal
        // overflow. Clamping it to 1023 introduces a discontinuity.
        return uint16_t(std::clamp(rounded, 0L, 1024L));
    }
    int halfExponent = exponent + 14;
    long fraction = std::lround((double(mantissa) * 2.0 - 1.0) * 1024.0);
    if (fraction >= 1024) { fraction = 0; ++halfExponent; }
    if (halfExponent >= 31) return 0x7bffu;
    return uint16_t((halfExponent << 10) | int(fraction));
}

inline bool readLine(std::string_view bytes, size_t& at, std::string_view& line) {
    if (at >= bytes.size()) return false;
    const size_t start = at;
    size_t end = bytes.find('\n', start);
    if (end == std::string_view::npos) return false;
    at = end + 1;
    if (end > start && bytes[end - 1] == '\r') --end;
    line = bytes.substr(start, end - start);
    return true;
}
} // namespace hdr_detail

// Supports modern Radiance 32-bit_rle_rgbe, including new per-channel scanline
// RLE and simple interleaved RGBE scanlines. The old (1,1,1,N) repeat marker
// and XY-major rotations are explicitly unsupported rather than misdecoded.
// Resolution signs preserve -Y/+Y and +X/-X orientation. Radiance pixel values
// are interpreted as linear radiance; EXPOSURE/GAMMA header tags are not
// baked into texels (callers may apply scene exposure themselves).
inline bool parseRadianceHdr(std::string_view bytes, RadianceImage& out,
                            std::string& error, size_t maxDecodedBytes=size_t(512)<<20) {
    out = {};
    error.clear();
    auto fail = [&](const char* why) {
        out = {};
        error = why;
        return false;
    };
    size_t at = 0;
    std::string_view line;
    if (!hdr_detail::readLine(bytes, at, line) ||
        (line != "#?RADIANCE" && line != "#?RGBE"))
        return fail("HDR: expected Radiance RGBE signature");
    bool hasFormat = false, terminated = false;
    for (int n = 0; n < 128; ++n) {
        if (!hdr_detail::readLine(bytes, at, line))
            return fail("HDR: truncated metadata header");
        if (line.empty()) { terminated = true; break; }
        if (line == "FORMAT=32-bit_rle_rgbe") hasFormat = true;
        else if (line.substr(0, 7) == "FORMAT=")
            return fail("HDR: unsupported pixel encoding");
    }
    if (!terminated || !hasFormat)
        return fail("HDR: missing FORMAT=32-bit_rle_rgbe");
    if (!hdr_detail::readLine(bytes, at, line))
        return fail("HDR: missing resolution line");
    char axisY[3]{}, axisX[3]{}, extra = 0;
    int height = 0, width = 0;
    if (std::sscanf(std::string(line).c_str(), " %2s %d %2s %d %c",
                    axisY, &height, axisX, &width, &extra) != 4 ||
        (std::strcmp(axisY, "-Y") && std::strcmp(axisY, "+Y")) ||
        (std::strcmp(axisX, "+X") && std::strcmp(axisX, "-X")) ||
        width < 1 || height < 1 || width > 16384 || height > 16384 ||
        uint64_t(width) * uint64_t(height) * 8u > std::min<uint64_t>(uint64_t(512)<<20,maxDecodedBytes))
        return fail("HDR: unsupported orientation, extent or memory budget");

    const bool flipY = axisY[0] == '+', flipX = axisX[0] == '-';
    out.width = width;
    out.height = height;
    out.rgba16f.resize(size_t(width) * size_t(height) * 4u);
    std::vector<uint8_t> row(size_t(width) * 4u);
    std::array<std::vector<uint8_t>, 4> planes;
    for (auto& plane : planes) plane.resize(size_t(width));

    for (int y = 0; y < height; ++y) {
        // Radiance's modern RLE only applies to widths from 8 to 32767.
        const bool rle = width >= 8 && at + 4 <= bytes.size() &&
            uint8_t(bytes[at]) == 2 && uint8_t(bytes[at + 1]) == 2 &&
            !(uint8_t(bytes[at + 2]) & 128) &&
            (uint16_t(uint8_t(bytes[at + 2])) << 8 |
             uint16_t(uint8_t(bytes[at + 3]))) == uint16_t(width);
        if (rle) {
            at += 4;
            for (int channel = 0; channel < 4; ++channel) {
                int x = 0;
                while (x < width) {
                    if (at >= bytes.size()) return fail("HDR: truncated RLE packet");
                    const uint8_t code = uint8_t(bytes[at++]);
                    const int count = code > 128 ? int(code) - 128 : int(code);
                    if (count == 0 || count > width - x)
                        return fail("HDR: invalid RLE packet length");
                    if (code > 128) {
                        if (at >= bytes.size()) return fail("HDR: truncated RLE run");
                        const uint8_t value = uint8_t(bytes[at++]);
                        std::fill_n(planes[size_t(channel)].begin() + x, count, value);
                    } else {
                        if (size_t(count) > bytes.size() - at)
                            return fail("HDR: truncated RLE literal");
                        for (int j = 0; j < count; ++j)
                            planes[size_t(channel)][size_t(x + j)] = uint8_t(bytes[at++]);
                    }
                    x += count;
                }
            }
            for (int x = 0; x < width; ++x)
                for (int channel = 0; channel < 4; ++channel)
                    row[size_t(x) * 4u + size_t(channel)] =
                        planes[size_t(channel)][size_t(x)];
        } else {
            const size_t rowBytes = size_t(width) * 4u;
            if (rowBytes > bytes.size() - at)
                return fail("HDR: truncated flat RGBE row");
            for (size_t i = 0; i < rowBytes; ++i)
                row[i] = uint8_t(bytes[at + i]);
            at += rowBytes;
            // Old Radiance scanline repeat codes are intentionally rejected.
            for (int x = 0; x < width; ++x)
                if (row[size_t(x)*4] == 1 && row[size_t(x)*4 + 1] == 1 &&
                    row[size_t(x)*4 + 2] == 1)
                    return fail("HDR: legacy repeat encoding is unsupported");
        }

        const int dstY = flipY ? height - 1 - y : y;
        for (int x = 0; x < width; ++x) {
            const int dstX = flipX ? width - 1 - x : x;
            const size_t offset = size_t(dstY * width + dstX) * 4u;
            const uint8_t* p = row.data() + size_t(x) * 4u;
            const float scale = p[3] ? std::ldexp(1.0f, int(p[3]) - 136) : 0.0f;
            for (int channel = 0; channel < 3; ++channel)
                out.rgba16f[offset + size_t(channel)] =
                    hdr_detail::toHalf(float(p[channel]) * scale);
            out.rgba16f[offset + 3] = 0x3c00u;
        }
    }
    return true;
}

// CPU pixels are consumed synchronously by the RHI allocation/upload call.
// Caller owns the resulting TextureHandle. Unlike RGBA8, HDR radiance stays
// linear and is never sRGB-decoded or silently tonemapped.
inline gfx::TextureHandle uploadRadianceHdr(gfx::Device& device,
                                             const RadianceImage& source,
                                             std::string& error) {
    error.clear();
    if (source.width < 1 || source.height < 1 ||
        source.rgba16f.size() != size_t(source.width) * size_t(source.height) * 4u) {
        error = "HDR: invalid decoded pixel buffer";
        return {};
    }
    if (!device.supports(gfx::PixelFormat::RGBA16F)) {
        error = "HDR: GPU does not support RGBA16F sampling";
        return {};
    }
    gfx::TextureDesc desc;
    desc.width = source.width;
    desc.height = source.height;
    desc.format = gfx::PixelFormat::RGBA16F;
    desc.data = source.rgba16f.data();
    desc.filter = gfx::Filter::Linear;
    gfx::TextureHandle texture = device.createTexture(desc);
    if (!texture.valid()) {
        error = device.lastError();
        if (error.empty()) error = "HDR: RGBA16F GPU allocation failed";
    }
    return texture;
}

// Convenience path for user-provided .hdr bytes. Decode only once, then
// upload; the decoded CPU scratch is released on return. Neither stage
// submits asynchronous GPU jobs or retains a dangling source pointer.
inline gfx::TextureHandle loadRadianceHdr(gfx::Device& device,
                                          std::string_view encoded,
                                          std::string& error) {
    RadianceImage source;
    if (!parseRadianceHdr(encoded, source, error)) return {};
    return uploadRadianceHdr(device, source, error);
}

} // namespace aiverse::render

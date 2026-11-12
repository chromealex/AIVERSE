// Edge-aware a-trous wavelet filter for baked indirect light (agent-lightbake): 5x5 B3 kernel at
// growing steps (1, 2, 4), weights from normals, world positions, chart / region ids and luminance,
// so noise is smoothed inside a surface and contact shadows / creases survive.
#include "BakerInternal.h"

namespace aiverse::bake::detail {

void denoiseTexels(std::vector<float>& rgb, const std::vector<float3>& nrm, const std::vector<float3>& pos, const std::vector<int32_t>& id,
                   int w, int h, float texelWorld) {
    static const float kB3[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
    std::vector<float> tmp(rgb.size());
    for (int pass = 0, step = 1; pass < 3; ++pass, step *= 2) {
        const float sigmaP = texelWorld * float(step) * 2.5f;
        const float sigmaL = 4.0f / float(step);   // relative luminance tolerance shrinks with the step
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t c = size_t(y) * size_t(w) + size_t(x);
                if (id[c] < 0) { for (int k = 0; k < 3; ++k) tmp[c * 3 + size_t(k)] = rgb[c * 3 + size_t(k)]; continue; }
                const float lc = luma({rgb[c * 3], rgb[c * 3 + 1], rgb[c * 3 + 2]});
                float acc[3] = {}, wsum = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int xx = x + dx * step, yy = y + dy * step;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        const size_t q = size_t(yy) * size_t(w) + size_t(xx);
                        if (id[q] != id[c]) continue;
                        const float nd = std::max(0.0f, dot(nrm[c], nrm[q]));
                        float wn = nd * nd; wn *= wn; wn *= wn;   // ^8
                        const float3 dp = pos[q] - pos[c];
                        const float wp = std::exp(-dot(dp, dp) / (sigmaP * sigmaP));
                        const float lq = luma({rgb[q * 3], rgb[q * 3 + 1], rgb[q * 3 + 2]});
                        const float wl = std::exp(-std::fabs(lq - lc) / (sigmaL * std::max(lc, 1e-3f) + 1e-4f));
                        const float wt = kB3[dx + 2] * kB3[dy + 2] * wn * wp * wl;
                        for (int k = 0; k < 3; ++k) acc[k] += rgb[q * 3 + size_t(k)] * wt;
                        wsum += wt;
                    }
                for (int k = 0; k < 3; ++k) tmp[c * 3 + size_t(k)] = wsum > 0 ? acc[k] / wsum : rgb[c * 3 + size_t(k)];
            }
        rgb.swap(tmp);
    }
}

} // namespace aiverse::bake::detail

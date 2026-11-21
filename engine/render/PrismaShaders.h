// Shaders of the post-processing chain (Prisma.cpp). Each program is written once in a GLSL/MSL
// common subset and wrapped per backend by prisma::buildProgram():
//   * types vec2/vec3/vec4 (MSL: #define to float2..4), no ## pasting (old GLES drivers);
//   * uniforms: one block U0 of vec4 fields, used by name (GLSL block members; MSL locals copied from u);
//   * textures: TEX0(uv) .. TEX4(uv); the output: RETURN(color);
//   * helper functions start with PF_FN (MSL: static);
//   * `s` = screen uv of the pixel (0..1, y up); TUV(p) -> texture uv of a render target
//     (Metal stores targets top-down: px.z = 1 flips v); TDIR(d) flips a direction the same way.
// The vertex stage is a full-screen triangle (one Float2 attribute).
#pragma once

namespace aiverse::render::prisma {

// Bloom bright pass + 4-tap downsample. px.xy = source texel * tap spread; p0 = threshold, knee.
inline const char* kPrefilter = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = (TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb +
              TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb) * 0.25;
    float br = max(c.r, max(c.g, c.b));
    float k = max(p0.x * p0.y, 0.0001);
    float soft = clamp(br - p0.x + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k + 0.0001);
    float w = max(soft, br - p0.x) / max(br, 0.0001);
    RETURN(vec4(c * w, 1.0));
)";

// Dual-filter downsample (centre x4 + 4 diagonals). px.xy = source texel.
inline const char* kDown = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = TEX0(uv).rgb * 4.0 + TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb +
             TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb;
    RETURN(vec4(c * 0.125, 1.0));
)";

// Dual-filter upsample of the lower level (tex0) added to this level's downsample (tex1).
// px.xy = texel of tex0; p0.x = weight of the lower level (scatter).
inline const char* kUp = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = TEX0(uv + vec2(-2.0 * o.x, 0.0)).rgb + TEX0(uv + vec2(2.0 * o.x, 0.0)).rgb +
             TEX0(uv + vec2(0.0, -2.0 * o.y)).rgb + TEX0(uv + vec2(0.0, 2.0 * o.y)).rgb +
             (TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb +
              TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb) * 2.0;
    RETURN(vec4(c * (p0.x / 12.0) + TEX1(uv).rgb, 1.0));
)";

// Separable 9-tap gaussian through 5 bilinear taps. px.xy = source texel; p0.xy = direction * spread.
inline const char* kBlur = R"(
    vec2 uv = TUV(s);
    vec2 d = px.xy * p0.xy;
    vec3 c = TEX0(uv).rgb * 0.2270270 +
             (TEX0(uv + d * 1.3846154).rgb + TEX0(uv - d * 1.3846154).rgb) * 0.3162162 +
             (TEX0(uv + d * 3.2307692).rgb + TEX0(uv - d * 3.2307692).rgb) * 0.0702703;
    RETURN(vec4(c, 1.0));
)";

// Sun shafts mask: bright pixels near the sun. px.xy = source texel * spread; p0.x = threshold;
// sun.xy = sun position (texture uv), sun.w = aspect.
inline const char* kShaftsPre = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 c = (TEX0(uv + vec2(-o.x, -o.y)).rgb + TEX0(uv + vec2(o.x, -o.y)).rgb +
              TEX0(uv + vec2(-o.x, o.y)).rgb + TEX0(uv + vec2(o.x, o.y)).rgb) * 0.25;
    float l = max(c.r, max(c.g, c.b));
    float m = smoothstep(p0.x, p0.x + 0.15, l);
    vec2 d = uv - sun.xy;
    d.x *= sun.w;
    float fall = clamp(1.0 - length(d) * 0.45, 0.0, 1.0);
    RETURN(vec4(c * (m * fall), 1.0));
)";

// Radial blur towards the sun. p0 = samples, length (0..1), decay, weight; sun.xy = sun (texture uv).
inline const char* kShafts = R"(
    vec2 uv = TUV(s);
    vec2 delta = (uv - sun.xy) * (p0.y / p0.x);
    vec3 acc = vec3(0.0);
    float illum = 1.0;
    vec2 q = uv;
    for (int i = 0; i < 64; i++) {
        if (float(i) >= p0.x) break;
        q -= delta;
        acc += TEX0(q).rgb * illum;
        illum *= p0.z;
    }
    RETURN(vec4(acc * (p0.w / p0.x), 1.0));
)";


// Stage 56: opt-in camera-linear DoF and depth-aware silhouette rejection.
// TEX0 = HDR scene color, TEX1 = device depth; depth is sampled but NEVER
// attached as a writable render target. CPU validates projection metadata.
// p0 = (legacy focus depth, legacy focus range, max blur pixels, intensity).
// depthInfo = (near, far, orthographic[0/1], reserved).
// focusInfo = (view-space focus distance, metric sharp interval,
//              silhouette tolerance in world units, metric mode[0/1]).
// Legacy branch is retained exactly for old authored game/map JSON.
inline const char* kDepthOfFieldHelpers = R"(
PF_FN float prismaDofViewDepth(float d, vec4 info) {
    if (info.z > 0.5) return mix(info.x,info.y,d);
    float denominator = max(info.y-d*(info.y-info.x),0.0001);
    return info.x*info.y/denominator;
}
// Normalized circle of confusion (0..1) of a view-space distance.
PF_FN float prismaDofCoc(float dist, vec4 focus, float intensity) {
    float range = max(focus.y,0.01);
    return clamp((abs(dist-focus.x)-range*0.5)/range,0.0,1.0)*intensity;
}
// Share of a tap whose blur circle (cocPx) reaches a pixel offsetPx away.
PF_FN float prismaDofReach(float cocPx, float offsetPx) {
    return clamp(cocPx-offsetPx+1.0,0.0,1.0);
}
)";
inline const char* kDepthOfField = R"(
    vec2 uv = TUV(s);
    if (focusInfo.w < 0.5) {
        vec3 original = TEX0(uv).rgb;
        float depth = TEX1(uv).r;
        float coc = clamp((abs(depth - p0.x) - p0.y * 0.5) /
                          max(p0.y, 0.0001), 0.0, 1.0) * p0.w;
        vec2 radius = px.xy * p0.z * coc;
        vec3 sum = original * 2.0;
        sum += TEX0(uv + vec2( radius.x, 0.0)).rgb;
        sum += TEX0(uv + vec2(-radius.x, 0.0)).rgb;
        sum += TEX0(uv + vec2(0.0,  radius.y)).rgb;
        sum += TEX0(uv + vec2(0.0, -radius.y)).rgb;
        sum += TEX0(uv + radius * vec2( 0.7071,  0.7071)).rgb;
        sum += TEX0(uv + radius * vec2(-0.7071,  0.7071)).rgb;
        sum += TEX0(uv + radius * vec2( 0.7071, -0.7071)).rgb;
        sum += TEX0(uv + radius * vec2(-0.7071, -0.7071)).rgb;
        RETURN(vec4(sum * 0.1, 1.0));
    }

    // Metric mode: the same focus plane works in perspective and ortho.
    // Scatter-as-gather over the full blur radius: a tap contributes when
    // its own blur circle reaches this pixel. Taps nearer than this pixel
    // use their own circle, so an out-of-focus foreground spreads over the
    // background and over the sharp zone (no hard cutout on its outline).
    // Taps at the same depth or behind are limited by the smaller of the two
    // circles, so the background never bleeds onto a sharper foreground; in
    // the near field the background shows through the blurred edge.
    // Depth 1 is background/sky (non-reversed RHI depth): treated as far.
    vec4 center = TEX0(uv);
    float maxPx = p0.z*p0.w;
    if (maxPx <= 0.00001) RETURN(center);
    float rawDepth = TEX1(uv).r;
    float centerDistance = rawDepth >= 0.99999 ? depthInfo.y
                         : prismaDofViewDepth(rawDepth,depthInfo);
    float centerPx = prismaDofCoc(centerDistance,focusInfo,p0.w)*p0.z;
    float tolerance = max(focusInfo.z,0.001);
    // A pixel in front of the sharp zone is a blurred foreground edge: the
    // background shows through it, so taps behind use this pixel's circle.
    float nearField = step(centerDistance,
                           focusInfo.x-max(focusInfo.y,0.01)*0.5);
    vec3 sum = center.rgb;
    float weight = 1.0;
    for (int i=0; i<24; ++i) {
        // Three rings of 8 taps, each ring turned by 15 degrees.
        float ring = float(i/8)+1.0;
        float angle = float(i-(i/8)*8)*0.785398+ring*0.261799;
        float offsetPx = maxPx*ring/3.0;
        vec2 sampleUv = TUV(clamp(s + px.xy*offsetPx*vec2(cos(angle),sin(angle)),
                                  vec2(0.0),vec2(1.0)));
        float tapDepth = TEX1(sampleUv).r;
        float tapDistance = tapDepth >= 0.99999 ? depthInfo.y
                          : prismaDofViewDepth(tapDepth,depthInfo);
        float tapPx = prismaDofCoc(tapDistance,focusInfo,p0.w)*p0.z;
        float nearer = 1.0-smoothstep(0.0,tolerance,centerDistance-tapDistance);
        // nearer: 0 when the tap is clearly in front of this pixel.
        float w = mix(prismaDofReach(tapPx,offsetPx),
                      prismaDofReach(mix(min(tapPx,centerPx),centerPx,nearField),
                                     offsetPx),nearer);
        if (w <= 0.00001) continue;
        sum += TEX0(sampleUv).rgb*w;
        weight += w;
    }
    // Preserve source alpha: the metric pass modifies HDR RGB only.
    RETURN(vec4(sum/max(weight,0.0001),center.a));
)";


// Stage 23: HDR log-luminance pyramid. All pyramid targets use RGBA16F.
// Values are log2(luma) stored in R (signed HDR float, never RGBA8).
// First pass stores weighted log2 luminance in R and total weight in G.
// p0.x = centerWeight (0 uniform, 1 strongly center-biased)
// p0.y = centerRadius (relative normalized image coordinates)
// p0.z = opt-in bright-outlier rejection (0 = legacy highlight weights)
// p0.w = weight OUTSIDE optional region, clamped to >= .02
// region = (screen center X, screen center Y, circle radius, feather);
//          negative radius disables region and reproduces legacy meter.
// regionBox = (half width fraction of frame width, half height fraction
//              of frame height, use box shape, reserved).
// sun.xy = min/max metering log2 luminance clamps
// sun.zw = bright-outlier start log2 L and smooth transition width in stops.
// Later reduction preserves numerator/denominator rather than averaging
// already-normalized values, so center weighting remains correct.
inline const char* kExposureLog = R"(
    vec2 uv = TUV(s);
    vec2 t = px.xy * 0.5;
    vec2 q0 = clamp(uv + vec2(-t.x,-t.y), vec2(0.0), vec2(1.0));
    vec2 q1 = clamp(uv + vec2( t.x,-t.y), vec2(0.0), vec2(1.0));
    vec2 q2 = clamp(uv + vec2(-t.x, t.y), vec2(0.0), vec2(1.0));
    vec2 q3 = clamp(uv + vec2( t.x, t.y), vec2(0.0), vec2(1.0));
    vec3 a = max(TEX0(q0).rgb, vec3(0.0));
    vec3 b = max(TEX0(q1).rgb, vec3(0.0));
    vec3 c = max(TEX0(q2).rgb, vec3(0.0));
    vec3 d = max(TEX0(q3).rgb, vec3(0.0));
    vec4 luminance = vec4(dot(a,vec3(0.2126,0.7152,0.0722)),
                          dot(b,vec3(0.2126,0.7152,0.0722)),
                          dot(c,vec3(0.2126,0.7152,0.0722)),
                          dot(d,vec3(0.2126,0.7152,0.0722)));
    vec4 logL = clamp(log2(max(luminance,vec4(0.0001))),
                      vec4(sun.x),vec4(max(sun.x,sun.y)));
    float radius = max(p0.y,0.05);
    vec2 center = vec2(0.5,0.5);
    vec2 d0 = (q0-center)/radius, d1 = (q1-center)/radius;
    vec2 d2 = (q2-center)/radius, d3 = (q3-center)/radius;
    vec4 weights = mix(vec4(1.0),
                       vec4(exp(-2.0*dot(d0,d0)),exp(-2.0*dot(d1,d1)),
                            exp(-2.0*dot(d2,d2)),exp(-2.0*dot(d3,d3))),
                       clamp(p0.x,0.0,1.0));
    // Stage 57 optional robust metering: reduce the influence of tiny,
    // intensely emissive regions on the average log luminance. Every
    // sample retains >=5% weight to avoid an empty meter in all-bright
    // scenes (and to prevent sudden overexposure).
    float reject = clamp(p0.z,0.0,0.95);
    if (reject > 0.0) {
        vec4 t = smoothstep(vec4(sun.z),
                            vec4(sun.z + max(sun.w,0.1)),logL);
        weights *= vec4(1.0) - t*reject;
    }
    // Stage 58: a camera-authored region influences metering only, not
    // final color. Sample positions are converted from backend texture UV
    // back into normalized bottom-up screen coordinates for consistency.
    // A nonzero outside weight prevents an empty metering denominator.
    if (region.z > 0.0) {
        float aspect = px.y / max(px.x,0.000001);
        vec2 ar = vec2(aspect,1.0);
        vec2 rc = region.xy;
        vec4 inside;
        if (regionBox.z > 0.5) {
            // Signed distance from a rectangle in normalized frame-height
            // units; half width is authored relative to viewport WIDTH.
            vec2 halfSize = max(regionBox.xy,vec2(0.01));
            vec2 b0 = abs(TUV(q0)-rc)-halfSize;
            vec2 b1 = abs(TUV(q1)-rc)-halfSize;
            vec2 b2 = abs(TUV(q2)-rc)-halfSize;
            vec2 b3 = abs(TUV(q3)-rc)-halfSize;
            vec4 signedDistance = vec4(max(b0.x*aspect,b0.y),
                                       max(b1.x*aspect,b1.y),
                                       max(b2.x*aspect,b2.y),
                                       max(b3.x*aspect,b3.y));
            inside = vec4(1.0) -
                smoothstep(vec4(0.0),
                           vec4(max(region.w,0.001)),signedDistance);
        } else {
            vec4 distance = vec4(length((TUV(q0)-rc)*ar),
                                 length((TUV(q1)-rc)*ar),
                                 length((TUV(q2)-rc)*ar),
                                 length((TUV(q3)-rc)*ar));
            inside = vec4(1.0) -
                smoothstep(vec4(region.z),
                           vec4(region.z+max(region.w,0.001)),distance);
        }
        weights *= mix(vec4(clamp(p0.w,0.02,1.0)),
                       vec4(1.0),inside);
    }
    RETURN(vec4(dot(logL,weights)*0.25,
                (weights.x+weights.y+weights.z+weights.w)*0.25,0.0,1.0));
)";

inline const char* kExposureReduce = R"(
    vec2 uv = TUV(s);
    vec2 t = px.xy * 0.5;
    vec2 a = TEX0(clamp(uv + vec2(-t.x,-t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 b = TEX0(clamp(uv + vec2( t.x,-t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 c = TEX0(clamp(uv + vec2(-t.x, t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 d = TEX0(clamp(uv + vec2( t.x, t.y),vec2(0.0),vec2(1.0))).rg;
    vec2 mean = (a+b+c+d)*0.25;
    RETURN(vec4(mean.x,mean.y,0.0,1.0));
)";

// TEX0: 1x1 mean log2(luminance); TEX1: previous 1x1 exposure factor.
// p0=(deltaSeconds, speedUp, speedDown, historyValid).
// sun=(keyValue, minEV, maxEV, compensationEV).
inline const char* kExposureAdapt = R"(
    vec2 weighted = TEX0(vec2(0.5)).rg;
    float logL = weighted.x / max(weighted.y,0.0000001);
    float ev = clamp(log2(max(sun.x,0.0001))-logL+sun.w,sun.y,sun.z);
    float target = exp2(ev);
    float previous = max(TEX1(vec2(0.5)).r,0.0001);
    float rate = target > previous ? p0.y : p0.z;
    float alpha = 1.0 - exp(-max(p0.x,0.0)*max(rate,0.0));
    float result = p0.w > 0.5 ? mix(previous,target,alpha) : target;
    RETURN(vec4(result,0.0,0.0,1.0));
)";


// Camera velocity in normalized screen UV per frame. The inverse current
// viewProjection transforms hardware depth back to world; previous VP then
// projects it to the previous screen. The B channel of the optional TEX2
// object-velocity texture is its presence mask; RG contains current-minus-
// previous screen UV displacement. This lets independent renderers supply
// true object motion without changing deterministic gameplay ECS.
// Matrix uniforms are column-major mat4 columns as 8 vec4 fields.
// px.xy = 1/viewport; px.z = renderTargetFlipY
// p0.x = hardware clipDepthZeroToOne, p0.y = object buffer enabled.
inline const char* kMotionVectors = R"(
    vec2 uv = TUV(s);
    float depth = TEX1(uv).r;
    vec2 velocity = vec2(0.0);
    if (depth < 0.999999) {
        vec4 clip = vec4(s * 2.0 - vec2(1.0),
                         p0.x > 0.5 ? depth : depth * 2.0 - 1.0, 1.0);
        vec4 world = inv0 * clip.x + inv1 * clip.y +
                     inv2 * clip.z + inv3 * clip.w;
        world /= max(abs(world.w),0.000001) * (world.w < 0.0 ? -1.0 : 1.0);
        vec4 previous = prev0 * world.x + prev1 * world.y +
                        prev2 * world.z + prev3 * world.w;
        if (previous.w > 0.00001) {
            vec2 prevUv = previous.xy / previous.w * 0.5 + 0.5;
            velocity = clamp(s - prevUv,vec2(-1.0),vec2(1.0));
        }
    }
    if (p0.y > 0.5) {
        vec4 objectMotion = TEX2(uv);
        if (objectMotion.b > 0.5) velocity = objectMotion.rg;
    }
    RETURN(vec4(velocity, 0.0, 1.0));
)";

// Returns device-depth by default for compatibility. In linear mode it
// reconstructs positive perspective view depth in metres, or orthographic
// camera depth in its projection's world units. The engine uses the standard
// (non-reversed) 0..1 window depth on both GL and Metal.
inline const char* kMotionBlurHelpers = R"(
PF_FN float prismaDepthMetric(float d, vec4 info) {
    if (info.w < 0.5) return d;
    float nearPlane = info.x, farPlane = info.y;
    if (info.z > 0.5)
        return mix(nearPlane,farPlane,d);
    float denominator = max(farPlane - d * (farPlane-nearPlane),0.0001);
    return nearPlane * farPlane / denominator;
}
)";

// Full-resolution temporal shutter blur; velocity is from the corresponding
// previous frame and is expressed as current-minus-previous normalized UV.
// tapInfo.x: opt-in reactive layer mismatch rejection [0,1]; disabled unless
// a correctly sized/typed current-frame reactive mask was supplied.
// Uses current frame samples only (not temporal color accumulation), so no
// ghost-producing history reads and no persistent color state.
inline const char* kMotionBlur = R"(
    vec2 uv = TUV(s);
    vec4 centerColor = TEX0(uv);
    float centerDepth = prismaDepthMetric(TEX2(uv).r,depthInfo);
    float centerReactive = tapInfo.x > 0.001 ? clamp(TEX3(uv).r,0.0,1.0) : 0.0;
    float depthLimit = depthInfo.w > 0.5 ? max(sun.w,0.01) :
                                           max(sun.x,0.0005);
    vec2 velocity = TEX1(uv).rg;
    // Depth-matched velocity dilation: preserve foreground motion along
    // silhouettes without importing unrelated background velocity.
    if (sun.z > 0.001) {
        vec2 delta = px.xy * sun.z;
        for (int i=0; i<4; ++i) {
            vec2 axis = i==0 ? vec2(delta.x,0.0) :
                        i==1 ? vec2(-delta.x,0.0) :
                        i==2 ? vec2(0.0,delta.y) : vec2(0.0,-delta.y);
            vec2 neighbor = TUV(clamp(s+axis,vec2(0.0),vec2(1.0)));
            float depthDifference = abs(prismaDepthMetric(TEX2(neighbor).r,depthInfo)-centerDepth);
            vec2 candidate = TEX1(neighbor).rg;
            if (depthDifference < depthLimit*0.25 &&
                dot(candidate,candidate)>dot(velocity,velocity))
                velocity=candidate;
        }
    }
    velocity *= p0.x;
    vec2 pixels = velocity / max(px.xy,vec2(0.000001));
    float distancePx = length(pixels);
    float scale = min(1.0, p0.y / max(distancePx,0.0001));
    velocity *= scale;
    int n = int(clamp(floor(p0.z + 0.5),2.0,16.0));
    // Scene depth is supplied as TEX2. Reject far-away samples where a
    // near silhouette would otherwise smear their color over foreground.
    // Optional linear-depth mode compares metric view distance instead of
    // nonlinear hardware depth. The default legacy threshold is unchanged.
    vec3 accum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < 16; ++i) {
        if (i >= n) break;
        float t = float(i) / float(n-1) - 0.5;
        vec2 q = clamp(s + velocity * t,vec2(0.0),vec2(1.0));
        vec2 sampleUv = TUV(q);
        float sampleDepth = TEX2(sampleUv).r;
        float depthDelta = abs(prismaDepthMetric(sampleDepth,depthInfo) - centerDepth);
        float threshold = depthLimit;
        float keep = 1.0 - smoothstep(threshold * 0.125,
                                      threshold, depthDelta);
        // Do not drag translucent/particle colors onto opaque neighbors
        // (or vice versa) when their current-frame coverage differs.
        // The mask is read only when its declared input was accepted.
        if (tapInfo.x > 0.001) {
            float tapReactive = clamp(TEX3(sampleUv).r,0.0,1.0);
            float mismatch = abs(tapReactive-centerReactive);
            keep *= 1.0 - clamp(tapInfo.x,0.0,1.0) *
                    smoothstep(0.1,0.8,mismatch);
        }
        accum += TEX0(sampleUv).rgb * keep;
        weight += keep;
    }
    vec3 sharp = centerColor.rgb;
    vec3 blurred = weight > 0.0001 ? accum / weight : sharp;
    // tex3 is the reactive coverage mask: R=0 unchanged, R=1 fully
    // suppresses blur for the pixel when the configured strength is 1.
    // This mask is current-frame only and never participates in history.
    float reactive = clamp(TEX3(uv).r, 0.0, 1.0);
    float strength = p0.w * (1.0 - reactive * clamp(sun.y, 0.0, 1.0));
    // Keep the incoming scene alpha intact; only HDR radiance is blurred.
    // The canonical temporal resolve will separately write hardware depth
    // to its own history alpha, so this does not change that contract.
    RETURN(vec4(mix(sharp,blurred,strength),centerColor.a));
)";

// Stages 42–43. HDR temporal color resolve with opt-in subpixel jitter.
// TEX0 = current linear HDR, TEX1 = previous per-camera HDR RGB + previous
// hardware depth alpha, TEX2 = normalized current-minus-previous velocity,
// TEX3 = current hardware depth, TEX4 = current-frame reactive mask.
// px.xy = reciprocal viewport size, px.z = render-target Y flip.
// p0.x = clip depth 0..1, p0.y = valid history, p0.z = max history weight,
// p0.w = reactive rejection. sun.x = depth threshold, sun.y = motion attenuation,
// sun.z = valid reactive mask, sun.w = confidence rejection (0..1).
// jitter.xy = current projection jitter UV, jitter.zw = prior projection
// jitter UV; inputPx.xy=1/internal render width/height, inputPx.z=sharpness. Jitter is intentionally removed from previous history lookup:
// input pixels are sampled in jittered projection space, while history is
// stored on the stable output pixel grid. History is NEVER sampled as RT.
inline const char* kTemporalResolveHelpers = R"(
PF_FN vec3 prismaHistoryCompress(vec3 c) {
    c=max(c,vec3(0.0));
    return c/(1.0+dot(c,vec3(0.2126,0.7152,0.0722)));
}
PF_FN vec3 prismaHistoryExpand(vec3 c) {
    return c/max(1.0-dot(c,vec3(0.2126,0.7152,0.0722)),0.0001);
}
)";
inline const char* kTemporalResolve = R"(
    vec2 sampleS=clamp(s+jitter.xy,px.xy*0.5,vec2(1.0)-px.xy*0.5);
    vec2 uv=TUV(sampleS);
    vec3 current=prismaHistoryCompress(TEX0(uv).rgb);
    float depth=TEX3(uv).r;
    vec3 lo=current,hi=current;
    vec3 neighborhoodSum=vec3(0.0);
    float meanLuma=0.0, meanLumaSq=0.0;
    float nearDepth=depth,farDepth=depth;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        // Neighborhood classification is at NATIVE scene texel scale;
        // history taps below are at OUTPUT pixel scale.
        vec2 q=TUV(clamp(sampleS+vec2(float(x)*inputPx.x,float(y)*inputPx.y),
                         inputPx.xy*0.5,vec2(1.0)-inputPx.xy*0.5));
        vec3 c=prismaHistoryCompress(TEX0(q).rgb);
        lo=min(lo,c);hi=max(hi,c);
        neighborhoodSum+=c;
        float luma=dot(c,vec3(0.2126,0.7152,0.0722));
        meanLuma+=luma;
        meanLumaSq+=luma*luma;
        float d=TEX3(q).r;
        nearDepth=min(nearDepth,d);farDepth=max(farDepth,d);
    }
    meanLuma*=1.0/9.0;meanLumaSq*=1.0/9.0;
    float localSigma=sqrt(max(meanLumaSq-meanLuma*meanLuma,0.0));
    // Mild bounded spatial reconstruction of a bilinearly upsampled scene.
    // Work in compressed HDR to avoid hot highlight ringing; the temporal
    // history provides subpixel detail over successive jitter phases.
    vec3 neighborhoodMean=neighborhoodSum*(1.0/9.0);
    current=clamp(current+(current-neighborhoodMean)*inputPx.z,lo,hi);
    vec3 resolved=current;
    if(p0.y>0.5 && depth<0.99999) {
        vec2 velocity=TEX2(uv).rg;
        // Motion vectors are defined in jittered input projection space;
        // subtract the previous frame jitter to address stable history UVs.
        vec2 previousUv=sampleS-velocity-jitter.zw;
        if(previousUv.x>px.x*0.5 && previousUv.y>px.y*0.5 &&
           previousUv.x<1.0-px.x*0.5 && previousUv.y<1.0-px.y*0.5) {
            vec4 clip=vec4(sampleS*2.0-vec2(1.0),
                           p0.x>0.5?depth:depth*2.0-1.0,1.0);
            vec4 world=inv0*clip.x+inv1*clip.y+inv2*clip.z+inv3*clip.w;
            world=world/max(abs(world.w),0.000001)*(world.w<0.0?-1.0:1.0);
            vec4 prev=prev0*world.x+prev1*world.y+prev2*world.z+prev3*world.w;
            if(prev.w>0.00001) {
                float expected=p0.x>0.5?prev.z/prev.w:prev.z/prev.w*0.5+0.5;
                vec2 pixel=previousUv/px.xy-vec2(0.5);
                vec2 base=floor(pixel),fraction=fract(pixel);
                vec3 oldSum=vec3(0.0);float weightSum=0.0;
                // Nearest samples: validate EACH historical depth before
                // bilinear reconstruction, never interpolate depth across edges.
                for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
                    vec2 q=(base+vec2(float(x),float(y))+vec2(0.5))*px.xy;
                    if(min(q.x,q.y)<0.0||max(q.x,q.y)>1.0)continue;
                    vec4 previous=TEX1(TUV(q));
                    float err=abs(previous.a-expected);
                    float keep=1.0-smoothstep(sun.x*0.5,sun.x,err);
                    float wx=x==0?1.0-fraction.x:fraction.x;
                    float wy=y==0?1.0-fraction.y:fraction.y;
                    float w=keep*wx*wy;
                    oldSum+=prismaHistoryCompress(previous.rgb)*w;
                    weightSum+=w;
                }
                if(weightSum>0.01) {
                    vec3 old=clamp(oldSum/weightSum,lo,hi);
                    float currentLuma=dot(current,vec3(0.2126,0.7152,0.0722));
                    float oldLuma=dot(old,vec3(0.2126,0.7152,0.0722));
                    float relativeDelta=abs(oldLuma-currentLuma)/max(localSigma,0.015);
                    float colorConfidence=1.0-sun.w*smoothstep(0.75,3.0,relativeDelta);
                    float depthEdge=farDepth-nearDepth;
                    float edgeConfidence=1.0-sun.w*0.8*
                        smoothstep(sun.x*0.5,sun.x*4.0,depthEdge);
                    // Filter out jitter-only movement when damping history.
                    vec2 physicalVelocity=velocity-(jitter.xy-jitter.zw);
                    float motionPixels=length(physicalVelocity/max(px.xy,vec2(0.000001)));
                    float reactive=sun.z>0.5?clamp(TEX4(uv).r,0.0,1.0):0.0;
                    float historyWeight=p0.z/(1.0+sun.y*motionPixels);
                    historyWeight*=1.0-reactive*p0.w;
                    historyWeight*=clamp(weightSum,0.0,1.0)*colorConfidence*edgeConfidence;
                    resolved=mix(current,old,clamp(historyWeight,0.0,0.98));
                }
            }
        }
    }
    RETURN(vec4(prismaHistoryExpand(resolved),depth));
)";

// FXAA (console-style, 5 taps + early out, 9 at most). px.xy = texel; p0.x = subpixel softening.
inline const char* kFxaaHelpers = R"(
PF_FN float pfLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
)";
inline const char* kFxaa = R"(
    vec2 uv = TUV(s);
    vec2 o = px.xy;
    vec3 cM = TEX0(uv).rgb;
    float lNW = pfLuma(TEX0(uv + vec2(-o.x, -o.y)).rgb), lNE = pfLuma(TEX0(uv + vec2(o.x, -o.y)).rgb);
    float lSW = pfLuma(TEX0(uv + vec2(-o.x, o.y)).rgb), lSE = pfLuma(TEX0(uv + vec2(o.x, o.y)).rgb);
    float lM = pfLuma(cM);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) RETURN(vec4(cM, 1.0));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * o;
    vec3 a = 0.5 * (TEX0(uv + dir * (1.0 / 3.0 - 0.5)).rgb + TEX0(uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (TEX0(uv - dir * 0.5).rgb + TEX0(uv + dir * 0.5).rgb);
    float lB = pfLuma(b);
    vec3 r = (lB < lMin || lB > lMax) ? a : b;
    RETURN(vec4(mix(r, cM, (1.0 - p0.x) * 0.5), 1.0));
)";

// Final composite. PF_* defines pick the effects (one program per combination, built on demand).
//   px      texel x, y, flip, grain seed     grade0  -, contrast, saturation, posterize levels
//   grade1  colour multiplier rgb, pixel size lift    lift rgb, vignette intensity
//   gamma   1/gamma rgb, vignette softness   gain    gain rgb, grain intensity
//   filt    -, -, -, grain size              vig     vignette colour rgb, aberration
//   bloom   bloom tint rgb, sharpen amount   shafts  shafts colour rgb, LUT intensity
//   tilt    focus, width, falloff, -          fade    colour rgb, amount
//   lut     size N, flipY, -, -               lbox    colour rgb, bars height
// Textures: 0 scene, 1 bloom, 2 tilt-shift blur, 3 sun shafts, 4 LUT.
inline const char* kCompositeHelpers = R"(
PF_FN float pfHash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
PF_FN float pfLensHash(vec2 p) {
    vec3 q=fract(vec3(p.x,p.y,p.x)*0.1031);
    q+=dot(q,q.yzx+33.33);
    return fract((q.x+q.y)*q.z);
}
// Waterline (lensWater "waterline"): world point on the view ray through
// screen position sp at clip depth z, from four inverse view-projection columns.
PF_FN vec3 pfUnprojectWater(vec4 c0, vec4 c1, vec4 c2, vec4 c3, vec2 sp, float z) {
    vec4 h = c0 * (sp.x * 2.0 - 1.0) + c1 * (sp.y * 2.0 - 1.0) + c2 * z + c3;
    return h.xyz / max(abs(h.w), 0.000001) * sign(h.w + 0.0000001);
}
// Three travelling waves; w = (surfaceOffset, height, length, speed).
PF_FN float pfWaterWave(vec2 p, float t, vec4 w) {
    float k = 6.2831853 / max(w.z, 0.05);
    float c = t * w.w * k;
    return w.y * (0.6 * sin(p.x * k + c) + 0.4 * sin(p.y * k * 0.71 - c * 1.3) +
                  0.25 * sin((p.x + p.y) * k * 1.9 + c * 0.7));
}
)";
inline const char* kComposite = R"(
    // When output-history reconstruction fails after a jittered scene frame,
    // restore stable screen-space sampling in the fallback composite.
    // graphFrame.zw remains zero on normal frames; the caller owns this
    // uniform and leaves HUD/UI rasterization untouched.
    vec2 sp = clamp(s + graphFrame.zw,vec2(0.0),vec2(1.0));
#ifdef PF_PIXELATE
    {
        vec2 cell = px.xy * grade1.w;
        sp = (floor(sp / cell) + 0.5) * cell;
    }
#endif
    vec2 uv = TUV(sp);
#ifdef PF_CA
    vec2 cd = TDIR((sp - 0.5) * (vig.w * 0.012));
    vec3 col = vec3(TEX0(uv + cd).r, TEX0(uv).g, TEX0(uv - cd).b);
#else
    vec3 col = TEX0(uv).rgb;
#endif
#ifdef PF_SHARPEN
    {
        vec2 o = px.xy * 0.66;
        vec3 n = (TEX0(uv + o).rgb + TEX0(uv - o).rgb) * 0.5;
        col = max(col + (col - n) * (bloom.w * 1.5), vec3(0.0));
    }
#endif
#ifdef PF_TILT
    {
        float dy = abs(sp.y - tilt.x);
        float m = smoothstep(tilt.y * 0.5, tilt.y * 0.5 + tilt.z + 0.0001, dy);
        col = mix(col, TEX2(uv).rgb, m);
    }
#endif
#ifdef PF_BLOOM
    col += TEX1(uv).rgb * bloom.rgb;
#endif
#ifdef PF_SHAFTS
    col += TEX3(uv).rgb * shafts.rgb;
#endif
#ifdef PF_AUTO_EXPOSURE
    // HDR-only 1x1 GPU-adapted exposure, before ACES/sRGB. A fourth
    // Shader Graph texture occupies slot 8; the composite then reserves
    // slot 9 for exposure instead. The CPU binding plan matches this flag.
#ifdef PF_AUTO_EXPOSURE_SLOT9
    col *= clamp(TEX9(vec2(0.5)).r, 0.00024414, 4096.0);
#else
    col *= clamp(TEX8(vec2(0.5)).r, 0.00024414, 4096.0);
#endif
#endif
    if (filt.x > 0.5) {
        col = max(col * filt.z, vec3(0.0));
        if (filt.y > 0.5) col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
        col = rtsSrgbEncode(clamp(col, vec3(0.0), vec3(1.0)));
    }
#ifdef PF_GRADE
    col *= grade1.rgb;
    col = gain.rgb * (col + lift.rgb * (vec3(1.0) - col));
    col = pow(max(col, vec3(0.0)), gamma.rgb);
    col = (col - vec3(0.5)) * grade0.y + vec3(0.5);
    col = mix(vec3(dot(col, vec3(0.2126, 0.7152, 0.0722))), col, grade0.z);
    col = clamp(col, vec3(0.0), vec3(1.0));
#endif
#ifdef PF_LUT
    {
        vec3 c = clamp(col, vec3(0.0), vec3(1.0));
        float n = lut.x;
        float bz = c.b * (n - 1.0);
        float s0 = floor(bz);
        float s1 = min(s0 + 1.0, n - 1.0);
        float x = (c.r * (n - 1.0) + 0.5) / (n * n);
        float y = (c.g * (n - 1.0) + 0.5) / n;
        if (lut.y > 0.5) y = 1.0 - y;
        vec3 la = TEX4(vec2(x + s0 / n, y)).rgb;
        vec3 lb = TEX4(vec2(x + s1 / n, y)).rgb;
        col = mix(col, mix(la, lb, bz - s0), shafts.w);
    }
#endif
#ifdef PF_POSTERIZE
    col = floor(col * grade0.w + vec3(0.5)) / grade0.w;
#endif
#ifdef PF_VIGNETTE
    {
        float aspect = px.y / px.x;
        vec2 d = (s - vec2(0.5)) * vec2(aspect, 1.0);
        float r = length(d) / length(vec2(aspect, 1.0) * 0.5);
        float m = smoothstep(mix(0.85, 0.0, gamma.w), 1.0, r);
        col = mix(col, vig.rgb, m * lift.w);
    }
#endif
#ifdef PF_GRAIN
    {
        vec2 g = floor(s / (px.xy * filt.w));
        col += vec3((pfHash(g + px.w * vec2(17.0, 31.0)) - 0.5) * gain.w);
    }
#endif
    // Lens fraction that is out of the water; only that part carries drops.
    float pfLensDry = 1.0;
#ifdef PF_WATERLINE
    vec3 pfAboveWater = col;
#endif
#ifdef PF_UNDERWATER
    {
        // Prisma owns the full-screen composite. All distortion samples use
        // the resolved scene source, with a clamped UV to avoid edge wraps.
        // underwater1.z is distortion strength, .w is scattering.
        // graphFrame.y is a decaying impulse on crossing the water surface.
        float phase = graphFrame.x;
        float pulse = graphFrame.y;
        float strength = clamp(underwater0.w * underwater1.z +
                               pulse * 0.004, 0.0, 0.02);
        // Low: one directional wave; Medium: two; High: three.
        // Dynamic uniforms retain one shared shader variant for all tiers.
        vec2 wave = vec2(sin(s.y * 31.0 + phase * 2.7), 0.0);
        if (underwater2.z > 1.5)
            wave += vec2(0.35 * sin(s.x * 43.0 - phase * 1.8),
                         cos(s.x * 27.0 + phase * 2.1));
        if (underwater2.z > 2.5)
            wave += vec2(0.16 * sin((s.x + s.y) * 67.0 + phase * 1.1),
                         0.12 * cos((s.x - s.y) * 59.0 - phase));
        vec2 warped = clamp(uv + TDIR(wave * strength),
                            vec2(0.002), vec2(0.998));
        if (strength > 0.00001) {
            // Shift the already graded colour by the refraction difference;
            // mixing the raw TEX0 sample in would undo grading (orange sand).
            vec3 warpedColor = TEX0(warped).rgb;
            col += (warpedColor - TEX0(uv).rgb) * clamp(underwater0.w + pulse * 0.35,0.0,1.0);
        }
        // Screen-space approximation: near-depth haze and environmental
        // scattering. No per-object depth/caustics are claimed here.
        float opticalDistance = min(underwater1.y + 5.0, 120.0);
#ifdef PF_UNDERWATER_DEPTH
        // Sample the completed scene depth. Unproject through four columns
        // of inverse view-projection supplied by the renderer.
        // Invalid sky depth falls back to the camera-depth approximation.
        float rawDepth = PF_DEPTH_SAMPLE(TUV(s));
        if (rawDepth < 0.9999 && rawDepth >= 0.0) {
            float ndcZ = underwaterCamera.w > 0.5 ?
                         rawDepth : rawDepth * 2.0 - 1.0;
            vec4 clip = vec4(s * 2.0 - 1.0, ndcZ, 1.0);
            vec4 worldPosition =
                inverseViewProj0 * clip.x +
                inverseViewProj1 * clip.y +
                inverseViewProj2 * clip.z +
                inverseViewProj3 * clip.w;
            if (abs(worldPosition.w) > 0.00001) {
                vec3 hitPosition = worldPosition.xyz / worldPosition.w;
                // Optical absorption only along the portion of the ray
                // below the water plane (camera may be above it).
                float startDepth = underwater2.w;  // surfaceY - cameraY
                float endDepth = startDepth +
                                 (underwaterCamera.y - hitPosition.y);
                float immersedFraction = 0.0;
                if (startDepth >= 0.0 && endDepth >= 0.0)
                    immersedFraction = 1.0;
                else if (startDepth > 0.0 || endDepth > 0.0)
                    immersedFraction = max(startDepth, endDepth) /
                        max(abs(startDepth) + abs(endDepth), 0.00001);
                opticalDistance = clamp(
                    length(hitPosition - underwaterCamera.xyz) *
                    immersedFraction, 0.0, 120.0);
            }
        }
#endif
        float extinction = 1.0 - exp(-underwater1.x * opticalDistance);
        float scatterWeight = clamp(underwater1.w * underwater2.y *
                                    (0.2 + 0.8 * extinction),0.0,1.0);
        float weight = clamp(underwater0.w *
                             max(0.12, extinction + scatterWeight * 0.25),
                             0.0, 1.0);
        col = mix(col, underwater0.rgb, weight);
        col += underwater0.rgb * scatterWeight * underwater0.w * 0.06;
        col *= 1.0 - underwater0.w * clamp(underwater1.y * 0.012, 0.0, 0.35);
    }
#endif
#ifdef PF_WATERLINE
    {
        // Camera at the surface: the lens is tested against the (waving) water
        // plane, pixel by pixel. Below the line keeps the underwater optics,
        // above it the dry image; a bright refracting meniscus follows the line.
        float zNear = waterline1.w > 0.5 ? 0.0 : -1.0;
        vec2 s1 = s + vec2(0.0, px.y);
        vec3 n0 = pfUnprojectWater(inverseViewProj0, inverseViewProj1, inverseViewProj2, inverseViewProj3, s, zNear);
        vec3 f0 = pfUnprojectWater(inverseViewProj0, inverseViewProj1, inverseViewProj2, inverseViewProj3, s, 1.0);
        vec3 n1 = pfUnprojectWater(inverseViewProj0, inverseViewProj1, inverseViewProj2, inverseViewProj3, s1, zNear);
        vec3 f1 = pfUnprojectWater(inverseViewProj0, inverseViewProj1, inverseViewProj2, inverseViewProj3, s1, 1.0);
        vec3 d0 = normalize(f0 - n0), d1 = normalize(f1 - n1);
        float lens = waterline1.y;
        float t = graphFrame.x;
        vec3 l0 = underwaterCamera.xyz + d0 * lens, l1 = underwaterCamera.xyz + d1 * lens;
        // > 0: the water surface is above this lens point (submerged pixel).
        float h0 = waterline0.x + pfWaterWave(l0.xz, t, waterline0) - d0.y * lens;
        float h1 = waterline0.x + pfWaterWave(l1.xz, t, waterline0) - d1.y * lens;
        float signedPx = h0 / max(abs(h1 - h0), 0.000001);
        float wet = clamp(signedPx * 0.5 + 0.5, 0.0, 1.0);
        col = mix(pfAboveWater, col, wet);
        pfLensDry = 1.0 - wet;
        float meniscus = waterline1.x;
        if (meniscus > 0.01) {
            float band = 1.0 - smoothstep(0.0, meniscus, abs(signedPx));
            float side = signedPx > 0.0 ? 1.0 : -1.0;
            vec2 bendUv = clamp(uv + TDIR(vec2(0.0, side * band * meniscus * px.y * 0.8)),
                                vec2(0.002), vec2(0.998));
            vec3 bent = TEX0(bendUv).rgb;
            col = mix(col, bent * 1.08 + vec3(0.05), band * 0.7);
            col += vec3(0.22) * pow(band, 6.0) * (0.4 + 0.6 * waterline1.z);
        }
    }
#endif
#ifdef PF_LENS_WATER
    {
        // Stable lens-space droplets: no water-world simulation or history texture.
        vec2 lensPixel=px.xy*vec2(1.0,lensWater1.w);
        // Just emerged: everything that was under water is one refracting film
        // whose ragged top edge drains down (gravity, ease-in) to the water level.
        float lensAge=lensWater1.y;
        // First third: the film clings to the lens; then it drains (ease-in).
        float sheetT=lensWater2.z>0.001?clamp((lensAge-0.33*lensWater2.z)/(0.67*lensWater2.z),0.0,1.0):1.0;
        float sheetEdge=0.035*sin(s.x*23.0+lensAge*3.0)+0.02*sin(s.x*57.0-lensAge*5.0)+
                        0.05*(pfLensHash(vec2(floor(s.x*9.0),3.0))-0.5);
        float sheetTop=1.08-1.25*pow(sheetT,1.6)+sheetEdge*(0.4+sheetT);
        float inSheet=smoothstep(0.0,0.012,sheetTop-s.y)*clamp(lensWater2.w,0.0,1.0)*step(0.0001,lensWater2.w);
        vec2 cellSize=max(lensPixel*lensWater0.y*4.0,vec2(0.0001));
        vec2 moving=s/cellSize+vec2(0.0,lensWater1.y*lensWater1.x/max(lensWater0.y*4.0,1.0));
        vec2 cell=floor(moving),local=fract(moving);
        float coverage=0.0,rim=0.0,glint=0.0;
        vec2 refractOffset=vec2(0.0);
        // Neighbour cells let fully jittered droplets cross cell boundaries
        // without a visible grid, clipped circles or a texture atlas.
        for(int iy=-1;iy<=1;++iy)for(int ix=-1;ix<=1;++ix){
            vec2 neighbour=vec2(float(ix),float(iy)),id=cell+neighbour;
            float seed=pfLensHash(id+vec2(73.1,19.7));
            if(seed>lensWater0.z)continue;
            vec2 centre=vec2(pfLensHash(id),pfLensHash(id+vec2(17.0,29.0)));
            float size=0.22+0.78*pfLensHash(id+vec2(39.2,5.7));
            vec2 q=(local-neighbour-centre)*4.0/size;
            q.y*=q.y>0.0?0.7:1.05;
            float radius=dot(q,q);
            float c=1.0-smoothstep(0.65,1.0,radius);
            if(c>coverage){
                coverage=c;
                refractOffset=q*(1.0-clamp(radius,0.0,1.0))*size;
                rim=c*smoothstep(0.5,0.95,radius);
                glint=rim*max(0.0,-q.x+q.y);
            }
        }
        // Meandering rivulets left by the draining film (lensWater "streaks"):
        // a thin refracting tube, bright on one side, with a drop at its head.
        if(lensWater2.x>0.001){
            vec2 bigCell=max(lensPixel*lensWater0.y*vec2(10.0,30.0),vec2(0.0001));
            vec2 slide=s/bigCell+vec2(0.0,lensAge*lensWater1.x*1.2/max(lensWater0.y*30.0,1.0));
            vec2 bid=floor(slide),bl=fract(slide);
            if(pfLensHash(bid+vec2(5.3,91.1))<lensWater2.x*0.55){
                float ph=pfLensHash(bid)*6.2831;
                float centre=0.5+0.26*sin(bl.y*5.0+ph)+0.09*sin(bl.y*13.0+ph*1.7);
                float width=0.05*(0.6+0.8*pfLensHash(bid+vec2(2.0,7.0)));
                float headY=0.12;
                float along=clamp((bl.y-headY)/0.8,0.0,1.0);
                float w=width*mix(1.0,0.45,along);
                float d=bl.x-centre;
                float tube=(1.0-smoothstep(w*0.5,w,abs(d)))*step(headY,bl.y)*(1.0-smoothstep(0.75,0.95,bl.y));
                float hr=width*1.8;
                vec2 hq=vec2((bl.x-(0.5+0.26*sin(headY*5.0+ph)+0.09*sin(headY*13.0+ph*1.7)))/hr,
                             (bl.y-headY)/(hr*bigCell.x/bigCell.y));
                float hd=dot(hq,hq);
                float head=1.0-smoothstep(0.55,1.0,hd);
                float c2=max(tube,head);
                if(c2>coverage){
                    coverage=c2;
                    float nx=clamp(d/max(w,0.0001),-1.0,1.0);
                    refractOffset=tube>head?vec2(nx*1.3*(1.0-0.5*nx*nx),0.0):hq*(1.0-clamp(hd,0.0,1.0));
                    rim=tube>head?tube*smoothstep(0.35,1.0,abs(nx))*0.9:head*smoothstep(0.5,0.95,hd);
                    glint=max(glint,tube>head?tube*smoothstep(0.1,0.8,-nx)*0.9:head*smoothstep(0.2,0.8,max(0.0,-hq.x+hq.y)*0.6));
                }
            }
        }
        float film=pow(max(0.0,sin(s.x*53.0+sin(s.y*11.0+lensWater1.y*lensWater1.x/max(lensWater0.y,1.0)))),12.0)*lensWater1.z;
        vec2 offset=(refractOffset*coverage+vec2(film*0.25,film*0.08))*lensWater0.w*lensPixel;
        // Water-covered lens (below the waterline) has no drops, trails or film.
        float lensAmount=lensWater0.x*pfLensDry*(1.0-inSheet);
        vec3 original=TEX0(uv).rgb;
        vec3 bent=TEX0(clamp(uv+TDIR(offset),vec2(0.002),vec2(0.998))).rgb;
        col+=(bent-original)*lensAmount;
        col*=1.0-0.055*rim*lensAmount;
        col+=vec3(0.035)*glint*lensAmount;
        col+=vec3(1.0,0.96,0.86)*pow(glint,3.0)*lensWater2.y*0.8*lensAmount;
        if(inSheet>0.001){
            // Fine flowing ripples (draining downward), moderate refraction.
            float fy=s.y*lensWater1.w;
            vec2 flowN=0.6*vec2(sin(fy*90.0+lensAge*16.0+sin(s.x*41.0)*1.5),cos(s.x*83.0-fy*37.0+lensAge*9.0))+
                       0.4*vec2(sin(s.x*173.0+fy*121.0+lensAge*21.0),sin(fy*157.0-s.x*61.0+lensAge*17.0));
            float edgeBand=1.0-smoothstep(0.0,0.03,abs(sheetTop-s.y));
            vec2 sheetOffset=(flowN*1.1+vec2(0.0,-2.2*edgeBand))*lensPixel*lensWater0.w;
            // Applied as a refraction difference over the graded colour, like the
            // drops: no raw-HDR leak, the film only bends and brightens light.
            vec3 sheetOriginal=TEX0(uv).rgb;
            vec3 sheetBent=TEX0(clamp(uv+TDIR(sheetOffset),vec2(0.002),vec2(0.998))).rgb;
            vec3 sheetCol=col+(sheetBent-sheetOriginal);
            // Flowing glare streaks along the draining water (not a dot grid).
            float streak=pow(clamp(0.5+0.5*sin(s.x*61.0+flowN.x*2.2+lensAge*2.0),0.0,1.0),24.0)*
                         clamp(0.5+0.5*flowN.y,0.0,1.0);
            sheetCol+=vec3(0.20,0.21,0.21)*streak*(0.4+0.6*lensWater2.y);
            // A water layer on the glass: slightly darker, cooler, less contrast.
            sheetCol=mix(sheetCol,sheetCol*vec3(0.88,0.94,0.97)+vec3(0.02,0.03,0.035),0.6);
            sheetCol+=vec3(0.30)*pow(edgeBand,3.0)*(0.5+0.5*lensWater2.y);
            sheetCol*=1.0-0.18*smoothstep(0.03,0.0,abs(sheetTop-0.04-s.y));
            col=mix(col,sheetCol,inSheet*pfLensDry*clamp(lensWater0.x*1.4,0.0,1.0));
        }
    }
#endif
#ifdef PF_FADE
    col = mix(col, fade.rgb, fade.w);
#endif
#ifdef PF_LETTERBOX
    if (s.y < lbox.w * 0.5 || s.y > 1.0 - lbox.w * 0.5) col = lbox.rgb;
#endif
    col += vec3((pfHash(s * 1931.0 + px.w) - 0.5) / 255.0);   // dither: no banding in gradients / vignette
    RETURN(vec4(clamp(col, vec3(0.0), vec3(1.0)), 1.0));
)";

} // namespace aiverse::render::postfx

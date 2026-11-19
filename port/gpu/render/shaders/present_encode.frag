// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the last pass before the swapchain. SDR: linear color encoded with the sRGB curve,
// its exponent shifted by the user's gamma (1: the standard curve); sRGB-encoded sources read
// through a UNORM view are decoded first. HDR: passed through. sharpen > 0 (no upscaler): RCAS
// (FSR 1's robust contrast-adaptive sharpening) over the source's texel and its 4 neighbours.
#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D source;

layout(push_constant) uniform Settings {
    float gamma;
    bool hdr;
    bool srgb_input;
    float sharpen;
    bool area_filter;
    bool fsr; // kr-port: FSR 1 EASU when the source is smaller than the output (KR_UPSCALER=fsr1)
} settings;

// kr-port: FSR 1's EASU (third_party/amd-fsr1, MIT): edge-adaptive spatial upscaling of the whole
// frame, in place of the bilinear read, for a render size below the window's (KR_RES). The HUD is
// in the same frame and is upscaled with it.
#define A_GPU 1
#define A_GLSL 1
#include "fsr/ffx_a.h"
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return textureGather(source, p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(source, p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(source, p, 2); }
#include "fsr/ffx_fsr1.h"

vec3 Easu(vec2 ratio) {
    const vec2 in_size = vec2(textureSize(source, 0));
    const vec2 out_size = floor(in_size / ratio + 0.5);
    // FsrEasuCon (ffx_fsr1.h) for the whole source scaled to the whole output.
    const uvec4 con0 = uvec4(floatBitsToUint(in_size.x / out_size.x),
                             floatBitsToUint(in_size.y / out_size.y),
                             floatBitsToUint(0.5 * in_size.x / out_size.x - 0.5),
                             floatBitsToUint(0.5 * in_size.y / out_size.y - 0.5));
    const uvec4 con1 = uvec4(floatBitsToUint(1.0 / in_size.x), floatBitsToUint(1.0 / in_size.y),
                             floatBitsToUint(1.0 / in_size.x), floatBitsToUint(-1.0 / in_size.y));
    const uvec4 con2 = uvec4(floatBitsToUint(-1.0 / in_size.x), floatBitsToUint(2.0 / in_size.y),
                             floatBitsToUint(1.0 / in_size.x), floatBitsToUint(2.0 / in_size.y));
    const uvec4 con3 = uvec4(floatBitsToUint(0.0), floatBitsToUint(4.0 / in_size.y), 0u, 0u);
    vec3 c;
    FsrEasuF(c, uvec2(gl_FragCoord.xy), con0, con1, con2, con3);
    return c;
}

// The sRGB curve's constants: linear below the knee, a power above it.
const float Knee = 0.0031308;
const float Scale = 1.055;
const float Offset = 0.055;
const float Slope = 12.92;

vec3 Encode(vec3 linear) {
    const float exponent = 1.0 / (3.4 - settings.gamma);
    const vec3 curve = Scale * pow(linear, vec3(exponent)) - Offset;
    return mix(curve, Slope * linear / settings.gamma, lessThan(linear, vec3(Knee)));
}

// The inverse of Encode at gamma 1.
vec3 Decode(vec3 encoded) {
    const vec3 curve = pow(max(encoded + Offset, 0.0) / Scale, vec3(2.4));
    return mix(curve, encoded / Slope, lessThan(encoded, vec3(Slope * Knee)));
}

vec3 Linear(vec3 rgb) {
    return settings.srgb_input ? Decode(rgb) : rgb;
}

// RCAS: the most negative lobe that keeps the result within the neighbours' range (no ringing),
// at most 3/16, scaled by the strength (1: AMD's strongest).
vec3 Rcas(vec3 e) {
    const vec2 texel_size = 1.0 / vec2(textureSize(source, 0));
    const vec3 b = clamp(Linear(texture(source, uv + vec2(0.0, -texel_size.y)).rgb), 0.0, 1.0);
    const vec3 d = clamp(Linear(texture(source, uv + vec2(-texel_size.x, 0.0)).rgb), 0.0, 1.0);
    const vec3 f = clamp(Linear(texture(source, uv + vec2(texel_size.x, 0.0)).rgb), 0.0, 1.0);
    const vec3 h = clamp(Linear(texture(source, uv + vec2(0.0, texel_size.y)).rgb), 0.0, 1.0);
    const vec3 mn4 = min(min(b, d), min(f, h));
    const vec3 mx4 = max(max(b, d), max(f, h));
    const vec3 hit_min = mn4 / max(4.0 * mx4, 1e-4);
    const vec3 hit_max = (1.0 - mx4) / min(4.0 * mn4 - 4.0, -1e-4);
    const vec3 lobe_rgb = max(-hit_min, hit_max);
    const float lobe = max(-0.1875, min(max(lobe_rgb.r, max(lobe_rgb.g, lobe_rgb.b)), 0.0)) *
                       min(settings.sharpen, 1.0);
    return (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
}

// kr-port: a source larger than the output (KR_RES above the window's size: supersampling).
// Each output pixel averages the source pixels its footprint covers, each weighted by the part of
// it inside the footprint, in linear light (HDR: as stored). The bilinear read it replaces took 4
// texels around one point: at 1.33x (1440p into 1080p) some source pixels weighed a lot and others
// nothing, so edges stayed jagged and thin lines shimmered. Up to 8 texels per axis (7x).
const int MaxTaps = 8;

vec4 AreaFilter(vec2 ratio) {
    const vec2 size = vec2(textureSize(source, 0));
    const vec2 lo = uv * size - 0.5 * ratio;
    const vec2 hi = lo + ratio;
    const ivec2 first = ivec2(floor(max(lo, vec2(0.0))));
    const ivec2 last = min(ivec2(ceil(hi)) - 1, ivec2(size) - 1);
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int y = first.y; y <= last.y && y < first.y + MaxTaps; ++y) {
        const float wy = min(hi.y, float(y + 1)) - max(lo.y, float(y));
        for (int x = first.x; x <= last.x && x < first.x + MaxTaps; ++x) {
            const float w = (min(hi.x, float(x + 1)) - max(lo.x, float(x))) * wy;
            vec4 texel = texelFetch(source, ivec2(x, y), 0);
            if (!settings.hdr) {
                texel.rgb = Linear(texel.rgb);
            }
            sum += texel * w;
            total += w;
        }
    }
    return sum / max(total, 1e-6);
}

void main() {
    // The source texels one output pixel spans (the triangle's uv is linear over the screen).
    const vec2 ratio = abs(vec2(dFdx(uv).x, dFdy(uv).y)) * vec2(textureSize(source, 0));
    const bool downscale = settings.area_filter && (ratio.x > 1.001 || ratio.y > 1.001);
    if (downscale) {
        const vec4 texel = AreaFilter(ratio);
        color = settings.hdr ? texel : vec4(Encode(texel.rgb), texel.a);
        return;
    }
    const bool upscale = ratio.x < 0.999 || ratio.y < 0.999;
    vec4 texel = settings.fsr && upscale && !settings.hdr ? vec4(Easu(ratio), texture(source, uv).a)
                                                         : texture(source, uv);
    if (settings.hdr) {
        color = texel;
        return;
    }
    texel.rgb = Linear(texel.rgb);
    if (settings.sharpen > 0.0) {
        texel.rgb = Rcas(clamp(texel.rgb, 0.0, 1.0));
    }
    color = vec4(Encode(texel.rgb), texel.a);
}

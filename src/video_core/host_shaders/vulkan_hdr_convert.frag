// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Converts a presented layer between SDR, HDR10 and scRGB. Based on Eglantine #557's
// hdr_tonemap.frag (PQ to SDR) and hdr_pq_encode.frag (SDR to PQ) by 101Leafy, with the
// luminances the game was told instead of fixed ones, and scRGB on either side.

#version 460

layout (location = 0) in vec4 posPos;

layout (location = 0) out vec4 frag_color;

layout (binding = 1) uniform sampler2D input_texture;

// 0: sRGB BT.709 (SDR), 1: PQ BT.2020 (HDR10), 2: linear BT.709 (scRGB).
layout (push_constant) uniform Params {
    uint input_encoding;
    uint output_encoding;
    float reference_white; // nits SDR white is shown at
    float peak;            // nits an SDR output rolls highlights off to
    float nits_per_unit;   // nits of linear 1.0 in a scRGB input
    uint premultiplied;    // colour is premultiplied by alpha
    uint tonemapper;       // 0: native, 1: RenoDX PsychoV35 (HDR inputs only)
    float contrast;        // PsychoV35 contrast, 1 is neutral
    uint sdr_to_hdr;       // expand an SDR picture's highlights to the HDR peak
};

const float PQ_M1 = 0.1593017578125;
const float PQ_M2 = 78.84375;
const float PQ_C1 = 0.8359375;
const float PQ_C2 = 18.8515625;
const float PQ_C3 = 18.6875;

const float SCRGB_NITS = 80.0;

const mat3 BT2020_TO_BT709 = mat3(
    1.6605, -0.1246, -0.0182,
   -0.5876,  1.1329, -0.1006,
   -0.0728, -0.0083,  1.1187);

const mat3 BT709_TO_BT2020 = mat3(
    0.6274, 0.0691, 0.0164,
    0.3293, 0.9195, 0.0880,
    0.0433, 0.0114, 0.8956);

vec3 PqToNits(vec3 e) {
    vec3 p = pow(clamp(e, 0.0, 1.0), vec3(1.0 / PQ_M2));
    return 10000.0 * pow(max(p - PQ_C1, 0.0) / (PQ_C2 - PQ_C3 * p), vec3(1.0 / PQ_M1));
}

vec3 NitsToPq(vec3 nits) {
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(PQ_M1));
    return pow((PQ_C1 + PQ_C2 * y) / (1.0 + PQ_C3 * y), vec3(PQ_M2));
}

vec3 SrgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 LinearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// RenoDX PsychoV35 (psychotm_test35, clshortfuse/renodx src/shaders/tonemap/psychov/test35.hlsl,
// Copyright (C) 2026 Carlos Lopez, MIT): its default public path, the exact target fit (fit
// mode 2) without human-gamut conditioning, at neutral exposure, highlights, shadows and purity,
// adapted to 0.18. Input and output: linear BT.709 relative to reference white.
const mat3 BT709_TO_LMS = mat3(
    0.2896057766, 0.09018375762, 0.01552874012,
    0.6972466442, 0.7073490257, 0.05360930731,
    0.07637127862, 0.1122031464, 0.5097978305);
const mat3 LMS_TO_BT709 = mat3(
    4.96761965, -0.6196828788, -0.08615200402,
    -4.922379529, 2.051750652, -0.065819383,
    0.3391991812, -0.3587439844, 1.988954483);
const mat3 LMS_TO_BT2020 = mat3(
    2.908921354, -0.227553273, -0.05027123211,
    -2.415564245, 1.545796661, 0.04094895167,
    0.1808342948, -0.2838427127, 1.755283912);
const vec3 D65_WHITE_LMS = vec3(1.063223699, 0.9097359297, 0.5789358779);
const vec3 PSYCHO_YF_WEIGHTS = vec3(0.68990272, 0.34832189, 0.0);

float PsychoMean(vec3 v) {
    return v.x + ((v.y - v.x) + (v.z - v.x)) / 3.0;
}

vec3 PsychoTargetResidual(vec3 normalized_lms, int target) {
    vec3 physical_lms = normalized_lms * D65_WHITE_LMS;
    return target == 0 ? LMS_TO_BT709 * physical_lms : LMS_TO_BT2020 * physical_lms;
}

// The finite cone response in the log domain (psycho35_ResponseLogs).
void PsychoResponseLogs(float log_q, float k, float n, float h, out float log_u,
                        out float log_deficit) {
    float latent = log(k) - log(1.0 - k) + n * log_q;
    float correction = log(1.0 + exp(-abs(latent)));
    log_u = (min(latent, 0.0) - correction) / h;
    float log_tail = -max(latent, 0.0) - correction;
    log_deficit = latent > 20.0
                      ? log_tail - log(h)
                      : log(abs(log_u) < 0.01
                                ? -log_u * (1.0 + log_u * (0.5 + log_u * (1.0 / 6.0 + log_u / 24.0)))
                                : 1.0 - exp(log_u));
}

vec3 Psycho35(vec3 bt709, float peak_value, float contrast_value, int target) {
    vec3 input_lms = BT709_TO_LMS * bt709;
    vec3 anchor = BT709_TO_LMS * vec3(0.18);
    vec3 source_ratio = input_lms / anchor;
    // Purity stays at 1: with contrast away from 1 the purity amount is 1/contrast.
    if (contrast_value != 1.0) {
        float neutral_ratio = dot(PSYCHO_YF_WEIGHTS, input_lms) / dot(PSYCHO_YF_WEIGHTS, anchor);
        vec3 purity_direction = source_ratio - neutral_ratio;
        float purity_amount = 1.0 / contrast_value;
        if (all(greaterThanEqual(source_ratio, vec3(0.0))) && neutral_ratio > 0.0 &&
            purity_amount > 1.0) {
            for (int cone = 0; cone < 3; ++cone) {
                if (purity_direction[cone] < 0.0) {
                    purity_amount = min(purity_amount, neutral_ratio / -purity_direction[cone]);
                }
            }
            source_ratio = max(neutral_ratio + purity_amount * purity_direction, 0.0);
        } else {
            source_ratio = neutral_ratio + purity_amount * purity_direction;
        }
    }
    // Response compression 1, cone exponent 1.
    float response_power = contrast_value;
    float response_compression = 1.0;
    vec3 anchor_power = anchor / (D65_WHITE_LMS * peak_value);
    vec3 input_exponent = response_power * response_compression / (1.0 - anchor_power);
    if (all(equal(source_ratio, vec3(0.0)))) {
        return vec3(0.0);
    }
    // Negative cones are outside the response's domain (the original's logs fail there).
    source_ratio = max(source_ratio, 0.0);
    float drive = max(source_ratio.x, max(source_ratio.y, source_ratio.z));
    if (!(drive > 0.0)) {
        return vec3(0.0);
    }
    vec3 ratio = source_ratio / drive;
    float log_drive = log(drive);

    vec3 response;
    vec3 deficit;
    for (int i = 0; i < 3; ++i) {
        if (ratio[i] == 0.0) {
            response[i] = 0.0;
            deficit[i] = 1.0;
        } else {
            float log_u;
            float log_deficit;
            PsychoResponseLogs(log(ratio[i]) + log_drive, anchor_power[i], input_exponent[i],
                               response_compression, log_u, log_deficit);
            response[i] = exp(log_u);
            deficit[i] = exp(log_deficit);
        }
    }
    float response_mean = PsychoMean(response);
    vec3 response_residual =
        response_mean > 0.5 ? PsychoMean(deficit) - deficit : response - response_mean;
    float residual_scale = max(abs(response_residual.x),
                                     max(abs(response_residual.y), abs(response_residual.z)));
    vec3 scaled_residual = residual_scale > 0.0 ? response_residual / residual_scale : vec3(0.0);
    float scaled_radius = length(scaled_residual);
    float radius = residual_scale * scaled_radius;
    vec3 direction = scaled_radius > 0.0 ? scaled_residual / scaled_radius : vec3(0.0);
    // The Mean-A2 angular midpoint between the source's and the response's directions.
    vec3 source = ratio - PsychoMean(ratio);
    float source_radius = length(source);
    if (source_radius > 0.0 && radius > 0.0) {
        vec3 midpoint_direction = source / source_radius + direction;
        float midpoint_length = length(midpoint_direction);
        if (midpoint_length > 0.0) {
            direction = midpoint_direction / midpoint_length;
        }
    }
    float mean = peak_value * response_mean;
    float headroom = peak_value * PsychoMean(deficit);
    vec3 residual = peak_value * radius * direction;
    if (all(equal(residual, vec3(0.0)))) {
        return vec3(mean);
    }
    // Exact fit into the target's faces: the minimum face support.
    vec3 displacement = PsychoTargetResidual(residual, target);
    float scale = 1.0;
    for (int channel = 0; channel < 3; ++channel) {
        if (displacement[channel] > 0.0) {
            scale = min(scale, headroom / displacement[channel]);
        } else if (displacement[channel] < 0.0) {
            scale = min(scale, mean / -displacement[channel]);
        }
    }
    return mean + PsychoTargetResidual(residual * scale, 0);
}

bool UsesPsycho() {
    return tonemapper == 1 && input_encoding != 0;
}

// The game's HDR picture through PsychoV35, fitted to the output's peak (SDR white for an SDR
// output) and its gamut.
vec3 Tonemap(vec3 nits) {
    if (!UsesPsycho()) {
        return nits;
    }
    float peak_units = output_encoding == 0 ? 1.0 : max(peak / reference_white, 1.0);
    return reference_white * Psycho35(nits / reference_white, peak_units, max(contrast, 0.01),
                                      output_encoding == 0 ? 0 : 1);
}

// Linear BT.709 in nits.
vec3 Decode(vec3 c) {
    if (input_encoding == 1) {
        return BT2020_TO_BT709 * PqToNits(c);
    }
    if (input_encoding == 2) {
        return c * nits_per_unit;
    }
    vec3 linear = SrgbToLinear(clamp(c, 0.0, 1.0));
    if (sdr_to_hdr != 0 && output_encoding != 0) {
        // The inverse of the extended Reinhard an HDR picture gets on an SDR screen (Encode), on
        // the brightest channel so hue holds: SDR white reaches the peak, shadows barely move.
        float m = max(max(linear.r, linear.g), linear.b);
        float w = max(peak / reference_white, 1.0);
        if (m > 0.0 && w > 1.0) {
            float a = 1.0 / (w * w);
            float b = 1.0 - m;
            float x = (-b + sqrt(b * b + 4.0 * a * m)) / (2.0 * a);
            linear *= x / m;
        }
    }
    return linear * reference_white;
}

vec3 Encode(vec3 nits) {
    if (output_encoding == 1) {
        return NitsToPq(max(BT709_TO_BT2020 * nits, 0.0));
    }
    if (output_encoding == 2) {
        return nits / SCRGB_NITS;
    }
    vec3 c = max(nits / reference_white, 0.0);
    // Extended Reinhard on the brightest channel, so hue holds as highlights roll off
    float m = max(max(c.r, c.g), c.b);
    if (m > 0.0 && !UsesPsycho()) {
        float w = max(peak / reference_white, 1.0);
        c *= (1.0 + m / (w * w)) / (1.0 + m);
    }
    return LinearToSrgb(clamp(c, 0.0, 1.0));
}

void main() {
    vec4 src = texture(input_texture, posPos.xy);
    if (premultiplied != 0) {
        vec3 color = src.a > 0.0 ? src.rgb / src.a : vec3(0.0);
        frag_color = vec4(Encode(Tonemap(Decode(color))) * src.a, src.a);
        return;
    }
    frag_color = vec4(Encode(Tonemap(Decode(src.rgb))), src.a);
}

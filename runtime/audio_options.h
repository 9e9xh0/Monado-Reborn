// [XCDE] Audio output channel policy (stereo / 5.1 surround) and mixing helpers.
// Shared between the runtime (audio.c, settings_ini.c, menu_vk.cpp), the launcher (launcher.cpp),
// and the native unit tests (tests/native/test_audio_options.c).
//
// Nintendo Switch (nn::audio / audren / audout) and SDL3 share the standard SMPTE 5.1 channel
// order: FL (0), FR (1), FC (2), LFE (3), BL (4), BR (5). Switch hardware/OS natively mixes up
// to 5.1 (6 channels); on 7.1 (8-channel) host endpoints, SDL3 maps the 6-channel 5.1 stream
// cleanly to the host layout.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    AUDIO_OUT_AUTO = 0,        // 5.1 (6 ch) on >= 6-channel host devices, otherwise Stereo (2 ch)
    AUDIO_OUT_STEREO = 1,      // Force 2.0 Stereo
    AUDIO_OUT_SURROUND_51 = 2, // Force 5.1 Surround (6 channels)
    AUDIO_OUT_COUNT = 3
};

static const char *const audio_out_ini[AUDIO_OUT_COUNT] = {"auto", "stereo", "5.1"};

static inline int audio_out_valid(int mode) {
    return (mode >= 0 && mode < AUDIO_OUT_COUNT) ? mode : AUDIO_OUT_AUTO;
}

static inline int audio_out_parse(const char *v) {
    if (!v || !*v) return AUDIO_OUT_AUTO;
    if (!strcmp(v, "auto")) return AUDIO_OUT_AUTO;
    if (!strcmp(v, "stereo") || !strcmp(v, "2") || !strcmp(v, "2.0")) return AUDIO_OUT_STEREO;
    if (!strcmp(v, "5.1") || !strcmp(v, "surround") || !strcmp(v, "6")) return AUDIO_OUT_SURROUND_51;
    return AUDIO_OUT_AUTO;
}

// Diagnostic / CLI override via XCDE_AUDIO_CHANNELS: NULL or empty keeps the configured mode.
static inline int audio_out_env_override(const char *env_val, int configured_mode) {
    int base = audio_out_valid(configured_mode);
    if (!env_val || !*env_val) return base;
    if (!strcmp(env_val, "auto")) return AUDIO_OUT_AUTO;
    if (!strcmp(env_val, "stereo") || !strcmp(env_val, "2") || !strcmp(env_val, "2.0")) return AUDIO_OUT_STEREO;
    if (!strcmp(env_val, "5.1") || !strcmp(env_val, "surround") || !strcmp(env_val, "6")) return AUDIO_OUT_SURROUND_51;
    return base;
}

// Resolve the active output channel count (2 or 6) reported to the game and used by the host stream.
// Deterministic runs always resolve AUDIO_OUT_AUTO to stereo (2) so recorded routes stay reproducible.
static inline int audio_out_resolve_channels(int mode, int host_channels, bool deterministic) {
    switch (audio_out_valid(mode)) {
    case AUDIO_OUT_STEREO: return 2;
    case AUDIO_OUT_SURROUND_51: return 6;
    case AUDIO_OUT_AUTO:
    default:
        if (deterministic) return 2;
        return host_channels >= 6 ? 6 : 2;
    }
}

static inline float audio_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Mix one audren device-sink frame (up to 6 channels in FL, FR, FC, LFE, BL, BR order, scaled as
// signed 16-bit float values) into `out_channels` (2 or 6) normalized [-1, 1] floats.
static inline void audio_sink_mix_frame(const float ch[6], int sink_inputs, int out_channels, float *dst) {
    if (out_channels == 6) {
        if (sink_inputs >= 6) {
            for (int c = 0; c < 6; c++) dst[c] = audio_clampf(ch[c] / 32768.0f, -1.0f, 1.0f);
        } else {
            // Stereo (or mono) mix routed to a 6-channel output: place on FL/FR and zero FC/LFE/BL/BR.
            dst[0] = sink_inputs >= 1 ? audio_clampf(ch[0] / 32768.0f, -1.0f, 1.0f) : 0.0f;
            dst[1] = sink_inputs >= 2 ? audio_clampf(ch[1] / 32768.0f, -1.0f, 1.0f) : dst[0];
            dst[2] = 0.0f;
            dst[3] = 0.0f;
            dst[4] = 0.0f;
            dst[5] = 0.0f;
        }
        return;
    }
    // 2-channel stereo output: fold 5.1 (FL FR FC LFE BL BR) with ITU/Ryujinx coefficients.
    float L = ch[0], R = ch[1];
    if (sink_inputs >= 6) {
        L += 0.707f * ch[2] + 0.251f * ch[3] + 0.707f * ch[4];
        R += 0.707f * ch[2] + 0.251f * ch[3] + 0.707f * ch[5];
    }
    dst[0] = audio_clampf(L / 32768.0f, -1.0f, 1.0f);
    dst[1] = audio_clampf(R / 32768.0f, -1.0f, 1.0f);
}

// Downmix interleaved 6-channel PCM16 (FL, FR, FC, LFE, BL, BR) to 2-channel stereo PCM16.
// Used by audout:u when the user explicitly forces Stereo on a 6-channel guest stream.
static inline void audio_downmix_s16_6to2(const int16_t *in, int16_t *out, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        const int16_t *s = in + i * 6;
        float L = (float)s[0] + 0.707f * (float)s[2] + 0.251f * (float)s[3] + 0.707f * (float)s[4];
        float R = (float)s[1] + 0.707f * (float)s[2] + 0.251f * (float)s[3] + 0.707f * (float)s[5];
        out[i * 2]     = (int16_t)audio_clampf(L, -32768.0f, 32767.0f);
        out[i * 2 + 1] = (int16_t)audio_clampf(R, -32768.0f, 32767.0f);
    }
}

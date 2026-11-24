// [XCDE] Unit tests for audio output channel policy and 5.1 / stereo mixing (runtime/audio_options.h).
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/audio_options.h"

static bool nearf(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main(void) {
    // 1. Parsing and validation
    assert(audio_out_valid(-1) == AUDIO_OUT_AUTO);
    assert(audio_out_valid(AUDIO_OUT_AUTO) == AUDIO_OUT_AUTO);
    assert(audio_out_valid(AUDIO_OUT_STEREO) == AUDIO_OUT_STEREO);
    assert(audio_out_valid(AUDIO_OUT_SURROUND_51) == AUDIO_OUT_SURROUND_51);
    assert(audio_out_valid(99) == AUDIO_OUT_AUTO);

    assert(audio_out_parse(NULL) == AUDIO_OUT_AUTO);
    assert(audio_out_parse("") == AUDIO_OUT_AUTO);
    assert(audio_out_parse("auto") == AUDIO_OUT_AUTO);
    assert(audio_out_parse("unknown") == AUDIO_OUT_AUTO);
    assert(audio_out_parse("stereo") == AUDIO_OUT_STEREO);
    assert(audio_out_parse("2") == AUDIO_OUT_STEREO);
    assert(audio_out_parse("2.0") == AUDIO_OUT_STEREO);
    assert(audio_out_parse("5.1") == AUDIO_OUT_SURROUND_51);
    assert(audio_out_parse("surround") == AUDIO_OUT_SURROUND_51);
    assert(audio_out_parse("6") == AUDIO_OUT_SURROUND_51);
    assert(!strcmp(audio_out_ini[AUDIO_OUT_AUTO], "auto"));
    assert(!strcmp(audio_out_ini[AUDIO_OUT_STEREO], "stereo"));
    assert(!strcmp(audio_out_ini[AUDIO_OUT_SURROUND_51], "5.1"));

    // 2. Environment variable override (XCDE_AUDIO_CHANNELS)
    assert(audio_out_env_override(NULL, AUDIO_OUT_SURROUND_51) == AUDIO_OUT_SURROUND_51);
    assert(audio_out_env_override("", AUDIO_OUT_STEREO) == AUDIO_OUT_STEREO);
    assert(audio_out_env_override("garbage", AUDIO_OUT_SURROUND_51) == AUDIO_OUT_SURROUND_51);
    assert(audio_out_env_override("5.1", AUDIO_OUT_STEREO) == AUDIO_OUT_SURROUND_51);
    assert(audio_out_env_override("stereo", AUDIO_OUT_SURROUND_51) == AUDIO_OUT_STEREO);
    assert(audio_out_env_override("auto", AUDIO_OUT_STEREO) == AUDIO_OUT_AUTO);

    // 3. Channel resolution across host layouts (2.0, 4.0, 5.1, 7.1, headless) and deterministic mode
    assert(audio_out_resolve_channels(AUDIO_OUT_AUTO, 0, false) == 2);
    assert(audio_out_resolve_channels(AUDIO_OUT_AUTO, 2, false) == 2);
    assert(audio_out_resolve_channels(AUDIO_OUT_AUTO, 4, false) == 2);
    assert(audio_out_resolve_channels(AUDIO_OUT_AUTO, 6, false) == 6);
    assert(audio_out_resolve_channels(AUDIO_OUT_AUTO, 8, false) == 6);  // 7.1 host -> Switch 5.1 (SDL maps 5.1 -> 7.1)
    assert(audio_out_resolve_channels(AUDIO_OUT_AUTO, 6, true) == 2);   // deterministic mode stays stereo
    assert(audio_out_resolve_channels(AUDIO_OUT_AUTO, 8, true) == 2);

    assert(audio_out_resolve_channels(AUDIO_OUT_STEREO, 6, false) == 2);
    assert(audio_out_resolve_channels(AUDIO_OUT_STEREO, 8, false) == 2);
    assert(audio_out_resolve_channels(AUDIO_OUT_SURROUND_51, 2, false) == 6);
    assert(audio_out_resolve_channels(AUDIO_OUT_SURROUND_51, 6, false) == 6);
    assert(audio_out_resolve_channels(AUDIO_OUT_SURROUND_51, 8, false) == 6);

    // 4. audren sink mixing: 6-channel discrete passthrough (FL, FR, FC, LFE, BL, BR)
    const float ch6[6] = {16384.0f, -16384.0f, 8192.0f, 4096.0f, 2048.0f, -2048.0f};
    float out6[6] = {0};
    audio_sink_mix_frame(ch6, 6, 6, out6);
    assert(nearf(out6[0], 0.5f));
    assert(nearf(out6[1], -0.5f));
    assert(nearf(out6[2], 0.25f));
    assert(nearf(out6[3], 0.125f));
    assert(nearf(out6[4], 0.0625f));
    assert(nearf(out6[5], -0.0625f));

    // 5. audren sink mixing: stereo mix into 6-channel output zeroes FC/LFE/BL/BR
    const float ch2[6] = {16384.0f, -8192.0f, 9999.0f, 9999.0f, 9999.0f, 9999.0f};
    audio_sink_mix_frame(ch2, 2, 6, out6);
    assert(nearf(out6[0], 0.5f));
    assert(nearf(out6[1], -0.25f));
    assert(out6[2] == 0.0f && out6[3] == 0.0f && out6[4] == 0.0f && out6[5] == 0.0f);

    // 6. audren sink mixing: 6-channel downmix to 2-channel stereo with ITU/Ryujinx weights
    float out2[2] = {0};
    audio_sink_mix_frame(ch6, 6, 2, out2);
    float expect_L = (16384.0f + 0.707f * 8192.0f + 0.251f * 4096.0f + 0.707f * 2048.0f) / 32768.0f;
    float expect_R = (-16384.0f + 0.707f * 8192.0f + 0.251f * 4096.0f + 0.707f * (-2048.0f)) / 32768.0f;
    assert(nearf(out2[0], expect_L));
    assert(nearf(out2[1], expect_R));

    // Clamping on hot signals
    const float hot[6] = {50000.0f, -50000.0f, 50000.0f, 50000.0f, 50000.0f, -50000.0f};
    audio_sink_mix_frame(hot, 6, 6, out6);
    assert(out6[0] == 1.0f && out6[1] == -1.0f && out6[2] == 1.0f && out6[5] == -1.0f);
    audio_sink_mix_frame(hot, 6, 2, out2);
    assert(out2[0] == 1.0f && out2[1] == -1.0f);

    // 7. audout PCM16 6ch -> 2ch downmix (used when Stereo is forced on a 6ch stream)
    const int16_t pcm6[12] = {
        10000, -10000, 4000, 2000, 1000, -1000,
        30000, -30000, 0, 0, 20000, -20000
    };
    int16_t pcm2[4] = {0};
    audio_downmix_s16_6to2(pcm6, pcm2, 2);
    int16_t exp0 = (int16_t)(10000.0f + 0.707f * 4000.0f + 0.251f * 2000.0f + 0.707f * 1000.0f);
    int16_t exp1 = (int16_t)(-10000.0f + 0.707f * 4000.0f + 0.251f * 2000.0f + 0.707f * (-1000.0f));
    assert(pcm2[0] == exp0 && pcm2[1] == exp1);
    assert(pcm2[2] == 32767 && pcm2[3] == -32768);

    puts("audio options: parse, env override, channel resolution, 5.1 passthrough and stereo downmix OK");
    return 0;
}

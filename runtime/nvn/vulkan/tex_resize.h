// [XCDE] #172: pure-C policies for scalable texture reallocation, new color render-target clears,
// and material-only temporal upscaler LOD bias selection. Shared with tests/native/test_tex_resize.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../../settings.h"

enum {
    XCDE_TEX_MAX_BLIT_LEVELS = 16,
    XCDE_SAMPLER_BIAS_SLOTS = 6,
};

typedef struct {
    uint32_t width, height, depth, levels, layers, samples;
    bool is_color, gpu_owned;
} XcdeTexDesc;

typedef struct {
    uint32_t level, src_w, src_h, src_d, dst_w, dst_h, dst_d;
} XcdeTexBlitLevel;

typedef struct {
    bool preserve, linear_filter, invalidate_upload;
    uint32_t layers, level_count;
    XcdeTexBlitLevel regions[XCDE_TEX_MAX_BLIT_LEVELS];
} XcdeTexResizePlan;

static inline uint32_t xcde_tex_mip_dim(uint32_t base, uint32_t level) {
    if (!base) return 0;
    if (level >= 32u) return 1u;
    uint32_t v = base >> level;
    return v ? v : 1u;
}

static inline bool xcde_tex_should_clear_new_color(bool is_depth, uint32_t samples, bool color_attachment,
                                                   bool for_render, bool gpu_owned, uint32_t nvn_flags,
                                                   bool clear_enabled) {
    return clear_enabled && !is_depth && samples == 1u && color_attachment &&
           (for_render || gpu_owned || (nvn_flags & 8u) != 0u);
}

static inline XcdeTexResizePlan xcde_tex_plan_resize(const XcdeTexDesc *old_tex, const XcdeTexDesc *new_tex,
                                                      bool preserve_enabled, bool blit_src_ok,
                                                      bool blit_dst_ok, bool linear_filter_ok) {
    XcdeTexResizePlan plan = {0};
    if (!old_tex || !new_tex) return plan;
    plan.invalidate_upload = !old_tex->gpu_owned;
    if (!preserve_enabled || !old_tex->gpu_owned || !old_tex->is_color || !new_tex->is_color ||
        old_tex->samples != 1u || new_tex->samples != 1u || !blit_src_ok || !blit_dst_ok)
        return plan;
    uint32_t nlv = old_tex->levels < new_tex->levels ? old_tex->levels : new_tex->levels;
    if (nlv > XCDE_TEX_MAX_BLIT_LEVELS) nlv = XCDE_TEX_MAX_BLIT_LEVELS;
    uint32_t nly = old_tex->layers < new_tex->layers ? old_tex->layers : new_tex->layers;
    if (!nlv || !nly || !old_tex->width || !old_tex->height || !new_tex->width || !new_tex->height)
        return plan;
    plan.preserve = true;
    plan.linear_filter = linear_filter_ok;
    plan.layers = nly;
    plan.level_count = nlv;
    uint32_t old_d = old_tex->depth ? old_tex->depth : 1u;
    uint32_t new_d = new_tex->depth ? new_tex->depth : 1u;
    for (uint32_t i = 0; i < nlv; i++) {
        plan.regions[i] = (XcdeTexBlitLevel){
            .level = i,
            .src_w = xcde_tex_mip_dim(old_tex->width, i),
            .src_h = xcde_tex_mip_dim(old_tex->height, i),
            .src_d = xcde_tex_mip_dim(old_d, i),
            .dst_w = xcde_tex_mip_dim(new_tex->width, i),
            .dst_h = xcde_tex_mip_dim(new_tex->height, i),
            .dst_d = xcde_tex_mip_dim(new_d, i),
        };
    }
    return plan;
}

static inline bool xcde_sampler_wants_lod_bias(bool has_tex, bool gpu_owned, bool presented,
                                               uint32_t tex_levels, bool force_all) {
    return force_all || (has_tex && !gpu_owned && !presented && tex_levels > 1u);
}

static inline int xcde_sampler_bias_bucket(bool apply_bias, uint32_t min_filter, bool compare_mode,
                                           uint32_t reduction, float lod_bias, bool forced_override,
                                           int upscale_quality, float *out_bias) {
    if (out_bias) *out_bias = 0.0f;
    if (!apply_bias || min_filter <= 1u || compare_mode || reduction != 0u || lod_bias == 0.0f)
        return 0;
    if (out_bias) *out_bias = lod_bias;
    if (forced_override) return 5;
    switch (upscale_quality) {
    case UQ_QUALITY: return 1;
    case UQ_BALANCED: return 2;
    case UQ_PERFORMANCE: return 3;
    case UQ_ULTRA: return 4;
    default: return 5;
    }
}

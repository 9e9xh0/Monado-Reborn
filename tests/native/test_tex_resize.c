// [XCDE] #172: scalable resize preservation, new color render-target clear, and material-only LOD bias policy.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../runtime/nvn/vulkan/tex_resize.h"

int main(void) {
    // 1. Dynamic resolution steps up while a menu samples a persistent GPU-owned backdrop (single mip and mipmapped).
    XcdeTexDesc old_single = {1024, 576, 1, 1, 1, 1, true, true};
    XcdeTexDesc new_single = {1280, 720, 1, 1, 1, 1, true, true};
    XcdeTexResizePlan p = xcde_tex_plan_resize(&old_single, &new_single, true, true, true, true);
    assert(p.preserve);
    assert(p.linear_filter);
    assert(!p.invalidate_upload);
    assert(p.layers == 1 && p.level_count == 1);
    assert(p.regions[0].level == 0);
    assert(p.regions[0].src_w == 1024 && p.regions[0].src_h == 576 && p.regions[0].src_d == 1);
    assert(p.regions[0].dst_w == 1280 && p.regions[0].dst_h == 720 && p.regions[0].dst_d == 1);

    // Multi-mip pyramid (e.g. 1088x612 -> 1280x720, matching mips and mismatched mip count).
    XcdeTexDesc old_mip = {1088, 612, 1, 5, 1, 1, true, true};
    XcdeTexDesc new_mip = {1280, 720, 1, 6, 1, 1, true, true};
    p = xcde_tex_plan_resize(&old_mip, &new_mip, true, true, true, true);
    assert(p.preserve && p.linear_filter && p.level_count == 5);
    assert(p.regions[4].src_w == 68 && p.regions[4].src_h == 38);
    assert(p.regions[4].dst_w == 80 && p.regions[4].dst_h == 45);

    // Mip dimensions clamp to 1x1 at the tail and level count clamps at XCDE_TEX_MAX_BLIT_LEVELS.
    XcdeTexDesc old_many = {65536, 32768, 1, 18, 1, 1, true, true};
    XcdeTexDesc new_many = {65536, 32768, 1, 17, 1, 1, true, true};
    p = xcde_tex_plan_resize(&old_many, &new_many, true, true, true, true);
    assert(p.preserve && p.level_count == XCDE_TEX_MAX_BLIT_LEVELS);
    assert(xcde_tex_mip_dim(1024, 15) == 1 && xcde_tex_mip_dim(1024, 32) == 1 && xcde_tex_mip_dim(0, 0) == 0);

    // Integer / non-linear-filterable color format preserves contents with nearest filter.
    p = xcde_tex_plan_resize(&old_single, &new_single, true, true, true, false);
    assert(p.preserve && !p.linear_filter);

    // Unblittable, depth, multisampled, or opt-out must not schedule a blit.
    assert(!xcde_tex_plan_resize(&old_single, &new_single, false, true, true, true).preserve);
    assert(!xcde_tex_plan_resize(&old_single, &new_single, true, false, true, true).preserve);
    assert(!xcde_tex_plan_resize(&old_single, &new_single, true, true, false, true).preserve);
    XcdeTexDesc depth = {1024, 576, 1, 1, 1, 1, false, true};
    assert(!xcde_tex_plan_resize(&depth, &new_single, true, true, true, true).preserve);
    assert(!xcde_tex_plan_resize(&old_single, &depth, true, true, true, true).preserve);
    XcdeTexDesc msaa = {1024, 576, 1, 1, 1, 4, true, true};
    assert(!xcde_tex_plan_resize(&msaa, &new_single, true, true, true, true).preserve);
    assert(!xcde_tex_plan_resize(&old_single, &msaa, true, true, true, true).preserve);

    // CPU-backed scalable texture invalidates upload stamps so guest contents re-upload at the new size.
    XcdeTexDesc cpu_old = {1024, 576, 1, 1, 1, 1, true, false};
    p = xcde_tex_plan_resize(&cpu_old, &new_single, true, true, true, true);
    assert(!p.preserve && p.invalidate_upload);

    // 2. New color render-target zero-clear gate.
    assert(xcde_tex_should_clear_new_color(false, 1, true, true, false, 0, true));
    assert(xcde_tex_should_clear_new_color(false, 1, true, false, true, 0, true));
    assert(xcde_tex_should_clear_new_color(false, 1, true, false, false, 8, true));
    assert(!xcde_tex_should_clear_new_color(false, 1, true, false, false, 0, true));   // CPU material asset
    assert(!xcde_tex_should_clear_new_color(false, 1, false, false, false, 8, true));  // non-renderable format
    assert(!xcde_tex_should_clear_new_color(true, 1, true, true, true, 8, true));      // depth uses depth clear
    assert(!xcde_tex_should_clear_new_color(false, 4, true, true, true, 8, true));     // MSAA skipped
    assert(!xcde_tex_should_clear_new_color(false, 1, true, true, true, 8, false));    // disabled via env

    // 3. Sampler LOD bias: material textures only, and bucketed by quality / override.
    assert(xcde_sampler_wants_lod_bias(true, false, false, 9, false));    // uploaded material texture
    assert(!xcde_sampler_wants_lod_bias(true, true, false, 9, false));    // GPU-owned RT pyramid (menu blur)
    assert(!xcde_sampler_wants_lod_bias(true, false, true, 9, false));    // presented window image
    assert(!xcde_sampler_wants_lod_bias(true, false, false, 1, false));   // single-mip texture
    assert(!xcde_sampler_wants_lod_bias(false, false, false, 0, false));  // unbound / dummy
    assert(xcde_sampler_wants_lod_bias(true, true, true, 1, true));       // XCDE_TMP_LOD_BIAS_ALL=1

    float bias = -1.0f;
    assert(xcde_sampler_bias_bucket(false, 5, false, 0, -0.766f, false, UQ_BALANCED, &bias) == 0 && bias == 0.0f);
    assert(xcde_sampler_bias_bucket(true, 1, false, 0, -0.766f, false, UQ_BALANCED, &bias) == 0 && bias == 0.0f);
    assert(xcde_sampler_bias_bucket(true, 5, true, 0, -0.766f, false, UQ_BALANCED, &bias) == 0 && bias == 0.0f);
    assert(xcde_sampler_bias_bucket(true, 5, false, 1, -0.766f, false, UQ_BALANCED, &bias) == 0 && bias == 0.0f);
    assert(xcde_sampler_bias_bucket(true, 5, false, 0, 0.0f, false, UQ_NATIVE, &bias) == 0 && bias == 0.0f);
    assert(xcde_sampler_bias_bucket(true, 5, false, 0, -0.585f, false, UQ_QUALITY, &bias) == 1 && fabsf(bias + 0.585f) < 1e-6f);
    assert(xcde_sampler_bias_bucket(true, 5, false, 0, -0.766f, false, UQ_BALANCED, &bias) == 2 && fabsf(bias + 0.766f) < 1e-6f);
    assert(xcde_sampler_bias_bucket(true, 5, false, 0, -1.0f, false, UQ_PERFORMANCE, &bias) == 3 && fabsf(bias + 1.0f) < 1e-6f);
    assert(xcde_sampler_bias_bucket(true, 5, false, 0, -1.585f, false, UQ_ULTRA, &bias) == 4 && fabsf(bias + 1.585f) < 1e-6f);
    assert(xcde_sampler_bias_bucket(true, 5, false, 0, -0.5f, true, UQ_QUALITY, &bias) == 5 && fabsf(bias + 0.5f) < 1e-6f);

    puts("tex resize policy: preservation, color RT clear, and material-only LOD bias passed");
    return 0;
}

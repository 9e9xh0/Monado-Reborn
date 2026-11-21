// [XCDE] T-GRAPHICS-SCALABILITY: shared typed controls and persistence.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// [XCDE] T275: timestamped diagnostics log (runtime/log.c). The launcher, which shares this header, provides a
// plain passthrough so the two retired-value warnings below keep their destination.
#ifdef __cplusplus
extern "C" {
#endif
void rt_logf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif
enum { GFX_SHADOWS, GFX_SHADOW_MASK, GFX_EFFECTS_RES, GFX_CLOUD, GFX_AO,
       GFX_REFLECTIONS, GFX_BLOOM, GFX_GODRAYS, GFX_SHAFTS, GFX_MOTION_BLUR,
       GFX_FUR_BLUR, GFX_FILTERING, GFX_SHADOW_MAPS, GFX_PROP_LOD, GFX_COUNT };
typedef struct { int value[GFX_COUNT]; } GraphicsOptions;
static const char *const gfx_keys[GFX_COUNT] = {"shadows", "shadow_mask", "effects_resolution", "cloud_shadows", "ambient_occlusion",
    "reflections", "bloom", "godrays", "light_shafts", "motion_blur", "fur_blur", "texture_filtering", "shadow_map_resolution", "prop_lod"};
static const char *const gfx_labels[GFX_COUNT] = {"Shadows", "Shadow mask (restart)", "Effects resolution (restart)", "Cloud shadows", "Ambient occlusion",
    "Screen-space reflections", "Bloom", "God rays", "Light shafts", "Motion blur", "Fur blur", "Texture filtering", "Shadow maps (restart)", "Tree / rock LOD (restart)"};
// Retired effects-resolution Full (2026-11-12): it set TransReduction=off, which the game never shipped. The
// occlusion boxes then test a full-size depth whose only content is a 640x360 quarter: field objects popped in and
// out on camera pans (prologue). Only Original remains; the slot stays so indices and live packing are unchanged.
// [XCDE] #125: ambient occlusion has a 4th choice, Classic: the game's per-frame SSAO instead of its temporal one (TSSAO
// keeps the darkening of thin things - ropes, railings - in its history: a faint second rope trails them while moving).
enum { GFX_AO_CLASSIC = 3 };
static inline int gfx_count(int i) { return i==GFX_EFFECTS_RES ? 1 : i==GFX_PROP_LOD || i==GFX_AO ? 4 : i==GFX_FILTERING ? 5 : i==GFX_SHADOWS || i==GFX_SHADOW_MASK || i==GFX_SHADOW_MAPS ? 2 : 3; }
// Retired 4096 choice (index 2) exceeded the native render-pool layout.
// Normalize persisted/in-memory legacy choices to the supported 2048 mode.
static inline int gfx_normalize(int i,int v) {
    if(i==GFX_SHADOW_MAPS && v==2)return 1;
    return v>=0 && v<gfx_count(i) ? v : 0;
}
static inline const char *gfx_choice(int i,int v) {
    static const char *const tri[]={"original","off","on"}, *const full[]={"original","full"}, *const af[]={"original","2x","4x","8x","16x"};
    v=gfx_normalize(i,v);
    if(i==GFX_SHADOW_MAPS) { static const char *const x[]={"original","2048"};return x[v]; }
    if(i==GFX_PROP_LOD) { static const char *const x[]={"original","low","high","ultra"};return x[v]; }
    if(i==GFX_AO) { static const char *const x[]={"original","off","on","classic"};return x[v]; }
    return i==GFX_FILTERING ? af[v] : i==GFX_SHADOW_MASK || i==GFX_EFFECTS_RES ? full[v] : tri[v];
}
static inline const char *gfx_display(int i,int v) {
    static const char *const tri[]={"Original","Off","On"}, *const full[]={"Original","Full resolution"}, *const af[]={"Original","2x","4x","8x","16x"};
    v=gfx_normalize(i,v);
    if(i==GFX_SHADOW_MAPS) { static const char *const x[]={"Original (1024 px)","High (2048 px)"};return x[v]; }
    if(i==GFX_PROP_LOD) { static const char *const x[]={"Original (1x)","Low (0.5x)","High (1.5x)","Ultra (2x)"};return x[v]; }
    if(i==GFX_AO) { static const char *const x[]={"Original","Off","On","Classic, no trails (restart)"};return x[v]; }
    return i==GFX_FILTERING ? af[v] : i==GFX_SHADOW_MASK || i==GFX_EFFECTS_RES ? full[v] : tri[v];
}
static inline int gfx_load_key(GraphicsOptions *o,const char *key,const char *value) {
    for(int i=0;i<GFX_COUNT;i++) if(!strcmp(key,gfx_keys[i])) {
        o->value[i]=0;
        if(i==GFX_SHADOW_MAPS && !strcmp(value,"4096")) {
            o->value[i]=1;
            rt_logf("[graphics] 4096px shadow maps exceed the supported native render-pool layout; using 2048px\n");
            return 1;
        }
        if(i==GFX_EFFECTS_RES && !strcmp(value,"full")) {
            rt_logf("[graphics] full effects resolution is no longer offered (it broke the game's occlusion culling); using original\n");
            return 1;
        }
        for(int v=0;v<gfx_count(i);v++) if(!strcmp(value,gfx_choice(i,v)))o->value[i]=v;
        return 1;
    }
    return 0;
}
static inline void gfx_save(FILE *f,const GraphicsOptions *o) {
    for(int i=0;i<GFX_COUNT;i++)fprintf(f,"%s = %s\n",gfx_keys[i],gfx_choice(i,o->value[i]));
}
static inline uint64_t gfx_pack(const GraphicsOptions *o) {
    uint64_t b=0;
    for(int i=0;i<GFX_COUNT;i++) { int v=o->value[i]; v=gfx_normalize(i,v); b|=(uint64_t)v<<(3*i); }
    return b;
}
static inline int gfx_value(uint64_t b,int i) { return (int)((b>>(3*i))&7); }
// Only the render frame's working copy is changed; the authored config/mod stays intact.
static inline void gfx_overlay(uint8_t *p,uint64_t b) {
    static const unsigned offsets[]={0,0,0,0x3ad,0x17c,0x492,0x3c,0xec,0x520,0x204,0x53d};
    for(int i=GFX_CLOUD;i<=GFX_FUR_BLUR;i++) {
        int v=gfx_value(b,i); if(v==1 || v==2)p[offsets[i]]=(uint8_t)(v==2);
    }
    if(gfx_value(b,GFX_AO)==1)p[0x19c]=0; // disable classic AO as well, preserving it for Original.
    if(gfx_value(b,GFX_AO)==GFX_AO_CLASSIC) { p[0x17c]=0; p[0x19c]=1; } // #125 (also lib_nx at boot: romfs_virtual.c)
    if(gfx_value(b,GFX_SHADOWS)==1) {
        const float zero=0;
        const unsigned f[]={0x368,0x47c,0x2b0,0x2b4};
        for(unsigned i=0;i<sizeof(f)/sizeof(*f);i++)memcpy(p+f[i],&zero,4);
        p[0x3ad]=0; // Shadows Off also suppresses cloud-shadow contribution.
    }
}
#ifdef __cplusplus
extern "C" {
#endif
void xcde_graphics_publish(const GraphicsOptions *o);
void xcde_graphics_taa_off(bool off);  // the game's temporal AA off this frame (runtime/hle/graphics.c)
void xcde_graphics_copy(uint64_t destination);
void xcde_graphics_forget(uint64_t destination);
void xcde_graphics_begin(uint64_t destination);
void xcde_graphics_finish(uint64_t destination);
uint32_t xcde_shadow_map_size(void);
void xcde_prop_lod(uint64_t data,uint32_t size);
#ifdef __cplusplus
}
#endif

// [XCDE] Spanish labels and values for the bilingual launcher / F1 menu.
static const char *const gfx_labels_es[GFX_COUNT] = {"Sombras", "Máscara de sombras (al reiniciar)", "Resolución de efectos (al reiniciar)",
    "Sombras de nubes", "Oclusión ambiental", "Reflejos en pantalla", "Bloom", "Rayos de luz (god rays)", "Haces de luz",
    "Desenfoque de movimiento", "Desenfoque del pelo", "Filtrado de texturas", "Mapas de sombras (al reiniciar)",
    "LOD de árboles / rocas (al reiniciar)"};
static inline const char *gfx_label(int i, int en) { return en ? gfx_labels[i] : gfx_labels_es[i]; }
static inline const char *gfx_display_lang(int i, int v, int en) {
    if (en) return gfx_display(i, v);
    static const char *const tri[] = {"Original", "Desactivado", "Activado"}, *const full[] = {"Original", "Resolución completa"};
    v = gfx_normalize(i, v);
    if (i == GFX_SHADOW_MAPS) { static const char *const x[] = {"Original (1024 px)", "Alta (2048 px)"}; return x[v]; }
    if (i == GFX_PROP_LOD) { static const char *const x[] = {"Original (1x)", "Bajo (0,5x)", "Alto (1,5x)", "Ultra (2x)"}; return x[v]; }
    if (i == GFX_AO) { static const char *const x[] = {"Original", "Desactivado", "Activado", "Clásica, sin estelas (al reiniciar)"}; return x[v]; }
    if (i == GFX_FILTERING) return gfx_display(i, v);
    return i == GFX_SHADOW_MASK || i == GFX_EFFECTS_RES ? full[v] : tri[v];
}
// [XCDE] The launcher and F1 show every option once, grouped as below (Monado-Reborn's layout). The old
// "experimental cosmetic reductions" checkboxes (xcde.ini effects_off, a 5-bit mask: TSSAO, SSR, bloom, god rays,
// cloud shadows) were a second switch for the same effects - two "ambient occlusion" controls in two places. They
// are folded into these choices at load (gfx_fold_effects_off). All five apply live (F1) in both directions: checked
// in deterministic captures with XCDE_GRAPHICS_LIVE (hle/graphics.c), and Off skips the passes, not just their output.
static const int gfx_ui_shadows[] = {GFX_SHADOWS, GFX_SHADOW_MAPS, GFX_SHADOW_MASK, GFX_CLOUD};
static const int gfx_ui_detail[] = {GFX_PROP_LOD, GFX_FILTERING};
static const int gfx_ui_effects[] = {GFX_AO, GFX_REFLECTIONS, GFX_BLOOM, GFX_GODRAYS, GFX_SHAFTS, GFX_MOTION_BLUR, GFX_FUR_BLUR};
// effects_off bit i -> the option it switched off (quality_policy.h EFFECT_* order).
static const int gfx_effect_bits[5] = {GFX_AO, GFX_REFLECTIONS, GFX_BLOOM, GFX_GODRAYS, GFX_CLOUD};
// A legacy bit turns its option Off unless the user already chose something other than Original; the mask is cleared.
static inline void gfx_fold_effects_off(GraphicsOptions *o, unsigned *effects_off) {
    for (int i = 0; i < 5; i++)
        if ((*effects_off & (1u << i)) && o->value[gfx_effect_bits[i]] == 0) o->value[gfx_effect_bits[i]] = 1;
    *effects_off = 0;
}

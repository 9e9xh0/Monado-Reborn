// [XCDE] T-GRAPHICS-SCALABILITY: atomic UI -> guest render-configuration copy.
#include "../graphics_options.h"
#include "../hle.h"
#include <stdatomic.h>
#include <pthread.h>
#include "../prop_lod.h"
static _Atomic uint64_t request;
void xcde_graphics_publish(const GraphicsOptions *o) { atomic_store_explicit(&request,gfx_pack(o),memory_order_release); }
// Per-view configurations can be reused without another memcpy. Restore only
// the fields our previous overlay actually changed, before native camera setup.
// A new native copy supersedes that saved state. This makes On/Off -> Original
// reversible while preserving camera restrictions and authored/mod values.
// (the last field, 0x111, is the game's temporal AA flag (lib_nx.ini tmaa): see xcde_game_taa_off below)
static const unsigned overlay_offsets[]={0x3ad,0x17c,0x19c,0x492,0x3c,0xec,0x520,0x204,0x53d,0x368,0x47c,0x2b0,0x2b4,0x111};
enum { OVERLAY_FIELDS = sizeof overlay_offsets/sizeof *overlay_offsets };
static unsigned overlay_width(unsigned field) { return field<9 || field==13 ? 1 : 4; }
_Static_assert(OVERLAY_FIELDS<=32,"overlay field mask");
typedef struct { uint64_t address; uint32_t changed; uint8_t before[OVERLAY_FIELDS][4],after[OVERLAY_FIELDS][4]; } GraphicsView;

// [XCDE] The game's temporal AA, live. Its on/off flag is byte 0x111 of this configuration copy (tmaa=on 1 / off 0: the
// only byte that differs). Cleared here per frame, the game drops its TAA resolve pass at once and the image is the
// same as a tmaa=off boot; set again (also after a tmaa=off boot), the resolve comes back the same as a tmaa=on boot
// (both byte-identical in field and menu captures) - it keeps its buffers either way. Off for xcde.ini taa = 0 (F1)
// and under a temporal upscaler (FSR 3 / FSR 4 Lite / DLSS do the anti-aliasing; the game's TAA would blur their
// jittered input, GitHub #14), switched with the upscaler too. This replaces the old boot-time lib_nx.ini tmaa=off edit.
// The renderer publishes the decision every presented frame (runtime/nvn/vulkan/device.c), so this file needs neither
// the settings nor the Vulkan side (the native tests link it alone).
static _Atomic bool taa_off;
void xcde_graphics_taa_off(bool off) { atomic_store_explicit(&taa_off,off,memory_order_release); }
static bool xcde_game_taa_off(void) { return atomic_load_explicit(&taa_off,memory_order_acquire); }
static GraphicsView views[128];
static pthread_mutex_t view_lock=PTHREAD_MUTEX_INITIALIZER;
RT_ON_RESET(graphics_view_reset) { memset(views,0,sizeof views); }
static int valid_view(uint64_t a) { return a>=0x7000000000ull && a<=0x7320000000ull-0x1220 && !(a&7); }
static GraphicsView *find_view(uint64_t a,int create) {
    GraphicsView *empty=NULL;
    for(unsigned i=0;i<sizeof views/sizeof *views;i++) {
        if(views[i].address==a)return &views[i];
        if(!views[i].address && !empty)empty=&views[i];
    }
    if(create && empty) { empty->address=a; return empty; }
    return NULL;
}
void xcde_graphics_copy(uint64_t destination) {
    if(!valid_view(destination))return;
    pthread_mutex_lock(&view_lock);
    GraphicsView *v=find_view(destination,0);
    if(v)memset(v,0,sizeof *v); // the fresh native copy already replaced our old overlay
    pthread_mutex_unlock(&view_lock);
}
void xcde_graphics_forget(uint64_t destination) { xcde_graphics_copy(destination); }
void xcde_graphics_begin(uint64_t destination) {
    if(!valid_view(destination))return;
    pthread_mutex_lock(&view_lock);
    GraphicsView *v=find_view(destination,0);
    uint8_t *p=(uint8_t *)(uintptr_t)destination;
    if(v)for(unsigned i=0;i<OVERLAY_FIELDS;i++)
        if((v->changed&(1u<<i)) && !memcmp(p+overlay_offsets[i],v->after[i],overlay_width(i)))
            memcpy(p+overlay_offsets[i],v->before[i],overlay_width(i));
    if(v)v->changed=0;
    pthread_mutex_unlock(&view_lock);
}
void xcde_graphics_finish(uint64_t destination) {
    if(!valid_view(destination))return;
    uint64_t bits=atomic_load_explicit(&request,memory_order_acquire);
    uint8_t *p=(uint8_t *)(uintptr_t)destination;
    pthread_mutex_lock(&view_lock);
    GraphicsView *v=find_view(destination,1);
    if(v) {
        for(unsigned i=0;i<OVERLAY_FIELDS;i++)memcpy(v->before[i],p+overlay_offsets[i],overlay_width(i));
        gfx_overlay(p,bits);
        if(xcde_game_taa_off())p[0x111]=0;
        v->changed=0;
        for(unsigned i=0;i<OVERLAY_FIELDS;i++)
            {
                memcpy(v->after[i],p+overlay_offsets[i],overlay_width(i));
                if(memcmp(v->before[i],v->after[i],overlay_width(i)))v->changed|=1u<<i;
            }
    }
    pthread_mutex_unlock(&view_lock);
    if(!getenv("XCDE_GRAPHICS_TRACE"))return;
    static uint64_t last=UINT64_MAX;
    if(bits==last)return;
    last=bits;
    rt_logf("[graphics-final] options=%llx cloud=%u tssao=%u ssao=%u ssr=%u bloom=%u godrays=%u shafts=%u motion=%u fur=%u taa=%u\n",
        (unsigned long long)bits,p[0x3ad],p[0x17c],p[0x19c],p[0x492],p[0x3c],p[0xec],p[0x520],p[0x204],p[0x53d],p[0x111]);
}

// Size and LOD choices are latched on first use: UI changes require a restart,
// keeping allocations, map dimensions and newly streamed groups consistent.
static _Atomic unsigned shadow_size;
uint32_t xcde_shadow_map_size(void) {
    unsigned size=atomic_load_explicit(&shadow_size,memory_order_acquire);
    if(!size) {
        int choice=gfx_value(atomic_load_explicit(&request,memory_order_acquire),GFX_SHADOW_MAPS);
        unsigned wanted=choice==1 || choice==2 ? 2048u : 1024u, empty=0;
        atomic_compare_exchange_strong_explicit(&shadow_size,&empty,wanted,memory_order_release,memory_order_acquire);
        size=atomic_load_explicit(&shadow_size,memory_order_acquire);
    }
    return size;
}
static int prop_choice=-1;
typedef struct { uint64_t addr,hash;uint32_t size; } PropSeen;
static PropSeen *prop_seen;
static size_t prop_count;
static pthread_mutex_t prop_mutex=PTHREAD_MUTEX_INITIALIZER;
static void prop_lod_locked(uint64_t data,uint32_t size) {
    if(prop_choice<0)prop_choice=gfx_value(atomic_load_explicit(&request,memory_order_acquire),GFX_PROP_LOD);
    if(!prop_choice || data<0x7000000000ull || size<0x70 || size>64u*1024*1024 || data>0x7320000000ull-size)return;
    uint8_t *p=(uint8_t *)(uintptr_t)data;
    uint64_t hash=prop_lod_hash(p,size);
    if(!hash)return;
    size_t slot=0;for(;slot<prop_count;slot++)if(prop_seen[slot].addr==data)break;
    if(slot<prop_count && prop_seen[slot].size==size && prop_seen[slot].hash==hash)return;
    static const float scales[]={1,.5f,1.5f,2};
    if(slot==prop_count) {
        PropSeen *more=realloc(prop_seen,(prop_count+1)*sizeof(*more));
        if(!more)return;prop_seen=more;prop_count++;
    }
    prop_seen[slot]=(PropSeen){data,hash,size};
    unsigned changed=prop_lod_adjust(p,size,scales[prop_choice>=1 && prop_choice<=3 ? prop_choice : 0]);
    if(changed) {
        prop_seen[slot].addr=data;prop_seen[slot].size=size;prop_seen[slot].hash=prop_lod_hash(p,size);
        rt_logf("[prop-lod] %u intermediate thresholds adjusted; final distances preserved\n",changed);
    }
}

void xcde_prop_lod(uint64_t data,uint32_t size) {
    pthread_mutex_lock(&prop_mutex);prop_lod_locked(data,size);pthread_mutex_unlock(&prop_mutex);
}

// [XCDE] Live-toggle test hook: XCDE_GRAPHICS_LIVE="<frame>:<key>=<value>[;<key>=<value>],<frame>:..." publishes graphics
// choices at those presented frames (keys/values as in xcde.ini, e.g. 2900:bloom=off,3000:bloom=original), the same
// path the F1 menu takes. With XCDE_DETERMINISTIC + XCDE_SHOTS it checks an option really applies while playing.
void xcde_graphics_debug_frame(uint64_t frame) {
    static int parsed; static char spec[1024];
    if (!parsed) { parsed = 1; const char *e = getenv("XCDE_GRAPHICS_LIVE"); snprintf(spec, sizeof spec, "%s", e ? e : ""); }
    if (!*spec) return;
    for (const char *tok = spec; *tok; ) {  // (hand-split: strtok_r is not in C11 nor in every Windows CRT)
        size_t len = strcspn(tok, ",");
        char item[256]; snprintf(item, sizeof item, "%.*s", (int)(len < sizeof item ? len : sizeof item - 1), tok);
        tok += len + (tok[len] == ',');
        char *colon = strchr(item, ':');
        if (!colon || strtoull(item, NULL, 10) != frame) continue;
        GraphicsOptions o = {0};
        uint64_t bits = atomic_load_explicit(&request, memory_order_acquire);
        for (int i = 0; i < GFX_COUNT; i++) o.value[i] = gfx_value(bits, i);
        for (char *kv = colon + 1; kv && *kv; ) {
            char *next = strchr(kv, ';'); if (next) *next++ = 0;
            char *eq = strchr(kv, '=');
            if (eq) { *eq = 0; gfx_load_key(&o, kv, eq + 1); }
            kv = next;
        }
        xcde_graphics_publish(&o);
        rt_logf("[graphics-live] frame %llu: options=%llx\n", (unsigned long long)frame, (unsigned long long)gfx_pack(&o));
    }
}

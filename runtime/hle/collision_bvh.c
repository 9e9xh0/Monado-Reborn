// [XCDE] Native C replacements for the hot static map collision-BVH traversal and AABB tests
// (generated/main/main_172.c: f_7000734320, f_7000732cd0, f_7000733930).
// Disable at runtime with XCDE_COL_BVH=0; verify against lifted code with XCDE_COL_BVH_VERIFY=1.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../platform.h"
#include "../rt.h"

#define ADDR_AABB_REJECT     0x7000734320ull
#define ADDR_SEG_AABB_REJECT 0x7000732cd0ull
#define ADDR_BVH_TRAVERSE    0x7000733930ull
#define ADDR_RAY_POLY        0x700072f7b0ull
#define ADDR_SWEEP_POLY      0x7000730d80ull
#define ADDR_CAPSULE_POLY    0x7000730490ull
#define ADDR_SEG_SEG         0x7000726950ull
#define ADDR_T_RANGE_01      0x7000731960ull
#define ADDR_T_RANGE_LOHI    0x7000731980ull
#define ADDR_T_RANGE_ABS     0x70007319b0ull
#define ADDR_T_RANGE_0HI     0x70007319d0ull

static int g_verify = -1;

static uint32_t col_aabb_reject(uint64_t ctx, uint64_t p_max, uint64_t p_min) {
    const float *q = (const float *)(uintptr_t)(*(const uint64_t *)(uintptr_t)(ctx + 0x18));
    const float *n_max = (const float *)(uintptr_t)p_max;
    const float *n_min = (const float *)(uintptr_t)p_min;
    return (n_min[0] > q[0] || n_min[1] > q[1] || n_min[2] > q[2] ||
            q[4] > n_max[0] || q[5] > n_max[1] || q[6] > n_max[2]) ? 1u : 0u;
}

static uint32_t col_seg_aabb_reject(uint64_t ctx, uint64_t p_max, uint64_t p_min) {
    const float *q = (const float *)(uintptr_t)(*(const uint64_t *)(uintptr_t)(ctx + 0x18));
    const float *n_max = (const float *)(uintptr_t)p_max;
    const float *n_min = (const float *)(uintptr_t)p_min;
    if (n_min[0] > q[0] || n_min[1] > q[1] || n_min[2] > q[2] ||
        q[4] > n_max[0] || q[5] > n_max[1] || q[6] > n_max[2]) {
        return 1u;
    }

    float hx = (n_max[0] - n_min[0]) * 0.5f;
    float hy = (n_max[1] - n_min[1]) * 0.5f;
    float hz = (n_max[2] - n_min[2]) * 0.5f;

    float dx = q[16] - (n_min[0] + hx);
    float dy = q[17] - (n_min[1] + hy);
    float dz = (-n_min[2] - hz) + q[18];

    float dir_x = q[20], dir_y = q[21], dir_z = q[22];
    float abs_x = q[24], abs_y = q[25], abs_z = q[26];

    if (!(fabsf(dir_z * dy - dz * dir_y) <= fmaf(abs_y, hz, abs_z * hy))) return 1u;
    if (!(fabsf(dir_x * dz - dir_z * dx) <= fmaf(abs_x, hz, abs_z * hx))) return 1u;
    return (fabsf(dx * dir_y - dir_x * dy) <= fmaf(abs_x, hy, abs_y * hx)) ? 0u : 1u;
}

static inline uint64_t resolve_pmf(uint64_t self, uint64_t off, uint64_t *out_this) {
    uint64_t ptr = *(const uint64_t *)(uintptr_t)(self + off);
    int64_t adj = *(const int64_t *)(uintptr_t)(self + off + 8);
    uint64_t adj_this = self + (uint64_t)(adj >> 1);
    *out_this = adj_this;
    if (adj & 1) {
        uint64_t vtable = *(const uint64_t *)(uintptr_t)adj_this;
        return *(const uint64_t *)(uintptr_t)(vtable + ptr);
    }
    return ptr;
}

static inline uint32_t call_guest_1(Cpu *c, uint64_t target, uint64_t x0, uint32_t x1) {
    c->x[0] = x0;
    c->x[1] = x1;
    GuestFn fn = dispatch_lookup(target);
    if (__builtin_expect(fn != NULL, 1)) {
        c->x[30] = 0;
        fn(c);
    } else {
        c->pc = target;
        c->x[30] = 0;
        rt_dispatch(c);
    }
    return (uint32_t)c->x[0];
}

static void verify_against_guest(const Cpu *orig_in, uint64_t addr, uint32_t native_res, const char *name) {
    Cpu tmp = *orig_in;
    GuestFn fn = dispatch_lookup(addr);
    if (!fn) return;
    tmp.x[30] = 0;
    fn(&tmp);
    uint32_t guest_res = (uint32_t)(tmp.x[0] & 1u);
    if (guest_res != (native_res & 1u)) {
        FATAL("[col_bvh] VERIFY MISMATCH in %s (%#llx): native=%u guest=%u (x0=%llx x1=%llx x2=%llx)\n",
              name, (unsigned long long)addr, native_res & 1u, guest_res,
              (unsigned long long)orig_in->x[0], (unsigned long long)orig_in->x[1],
              (unsigned long long)orig_in->x[2]);
    }
}

static inline int col_edge_hit(float px, float py, float pz, const float *a, const float *b, float eps_sq) {
    float ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2];
    float apx = px - a[0], apy = py - a[1], apz = pz - a[2];
    float ee = fmaf(ez, ez, ex * ex) + ey * ey;
    float ae = fmaf(ex, apx, apz * ez) + ey * apy;
    float s = (ee > 0.0f) ? (ae / ee) : 0.0f;
    if (s < 0.0f || !(s == s)) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    float dx = ex * s - apx, dy = ey * s - apy, dz = ez * s - apz;
    float dist_sq = fmaf(dz, dz, dx * dx) + dy * dy;
    return dist_sq <= eps_sq;
}

static inline uint32_t col_check_pmf(Cpu *c, uint64_t self, uint64_t pmf_off, float val, float nx, float ny, float nz) {
    uint64_t t_this = 0;
    uint64_t t_fn = resolve_pmf(self, pmf_off, &t_this);
    if (t_fn == ADDR_T_RANGE_01) {
        return (val >= 0.0f && val <= 1.0f) ? 1u : 0u;
    }
    if (t_fn == ADDR_T_RANGE_LOHI) {
        uint64_t q = *(const uint64_t *)(uintptr_t)(t_this + 0x18);
        float lo = *(const float *)(uintptr_t)(q + 0x94);
        float hi = *(const float *)(uintptr_t)(q + 0x90);
        return (val >= lo && val <= hi) ? 1u : 0u;
    }
    if (t_fn == ADDR_T_RANGE_ABS) {
        uint64_t q = *(const uint64_t *)(uintptr_t)(t_this + 0x18);
        float hi = *(const float *)(uintptr_t)(q + 0x90);
        return !(fabsf(val) > hi) ? 1u : 0u;
    }
    if (t_fn == ADDR_T_RANGE_0HI) {
        uint64_t q = *(const uint64_t *)(uintptr_t)(t_this + 0x18);
        float hi = *(const float *)(uintptr_t)(q + 0x90);
        return (val >= 0.0f && val < hi) ? 1u : 0u;
    }
    c->sp -= 0x20;
    uint64_t buf = c->sp;
    uint32_t tbits, nbits[4];
    memcpy(&tbits, &val, 4);
    memcpy(&nbits[0], &nx, 4);
    memcpy(&nbits[1], &ny, 4);
    memcpy(&nbits[2], &nz, 4);
    nbits[3] = 0;
    *(uint32_t *)(uintptr_t)buf = tbits;
    memcpy((void *)(uintptr_t)(buf + 0x10), nbits, 16);
    c->x[0] = t_this;
    c->x[1] = buf;
    c->x[2] = buf + 0x10;
    c->pc = t_fn;
    c->x[30] = 0;
    rt_dispatch(c);
    c->sp += 0x20;
    return (uint32_t)(c->x[0] & 1u);
}

static inline void col_closest_on_seg(float out[3], const float *a, const float *b, const float *p) {
    float ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2];
    float apx = p[0] - a[0], apy = p[1] - a[1], apz = p[2] - a[2];
    float ee = fmaf(ez, ez, ex * ex) + ey * ey;
    float ae = fmaf(apz, ez, apx * ex) + apy * ey;
    float s = (ee > 0.0f) ? (ae / ee) : 0.0f;
    if (s < 0.0f || !(s == s)) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    out[0] = ex * s + a[0];
    out[1] = ey * s + a[1];
    out[2] = ez * s + a[2];
}

static inline void col_mark_bit(uint64_t filter, uint64_t tbl_off) {
    uint64_t tbl = *(const uint64_t *)(uintptr_t)(filter + tbl_off);
    if (tbl != 0) {
        uint32_t bit = *(const uint32_t *)(uintptr_t)(filter + 0xf0);
        uint32_t *w = (uint32_t *)(uintptr_t)(tbl + ((uint64_t)(bit >> 5) << 2));
        *w |= (1u << (bit & 31u));
    }
}

static inline int col_invoke_hit(Cpu *c, uint64_t self, uint64_t poly, uint64_t mat,
                                 float dist, const float pos[3], const float nrm[3], uint64_t bitset_off) {
    uint64_t hptr = *(const uint64_t *)(uintptr_t)(self + 56);
    int64_t hadj = *(const int64_t *)(uintptr_t)(self + 64);
    if (((hadj & 1) | hptr) != 0) {
        if (bitset_off != 0) {
            col_mark_bit(*(const uint64_t *)(uintptr_t)(self + 0x08), bitset_off);
        }
        uint64_t hit_this = 0;
        uint64_t hit_fn = resolve_pmf(self, 56, &hit_this);
        c->sp -= 0x40;
        uint64_t buf = c->sp;
        float hp[4] = {pos[0], pos[1], pos[2], 0.0f};
        float hn[4] = {nrm[0], nrm[1], nrm[2], 0.0f};
        memcpy((void *)(uintptr_t)buf, hp, 16);
        memcpy((void *)(uintptr_t)(buf + 0x10), hn, 16);
        *(uint64_t *)(uintptr_t)(buf + 0x30) = mat;
        uint32_t dbits = 0;
        memcpy(&dbits, &dist, 4);
        c->v[0] = dbits;
        c->x[0] = hit_this;
        c->x[1] = buf;
        c->x[2] = poly;
        c->pc = hit_fn;
        c->x[30] = 0;
        rt_dispatch(c);
        c->sp += 0x40;
        return 0;
    } else {
        uint64_t fobj = *(const uint64_t *)(uintptr_t)(self + 0x08);
        uint64_t cb_obj = *(const uint64_t *)(uintptr_t)(fobj + 0x110);
        if (cb_obj != 0) {
            uint64_t vt = *(const uint64_t *)(uintptr_t)cb_obj;
            uint64_t fn = *(const uint64_t *)(uintptr_t)vt;
            c->x[0] = cb_obj;
            c->x[1] = *(const uint64_t *)(uintptr_t)(fobj + 0x100);
            c->pc = fn;
            c->x[30] = 0;
            rt_dispatch(c);
        }
        return 1;
    }
}

static uint32_t col_ray_poly(Cpu *c, uint64_t self, uint32_t poly_idx) {
    uint64_t mesh = *(const uint64_t *)(uintptr_t)self;
    uint64_t poly = *(const uint64_t *)(uintptr_t)(mesh + 0x10) + (uint64_t)poly_idx * 12u;
    uint16_t mat_idx = *(const uint16_t *)(uintptr_t)(poly + 0x0a);
    uint64_t mat = *(const uint64_t *)(uintptr_t)(mesh + 0x40) + (uint64_t)mat_idx * 0x28u;
    uint32_t mat_flags = *(const uint32_t *)(uintptr_t)(mat + 0x04);
    uint64_t filter = *(const uint64_t *)(uintptr_t)(self + 0x08);
    if ((*(const uint32_t *)(uintptr_t)(filter + 0xf8) & mat_flags) != 0) return 0u;
    if ((*(const uint32_t *)(uintptr_t)(filter + 0xfc) & mat_flags) == 0) return 0u;

    uint32_t num_tris = *(const uint8_t *)(uintptr_t)(poly + 0x04);
    if (num_tris == 0) return 0u;

    uint64_t vtx_tbl = *(const uint64_t *)(uintptr_t)(mesh + 0x18);
    uint64_t nrm_tbl = *(const uint64_t *)(uintptr_t)(mesh + 0x20);
    uint64_t vtx_base = *(const uint64_t *)(uintptr_t)(vtx_tbl + ((uint64_t)*(const uint8_t *)(uintptr_t)(poly + 5) << 3));
    uint64_t nrm_base = *(const uint64_t *)(uintptr_t)(nrm_tbl + ((uint64_t)*(const uint8_t *)(uintptr_t)(poly + 6) << 3));

    uint64_t idx_ptr = *(const uint64_t *)(uintptr_t)mesh + *(const uint32_t *)(uintptr_t)poly;
    uint16_t v0_idx = *(const uint16_t *)(uintptr_t)(idx_ptr + ((uint64_t)num_tris << 1));
    const float *v0 = (const float *)(uintptr_t)(vtx_base + ((uint64_t)v0_idx << 4));
    uint64_t v_off = ((uint64_t)num_tris << 1) + 2u;

    float norm_scale = *(const float *)(uintptr_t)0x70009d3f7cull;
    float edge_eps_sq = *(const float *)(uintptr_t)0x70009d3aa8ull;

    for (uint32_t i = 0; i < num_tris; i++, idx_ptr += 2u) {
        uint16_t n_idx = *(const uint16_t *)(uintptr_t)idx_ptr;
        const int16_t *n16 = (const int16_t *)(uintptr_t)(nrm_base + (uint64_t)n_idx * 6u);
        float nx = (float)n16[0] * norm_scale;
        float ny = (float)n16[1] * norm_scale;
        float nz = (float)n16[2] * norm_scale;

        uint64_t q = *(const uint64_t *)(uintptr_t)(self + 0x18);
        const float *orig = (const float *)(uintptr_t)(q + 0x20);
        const float *dir = (const float *)(uintptr_t)(q + 0x30);

        float denom = fmaf(dir[2], nz, dir[0] * nx) + dir[1] * ny;
        if (denom == 0.0f) continue;

        float d_v0 = fmaf(v0[2], nz, v0[0] * nx) + v0[1] * ny;
        float d_orig = fmaf(orig[2], nz, orig[0] * nx) + orig[1] * ny;
        float t = (d_v0 - d_orig) / denom;
        if (!col_check_pmf(c, self, 88, t, nx, ny, nz)) continue;

        q = *(const uint64_t *)(uintptr_t)(self + 0x18);
        orig = (const float *)(uintptr_t)(q + 0x20);
        dir = (const float *)(uintptr_t)(q + 0x30);

        float px = orig[0] + dir[0] * t;
        float py = orig[1] + dir[1] * t;
        float pz = orig[2] + dir[2] * t;

        uint16_t v1_idx = *(const uint16_t *)(uintptr_t)(idx_ptr + v_off);
        uint16_t v2_idx = *(const uint16_t *)(uintptr_t)(idx_ptr + v_off + 2u);
        const float *v1 = (const float *)(uintptr_t)(vtx_base + ((uint64_t)v1_idx << 4));
        const float *v2 = (const float *)(uintptr_t)(vtx_base + ((uint64_t)v2_idx << 4));

        float ax = v0[0] - px, ay = v0[1] - py, az = v0[2] - pz;
        float bx = v1[0] - px, by = v1[1] - py, bz = v1[2] - pz;
        float cx = v2[0] - px, cy = v2[1] - py, cz = v2[2] - pz;

        float ux = fmaf(-bz, cy, cz * by), uy = fmaf(-bx, cz, cx * bz), uz = fmaf(-by, cx, cy * bx);
        float vx = fmaf(-cz, ay, az * cy), vy = fmaf(-cx, az, ax * cz), vz = fmaf(-cy, ax, ay * cx);

        int hit = 0;
        if ((fmaf(vz, uz, vx * ux) + vy * uy) >= 0.0f) {
            float wx = fmaf(-az, by, bz * ay), wy = fmaf(-ax, bz, bx * az), wz = fmaf(-ay, bx, by * ax);
            if ((fmaf(wz, uz, wx * ux) + wy * uy) >= 0.0f) {
                hit = 1;
            }
        }
        if (!hit) {
            if (col_edge_hit(px, py, pz, v0, v1, edge_eps_sq) ||
                col_edge_hit(px, py, pz, v1, v2, edge_eps_sq) ||
                col_edge_hit(px, py, pz, v2, v0, edge_eps_sq)) {
                hit = 1;
            }
        }
        if (!hit) continue;

        float hp[3] = {px, py, pz};
        float hn[3] = {nx, ny, nz};
        float dist = (*(const float *)(uintptr_t)(q + 0x98)) * t;
        col_invoke_hit(c, self, poly, mat, dist, hp, hn, 0);
        return 1u;
    }
    return 0u;
}

static uint32_t col_sweep_poly(Cpu *c, uint64_t self, uint32_t poly_idx) {
    uint64_t mesh = *(const uint64_t *)(uintptr_t)self;
    uint64_t poly = *(const uint64_t *)(uintptr_t)(mesh + 0x10) + (uint64_t)poly_idx * 12u;
    uint16_t mat_idx = *(const uint16_t *)(uintptr_t)(poly + 0x0a);
    uint64_t mat = *(const uint64_t *)(uintptr_t)(mesh + 0x40) + (uint64_t)mat_idx * 0x28u;
    uint32_t mat_flags = *(const uint32_t *)(uintptr_t)(mat + 0x04);
    uint64_t filter = *(const uint64_t *)(uintptr_t)(self + 0x08);
    if ((*(const uint32_t *)(uintptr_t)(filter + 0xf8) & mat_flags) != 0) return 0u;
    if ((*(const uint32_t *)(uintptr_t)(filter + 0xfc) & mat_flags) == 0) return 0u;

    uint32_t num_tris = *(const uint8_t *)(uintptr_t)(poly + 0x04);
    uint64_t vtx_tbl = *(const uint64_t *)(uintptr_t)(mesh + 0x18);
    uint64_t vtx_base = *(const uint64_t *)(uintptr_t)(vtx_tbl + ((uint64_t)*(const uint8_t *)(uintptr_t)(poly + 5) << 3));
    uint64_t idx_ptr = *(const uint64_t *)(uintptr_t)mesh + *(const uint32_t *)(uintptr_t)poly;
    uint64_t vtx_ptr = idx_ptr + ((uint64_t)num_tris << 1);

    float norm_scale = *(const float *)(uintptr_t)0x70009d3f7cull;
    float edge_eps_sq = *(const float *)(uintptr_t)0x70009d3aa8ull;

    uint16_t v0_idx = *(const uint16_t *)(uintptr_t)vtx_ptr;
    const float *v0 = (const float *)(uintptr_t)(vtx_base + ((uint64_t)v0_idx << 4));

    uint32_t any_hit = 0;
    uint32_t last_flag = 0;

    if (num_tris != 0) {
        uint64_t nrm_tbl = *(const uint64_t *)(uintptr_t)(mesh + 0x20);
        uint64_t nrm_base = *(const uint64_t *)(uintptr_t)(nrm_tbl + ((uint64_t)*(const uint8_t *)(uintptr_t)(poly + 6) << 3));

        for (uint32_t i = 0; i < num_tris; i++, idx_ptr += 2u) {
            uint16_t n_idx = *(const uint16_t *)(uintptr_t)idx_ptr;
            const int16_t *n16 = (const int16_t *)(uintptr_t)(nrm_base + (uint64_t)n_idx * 6u);
            float nx = (float)n16[0] * norm_scale;
            float ny = (float)n16[1] * norm_scale;
            float nz = (float)n16[2] * norm_scale;

            uint16_t v1_idx = *(const uint16_t *)(uintptr_t)(vtx_ptr + ((uint64_t)(i + 1u) << 1));
            uint16_t v2_idx = *(const uint16_t *)(uintptr_t)(vtx_ptr + ((uint64_t)(i + 2u) << 1));
            const float *v1 = (const float *)(uintptr_t)(vtx_base + ((uint64_t)v1_idx << 4));
            const float *v2 = (const float *)(uintptr_t)(vtx_base + ((uint64_t)v2_idx << 4));

            // Part 1: ray from q + 0x70, dir q + 0x30
            uint64_t q = *(const uint64_t *)(uintptr_t)(self + 0x18);
            const float *orig70 = (const float *)(uintptr_t)(q + 0x70);
            const float *dir = (const float *)(uintptr_t)(q + 0x30);
            float denom = fmaf(dir[2], nz, dir[0] * nx) + dir[1] * ny;
            if (denom != 0.0f) {
                float d_v0 = fmaf(v0[2], nz, v0[0] * nx) + v0[1] * ny;
                float d_o70 = fmaf(orig70[2], nz, orig70[0] * nx) + orig70[1] * ny;
                float t = (d_v0 - d_o70) / denom;
                if (col_check_pmf(c, self, 88, t, nx, ny, nz)) {
                    q = *(const uint64_t *)(uintptr_t)(self + 0x18);
                    orig70 = (const float *)(uintptr_t)(q + 0x70);
                    dir = (const float *)(uintptr_t)(q + 0x30);
                    float px = orig70[0] + dir[0] * t;
                    float py = orig70[1] + dir[1] * t;
                    float pz = orig70[2] + dir[2] * t;

                    float ax = v0[0] - px, ay = v0[1] - py, az = v0[2] - pz;
                    float bx = v1[0] - px, by = v1[1] - py, bz = v1[2] - pz;
                    float cx = v2[0] - px, cy = v2[1] - py, cz = v2[2] - pz;
                    float ux = fmaf(-bz, cy, cz * by), uy = fmaf(-bx, cz, cx * bz), uz = fmaf(-by, cx, cy * bx);
                    float vx = fmaf(-cz, ay, az * cy), vy = fmaf(-cx, az, ax * cz), vz = fmaf(-cy, ax, ay * cx);

                    int hit1 = 0;
                    if ((fmaf(vz, uz, vx * ux) + vy * uy) >= 0.0f) {
                        float wx = fmaf(-az, by, bz * ay), wy = fmaf(-ax, bz, bx * az), wz = fmaf(-ay, bx, by * ax);
                        if ((fmaf(wz, uz, wx * ux) + wy * uy) >= 0.0f) hit1 = 1;
                    }
                    if (!hit1) {
                        if (col_edge_hit(px, py, pz, v0, v1, edge_eps_sq) ||
                            col_edge_hit(px, py, pz, v1, v2, edge_eps_sq) ||
                            col_edge_hit(px, py, pz, v2, v0, edge_eps_sq)) {
                            hit1 = 1;
                        }
                    }
                    if (hit1) {
                        float hp1[3] = {px, py, pz};
                        float hn1[3] = {nx, ny, nz};
                        float dist1 = (*(const float *)(uintptr_t)(q + 0x98)) * t;
                        col_invoke_hit(c, self, poly, mat, dist1, hp1, hn1, 0xe0);
                        return 1u;
                    }
                }
            }

            // Part 2: sphere at q + 0x20, radius^2 at q + 0x94
            q = *(const uint64_t *)(uintptr_t)(self + 0x18);
            const float *center = (const float *)(uintptr_t)(q + 0x20);
            float d_c = fmaf(center[2], nz, center[0] * nx) + center[1] * ny;
            float d_v = fmaf(v0[2], nz, v0[0] * nx) + v0[1] * ny;
            float d = d_c - d_v;
            if (!col_check_pmf(c, self, 104, d, nx, ny, nz)) {
                last_flag = 1;
                continue;
            }

            q = *(const uint64_t *)(uintptr_t)(self + 0x18);
            center = (const float *)(uintptr_t)(q + 0x20);
            float px = center[0] - nx * d;
            float py = center[1] - ny * d;
            float pz = center[2] - nz * d;

            float ax = v0[0] - px, ay = v0[1] - py, az = v0[2] - pz;
            float bx = v1[0] - px, by = v1[1] - py, bz = v1[2] - pz;
            float cx = v2[0] - px, cy = v2[1] - py, cz = v2[2] - pz;
            float ux = fmaf(-bz, cy, cz * by), uy = fmaf(-bx, cz, cx * bz), uz = fmaf(-by, cx, cy * bx);
            float vx = fmaf(-cz, ay, az * cy), vy = fmaf(-cx, az, ax * cz), vz = fmaf(-cy, ax, ay * cx);

            if ((fmaf(vz, uz, vx * ux) + vy * uy) >= 0.0f) {
                float wx = fmaf(-az, by, bz * ay), wy = fmaf(-ax, bz, bx * az), wz = fmaf(-ay, bx, by * ax);
                if ((fmaf(wz, uz, wx * ux) + wy * uy) >= 0.0f) {
                    float hp2[3] = {px, py, pz};
                    float hn2[3] = {nx, ny, nz};
                    if (col_invoke_hit(c, self, poly, mat, fabsf(d), hp2, hn2, 0)) {
                        return 1u;
                    }
                    any_hit = 1;
                    last_flag = 1;
                    continue;
                }
            }

            float cp[3];
            col_closest_on_seg(cp, v1, v2, center);
            float dx = center[0] - cp[0], dy = center[1] - cp[1], dz = center[2] - cp[2];
            float dsq = fmaf(dz, dz, dx * dx) + dy * dy;
            float rad_sq = *(const float *)(uintptr_t)(q + 0x94);
            if (!(dsq < rad_sq)) {
                last_flag = 0;
                continue;
            }

            float hn[3] = {nx, ny, nz};
            float dist = 0.0f;
            if (!(dsq < edge_eps_sq)) {
                dist = sqrtf(dsq);
                float inv = 1.0f / dist;
                hn[0] = dx * inv;
                hn[1] = dy * inv;
                hn[2] = dz * inv;
            }
            if (col_invoke_hit(c, self, poly, mat, dist, cp, hn, 0xd8)) {
                return 1u;
            }

            q = *(const uint64_t *)(uintptr_t)(self + 0x18);
            center = (const float *)(uintptr_t)(q + 0x20);
            rad_sq = *(const float *)(uintptr_t)(q + 0x94);
            float cp2[3];
            col_closest_on_seg(cp2, v0, v1, center);
            float dx2 = center[0] - cp2[0], dy2 = center[1] - cp2[1], dz2 = center[2] - cp2[2];
            float dsq2 = fmaf(dz2, dz2, dx2 * dx2) + dy2 * dy2;
            if (dsq2 < rad_sq) {
                float hn2[3] = {hn[0], hn[1], hn[2]};
                float dist2 = 0.0f;
                if (!(dsq2 < edge_eps_sq)) {
                    dist2 = sqrtf(dsq2);
                    float inv2 = 1.0f / dist2;
                    hn2[0] = dx2 * inv2;
                    hn2[1] = dy2 * inv2;
                    hn2[2] = dz2 * inv2;
                }
                if (col_invoke_hit(c, self, poly, mat, dist2, cp2, hn2, 0xd8)) {
                    return 1u;
                }
            }
            any_hit = 1;
            last_flag = 1;
        }
    }

    // Part 3: closing edge (v0, v_last) when num_tris == 0 || last_flag == 0
    if (!last_flag) {
        uint16_t v_last_idx = *(const uint16_t *)(uintptr_t)(vtx_ptr + ((uint64_t)num_tris << 1) + 2u);
        uint16_t v1_idx = *(const uint16_t *)(uintptr_t)(vtx_ptr + 2u);
        if (v_last_idx != v1_idx) {
            const float *v_last = (const float *)(uintptr_t)(vtx_base + ((uint64_t)v_last_idx << 4));
            uint64_t q = *(const uint64_t *)(uintptr_t)(self + 0x18);
            const float *center = (const float *)(uintptr_t)(q + 0x20);
            float cp3[3];
            col_closest_on_seg(cp3, v0, v_last, center);
            float dx3 = center[0] - cp3[0], dy3 = center[1] - cp3[1], dz3 = center[2] - cp3[2];
            float dsq3 = fmaf(dz3, dz3, dx3 * dx3) + dy3 * dy3;
            if (dsq3 < *(const float *)(uintptr_t)(q + 0x94)) {
                float dist3 = sqrtf(dsq3);
                float hn3[3];
                if (dist3 < edge_eps_sq) {
                    const float *def_n = (const float *)(uintptr_t)0x70009cfc98ull;
                    hn3[0] = def_n[0];
                    hn3[1] = def_n[1];
                    hn3[2] = 0.0f;
                } else {
                    float inv3 = 1.0f / dist3;
                    hn3[0] = dx3 * inv3;
                    hn3[1] = dy3 * inv3;
                    hn3[2] = dz3 * inv3;
                }
                col_invoke_hit(c, self, poly, mat, dist3, cp3, hn3, 0xd8);
                any_hit = 1;
            }
        }
    }

    return any_hit & 1u;
}

static inline float clamp01_col(float x) {
    float r = fmaxf(x, 0.0f);
    return fminf(r, 1.0f);
}

static void col_seg_seg_native(float out1[3], float out2[3],
                               const float *p1, const float *q1,
                               const float *p2, const float *q2) {
    float d1x = q1[0] - p1[0], d1y = q1[1] - p1[1], d1z = q1[2] - p1[2];
    float d2x = q2[0] - p2[0], d2y = q2[1] - p2[1], d2z = q2[2] - p2[2];
    float nd2x = p2[0] - q2[0], nd2y = p2[1] - q2[1], nd2z = -d2z;
    float rx = p2[0] - p1[0], ry = p2[1] - p1[1], rz = p2[2] - p1[2];

    float eps = *(const float *)(uintptr_t)0x70009d3aa8ull;
    float a = fmaf(d1z, d1z, d1x * d1x) + d1y * d1y;
    float e = fmaf(d2z, d2z, d2x * d2x) + d2y * d2y;
    float neg_b = fmaf(nd2z, d1z, d1x * nd2x) + d1y * nd2y;
    float denom_b = neg_b * neg_b;
    float denom_ae = e * a;

    if (fabsf(denom_ae - denom_b) <= eps) {
        float inv_a = 1.0f / a;
        float t_p2 = inv_a * (fmaf(rz, d1z, rx * d1x) + ry * d1y);
        if (t_p2 >= 0.0f && t_p2 <= 1.0f) {
            out1[0] = p1[0] + d1x * t_p2;
            out1[1] = p1[1] + d1y * t_p2;
            out1[2] = p1[2] + d1z * t_p2;
            out2[0] = p2[0]; out2[1] = p2[1]; out2[2] = p2[2];
            return;
        }
        float inv_e = 1.0f / e;
        float s_p1 = inv_e * (fmaf(-rx, d2x, (-rz) * d2z) + (-ry) * d2y);
        if (s_p1 >= 0.0f && s_p1 <= 1.0f) {
            out2[0] = p2[0] + d2x * s_p1;
            out2[1] = p2[1] + d2y * s_p1;
            out2[2] = p2[2] + d2z * s_p1;
            out1[0] = p1[0]; out1[1] = p1[1]; out1[2] = p1[2];
            return;
        }
        float q2rx = q2[0] - p1[0], q2ry = q2[1] - p1[1], q2rz = q2[2] - p1[2];
        float t_q2 = inv_a * (fmaf(q2rz, d1z, q2rx * d1x) + q2ry * d1y);
        if (t_q2 >= 0.0f && t_q2 <= 1.0f) {
            out1[0] = p1[0] + d1x * t_q2;
            out1[1] = p1[1] + d1y * t_q2;
            out1[2] = p1[2] + d1z * t_q2;
            out2[0] = q2[0]; out2[1] = q2[1]; out2[2] = q2[2];
            return;
        }
        float q1rx = q1[0] - p2[0], q1ry = q1[1] - p2[1], q1rz = q1[2] - p2[2];
        float s_q1 = inv_e * (fmaf(q1rz, d2z, q1rx * d2x) + q1ry * d2y);
        if (s_q1 >= 0.0f && s_q1 <= 1.0f) {
            out2[0] = p2[0] + d2x * s_q1;
            out2[1] = p2[1] + d2y * s_q1;
            out2[2] = p2[2] + d2z * s_q1;
            out1[0] = q1[0]; out1[1] = q1[1]; out1[2] = q1[2];
            return;
        }
        float v18x = (t_p2 < 0.0f) ? 0.0f : d1x;
        float v18y = (t_p2 < 0.0f) ? 0.0f : d1y;
        float v18z = (t_p2 < 0.0f) ? 0.0f : d1z;
        float v4x  = (s_p1 < 0.0f) ? 0.0f : d2x;
        float v4y  = (s_p1 < 0.0f) ? 0.0f : d2y;
        float v4z  = (s_p1 < 0.0f) ? 0.0f : d2z;
        out1[0] = p1[0] + v18x; out1[1] = p1[1] + v18y; out1[2] = p1[2] + v18z;
        out2[0] = p2[0] + v4x;  out2[1] = p2[1] + v4y;  out2[2] = p2[2] + v4z;
        return;
    }

    float dot_r_nd2 = fmaf(nd2z, rz, rx * nd2x) + ry * nd2y;
    float dot_r_d1 = fmaf(rz, d1z, rx * d1x) + ry * d1y;
    float s_num = fmaf(-neg_b, dot_r_d1, a * dot_r_nd2);
    float s = s_num / (denom_ae - denom_b);
    float t = fmaf(-s, neg_b, dot_r_d1) / a;

    int s_oob = !(s >= 0.0f && s <= 1.0f);
    if (!(t >= 0.0f && t <= 1.0f)) {
        float tc = clamp01_col(t);
        float c1x = p1[0] + d1x * tc, c1y = p1[1] + d1y * tc, c1z = p1[2] + d1z * tc;
        out1[0] = c1x; out1[1] = c1y; out1[2] = c1z;
        float dx = c1x - p2[0], dy = c1y - p2[1], dz = c1z - p2[2];
        float u = (fmaf(dz, d2z, dx * d2x) + dy * d2y) / e;
        if (!s_oob || (u >= 0.0f && u <= 1.0f)) {
            float uc = clamp01_col(u);
            out2[0] = p2[0] + d2x * uc;
            out2[1] = p2[1] + d2y * uc;
            out2[2] = p2[2] + d2z * uc;
            return;
        }
        float uc = clamp01_col(u);
        float c2x = p2[0] + d2x * uc, c2y = p2[1] + d2y * uc, c2z = p2[2] + d2z * uc;
        out2[0] = c2x; out2[1] = c2y; out2[2] = c2z;
        float t2 = clamp01_col((fmaf(c2z - p1[2], d1z, (c2x - p1[0]) * d1x) + (c2y - p1[1]) * d1y) / a);
        c1x = p1[0] + d1x * t2; c1y = p1[1] + d1y * t2; c1z = p1[2] + d1z * t2;
        out1[0] = c1x; out1[1] = c1y; out1[2] = c1z;
        float u2 = clamp01_col((fmaf(c1z - p2[2], d2z, (c1x - p2[0]) * d2x) + (c1y - p2[1]) * d2y) / e);
        out2[0] = p2[0] + d2x * u2;
        out2[1] = p2[1] + d2y * u2;
        out2[2] = p2[2] + d2z * u2;
        return;
    }

    if (!s_oob) {
        out1[0] = p1[0] + d1x * t;
        out1[1] = p1[1] + d1y * t;
        out1[2] = p1[2] + d1z * t;
        out2[0] = p2[0] + d2x * s;
        out2[1] = p2[1] + d2y * s;
        out2[2] = p2[2] + d2z * s;
        return;
    }

    float sc = clamp01_col(s);
    float c2x = p2[0] + d2x * sc, c2y = p2[1] + d2y * sc, c2z = p2[2] + d2z * sc;
    out2[0] = c2x; out2[1] = c2y; out2[2] = c2z;
    float t2 = clamp01_col((fmaf(c2z - p1[2], d1z, (c2x - p1[0]) * d1x) + (c2y - p1[1]) * d1y) / a);
    out1[0] = p1[0] + d1x * t2;
    out1[1] = p1[1] + d1y * t2;
    out1[2] = p1[2] + d1z * t2;
}

static inline void col_seg_seg(Cpu *c, float out_a[3], float out_b[3],
                               uint64_t s1a, uint64_t s1b, uint64_t s2a, uint64_t s2b) {
    (void)c;
    col_seg_seg_native(out_a, out_b,
                       (const float *)(uintptr_t)s1a, (const float *)(uintptr_t)s1b,
                       (const float *)(uintptr_t)s2a, (const float *)(uintptr_t)s2b);
}

void xcde_col_seg_seg(Cpu *c) {
    float out1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float out2[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    col_seg_seg_native(out1, out2,
                       (const float *)(uintptr_t)c->x[2], (const float *)(uintptr_t)c->x[3],
                       (const float *)(uintptr_t)c->x[4], (const float *)(uintptr_t)c->x[5]);
    memcpy((void *)(uintptr_t)c->x[0], out1, 16);
    memcpy((void *)(uintptr_t)c->x[1], out2, 16);
}

static uint32_t col_capsule_poly(Cpu *c, uint64_t self, uint32_t poly_idx) {
    uint64_t mesh = *(const uint64_t *)(uintptr_t)self;
    uint64_t poly = *(const uint64_t *)(uintptr_t)(mesh + 0x10) + (uint64_t)poly_idx * 12u;
    uint16_t mat_idx = *(const uint16_t *)(uintptr_t)(poly + 0x0a);
    uint64_t mat = *(const uint64_t *)(uintptr_t)(mesh + 0x40) + (uint64_t)mat_idx * 0x28u;
    uint32_t mat_flags = *(const uint32_t *)(uintptr_t)(mat + 0x04);
    uint64_t filter = *(const uint64_t *)(uintptr_t)(self + 0x08);
    if ((*(const uint32_t *)(uintptr_t)(filter + 0xf8) & mat_flags) != 0) return 0u;
    if ((*(const uint32_t *)(uintptr_t)(filter + 0xfc) & mat_flags) == 0) return 0u;

    uint32_t num_tris = *(const uint8_t *)(uintptr_t)(poly + 0x04);
    if (num_tris == 0) return 0u;

    uint64_t vtx_tbl = *(const uint64_t *)(uintptr_t)(mesh + 0x18);
    uint64_t nrm_tbl = *(const uint64_t *)(uintptr_t)(mesh + 0x20);
    uint64_t vtx_base = *(const uint64_t *)(uintptr_t)(vtx_tbl + ((uint64_t)*(const uint8_t *)(uintptr_t)(poly + 5) << 3));
    uint64_t nrm_base = *(const uint64_t *)(uintptr_t)(nrm_tbl + ((uint64_t)*(const uint8_t *)(uintptr_t)(poly + 6) << 3));

    uint64_t idx_ptr = *(const uint64_t *)(uintptr_t)mesh + *(const uint32_t *)(uintptr_t)poly;
    uint64_t vtx_ptr = idx_ptr + ((uint64_t)num_tris << 1);

    float norm_scale = *(const float *)(uintptr_t)0x70009d3f7cull;
    uint16_t v0_idx = *(const uint16_t *)(uintptr_t)vtx_ptr;
    uint64_t v0_addr = vtx_base + ((uint64_t)v0_idx << 4);
    const float *v0 = (const float *)(uintptr_t)v0_addr;

    uint32_t any_hit = 0;
    uint32_t last_flag = 0;
    uint32_t need_closing = 0;
    float last_nrm[3] = {0.0f, 0.0f, 0.0f};

    for (uint32_t i = 0; i < num_tris; i++, idx_ptr += 2u) {
        uint16_t n_idx = *(const uint16_t *)(uintptr_t)idx_ptr;
        const int16_t *n16 = (const int16_t *)(uintptr_t)(nrm_base + (uint64_t)n_idx * 6u);
        float nx = (float)n16[0] * norm_scale;
        float ny = (float)n16[1] * norm_scale;
        float nz = (float)n16[2] * norm_scale;
        last_nrm[0] = nx;
        last_nrm[1] = ny;
        last_nrm[2] = nz;

        uint32_t prev_need_closing = need_closing;

        uint16_t v1_idx = *(const uint16_t *)(uintptr_t)(vtx_ptr + ((uint64_t)(i + 1u) << 1));
        uint16_t v2_idx = *(const uint16_t *)(uintptr_t)(vtx_ptr + ((uint64_t)(i + 2u) << 1));
        uint64_t v1_addr = vtx_base + ((uint64_t)v1_idx << 4);
        uint64_t v2_addr = vtx_base + ((uint64_t)v2_idx << 4);
        const float *v1 = (const float *)(uintptr_t)v1_addr;
        const float *v2 = (const float *)(uintptr_t)v2_addr;

        // Part 1: ray from q + 0x20, dir q + 0x30
        uint64_t q = *(const uint64_t *)(uintptr_t)(self + 0x18);
        const float *orig = (const float *)(uintptr_t)(q + 0x20);
        const float *dir = (const float *)(uintptr_t)(q + 0x30);
        float denom = fmaf(dir[2], nz, dir[0] * nx) + dir[1] * ny;
        if (denom == 0.0f) {
            need_closing = 0;
        } else {
            float d_v0 = fmaf(v0[2], nz, v0[0] * nx) + v0[1] * ny;
            float d_orig = fmaf(orig[2], nz, orig[0] * nx) + orig[1] * ny;
            float t = (d_v0 - d_orig) / denom;
            if (!col_check_pmf(c, self, 88, t, nx, ny, nz)) {
                need_closing = 0;
            } else {
                q = *(const uint64_t *)(uintptr_t)(self + 0x18);
                orig = (const float *)(uintptr_t)(q + 0x20);
                dir = (const float *)(uintptr_t)(q + 0x30);
                float px = orig[0] + dir[0] * t;
                float py = orig[1] + dir[1] * t;
                float pz = orig[2] + dir[2] * t;

                float ax = v0[0] - px, ay = v0[1] - py, az = v0[2] - pz;
                float bx = v1[0] - px, by = v1[1] - py, bz = v1[2] - pz;
                float cx = v2[0] - px, cy = v2[1] - py, cz = v2[2] - pz;
                float ux = fmaf(-bz, cy, cz * by), uy = fmaf(-bx, cz, cx * bz), uz = fmaf(-by, cx, cy * bx);
                float vx = fmaf(-cz, ay, az * cy), vy = fmaf(-cx, az, ax * cz), vz = fmaf(-cy, ax, ay * cx);

                if ((fmaf(vz, uz, vx * ux) + vy * uy) >= 0.0f) {
                    float wx = fmaf(-az, by, bz * ay), wy = fmaf(-ax, bz, bx * az), wz = fmaf(-ay, bx, by * ax);
                    if ((fmaf(wz, uz, wx * ux) + wy * uy) >= 0.0f) {
                        float hp1[3] = {px, py, pz};
                        float dist1 = (*(const float *)(uintptr_t)(q + 0x98)) * t;
                        col_invoke_hit(c, self, poly, mat, dist1, hp1, last_nrm, 0);
                        return 1u;
                    }
                }
                need_closing = 1;
            }
        }

        // Part 2: 2 capsule endpoints at q + 0x70 and q + 0x80
        float d_v0 = fmaf(v0[2], nz, v0[0] * nx) + v0[1] * ny;
        int hit_ep = 0;
        for (int ep = 0; ep < 2; ep++) {
            q = *(const uint64_t *)(uintptr_t)(self + 0x18);
            const float *ep_pos = (const float *)(uintptr_t)(q + 0x70 + (uint64_t)ep * 0x10u);
            float d = (fmaf(ep_pos[2], nz, ep_pos[0] * nx) + ep_pos[1] * ny) - d_v0;
            if (!col_check_pmf(c, self, 104, d, nx, ny, nz)) continue;

            q = *(const uint64_t *)(uintptr_t)(self + 0x18);
            ep_pos = (const float *)(uintptr_t)(q + 0x70 + (uint64_t)ep * 0x10u);
            float px = ep_pos[0] - nx * d;
            float py = ep_pos[1] - ny * d;
            float pz = ep_pos[2] - nz * d;

            float ax = v0[0] - px, ay = v0[1] - py, az = v0[2] - pz;
            float bx = v1[0] - px, by = v1[1] - py, bz = v1[2] - pz;
            float cx = v2[0] - px, cy = v2[1] - py, cz = v2[2] - pz;
            float ux = fmaf(-bz, cy, cz * by), uy = fmaf(-bx, cz, cx * bz), uz = fmaf(-by, cx, cy * bx);
            float vx = fmaf(-cz, ay, az * cy), vy = fmaf(-cx, az, ax * cz), vz = fmaf(-cy, ax, ay * cx);

            if ((fmaf(vz, uz, vx * ux) + vy * uy) >= 0.0f) {
                float wx = fmaf(-az, by, bz * ay), wy = fmaf(-ax, bz, bx * az), wz = fmaf(-ay, bx, by * ax);
                if ((fmaf(wz, uz, wx * ux) + wy * uy) >= 0.0f) {
                    float hp2[3] = {px, py, pz};
                    if (col_invoke_hit(c, self, poly, mat, fabsf(d), hp2, last_nrm, 0)) {
                        return 1u;
                    }
                    any_hit = 1;
                    last_flag = 1;
                    need_closing = 1;
                    hit_ep = 1;
                    break;
                }
            }
            need_closing = 1;
        }
        if (hit_ep) continue;

        // Part 3: capsule segment (q+0x70, q+0x80) vs edges (v1, v2) and (v0, v1)
        if (!need_closing) {
            need_closing = prev_need_closing | need_closing;
            last_flag = 0;
            continue;
        }

        q = *(const uint64_t *)(uintptr_t)(self + 0x18);
        float rad_sq = *(const float *)(uintptr_t)(q + 0x94);
        float out_a[3], out_b[3];
        col_seg_seg(c, out_a, out_b, q + 0x70, q + 0x80, v1_addr, v2_addr);
        float dx = out_a[0] - out_b[0], dy = out_a[1] - out_b[1], dz = out_a[2] - out_b[2];
        float dsq = fmaf(dz, dz, dx * dx) + dy * dy;
        if (dsq < rad_sq) {
            if (col_invoke_hit(c, self, poly, mat, sqrtf(dsq), out_b, last_nrm, 0xd8)) {
                return 1u;
            }
            any_hit = 1;
            last_flag = 1;
            need_closing = 1;
            continue;
        }

        q = *(const uint64_t *)(uintptr_t)(self + 0x18);
        rad_sq = *(const float *)(uintptr_t)(q + 0x94);
        col_seg_seg(c, out_a, out_b, q + 0x70, q + 0x80, v0_addr, v1_addr);
        float dx2 = out_a[0] - out_b[0], dy2 = out_a[1] - out_b[1], dz2 = out_a[2] - out_b[2];
        float dsq2 = fmaf(dz2, dz2, dx2 * dx2) + dy2 * dy2;
        if (dsq2 < rad_sq) {
            if (col_invoke_hit(c, self, poly, mat, sqrtf(dsq2), out_b, last_nrm, 0xd8)) {
                return 1u;
            }
            any_hit = 1;
            last_flag = 1;
            need_closing = 1;
            continue;
        }

        last_flag = 0;
        need_closing = 1;
    }

    // Part 4: closing edge (v0, v_last) when !last_flag && need_closing
    if (!last_flag && need_closing) {
        uint16_t v_last_idx = *(const uint16_t *)(uintptr_t)(vtx_ptr + ((uint64_t)num_tris << 1) + 2u);
        uint64_t v_last_addr = vtx_base + ((uint64_t)v_last_idx << 4);
        uint64_t q = *(const uint64_t *)(uintptr_t)(self + 0x18);
        float rad_sq = *(const float *)(uintptr_t)(q + 0x94);
        float out_a[3], out_b[3];
        col_seg_seg(c, out_a, out_b, q + 0x70, q + 0x80, v0_addr, v_last_addr);
        float dx3 = out_a[0] - out_b[0], dy3 = out_a[1] - out_b[1], dz3 = out_a[2] - out_b[2];
        float dsq3 = fmaf(dz3, dz3, dx3 * dx3) + dy3 * dy3;
        if (dsq3 < rad_sq) {
            if (col_invoke_hit(c, self, poly, mat, sqrtf(dsq3), out_b, last_nrm, 0xd8)) {
                return 1u;
            }
        }
    }

    return any_hit & 1u;
}

static uint32_t col_bvh_traverse(Cpu *c, uint64_t self, uint32_t node_idx) {
    uint64_t mesh = *(const uint64_t *)(uintptr_t)self;
    uint64_t aabbs = *(const uint64_t *)(uintptr_t)(mesh + 0x60);
    uint64_t p_max = aabbs + ((uint64_t)node_idx << 5);
    uint64_t p_min = p_max + 0x10;

    uint64_t adj_this = 0;
    uint64_t target = resolve_pmf(self, 184, &adj_this);
    uint32_t rej;
    if (target == ADDR_SEG_AABB_REJECT) {
        rej = col_seg_aabb_reject(adj_this, p_max, p_min);
        if (__builtin_expect(g_verify == 1, 0)) {
            Cpu chk = *c;
            chk.x[0] = adj_this;
            chk.x[1] = p_max;
            chk.x[2] = p_min;
            verify_against_guest(&chk, ADDR_SEG_AABB_REJECT, rej, "col_seg_aabb_reject(trav)");
        }
    } else if (target == ADDR_AABB_REJECT) {
        rej = col_aabb_reject(adj_this, p_max, p_min);
        if (__builtin_expect(g_verify == 1, 0)) {
            Cpu chk = *c;
            chk.x[0] = adj_this;
            chk.x[1] = p_max;
            chk.x[2] = p_min;
            verify_against_guest(&chk, ADDR_AABB_REJECT, rej, "col_aabb_reject(trav)");
        }
    } else {
        c->x[0] = adj_this;
        c->x[1] = p_max;
        c->x[2] = p_min;
        c->pc = target;
        c->x[30] = 0;
        rt_dispatch(c);
        rej = (uint32_t)(c->x[0] & 1u);
    }
    if (rej & 1u) return 0u;

    uint64_t nodes = *(const uint64_t *)(uintptr_t)(mesh + 0x68);
    uint64_t node = nodes + (uint64_t)node_idx * 12u;
    uint64_t data_base = *(const uint64_t *)(uintptr_t)mesh;
    uint64_t cur = data_base + *(const uint32_t *)(uintptr_t)node;

    uint32_t hit = 0;

    uint32_t n1 = *(const uint8_t *)(uintptr_t)(node + 5);
    if (n1 != 0) {
        uint32_t idx16 = *(const uint16_t *)(uintptr_t)cur;
        cur += 2;
        uint64_t tbl1 = *(const uint64_t *)(uintptr_t)(mesh + 0x48);
        uint32_t base1 = *(const uint32_t *)(uintptr_t)(tbl1 + ((uint64_t)*(const uint8_t *)(uintptr_t)(node + 8) << 2));
        uint32_t prim = base1 + idx16;
        uint32_t any1 = 0;
        for (uint32_t i = 0; i < n1; i++, prim++) {
            uint64_t leaf_this = 0;
            uint64_t leaf_fn = resolve_pmf(self, 40, &leaf_this);
            if (leaf_fn == ADDR_RAY_POLY) {
                any1 |= col_ray_poly(c, leaf_this, prim);
            } else if (leaf_fn == ADDR_SWEEP_POLY) {
                any1 |= col_sweep_poly(c, leaf_this, prim);
            } else if (leaf_fn == ADDR_CAPSULE_POLY) {
                any1 |= col_capsule_poly(c, leaf_this, prim);
            } else {
                any1 |= (call_guest_1(c, leaf_fn, leaf_this, prim) & 1u);
            }
        }
        hit = any1 ? 1u : 0u;
    }

    uint32_t n2 = *(const uint8_t *)(uintptr_t)(node + 6);
    if (n2 != 0) {
        uint32_t idx16 = *(const uint16_t *)(uintptr_t)cur;
        cur += 2;
        uint64_t tbl2 = *(const uint64_t *)(uintptr_t)(mesh + 0x50);
        uint32_t base2 = *(const uint32_t *)(uintptr_t)(tbl2 + ((uint64_t)*(const uint8_t *)(uintptr_t)(node + 9) << 2));
        uint32_t child = base2 + idx16;
        for (uint32_t i = 0; i < n2; i++, child++) {
            uint64_t ch_this = 0;
            uint64_t ch_fn = resolve_pmf(self, 168, &ch_this);
            if (ch_fn == ADDR_BVH_TRAVERSE) {
                hit |= col_bvh_traverse(c, ch_this, child);
            } else {
                hit |= call_guest_1(c, ch_fn, ch_this, child);
            }
        }
    }

    uint32_t n3 = *(const uint8_t *)(uintptr_t)(node + 4);
    if (n3 != 0) {
        uint32_t idx16 = *(const uint16_t *)(uintptr_t)cur;
        uint64_t tbl3 = *(const uint64_t *)(uintptr_t)(mesh + 0x58);
        uint32_t base3 = *(const uint32_t *)(uintptr_t)(tbl3 + ((uint64_t)*(const uint8_t *)(uintptr_t)(node + 7) << 2));
        uint32_t child = base3 + idx16;
        for (uint32_t i = 0; i < n3; i++, child++) {
            uint64_t ch_this = 0;
            uint64_t ch_fn = resolve_pmf(self, 152, &ch_this);
            if (ch_fn == ADDR_BVH_TRAVERSE) {
                hit |= col_bvh_traverse(c, ch_this, child);
            } else {
                hit |= call_guest_1(c, ch_fn, ch_this, child);
            }
        }
    }

    return hit & 1u;
}

static void xcde_col_aabb_reject(Cpu *c) {
    uint32_t r = col_aabb_reject(c->x[0], c->x[1], c->x[2]);
    if (__builtin_expect(g_verify == 1, 0)) {
        verify_against_guest(c, ADDR_AABB_REJECT, r, "col_aabb_reject");
    }
    c->x[0] = r;
}

static void xcde_col_seg_aabb_reject(Cpu *c) {
    uint32_t r = col_seg_aabb_reject(c->x[0], c->x[1], c->x[2]);
    if (__builtin_expect(g_verify == 1, 0)) {
        verify_against_guest(c, ADDR_SEG_AABB_REJECT, r, "col_seg_aabb_reject");
    }
    c->x[0] = r;
}

static void xcde_col_bvh_traverse(Cpu *c) {
    uint64_t saved_lr = c->x[30];
    uint32_t r = col_bvh_traverse(c, c->x[0], (uint32_t)c->x[1]);
    c->x[30] = saved_lr;
    c->x[0] = r;
}

static void xcde_col_ray_poly(Cpu *c) {
    uint64_t saved_lr = c->x[30];
    uint32_t r = col_ray_poly(c, c->x[0], (uint32_t)c->x[1]);
    c->x[30] = saved_lr;
    c->x[0] = r;
}

static void xcde_col_sweep_poly(Cpu *c) {
    uint64_t saved_lr = c->x[30];
    uint32_t r = col_sweep_poly(c, c->x[0], (uint32_t)c->x[1]);
    c->x[30] = saved_lr;
    c->x[0] = r;
}

static void xcde_col_capsule_poly(Cpu *c) {
    uint64_t saved_lr = c->x[30];
    uint32_t r = col_capsule_poly(c, c->x[0], (uint32_t)c->x[1]);
    c->x[30] = saved_lr;
    c->x[0] = r;
}

// [XCDE] Native replacement for f_70006f5460 (5-plane sphere-vs-frustum test).
void xcde_frustum_sphere(Cpu *c) {
    const uint8_t *frustum = (const uint8_t *)(uintptr_t)c->x[0];
    const float *pos = (const float *)(uintptr_t)c->x[1];
    float rad;
    uint32_t rbits = (uint32_t)c->v[0];
    memcpy(&rad, &rbits, 4);
    float px = pos[0], py = pos[1], pz = pos[2];
    for (int i = 0; i < 4; i++) {
        const float *pl = (const float *)(frustum + 0x10 + i * 0x20);
        float dist = ((px * pl[0] + pz * pl[2]) + (py * pl[1])) + pl[4];
        if (dist > rad) { c->x[0] = 0; return; }
    }
    const float *pl = (const float *)(frustum + 0x90);
    float dist = ((px * pl[0] + pz * pl[2]) + (py * pl[1])) + pl[4];
    c->x[0] = (dist <= rad) ? 1u : 0u;
}

static inline void quat_to_rot(const float q[4], float r0[4], float r1[4], float r2[4]) {
    float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    float x2 = qx + qx, y2 = qy + qy, z2 = qz + qz;
    float xx = qx * x2, yy = qy * y2, zz = qz * z2;
    float xy = qx * y2, xz = qx * z2, yz = qy * z2;
    float wx = qw * x2, wy = qw * y2, wz = qw * z2;
    r0[0] = 1.0f - (yy + zz); r0[1] = xy + wz;          r0[2] = xz - wy;          r0[3] = 0.0f;
    r1[0] = xy - wz;          r1[1] = 1.0f - (xx + zz); r1[2] = yz + wx;          r1[3] = 0.0f;
    r2[0] = xz + wy;          r2[1] = yz - wx;          r2[2] = 1.0f - (xx + yy); r2[3] = 0.0f;
}

static inline void mat4_mul_rows(float out[4][4], const float A[4][4], const float B[4][4]) {
    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 4; k++) {
            out[r][k] = fmaf(A[3][k], B[r][3], fmaf(A[2][k], B[r][2], fmaf(A[1][k], B[r][1], A[0][k] * B[r][0])));
        }
    }
}

// [XCDE] Native replacement for f_700001cf30 (bone TRS-to-4x4 matrix + parent multiply).
void xcde_bone_trs_mat(Cpu *c) {
    uint64_t self = c->x[0], node = c->x[1], out_addr = c->x[2];
    uint64_t skel = *(const uint64_t *)(uintptr_t)(self + 0x50);
    uint32_t bone_idx = (uint32_t)(int32_t)(int16_t)(*(const uint16_t *)(uintptr_t)(node + 0x78));
    uint64_t trs_arr = *(const uint64_t *)(uintptr_t)(skel + 0x80);
    const float *trs = (const float *)(uintptr_t)(trs_arr + (uint64_t)bone_idx * 0x30u);

    float r0[4], r1[4], r2[4];
    quat_to_rot(trs + 4, r0, r1, r2);
    float sx = trs[8], sy = trs[9], sz = trs[10];
    float M[4][4] = {
        { r0[0] * sx, r0[1] * sx, r0[2] * sx, 0.0f },
        { r1[0] * sy, r1[1] * sy, r1[2] * sy, 0.0f },
        { r2[0] * sz, r2[1] * sz, r2[2] * sz, 0.0f },
        { trs[0],     trs[1],     trs[2],     1.0f }
    };

    uint64_t parent_tbl = *(const uint64_t *)(uintptr_t)(skel + 0x60);
    uint32_t parent_idx = *(const uint16_t *)(uintptr_t)(parent_tbl + ((uint64_t)(bone_idx & 0xffffu) << 1));
    if (parent_idx != 0xffffu) {
        uint64_t saved_lr = c->x[30];
        c->x[0] = *(const uint64_t *)(uintptr_t)(self + 0x58);
        c->x[1] = parent_idx;
        GuestFn fn = dispatch_lookup(0x700077abc0ull);
        if (__builtin_expect(fn != NULL, 1)) {
            c->x[30] = 0;
            fn(c);
        } else {
            c->pc = 0x700077abc0ull;
            c->x[30] = 0;
            rt_dispatch(c);
        }
        c->x[30] = saved_lr;
        const float *ptrs = (const float *)(uintptr_t)c->x[0];
        float pr0[4], pr1[4], pr2[4];
        quat_to_rot(ptrs + 4, pr0, pr1, pr2);
        float psx = ptrs[8], psy = ptrs[9], psz = ptrs[10];
        float P[4][4] = {
            { pr0[0] * psx, pr0[1] * psx, pr0[2] * psx, 0.0f },
            { pr1[0] * psy, pr1[1] * psy, pr1[2] * psy, 0.0f },
            { pr2[0] * psz, pr2[1] * psz, pr2[2] * psz, 0.0f },
            { ptrs[0],      ptrs[1],      ptrs[2],      1.0f }
        };
        float tmp[4][4];
        mat4_mul_rows(tmp, P, M);
        memcpy(M, tmp, sizeof(M));
    }

    float u = *(const float *)(uintptr_t)(self + 0x60);
    for (int r = 0; r < 4; r++) {
        M[r][0] *= u;
        M[r][1] *= u;
        M[r][2] *= u;
    }
    memcpy((void *)(uintptr_t)out_addr, M, sizeof(M));
}

void xcde_anim_eval_vec3(Cpu *c) {
    uint64_t track = c->x[0];
    float *out = (float *)(uintptr_t)c->x[1];
    float t;
    uint32_t t_bits = (uint32_t)c->v[0];
    memcpy(&t, &t_bits, sizeof(t));

    uint64_t begin = *(const uint64_t *)(uintptr_t)track;
    uint32_t count = *(const uint32_t *)(uintptr_t)(track + 8u);
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        float kt = *(const float *)(uintptr_t)(begin + (uint64_t)mid * 52u);
        if (kt > t) hi = mid;
        else lo = mid + 1u;
    }
    uint32_t idx = (lo == 0u) ? 0u : (lo - 1u);
    const float *kp = (const float *)(uintptr_t)(begin + (uint64_t)idx * 52u);
    float dt = t - kp[0];
    float dt2 = dt * dt;
    float dt3 = dt * dt2;
    for (int ch = 0; ch < 3; ch++) {
        const float *k = kp + 1 + ch * 4;
        out[ch] = (dt * k[2] + dt3 * k[0]) + (k[3] + dt2 * k[1]);
    }
}

void xcde_anim_eval_quat(Cpu *c) {
    uint64_t track = c->x[0];
    float *out = (float *)(uintptr_t)c->x[1];
    float t;
    uint32_t t_bits = (uint32_t)c->v[0];
    memcpy(&t, &t_bits, sizeof(t));

    uint64_t begin = *(const uint64_t *)(uintptr_t)track;
    uint32_t count = *(const uint32_t *)(uintptr_t)(track + 8u);
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        float kt = *(const float *)(uintptr_t)(begin + (uint64_t)mid * 68u);
        if (kt > t) hi = mid;
        else lo = mid + 1u;
    }
    uint32_t idx = (lo == 0u) ? 0u : (lo - 1u);
    const float *kp = (const float *)(uintptr_t)(begin + (uint64_t)idx * 68u);
    float dt = t - kp[0];
    float dt2 = dt * dt;
    float dt3 = dt * dt2;
    float q[4];
    for (int ch = 0; ch < 4; ch++) {
        const float *k = kp + 1 + ch * 4;
        q[ch] = (dt * k[2] + dt3 * k[0]) + (k[3] + dt2 * k[1]);
    }
    float len_sq = (q[2] * q[2] + q[0] * q[0]) + (q[3] * q[3] + q[1] * q[1]);
    float eps = **(const float *const *)(uintptr_t)0x7000b80378ull;
    if (eps > len_sq) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 0.0f;
    } else {
        float inv_len = rsqrte_s(c, len_sq);
        inv_len *= fmaf(-inv_len, len_sq * inv_len, 3.0f) * 0.5f;
        inv_len *= fmaf(-inv_len, len_sq * inv_len, 3.0f) * 0.5f;
        out[0] = q[0] * inv_len;
        out[1] = q[1] * inv_len;
        out[2] = q[2] * inv_len;
        out[3] = q[3] * inv_len;
    }
}

__attribute__((constructor)) static void col_bvh_init(void) {
    const char *v = getenv("XCDE_COL_BVH_VERIFY");
    g_verify = (v && atoi(v) != 0) ? 1 : 0;

    const char *e = getenv("XCDE_COL_BVH");
    if (e && atoi(e) == 0) return;

    hle_override(ADDR_AABB_REJECT, xcde_col_aabb_reject, "xcde_col_aabb_reject");
    hle_override(ADDR_SEG_AABB_REJECT, xcde_col_seg_aabb_reject, "xcde_col_seg_aabb_reject");
    hle_override(ADDR_BVH_TRAVERSE, xcde_col_bvh_traverse, "xcde_col_bvh_traverse");
    hle_override(ADDR_RAY_POLY, xcde_col_ray_poly, "xcde_col_ray_poly");
    hle_override(ADDR_SWEEP_POLY, xcde_col_sweep_poly, "xcde_col_sweep_poly");
    hle_override(ADDR_CAPSULE_POLY, xcde_col_capsule_poly, "xcde_col_capsule_poly");
}

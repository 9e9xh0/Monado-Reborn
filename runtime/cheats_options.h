// [XCDE] Optional gameplay cheats, ported from Monado II (github.com/brendonbusker/Monado-II, MIT; LINUX-072/120/129).
// Coherent live settings shared by the launcher (Cheats tab) and the F1 menu (runtime/ui/cheats_controls.h), the INI
// reader/writer (runtime/host/settings_ini.c) and the guest hooks (runtime/hle/cheats_*.c, games/xcde/patches.py).
// Movement speed is not here: this recomp already has its own leader-only run speed (T270, runtime/hle/party.c). The
// optional high jump (T276) is a jump-height scale, not a speed, so it lives with the other cheats.
#pragma once
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {
    XCDE_CHEAT_DAMAGE_DEALT, XCDE_CHEAT_DAMAGE_RECEIVED,
    XCDE_CHEAT_EXP, XCDE_CHEAT_AP, XCDE_CHEAT_SP, XCDE_CHEAT_GOLD,
    XCDE_CHEAT_ENEMY_HEALTH, XCDE_CHEAT_COUNT
};
typedef struct {
    bool enabled[XCDE_CHEAT_COUNT];
    int percent[XCDE_CHEAT_COUNT];
    bool no_fall_damage;
    bool high_jump;
} XcdeCheats;

// The cheat multipliers as (name, minimum %) for the settings loader / saver / frontends.
#define XCDE_CHEAT_MULTIPLIERS(X) \
    X(damage_dealt, 0) X(damage_received, 0) X(exp, 0) X(ap, 0) X(sp, 0) X(gold, 0) X(enemy_health, 10)

// xcde.ini parsing, shared by the game (settings_ini.c) and the launcher (tools/launcher/launcher.cpp): only an exact
// 1 / true opts in; malformed or out-of-range percentages read as 1x instead of silently mis-scaling the game.
static inline bool xcde_cheat_enabled(const char *value) {
    return !strcmp(value, "1") || !strcmp(value, "true");
}
static inline int xcde_cheat_percent_bound(int value, int minimum) {
    return value >= minimum && value <= 2000 ? value : 100;
}
static inline int xcde_cheat_percent(const char *value, int minimum) {
    if (!*value) return 100;
    for (const char *p = value; *p; ++p) if (*p < '0' || *p > '9') return 100;
    if (strlen(value) > 4) return 100;
    return xcde_cheat_percent_bound(atoi(value), minimum);
}

// Copies a complete, validated request. No lock is retained by the caller.
XcdeCheats xcde_cheats_snapshot(void);
void xcde_cheats_publish(const Settings *settings);
// Cheap coherent effective value, without a per-enemy mutex.
int xcde_cheats_enemy_health_percent(void);
static inline int xcde_cheats_minimum_percent(int kind) {
    return kind == XCDE_CHEAT_ENEMY_HEALTH ? 10 : 0;
}
static inline int xcde_cheats_effective_percent(const XcdeCheats *options, int kind) {
    if (!options || kind < 0 || kind >= XCDE_CHEAT_COUNT || !options->enabled[kind]) return 100;
    int percent = options->percent[kind];
    return percent >= xcde_cheats_minimum_percent(kind) && percent <= 2000 ? percent : 100;
}
#ifdef __cplusplus
}
#endif

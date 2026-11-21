// [XCDE] (ported from Monado II, MIT; LINUX-072) optional native combat event adapters.
#pragma once
#include "../cpu.h"
#include <stdbool.h>

// Called after native entity lookups, before a per-hit event is copied to its
// recipients, damage HUD and statistics. Neither calls/yields into the guest.
void cheats_combat_hit(Cpu *c);
void cheats_combat_linked_hit(Cpu *c);

// Predicate for the checked native height-based landing damage calculation.
// True selects the original no-damage return. Forced collision deaths remain.
bool cheats_skip_fall_damage(Cpu *c);

// [XCDE] T276: the jump-height scale the native jump state passes to its sqrt(2*g*h*scale) impulse helper. Native is
// 1.0f; the optional high jump returns 8.0f (the community "jump x8" code). Float bits, as consumed by c->v[0].
uint32_t xcde_cheats_jump_scale(void);

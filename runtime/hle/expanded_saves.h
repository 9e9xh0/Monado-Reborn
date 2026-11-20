// [XCDE] (ported from Monado II, MIT; LINUX-035) authored adapter for the checked DE 2.0.0 save-list ABI.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../cpu.h"

enum {
    XCDE_SAVE_MANUAL_COUNT = 99,
    XCDE_SAVE_RECORD_COUNT = 100,
    XCDE_SAVE_VISIBLE_COUNT = 4,
    XCDE_SAVE_RECORD_BASE = 0x1541e8,
    XCDE_SAVE_RECORD_STRIDE = 0x50,
    XCDE_SAVE_RECORD_END = 0x156128,
    XCDE_SAVE_COUNT_OFFSET = 0x1fcf28,
    XCDE_SAVE_FLAGS_OFFSET = 0x1fcf2c,
    XCDE_SAVE_THUMB_BASE = 0x587150,
    XCDE_SAVE_THUMB_STRIDE = 0x2a300,
    XCDE_SAVE_THUMB_BYTES = 0x107ac00,
    XCDE_SAVE_STATE_BYTES = 0x1601d50,
    // [XCDE] xcde.ini save_slots_99 = 0 restores the game's own list: the autosave plus three
    // manual slots (records 0..3). The patched allocation and thumbnail arena stay, but the
    // logical record count, sort end and every extra adapter step follow the native values.
    XCDE_SAVE_NATIVE_RECORD_COUNT = 4,
    XCDE_SAVE_NATIVE_RECORD_END = XCDE_SAVE_RECORD_BASE + XCDE_SAVE_NATIVE_RECORD_COUNT * XCDE_SAVE_RECORD_STRIDE,
    XCDE_SAVE_NATIVE_SELECTED_MAX = XCDE_SAVE_NATIVE_RECORD_COUNT - 1,
};

bool xcde_expanded_saves_enabled(void);

void xcde_expanded_saves_initialize(Cpu *c);
void xcde_expanded_saves_forget(uint64_t state);
void xcde_expanded_saves_capture(Cpu *c);
void xcde_expanded_saves_legacy_set(Cpu *c);
void xcde_expanded_saves_default(Cpu *c);

// [XCDE] LINUX-036: called after the one original native UI update. Only cold
// metadata reads can receive extra native steps; the complete caller CPU stays.
enum { XCDE_SAVE_SCAN_MAX_EXTRA = 7, XCDE_SAVE_SCAN_BUDGET_NS = 500000 };
void xcde_expanded_saves_pump(Cpu *c, uint64_t ui, uint64_t state);

// Shared with the title Continue scanner. The reference contains a native
// context pointer. This helper preserves every caller register and FP state.
uint32_t xcde_expanded_saves_story(Cpu *c, uint64_t context_reference);
void xcde_expanded_saves_remember(uint32_t story, uint16_t file_id, bool autosave);

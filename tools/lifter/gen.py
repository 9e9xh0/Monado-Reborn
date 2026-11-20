"""Generate C for a whole NSO module.

Usage: gen.py <name> <nso> <base> <outdir> [--hle hle_symbols.txt] [--chunk N] [--simd asm|c]

--simd: FP/SIMD/CRC32 as AArch64 inline asm (default on arm64 hosts) or portable C (default elsewhere).
"""
import argparse
import bisect
import os
import platform
import re
import struct
import sys
import time
from collections import Counter

import unicorn as uc
from unicorn import arm64_const as A

from analyze import eh_frame_functions
from lifter import Insn, Lifter, Unsupported, bits, emit_asm_batch, hx, sext
from nso import Nso, R_AARCH64_RELATIVE
from patchfmt import JumpTable
import flags  # noqa: E402  (function-local NZCV, from TotK Recomp)
import regs  # noqa: E402  (function-local general registers, from TotK Recomp)

# [XCDE] Function-local general registers (tools/lifter/regs.py, docs/guides/LIFTER_LOCALS.md). XCDE_REG_LOCALS=<n> keeps the
# n most used X registers / SP of each function in C locals (0: none); XCDE_CALLEE_TRUST=0 reloads X19-X29 and SP
# after calls too. The defaults are what ships: GAMELIB_ID hashes this file, not the environment.
REG_LOCALS = int(os.environ.get('XCDE_REG_LOCALS', regs.LOCALS))
CALLEE_TRUST = os.environ.get('XCDE_CALLEE_TRUST', '1') != '0'
# Guest functions that leave other than by a return (longjmp, the C++ unwinder): with the trust on, their entry stops
# the game (rt_trust_fault) rather than letting callers run on with stale X19-X29 / SP.
TRUST_BREAKERS = ('longjmp', '_longjmp', '__cxa_throw', '__cxa_rethrow', '_Unwind_RaiseException', '_Unwind_Resume',
                  '_Unwind_Resume_or_Rethrow', '_Unwind_ForcedUnwind')

SCRATCH_MEM = 0x7300000000


def sext26(w):
    v = w & 0x03FFFFFF
    return v - 0x04000000 if v & 0x02000000 else v


def fname(a):
    return f'f_{a:x}'


def sanitize(sym):
    return 'hle_' + re.sub(r'[^A-Za-z0-9_]', '_', sym)


class Module:
    def __init__(self, name, path, base, simd='asm'):
        self.name = name
        self.nso = Nso(path)
        self.base = base
        n = self.nso
        self.lo = base + n.text[0]
        self.hi = base + n.text[0] + n.text[1]
        self.words = struct.unpack(f'<{n.text[1] // 4}I', bytes(n.image[n.text[0]:n.text[0] + n.text[1]]))
        self.lifter = Lifter(base, simd)
        self.cache = {}
        self.stats = Counter()
        self.mu = None

    def in_text(self, a):
        return self.lo <= a < self.hi and a % 4 == 0

    def word(self, a):
        return self.words[(a - self.lo) // 4]

    def lift(self, a):
        ins = self.cache.get(a)
        if ins is None:
            try:
                ins = self.lifter.lift(self.word(a), a)
            except Unsupported as e:
                self.stats['unsupported'] += 1
                ins = Insn('trap', f'rt_udf(c, {hx(a)}); /* {e} */')
            self.cache[a] = ins
        return ins

    def boundaries(self):
        n, b = self.nso, self.base
        starts = set()
        for a in eh_frame_functions(n):
            starts.add(b + a)
        for name, info, shndx, value, size in n.syms:
            if shndx and (info & 0xF) == 2:
                starts.add(b + value)
        for off, typ, sym, add in n.relocs:
            if typ == R_AARCH64_RELATIVE:
                starts.add(b + add)
        starts = sorted(a for a in starts if self.in_text(a))
        return starts

    # ------------------------------------------------------------ jump tables

    def emu(self):
        if self.mu is None:
            mu = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
            n = self.nso
            mu.mem_map(self.base, len(n.image))
            mu.mem_write(self.base, bytes(n.image))
            mu.mem_map(SCRATCH_MEM, 0x100000)
            mu.reg_write(A.UC_ARM64_REG_CPACR_EL1, 0x300000)
            self.mu = mu
        return self.mu

    def jump_table(self, br_pc, entry, end):
        """Resolve `br xN` targets from a bounds-checked jump table, or None."""
        r = self._jump_table_fallthrough(br_pc, entry, end)
        if r is None:
            r = self._jump_table_taken(br_pc, entry, end)
        return r

    def _index_regs(self, start, at, idx):
        """The index register plus registers holding the same value in start..at: sources it was
        copied/masked from, and copies of those (the table lookup sometimes recomputes the index
        from another copy after the bounds check)."""
        def copy_src(w):
            if (w & 0x7F800000) == 0x12000000:  # AND (imm)
                return bits(w, 5, 5)
            if (w & 0x7FE0FFE0) == 0x2A0003E0:  # MOV (ORR with zr)
                return bits(w, 16, 5)
            if (w & 0x7F800000) in (0x53000000, 0x13000000):  # UBFM/SBFM (uxtb, uxth, sxtw...)
                return bits(w, 5, 5)
            return None
        regs = {idx}
        changed = True
        while changed:
            changed = False
            for a in range(start, at, 4):
                w = self.word(a)
                rd, src = w & 31, copy_src(w)
                if src is None or src >= 31 or rd >= 31:
                    continue
                for x, y in ((rd, src), (src, rd)):
                    if x in regs and y not in regs:
                        regs.add(y)
                        changed = True
        return sorted(regs)

    def _set_index(self, mu, regs, i):
        for r in regs:
            mu.reg_write(A.UC_ARM64_REG_X0 + r if r < 29 else A.UC_ARM64_REG_X29, i)
        # [XCDE] T141: the lookup may reload the index from a spill slot after the bounds check (ldr x8,[sp,#8] after
        # cmp w8,#3 - main 0x70005264b4): every word of the scratch window the registers and sp point at holds it too
        mu.mem_write(SCRATCH_MEM + 0x80000 - 0x1000, struct.pack('<Q', i) * 0x400)

    @staticmethod
    def _trusted(targets, count):
        # [XCDE] T141: one target for a multi-entry table means the index never reached the lookup (it came from
        # somewhere the emulation doesn't set): not a resolution - the caller keeps the any-instruction fallback
        return None if targets is None or (count > 1 and len(targets) == 1) else targets

    def _emulate_table(self, br_pc, entry, end, start, idx, count, set_at):
        """Run start..br for each index value (set at address set_at), collect targets."""
        mu = self.emu()
        rn = bits(self.word(br_pc), 5, 5)

        def step_to(frm, to):
            pc = frm
            for _ in range(64):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        targets = []
        try:
            for i in range(count):
                for r in range(29):
                    mu.reg_write(A.UC_ARM64_REG_X0 + r, SCRATCH_MEM + 0x80000)
                mu.reg_write(A.UC_ARM64_REG_SP, SCRATCH_MEM + 0x80000)
                a2 = entry
                while a2 < start:
                    w2 = self.word(a2)
                    if (w2 & 0x1F000000) == 0x10000000 or (w2 & 0xFF800000) == 0x91000000:
                        mu.emu_start(a2, 0, count=1)
                    a2 += 4
                if set_at != start and not step_to(start, set_at):
                    return None
                self._set_index(mu, [idx], i)
                if not step_to(set_at, br_pc):
                    return None
                t = mu.reg_read(A.UC_ARM64_REG_X0 + rn if rn < 29 else
                                (A.UC_ARM64_REG_X29 if rn == 29 else A.UC_ARM64_REG_X30))
                if not (entry <= t < end) or t % 4:
                    return None
                targets.append(t)
        except uc.UcError:
            return None
        return self._trusted(sorted(set(targets)), count)

    def _jump_table_taken(self, br_pc, entry, end):
        """Form: cmp idx, #N ; b.ls/b.lo table ... table: <address math> ; br"""
        # start of the block holding the br
        s = br_pc
        while s - 4 >= entry and br_pc - (s - 4) <= 4 * 12 and self.lift(s - 4).kind in ('op', 'asm'):
            s -= 4
        for g in range(entry, end, 4):
            w = self.word(g)
            if (w & 0xFF000010) != 0x54000000 or bits(w, 0, 4) not in (9, 3):
                continue
            t = g + sext(bits(w, 5, 19), 19) * 4
            if t != s:
                continue
            cw = self.word(g - 4) if g - 4 >= entry else 0
            if (cw & 0x7F80001F) != 0x7100001F or bits(cw, 22, 1):
                continue
            limit = bits(cw, 10, 12)
            count = limit + 1 if bits(w, 0, 4) == 9 else limit
            if count <= 0 or count > 4096:
                continue
            return self._emulate_table(br_pc, entry, end, s, bits(cw, 5, 5), count, s)
        return None

    def _jump_table_fallthrough(self, br_pc, entry, end):
        rn = bits(self.word(br_pc), 5, 5)
        # find the guarding b.hi / b.hs and its cmp, scanning backwards
        a = br_pc - 4
        guard = cmp = None
        while a >= entry and br_pc - a <= 4 * 16:
            w = self.word(a)
            if (w & 0xFF000010) == 0x54000000 and bits(w, 0, 4) in (8, 2):
                guard = a
                break
            if self.lift(a).kind in ('b', 'bl', 'blr', 'br', 'ret', 'trap'):
                return None
            a -= 4
        if guard is None:
            return None
        a = guard - 4
        while a >= entry and guard - a <= 4 * 8:
            w = self.word(a)
            # SUBS (imm) with Rd = zr, i.e. CMP #imm
            if (w & 0x7F80001F) == 0x7100001F:
                cmp = a
                break
            if self.lift(a).kind != 'op':
                return None
            a -= 4
        if cmp is None:
            return None
        w = self.word(cmp)
        if bits(w, 22, 1):
            return None
        limit = bits(w, 10, 12)
        count = limit + 1 if bits(self.word(guard), 0, 4) == 8 else limit
        if count <= 0 or count > 4096:
            return None
        idx = bits(w, 5, 5)
        # window start: back from cmp to the first control-flow instruction
        start = cmp
        while start - 4 >= entry and cmp - (start - 4) <= 4 * 12 and self.lift(start - 4).kind in ('op', 'asm'):
            start -= 4
        idx_regs = self._index_regs(start, cmp, idx)
        mu = self.emu()
        targets = []

        def step_to(frm, to):
            # single-step: Unicorn's `until` is unreliable mid translation-block
            pc = frm
            for _ in range(64):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        try:
            for i in range(count):
                for r in range(29):
                    mu.reg_write(A.UC_ARM64_REG_X0 + r, SCRATCH_MEM + 0x80000)
                mu.reg_write(A.UC_ARM64_REG_SP, SCRATCH_MEM + 0x80000)
                # replay address materialization (ADRP/ADR/ADD-imm) from the function entry, so table
                # bases computed before loops or branches are known
                a2 = entry
                while a2 < start:
                    w2 = self.word(a2)
                    if (w2 & 0x1F000000) == 0x10000000 or (w2 & 0xFF800000) == 0x91000000:
                        mu.emu_start(a2, 0, count=1)
                    a2 += 4
                if not step_to(start, cmp):
                    return None
                self._set_index(mu, idx_regs, i)
                if not step_to(cmp, br_pc):
                    return None
                t = mu.reg_read(A.UC_ARM64_REG_X0 + rn if rn < 29 else
                                (A.UC_ARM64_REG_X29 if rn == 29 else A.UC_ARM64_REG_X30))
                if not (entry <= t < end) or t % 4:
                    return None
                targets.append(t)
        except uc.UcError:
            return None
        return self._trusted(sorted(set(targets)), count)

    # ------------------------------------------------------------ functions

    def computed_target(self, br_pc, entry):
        """True if the br register comes from address arithmetic (a table) rather than a load."""
        rn = bits(self.word(br_pc), 5, 5)
        a = br_pc - 4
        while a >= entry and br_pc - a <= 4 * 16:
            w = self.word(a)
            if self.lift(a).kind not in ('op', 'asm'):
                return False
            if (w & 31) == rn:
                if (w & 0x1F000000) == 0x0B000000:  # ADD/SUB (shifted or extended register)
                    return True
                if (w & 0x9F000000) == 0x10000000:  # ADR
                    return True
                return False
            a -= 4
        return False

    def scan(self, entry, end):
        """Collect labels and external entries referenced by a function."""
        labels, ext, tables = {entry}, set(), {}
        terminated = False
        for a in range(entry, end, 4):
            ins = self.lift(a)
            if ins.kind in ('b', 'bcond'):
                t = ins.target
                if entry <= t < end:
                    labels.add(t)
                elif self.in_text(t):
                    ext.add(t)
            elif ins.kind == 'bl':
                if self.in_text(ins.target):
                    ext.add(ins.target)
            elif ins.kind == 'br':
                patch = PATCHES.get(a)
                jt = patch.resolve(self, a) if isinstance(patch, JumpTable) else self.jump_table(a, entry, end)
                if jt:
                    tables[a] = jt
                    labels.update(t for t in jt if entry <= t < end)
                    ext.update(t for t in jt if not entry <= t < end)
                    self.stats['jumptables'] += 1
                elif self.computed_target(a, entry):
                    # unresolved table jump: allow jumping to any instruction of the function
                    tables[a] = 'all'
                    self.stats['br_all_labels'] += 1
                else:
                    self.stats['br_dispatch'] += 1
            terminated = ins.kind in ('b', 'br', 'ret', 'trap')
        if not terminated and end < self.hi:
            ext.add(end)
        return labels, ext, tables

    def render_single(self, ins, a, jump, refs):
        """C for one lifted instruction (no batching); used for patch variants."""
        k = ins.kind
        if k == 'asm':
            return [emit_asm_batch([ins.asm])]
        if k in ('op', 'trap'):
            return [ins.code]
        if k == 'b':
            return [jump(ins.target)]
        if k == 'bcond':
            return [f'if ({ins.cond}) {{ {jump(ins.target)} }}']
        if k == 'bl':
            t = ins.target
            if self.in_text(t):
                refs.add(t)
                return [f'c->x[30] = {hx(a + 4)}; {fname(t)}(c);']
            return [f'c->x[30] = {hx(a + 4)}; c->pc = {hx(t)}; rt_dispatch(c);']
        if k == 'blr':
            return [f'c->pc = {ins.target}; c->x[30] = {hx(a + 4)}; rt_dispatch(c);']
        if k == 'br':
            return [f'c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);']
        if k == 'ret':
            return ['return;']
        raise Unsupported(k)

    def flow(self, insns, entry, end, tables):
        """Successors of each instruction inside the function [entry, end); index len(insns) stands for falling
        through into the next function and has none (TotK Recomp's flow(), without patched sites)."""
        def idx(t):
            return [(t - entry) // 4] if isinstance(t, int) and entry <= t < end else []

        succ = []
        for i, ins in enumerate(insns):
            k = ins.kind
            if k in ('op', 'bl', 'blr'):
                s = [i + 1]
            elif k == 'b':
                s = idx(ins.target)
            elif k == 'bcond':
                s = idx(ins.target) + [i + 1]
            elif k == 'br':
                jt = tables.get(entry + 4 * i)
                s = [t for x in jt for t in idx(x)] if jt and jt != 'all' else []
            else:
                s = []   # ret, trap
            succ.append(s)
        succ.append([])
        return succ

    def flag_plan(self, insns, entry, end, tables):
        """tools/lifter/flags.py plan for the function: each instruction's flag use over its control flow."""
        n = len(insns)
        calls, effects, exits = set(), [], {n}
        for i, ins in enumerate(insns):
            k = ins.kind
            if k in ('bl', 'blr'):
                calls.add(i)
            elif k in ('b', 'bcond') and not (isinstance(ins.target, int) and entry <= ins.target < end):
                exits.add(i)
            elif k == 'br':
                exits.add(i)   # through rt_dispatch, also a jump table's external cases and its default
            effects.append(flags.effect(ins.cond if k == 'bcond' else ins.code or ''))
        effects.append((False, None))
        return flags.plan(effects, self.flow(insns, entry, end, tables), calls, exits)

    @staticmethod
    def _own(owner, mark, i):
        """Record that the lines from `mark` on belong to instruction i (until the next mark)."""
        owner[mark] = i

    def reg_locals(self, out, marks, decl_at, insns, entry, end, tables):
        """Rewrite a function's lines for the register plan (tools/lifter/regs.py): registers on locals, markers
        expanded into spills and reloads, the locals declared after the flags."""
        n = len(insns)
        own = [None] * len(out)
        starts = sorted(marks)
        for j, m in enumerate(starts):
            stop = starts[j + 1] if j + 1 < len(starts) else len(out)
            for k in range(m, stop):
                if not out[k].startswith('L_'):
                    own[k] = marks[m]
        texts = [''] * (n + 1)
        for k, i in enumerate(own):
            if i is not None:
                texts[i] += out[k] + '\n'
        clean = {i for i, ins in enumerate(insns) if ins.kind in ('bl', 'blr') or regs.RELOAD in texts[i]}
        rp = regs.plan(texts, self.flow(insns, entry, end, tables), clean, REG_LOCALS,
                       regs.CALLEE_SAVED if CALLEE_TRUST else frozenset())
        res = [rp.resolve(i, line) if i is not None else line for line, i in zip(out, own)]
        if rp.regs:
            res.insert(decl_at, '    ' + rp.decl())
            self.stats['reg_local_functions'] += 1
        return res

    def emit_function(self, entry, end, labels, tables, hle):
        # [XCDE] T165: `restrict`: inside a lifted function the Cpu is reached only through c, never through a guest
        # pointer, so the compiler may keep registers / flags in host registers across guest loads and stores (it had to
        # reload them after every store through the may_alias pointers). Measured on 4 cores: process CPU -13 %, the
        # game's worker threads -37 %. (Prototypes stay `Cpu *c`: a qualifier on a parameter is not part of the type.)
        out = [f'void {fname(entry)}(Cpu *restrict c) {{']
        if entry in hle:
            out.append(f'    {sanitize(hle[entry])}(c);')
            out.append('}')
            return out
        out.append(f'    FN_ENTER({hx(entry)});')
        if CALLEE_TRUST and REG_LOCALS and entry in getattr(self, 'trust_breakers', {}):
            out.append(f'    rt_trust_fault(c, {hx(entry)});  /* {self.trust_breakers[entry]} */')
        batch = []
        refs = set()
        SP = RL = RC = ''   # register-locals markers (regs.SPILL / RELOAD / RELOAD_CALL), set below

        def flush():
            if batch:
                out.append('    ' + emit_asm_batch(batch))
                batch.clear()

        fp = None   # function-local flags plan (set below)

        def publish(i):
            return fp.publish(i) if fp is not None and i is not None else ''

        def jump(t, i=None):
            if entry <= t < end:
                return f'goto L_{t:x};'
            if not self.in_text(t):
                return f'{publish(i)}{SP}c->pc = {hx(t)}; __attribute__((musttail)) return rt_dispatch(c);'
            refs.add(t)
            return f'{publish(i)}{SP}__attribute__((musttail)) return {fname(t)}(c);'

        # Functions with jump tables also get the all-instruction fallback: a table whose emulation
        # missed some cases then still reaches the right instruction instead of aborting.
        # [XCDE] only functions with an UNRESOLVED table keep the all-instruction fallback; a resolved table is
        # bounded by its guard, so its switch is exact and the function keeps labels only where it needs them.
        all_labels = any(v == 'all' for v in tables.values())
        if all_labels:
            labels = set(range(entry, end, 4))
        rando_here = [a for a in range(entry, end, 4) if a - self.base in RANDO_CODE]
        if rando_here:
            labels = set(labels)
            for a in rando_here:
                for w in RANDO_CODE[a - self.base]:
                    v = self.lifter.lift(w, a)
                    if v.kind in ('b', 'bcond') and isinstance(v.target, int) and entry <= v.target < end:
                        labels.add(v.target)
        # Function-local NZCV (tools/lifter/flags.py, after TotK Recomp): flags live in a local `Flags F` and reach
        # c->nzcv only where the function leaves other than by a call or a return. Functions re-entered through the
        # all-instruction dispatch, patched or randomizer sites and inline-asm batches keep them in c->nzcv.
        insns = [self.lift(a) for a in range(entry, end, 4)]
        rewritten = {}
        eligible = (not all_labels and not rando_here and not any(i.kind == 'asm' for i in insns)
                    and not any(a in PATCHES and not isinstance(PATCHES[a], JumpTable) for a in range(entry, end, 4)))
        if eligible and REG_LOCALS:
            SP, RL, RC = regs.SPILL, regs.RELOAD, regs.RELOAD_CALL
        if eligible:
            try:
                plan = self.flag_plan(insns, entry, end, tables)
                if plan.used:
                    for i, ins in enumerate(insns):
                        if ins.kind in ('op', 'trap'):
                            rewritten[i] = plan.rewrite(i, ins.code)
                        elif ins.kind == 'bcond':
                            rewritten[i] = plan.rewrite(i, ins.cond)
                    fp = plan
                    out.append('    ' + plan.decl())
                    self.stats['flags_local_functions'] += 1
                    self.stats['flags_reads_direct'] += sum(1 for k in plan.reader if k and k != 'nz')
                    self.stats['flags_reads_packed'] += sum(1 for k in plan.reader if k == 'nz')
                    self.stats['flags_publishing_exits'] += sum(1 for k in plan.exit if k)
            except flags.FlagError as error:
                rewritten = {}
                self.stats['flags_fallback'] += 1
        elif any(i.kind != 'asm' and flags.effect(i.cond if i.kind == 'bcond' else i.code or '') != (False, None)
                 for i in insns):
            self.stats['flags_kept_in_cpu'] += 1

        decl_at = len(out)
        owner = {}   # line of `out` -> index of the instruction it lifts (register plan)
        last = None
        for a in range(entry, end, 4):
            ins = insns[(a - entry) // 4]
            i = (a - entry) // 4
            mark = len(out)
            self._own(owner, mark, i)
            if a in labels:
                flush()
                out.append(f'L_{a:x}:;')
            if a - self.base in RANDO_CODE:  # randomizer patch site: the active seed picks the instruction
                flush()
                out.append(f'    {{ extern uint32_t rando_word(uint64_t); uint32_t rw = rando_word({hx(a)});')
                first = True
                for w in RANDO_CODE[a - self.base]:
                    v = self.lifter.lift(w, a)
                    out.append(f'    {"if" if first else "else if"} (rw == {hx(w)}) {{ ' + ' '.join(self.render_single(v, a, jump, refs)) + ' }')
                    first = False
                out.append('    else { ' + ' '.join(self.render_single(ins, a, jump, refs)) + ' } }')
                last = 'op'
                continue
            if a in PATCHES and not isinstance(PATCHES[a], JumpTable):
                flush()
                out.append('    ' + PATCHES[a].apply(self, a))
                last = 'op'
                continue
            k = ins.kind
            if k == 'asm':
                if len(batch) >= 24:
                    flush()
                batch.append(ins.asm)
                try:
                    emit_asm_batch(batch)
                except Unsupported:
                    batch.pop()
                    flush()
                    batch.append(ins.asm)
                last = k
                continue
            flush()
            if k == 'op' or k == 'trap':
                code = rewritten.get(i, ins.code)
                out.append('    ' + (f'{SP}{code}{RL}' if SP and regs.ESCAPE.search(code) else code))
            elif k == 'b':
                out.append('    ' + jump(ins.target, i))
            elif k == 'bcond':
                out.append(f'    if ({rewritten.get(i, ins.cond)}) {{ {jump(ins.target, i)} }}')
            elif k == 'bl':
                t = ins.target
                if self.in_text(t):
                    refs.add(t)
                    out.append(f'    c->x[30] = {hx(a + 4)}; {SP}{fname(t)}(c);{RC}')
                else:
                    out.append(f'    c->x[30] = {hx(a + 4)}; {SP}c->pc = {hx(t)}; rt_dispatch(c);{RC}')
            elif k == 'blr':
                out.append(f'    c->pc = {ins.target}; c->x[30] = {hx(a + 4)}; {SP}rt_dispatch(c);{RC}')
            elif k == 'br':
                jt = tables.get(a)
                if jt == 'all':
                    out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    continue
                if jt:
                    cases = ' '.join(f'case {hx(t)}: {jump(t, i)}' for t in jt)
                    out.append(f'    switch ({ins.target}) {{ {cases} default: break; }}')
                    if all_labels:
                        out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    else:
                        out.append(f'    {publish(i)}{SP}c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);')
                    continue
                out.append(f'    {publish(i)}{SP}c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);')
            elif k == 'ret':
                out.append(f'    {SP}return;')
            last = k
        flush()
        tail = len(out)
        if last not in ('b', 'br', 'ret', 'trap'):
            out.append('    ' + jump(end, len(insns)) if end < self.hi else f'    rt_udf(c, {hx(end)});')
        if SP:
            self._own(owner, tail, len(insns))
            out = self.reg_locals(out, owner, decl_at, insns, entry, end, tables)
        if all_labels:
            out.append('L_dispatch:')
            cases = ' '.join(f'case {hx(t)}: goto L_{t:x};' for t in range(entry, end, 4))
            out.append(f'    switch (c->pc) {{ {cases} default: break; }}')
            out.append('    __attribute__((musttail)) return rt_dispatch(c);')
        out.append('}')
        self._refs = refs
        return out


# [XCDE] patches come from the game profile (patchfmt.Patch, with expected original words); no randomizer.
PATCHES = {}
RANDO_CODE = {}


def adr_targets(words, lo, in_text):
    """Code addresses materialized by ADR (callbacks): extra function starts.  [XCDE]

    An ADR that is followed within 8 words by a `br xN` is the base of a jump table's cases (clang's
    byte/halfword offset tables): it points into the middle of the function, so it must not start a function
    (that split the function before its cases, and the table resolver found targets outside it).
    """
    out = set()
    n = len(words)
    for i, w in enumerate(words):
        if (w & 0x9F000000) != 0x10000000:
            continue
        if any((words[i + k] & 0xFFFFFC1F) == 0xD61F0000 for k in range(1, 9) if i + k < n):
            continue
        imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
        if imm & (1 << 20):
            imm -= 1 << 21
        t = lo + i * 4 + imm
        if in_text(t):
            out.add(t)
    return out


def write_if_changed(path, text):
    """Leaves unchanged chunks untouched so make only recompiles what a patch affected."""
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(path, 'w') as f:
        f.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('nso')
    ap.add_argument('base', type=lambda s: int(s, 0))
    ap.add_argument('outdir')
    ap.add_argument('--hle')
    ap.add_argument('--chunk', type=int, default=1500)  # functions per file at most
    ap.add_argument('--chunk-bytes', type=int, default=1500000)  # and about this much C (even compile times)
    ap.add_argument('--big-fn', type=int, default=100000)  # bytes of C from which a function gets its own file
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--patches')  # [XCDE] games/<id>/patches.py
    ap.add_argument('--simd', choices=('asm', 'c'),
                    default='asm' if platform.machine().lower() in ('arm64', 'aarch64') else 'c')
    args = ap.parse_args(argv)

    t0 = time.time()
    import patchfmt  # [XCDE]
    PATCHES.update(patchfmt.load_patches(args.patches))
    m = Module(args.name, args.nso, args.base, args.simd)
    for addr, patch in PATCHES.items():
        if isinstance(patch, JumpTable) and m.in_text(addr):
            patch.resolve(m, addr)
    starts = m.boundaries()
    hle = {}
    if args.hle and os.path.exists(args.hle):
        want = {l.strip() for l in open(args.hle) if l.strip() and not l.startswith('#')}
        for name, info, shndx, value, size in m.nso.syms:
            if shndx and name in want:
                hle[args.base + value] = name
    m.trust_breakers = {args.base + value: name for name, info, shndx, value, size in m.nso.syms
                        if shndx and name in TRUST_BREAKERS}

    # Partition text into functions; any branch/call target outside its function becomes a new
    # boundary (splitting the containing function) until a fixpoint is reached.
    start_set = set(starts)
    for i, w in enumerate(m.words):
        if (w & 0xFC000000) == 0x94000000:
            t = m.lo + i * 4 + sext26(w) * 4
            if m.in_text(t):
                start_set.add(t)
    # Code addresses materialized in registers (callbacks, vtables built at runtime): ADR, or
    # ADRP followed shortly by ADD on the same register.
    adrp_regs = {}
    for i, w in enumerate(m.words):
        pc = m.lo + i * 4
        if (w & 0x9F000000) == 0x10000000:  # ADR (handled by adr_targets, below)
            pass
        elif (w & 0x9F000000) == 0x90000000:  # ADRP
            imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
            if imm & (1 << 20):
                imm -= 1 << 21
            adrp_regs[w & 31] = ((pc & ~0xFFF) + (imm << 12), i)
        elif (w & 0xFFC00000) == 0x91000000:  # ADD (imm, 64-bit, no shift)
            rn, rd = (w >> 5) & 31, w & 31
            if rn in adrp_regs and i - adrp_regs[rn][1] <= 8:
                t = adrp_regs[rn][0] + ((w >> 10) & 0xFFF)
                if m.in_text(t):
                    start_set.add(t)
    start_set |= adr_targets(m.words, m.lo, m.in_text)  # [XCDE]
    scanned = {}
    rounds = 0
    while True:
        rounds += 1
        order = sorted(start_set)
        new = set()
        for i, a in enumerate(order):
            end = order[i + 1] if i + 1 < len(order) else m.hi
            key = (a, end)
            if key not in scanned:
                scanned[key] = m.scan(a, end)
            new |= scanned[key][1] - start_set
        if not new:
            break
        start_set |= new
    entries = {}
    for i, a in enumerate(order):
        end = order[i + 1] if i + 1 < len(order) else m.hi
        labels, ext, tables = scanned[(a, end)]
        entries[a] = (end, labels, tables)
    print(f'{args.name}: {len(starts)} boundaries, {len(entries)} functions, {rounds} rounds, {dict(m.stats)}, '
          f'{time.time() - t0:.0f}s', file=sys.stderr)

    os.makedirs(args.outdir, exist_ok=True)
    order = sorted(entries)
    if args.limit:
        order = order[:args.limit]
    nchunks = 0
    ci = 0
    pending = None  # (address, lines, refs) of a big function that starts the next file
    while ci < len(order) or pending:
        body, refs, size, fns = [], set(), 0, []
        while (pending or ci < len(order)) and len(fns) < args.chunk and size < args.chunk_bytes:
            if pending:
                a, lines, r = pending
                pending = None
            else:
                a = order[ci]
                ci += 1
                end, labels, tables = entries[a]
                lines = m.emit_function(a, end, labels, tables, hle)
                r = getattr(m, '_refs', set())
                m._refs = set()
            n = sum(len(l) + 1 for l in lines)
            if n > args.big_fn and fns:  # (a big function gets a file of its own: they compile in parallel)
                pending = (a, lines, r)
                break
            body += lines
            size += n
            fns.append(a)
            refs |= r
            if n > args.big_fn:
                break
        path = os.path.join(args.outdir, f'{args.name}_{nchunks:03d}.c')
        text = ['#include "cpu.h"\n' + ('#include "simd.h"\n' if m.lifter.sc else '')]
        text += [f'void {fname(r)}(Cpu *c);\n' for r in sorted(refs)]
        text += [f'void {sanitize(hle[a])}(Cpu *c);\n' for a in sorted(set(hle) & set(fns))]
        text += ['\n'.join(body), '\n']
        write_if_changed(path, ''.join(text))
        nchunks += 1
        # (the start screen's setup reads these for its progress bar: tools/gamebuild/gamebuild.py)
        print(f'{args.name}: progress {ci}/{len(order)} files {nchunks}', file=sys.stderr, flush=True)
    for fn in os.listdir(args.outdir):  # an earlier run's extra chunks
        mm = re.fullmatch(re.escape(args.name) + r'_(\d{3})\.c', fn)
        if mm and int(mm.group(1)) >= nchunks:
            os.remove(os.path.join(args.outdir, fn))
    import io
    with io.StringIO() as f:
        f.write('#include "cpu.h"\n')
        for a in order:
            f.write(f'void {fname(a)}(Cpu *c);\n')
        f.write(f'const uint64_t {args.name}_fn_addrs[] = {{\n')
        f.write(',\n'.join(hx(a) for a in order))
        f.write('};\n')
        f.write(f'const GuestFn {args.name}_fn_ptrs[] = {{\n')
        f.write(',\n'.join(fname(a) for a in order))
        f.write('};\n')
        f.write(f'const uint64_t {args.name}_fn_count = {len(order)};\n')
        write_if_changed(os.path.join(args.outdir, f'{args.name}_table.c'), f.getvalue())
    # The NSO build id this code was lifted from: the runtime (runtime/loader.c) refuses to start on a
    # different one — other game versions' files crash far from anything localizable (GitHub #18), and the
    # launcher (tools/launcher/launcher.cpp) reads the .h twin to warn inside the UI before starting.
    with io.StringIO() as f:
        f.write("// emitted by gen.py: the NSO build id this module's code was lifted from (compared at load\n"
                "// time by runtime/loader.c: the game files of another game version are refused)\n")
        f.write(f'const char nso_buildid_{args.name}[] = "{m.nso.build_id}";\n')
        write_if_changed(os.path.join(args.outdir, f'{args.name}_id.c'), f.getvalue())
    with io.StringIO() as f:
        f.write('// emitted by gen.py: keep in sync with the *_id.c next to this (NSO build-id check)\n')
        f.write(f'#define XCDE_GAME_{args.name.upper()}_ID "{m.nso.build_id}"\n')
        write_if_changed(os.path.join(args.outdir, f'{args.name}_id.h'), f.getvalue())
    print(f'{args.name}: wrote {nchunks} chunks, {time.time() - t0:.0f}s total', file=sys.stderr, flush=True)


if __name__ == '__main__':
    main()

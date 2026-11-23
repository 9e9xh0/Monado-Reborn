"""T012 (tests first): patch format with expected original bytes (contract: docs/contracts/patch-format.md)."""
import importlib.util
import os
import sys
import tempfile
import unittest

LIFTER = os.path.join(os.path.dirname(__file__), '..', '..', 'tools', 'lifter')
sys.path.insert(0, LIFTER)
import lifter  # noqa: E402
import patchfmt  # noqa: E402


def load_xcde_patches():
    path = os.path.join(os.path.dirname(__file__), '..', '..', 'games', 'xcde', 'patches.py')
    spec = importlib.util.spec_from_file_location('xcde_patches_sites', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeGen:
    def __init__(self, words):
        self._w = words

    def word(self, a):
        return self._w[a]


class TestPatch(unittest.TestCase):
    def test_apply_returns_emitted_code_when_bytes_match(self):
        p = patchfmt.Patch(expected=0xD503201F, emit=lambda gen, a: f'/* nop at {a:#x} */', req='FR-013', note='t')
        self.assertEqual(p.apply(FakeGen({0x100: 0xD503201F}), 0x100), '/* nop at 0x100 */')

    def test_mismatch_aborts_with_address_and_found_bytes(self):
        p = patchfmt.Patch(expected=0xD503201F, emit=lambda g, a: 'x', req='FR-013')
        with self.assertRaises(patchfmt.PatchMismatch) as cm:
            p.apply(FakeGen({0x100: 0x12345678}), 0x100)
        msg = str(cm.exception)
        self.assertIn('0x100', msg)
        self.assertIn('0x12345678', msg)
        self.assertIn('0xd503201f', msg.lower())

    def test_req_is_mandatory(self):
        with self.assertRaises(ValueError):
            patchfmt.Patch(expected=1, emit=lambda g, a: 'x', req='')

    def test_load_patches_from_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'patches.py')
            with open(path, 'w') as f:
                f.write("from patchfmt import Patch\n"
                        "PATCHES = {0x200: Patch(expected=7, emit=lambda g, a: 'ok', req='T012')}\n")
            loaded = patchfmt.load_patches(path)
            self.assertEqual(set(loaded), {0x200})
            self.assertEqual(loaded[0x200].apply(FakeGen({0x200: 7}), 0x200), 'ok')

    def test_load_patches_missing_file_is_empty(self):
        self.assertEqual(patchfmt.load_patches(os.path.join(tempfile.gettempdir(), 'nope_patches.py')), {})

    def test_duplicate_or_misaligned_addresses_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'patches.py')
            with open(path, 'w') as f:
                f.write("from patchfmt import Patch\n"
                        "PATCHES = {0x201: Patch(expected=7, emit=lambda g, a: 'ok', req='T012')}\n")
            with self.assertRaises(ValueError):
                patchfmt.load_patches(path)


class FakeNso:
    build_id = '7e1df8e08d60544bbdca1e333c153c97'   # XCDE 2.0.0 main


class DeclaredGen:
    """The gen.Module API the XCDE patch emitters use, fed with each Patch's declared original word."""

    name = 'main'
    nso = FakeNso()

    def __init__(self, words):
        self._words = words
        self._lifter = lifter.Lifter(0x7000000000, 'c')

    def word(self, addr):
        return self._words[addr]

    def lift(self, addr):
        return self._lifter.lift(self._words[addr], addr)


class TestXcdePatchSites(unittest.TestCase):
    """Every XCDE patch emitter must run on the instruction its Patch declares (games/xcde/patches.py).

    Patch.apply already checks the declared original word, so decoding that word with the lifter exercises the
    emitter without game data: an emitter that assumed the wrong instruction class aborts here instead of after a
    full gen_all.py run (0x70005a98b8 used `_plain` at a `bl` hook, which broke code generation).
    """

    @classmethod
    def setUpClass(cls):
        cls.module = load_xcde_patches()
        words = {}
        for value in vars(cls.module).values():
            if (isinstance(value, dict) and value
                    and all(isinstance(k, int) and isinstance(v, int) for k, v in value.items())):
                words.update(value)   # the *_CONTEXT tables the emitters check against the native build
        cls.patches = sorted((a, p) for a, p in cls.module.PATCHES.items() if isinstance(p, patchfmt.Patch))
        for addr, patch in cls.patches:
            words[addr] = patch.expected
        cls.gen = DeclaredGen(words)

    def test_emitters_accept_their_declared_instruction(self):
        self.assertGreater(len(self.patches), 0)
        for addr, patch in self.patches:
            with self.subTest(addr=hex(addr), req=patch.req):
                self.assertTrue(patch.apply(self.gen, addr).strip())


if __name__ == '__main__':
    unittest.main()

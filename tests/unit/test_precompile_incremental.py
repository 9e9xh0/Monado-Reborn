"""Incremental shader precompile (T282): the build fingerprint and the contracts that keep the precompiled shader cache
across rebuilds that cannot change the shaders. No game data is needed.

Before T282 cache/precompile_done.txt was stamped with the game executable's size and time, so every build precompiled
all shaders again. Now the stamp carries a signature of the inputs (translator/SPIR-V compiler sources, the SPIR-V
options and the game files); the runtime answers `--precompile-status`, and the launcher and RecompLauncher's builder
ask it instead of comparing the executable.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'build'))
import shader_sig  # noqa: E402


class ShaderSig(unittest.TestCase):
    def test_fingerprint_is_16_lowercase_hex(self):
        value = shader_sig.fingerprint()
        self.assertEqual(len(value), 16)
        self.assertTrue(all(c in '0123456789abcdef' for c in value))

    def test_covers_the_translator_compiler_and_precompile_sources(self):
        names = shader_sig.sources()
        for name in ('runtime/nvn/maxwell/translate.c', 'runtime/nvn/vulkan/shaders.c',
                     'runtime/nvn/vulkan/precompile.c', 'tools/build/shader_sig.py'):
            self.assertIn(name, names)

    def test_the_script_prints_the_same_fingerprint(self):
        done = subprocess.run([sys.executable, str(ROOT / 'tools' / 'build' / 'shader_sig.py')], cwd=ROOT,
                              capture_output=True, text=True, check=True)
        self.assertEqual(done.stdout.strip(), shader_sig.fingerprint())

    def test_the_header_defines_the_macro_for_the_build(self):
        header = shader_sig.header()
        self.assertIn('do not edit', header)
        self.assertIn(f'#define XCDE_SHADER_SIG "{shader_sig.fingerprint()}"', header)

    def test_sources_change_the_fingerprint(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inputs = {
                'runtime/nvn/maxwell/translate.c': 'translator',
                'runtime/nvn/vulkan/shaders.c': 'compiler',
                'runtime/nvn/vulkan/shaders.h': 'header',
                'runtime/nvn/vulkan/vk.h': 'vulkan',
                'runtime/nvn/vulkan/precompile.c': 'guesses',
            }
            for rel, text in inputs.items():
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
            first = shader_sig.fingerprint(root)
            self.assertEqual(first, shader_sig.fingerprint(root))
            (root / 'runtime/nvn/maxwell/translate.c').write_text('translator, fixed', encoding='utf-8')
            self.assertNotEqual(first, shader_sig.fingerprint(root))
            added = root / 'runtime/nvn/maxwell' / 'new_helper.h'
            added.write_text('helper', encoding='utf-8')
            changed = shader_sig.fingerprint(root)
            added.write_text('helper, more', encoding='utf-8')
            self.assertNotEqual(changed, shader_sig.fingerprint(root))


class RuntimeContracts(unittest.TestCase):
    def test_the_done_stamp_records_the_inputs_not_the_executable(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        self.assertIn('fprintf(f, "sig %s%c", sig, 10);', precompile)
        self.assertNotIn('stamp_exe', precompile)
        self.assertNotIn('exe %lld %lld', precompile)

    def test_the_status_answer_does_not_scan_or_open_vulkan(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        start = precompile.index('int vk_precompile_status(const char *root) {')
        block = precompile[start:precompile.index('int vk_precompile_main(const char *root) {')]
        self.assertIn('vk_precompile_sig(root, sig)', block)
        for expensive in ('ui_window_init', 'gf_init', 'list_dir', 'scan_file', 'pthread_create'):
            self.assertNotIn(expensive, block)

    def test_a_current_cache_stops_before_any_work(self):
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        start = precompile.index('int vk_precompile_main(const char *root) {')
        body = precompile[start:precompile.index('vkey_guess.glsl', start)]  # (the rest of the setup)
        self.assertIn('precompile_current(sig)', body)
        self.assertIn('XCDE_PRECOMPILE_FORCE', body)
        self.assertLess(body.index('precompile_current(sig)'), body.index('remove("cache/precompile_done.txt")'))

    def test_the_found_shaders_input_masks_are_initialized(self):
        # found[] comes from realloc: a shader that no container named had a garbage int_in/named, so the build made
        # random "named input" variants of it - a different set every run (19,878 / 7,685 / 6,959 stages compiled in
        # three runs) and a SPIR-V cache that never converged.
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        add_shader = precompile[precompile.index('static uint32_t add_shader('):precompile.index('static void add_prog(')]
        self.assertIn('f->int_in = 0; f->named = false;', add_shader)

    def test_the_build_starts_after_the_scan_so_the_cache_converges(self):
        # The named-input masks (found[].int_in/named) are merged by the scanners while the scan runs: a build that
        # starts during it copies a partial mask, so each run translated a different set of variants.
        precompile = (ROOT / 'runtime/nvn/vulkan/precompile.c').read_text(encoding='utf-8')
        body = precompile[precompile.index('int vk_precompile_main(const char *root) {'):]
        scan_joined = body.index('for (int i = 0; i < nscanners; i++) pthread_join(sc[i], NULL);')
        worker_started = body.index('pthread_create(&th[i], NULL, worker, NULL)')
        self.assertLess(scan_joined, worker_started)
        self.assertNotIn('nworkers0', body)

    def test_the_signature_unit_takes_the_build_header_or_a_native_define(self):
        signature = (ROOT / 'runtime/nvn/vulkan/precompile_sig.c').read_text(encoding='utf-8')
        self.assertIn('#include "shader_sig.h"', signature)
        self.assertIn('#ifndef XCDE_SHADER_SIG', signature)
        self.assertIn('.nup', signature)  # (a package is one file: its size and time)
        self.assertIn('cache/variants.bin', signature)  # (documented as deliberately not covered)

    def test_the_status_mode_is_wired_into_the_entry_point(self):
        main = (ROOT / 'runtime/host/main_boot.c').read_text(encoding='utf-8')
        self.assertIn('"--precompile-status"', main)
        self.assertIn('vk_precompile_status', main)


class BuilderAndLauncherContracts(unittest.TestCase):
    def test_the_launcher_asks_the_runtime(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        self.assertIn('--precompile-status', launcher)
        self.assertIn('pc_status_now()', launcher)
        self.assertNotIn('sscanf(line, "exe', launcher)

    def test_the_status_is_memoized_and_dropped_when_the_inputs_can_change(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        self.assertIn('if (g_pc_current < 0) g_pc_current = pc_status_now() ? 1 : 0;', launcher)
        poll = launcher[launcher.index('static void pc_poll() {'):launcher.index('// ------------------------------------------------------------------ game code')]
        self.assertIn('pc_forget();', poll)
        gc_start = launcher.index('static void gc_poll() {')
        gc = launcher[gc_start:launcher.index('\nstatic void ', gc_start + 10)]
        self.assertIn('pc_forget();', gc)
        dump = launcher[launcher.index('static void dump_poll() {'):launcher.index('static void launch() {')]
        self.assertIn('pc_forget();', dump)

    def test_the_explicit_button_forces_a_run(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        start = launcher[launcher.index('static void pc_start(bool then_play, bool fresh = false) {'):
                         launcher.index('static void pc_stop() {')]
        self.assertIn('precompile_done.txt', start)

    def test_the_builder_asks_the_runtime(self):
        builder = (ROOT / 'tools/gamebuild/gamebuild.py').read_text(encoding='utf-8')
        self.assertIn("'--precompile-status', str(dump)", builder)
        self.assertNotIn('f\'exe {stat.st_size}', builder)


class BuildContracts(unittest.TestCase):
    def test_every_build_path_generates_the_fingerprint_header(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('vulkan/precompile_sig', cmake)
        self.assertIn('shader_sig.py --out', cmake)
        for script in ('scripts/build_linux.sh', 'scripts/build_rt_gfx.sh'):
            build = (ROOT / script).read_text(encoding='utf-8')
            self.assertIn('shader_sig.py', build)
            self.assertIn('precompile_sig', build)
        validation = (ROOT / 'tools/diag/validate_port.py').read_text(encoding='utf-8')
        self.assertIn('precompile_sig.c', validation)
        native = (ROOT / 'scripts/native_tests_linux.sh').read_text(encoding='utf-8')
        self.assertIn('test_precompile_sig', native)
        self.assertIn('runtime/nvn/vulkan/precompile_sig.c', native)


if __name__ == '__main__':
    unittest.main()

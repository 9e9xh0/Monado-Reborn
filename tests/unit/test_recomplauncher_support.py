"""RecompLauncher support (T279): AppRun's --game / --prepare modes, run against stand-in programs, the builder's
progress lines, and source contracts for the Big Picture fullscreen and the launcher's update check (no game data)."""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'gamebuild'))
# The lifter needs unicorn and these tests never lift (as in test_gamebuild_progress).
_stub = 'gen' not in sys.modules
if _stub:
    sys.modules['gen'] = types.ModuleType('gen')
import gamebuild as gb  # noqa: E402
if _stub:
    del sys.modules['gen']

# A stand-in that prints its own name, its arguments and the folder it runs in, one per line.
STAND_IN = '#!/bin/sh\necho "$(basename "$0")"\nfor a in "$@"; do echo "arg $a"; done\necho "cwd $(pwd -P)"\n'


@unittest.skipUnless(os.name == 'posix' and shutil.which('sh'), 'AppRun is a POSIX shell script')
class AppRunModes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.appdir = base / 'install'
        for rel in ('usr/bin/monado', 'usr/bin/monado-launcher', 'usr/share/monado/gamebuild/monado-gamebuild'):
            program = self.appdir / rel
            program.parent.mkdir(parents=True, exist_ok=True)
            program.write_text(STAND_IN, encoding='utf-8')
            program.chmod(0o755)
        shutil.copy2(ROOT / 'packaging/linux/AppRun', self.appdir / 'AppRun')
        self.dump = base / 'dump'
        (self.dump / 'exefs').mkdir(parents=True)
        (self.dump / 'exefs/main').write_bytes(b'')
        (self.dump / 'romfs').mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def run_apprun(self, *args):
        env = {'PATH': os.environ.get('PATH', '/usr/bin:/bin'), 'HOME': self.tmp.name,
               'MONADO_DATA_ROOT': str(self.appdir)}
        done = subprocess.run(['sh', str(self.appdir / 'AppRun'), *args], env=env, text=True, capture_output=True)
        return done.returncode, done.stdout.splitlines()

    def test_game_plays_the_dump_directly(self):
        code, lines = self.run_apprun('--game', str(self.dump))
        self.assertEqual(code, 0)
        self.assertEqual(lines[:3], ['monado', 'arg --game', f'arg {self.dump.resolve()}'])

    def test_game_runs_in_the_data_folder(self):
        _, lines = self.run_apprun('--game', str(self.dump))
        self.assertEqual(lines[-1], f'cwd {self.appdir.resolve()}')

    def test_game_leaves_no_link_to_the_dump_in_the_install_folder(self):
        self.run_apprun('--game', str(self.dump))
        self.assertFalse(os.path.lexists(self.appdir / 'targets'))

    def test_game_without_a_dump_stops_with_code_2(self):
        code, _ = self.run_apprun('--game')
        self.assertEqual(code, 2)

    def test_prepare_runs_the_builder_with_its_arguments(self):
        code, lines = self.run_apprun('--prepare', '--root', str(self.dump), '--out', 'x')
        self.assertEqual(code, 0)
        self.assertEqual(lines[:5], ['monado-gamebuild', 'arg --root', f'arg {self.dump}', 'arg --out', 'arg x'])

    def test_play_still_links_the_dump(self):
        self.run_apprun('--play', str(self.dump))
        self.assertEqual((self.appdir / 'targets').resolve(), self.dump.resolve())


class BuilderProgressLines(unittest.TestCase):
    def report(self, env, *args):
        printed = io.StringIO()
        with tempfile.TemporaryDirectory() as out, mock.patch.dict(os.environ, env, clear=False), \
                contextlib.redirect_stdout(printed):
            gb.report(Path(out), *args)
        return printed.getvalue().splitlines()

    def test_recomplauncher_gets_a_progress_line(self):
        gb.set_shares(8)
        lines = self.report({'RECOMPLAUNCHER': '1'}, 'compile', 3, 10, 'Compiling main/x.c', 0.5)
        expected = f'progress {gb.overall("compile", 0.5):.4f} Compiling the game code: 3 of 10 files'
        self.assertEqual(lines, [expected])

    def test_own_launcher_runs_stay_quiet(self):
        lines = self.report({'RECOMPLAUNCHER': ''}, 'compile', 3, 10, 'Compiling main/x.c', 0.5)
        self.assertEqual(lines, [])

    def test_link_step_shows_its_own_text(self):
        self.assertEqual(gb.launcher_step_text('link', 1, 1, 'Already built'), 'Already built')

    def test_prepare_switch_is_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / 'gamebuild.log'
            log.write_text('[00:00:01 +1s] compile main/a.c: exit 0 after 2.5s\n', encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(gb.main(['--prepare', '--write-costs', str(log), str(Path(d) / 'cost.json')]), 0)


class SourceContracts(unittest.TestCase):
    def test_big_picture_opens_fullscreen(self):
        device = (ROOT / 'runtime/nvn/vulkan/device.c').read_text(encoding='utf-8')
        self.assertIn('getenv("RECOMPLAUNCHER_MODE")', device)
        self.assertIn('bool forced_fs = gamescope_fs || bigpicture_fs;', device)

    def test_big_picture_keeps_the_saved_setting(self):
        device = (ROOT / 'runtime/nvn/vulkan/device.c').read_text(encoding='utf-8')
        self.assertNotIn('g_settings.fullscreen = true', device)

    def test_no_update_banner_under_recomplauncher(self):
        launcher = (ROOT / 'tools/launcher/launcher.cpp').read_text(encoding='utf-8')
        self.assertIn('upd_is_release_build() && !from_recomplauncher) upd_start(false);', launcher)

    def test_appimage_carries_the_manifest(self):
        packager = (ROOT / 'tools/package/make_appimage.py').read_text(encoding='utf-8')
        self.assertIn("shutil.copy2(ROOT / 'recomplauncher.json', appdir / 'recomplauncher.json')", packager)


if __name__ == '__main__':
    unittest.main()

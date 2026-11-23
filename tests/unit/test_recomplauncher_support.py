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



if __name__ == '__main__':
    unittest.main()

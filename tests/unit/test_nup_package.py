""".nup packages as the dump (T280): tools/extract/nup.py, check_dump and the builder on small packages written here, AppRun
--game with a package, and source contracts for the runtime's readers. No game data."""
import contextlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'extract'))
sys.path.insert(0, str(ROOT / 'tools' / 'gamebuild'))
import check_dump  # noqa: E402
import nup  # noqa: E402
from manifest import manifest_of  # noqa: E402

# The lifter needs unicorn and these tests never lift (as in test_gamebuild_progress).
_stub = 'gen' not in sys.modules
if _stub:
    sys.modules['gen'] = types.ModuleType('gen')
import gamebuild as gb  # noqa: E402
if _stub:
    del sys.modules['gen']


def nso(build_id_byte, size=0x100):
    """An NSO header with a build id of 32 equal bytes, padded to size."""
    head = bytearray(size)
    head[:4] = b'NSO0'
    head[0x40:0x60] = bytes([build_id_byte]) * 32
    return bytes(head)


def make_nup(path, titles, compressed=()):
    """A .nup of titles [(version, exefs {name: bytes}, romfs {name: bytes or {...}})]; compressed: file names whose
    codec byte says zstd. Layout as tools/extract/nup.py reads it."""
    dirs, files, kids = [], [], []  # dirs: [name, children]; files: [name, data]

    def add_dir(name, tree):
        index = len(dirs)
        dirs.append([name, []])
        for child, value in tree.items():
            dirs[index][1].append(('dir', add_dir(child, value)) if isinstance(value, dict) else ('file', add_file(child, value)))
        return index

    def add_file(name, data):
        files.append([name, data])
        return len(files) - 1

    title_rows = []
    for version, exefs, romfs in titles:
        root = add_dir('', {})
        exefs_dir, romfs_dir = add_dir('exefs', exefs), add_dir('romfs', romfs)
        dirs[root][1] = [('dir', exefs_dir), ('dir', romfs_dir)]
        title_rows.append((root, exefs_dir, romfs_dir, version))
    nd = len(dirs)
    firsts = []
    for _, children in dirs:
        firsts.append(len(kids))
        kids.extend(i if kind == 'dir' else nd + i for kind, i in children)
    names = b''
    name_at = []
    for name, _ in dirs + files:
        name_at.append((len(names), len(name.encode())))
        names += name.encode()
    titles_at, dirs_at = 0xb0, 0xb0 + 0x40 * len(titles)
    files_at = dirs_at + 24 * nd
    kids_at = files_at + 80 * len(files)
    names_at = kids_at + 4 * len(kids)
    data_at = (names_at + len(names) + 15) & ~15
    offsets, data = [], b''
    for _, blob in files:
        offsets.append(data_at + len(data))
        data += blob + b'\0' * (-len(blob) % 16)
    out = bytearray(data_at + len(data))
    out[:4] = b'NUP\x01'
    struct.pack_into('<QQ', out, 0x38, data_at, len(out))
    out[0x80:0x84] = b'TOC\x01'
    struct.pack_into('<IQQQQ', out, 0x84, len(titles), nd, len(files), names_at - 0x80, len(names))
    for i, (root, exefs_dir, romfs_dir, version) in enumerate(title_rows):
        struct.pack_into('<QIIII', out, titles_at + 0x40 * i, 0x0100ff500e34a000, root, exefs_dir, romfs_dir, version)
    for i, (_, children) in enumerate(dirs):
        struct.pack_into('<IIII', out, dirs_at + 24 * i, *name_at[i], firsts[i], len(children))
    for i, (name, blob) in enumerate(files):
        at = files_at + 80 * i
        struct.pack_into('<IIQQQ', out, at, *name_at[nd + i], len(blob), offsets[i], len(blob))
        out[at + 64] = 1 if name in compressed else 0
    struct.pack_into(f'<{len(kids)}I', out, kids_at, *kids)
    out[names_at:names_at + len(names)] = names
    out[data_at:] = data
    Path(path).write_bytes(bytes(out))


BASE = (0, {'main': nso(0x11), 'sdk': nso(0x22)}, {'old.bin': b'old'})
UPDATE = (65536, {'main': nso(0x33), 'sdk': nso(0x44), 'main.npdm': b'META'},
          {'menu': {'a.bin': b'hello romfs'}, 'top.txt': b'top'})


class Package(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'game.nup'
        make_nup(self.path, [BASE, UPDATE])

    def tearDown(self):
        self.tmp.cleanup()

    def test_recognised_by_its_magic(self):
        self.assertTrue(nup.is_package(self.path))

    def test_the_newest_titles_main_is_read(self):
        self.assertEqual(nup.NupPackage(self.path).read('exefs/main'), nso(0x33))

    def test_the_romfs_is_the_newest_titles(self):
        files = {rel: size for rel, size, _ in nup.NupPackage(self.path).walk('romfs')}
        self.assertEqual(files, {'menu/a.bin': 11, 'top.txt': 3})

    def test_a_file_can_be_read_in_part(self):
        self.assertEqual(nup.NupPackage(self.path).read('romfs/menu/a.bin', 5), b'hello')

    def test_a_compressed_package_is_refused(self):
        make_nup(self.path, [BASE, UPDATE], compressed={'a.bin'})
        with self.assertRaisesRegex(nup.NupError, 'compressed'):
            nup.NupPackage(self.path)

    def test_an_incomplete_package_is_refused(self):
        data = self.path.read_bytes()
        self.path.write_bytes(data[:-8])
        with self.assertRaisesRegex(nup.NupError, 'incomplete'):
            nup.NupPackage(self.path)


class CheckDump(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'game.nup'
        make_nup(self.path, [BASE, UPDATE])
        romfs = manifest_of([('menu/a.bin', 11), ('top.txt', 3)])
        self.profile = {'version': '2.0.0', 'exefs_required': ['main', 'sdk', 'main.npdm'], 'romfs_manifest': romfs,
                        'modules': [{'name': 'main', 'build_id': '33' * 32}, {'name': 'sdk', 'build_id': '44' * 32}]}

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_right_version_passes(self):
        self.assertTrue(check_dump.check_dump(str(self.path), self.profile).ok)

    def test_another_version_is_refused_by_build_id(self):
        self.profile['modules'][0]['build_id'] = '55' * 32
        self.assertEqual(check_dump.check_dump(str(self.path), self.profile).code, check_dump.ERR_VERSION)

    def test_an_unusable_package_says_why(self):
        make_nup(self.path, [BASE, UPDATE], compressed={'a.bin'})
        result = check_dump.check_dump(str(self.path), self.profile)
        self.assertIn('compressed', ' '.join(result.messages))


class Builder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'game.nup'
        self.out = Path(self.tmp.name) / 'out'
        make_nup(self.path, [BASE, UPDATE])

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_lifted_modules_come_out_of_the_package(self):
        folder = gb.exefs_folder(self.path, self.out, [{'name': 'main'}, {'name': 'sdk'}])
        self.assertEqual((folder / 'main').read_bytes(), nso(0x33))

    def test_a_folder_dump_keeps_its_own_exefs(self):
        self.assertEqual(gb.exefs_folder(Path(self.tmp.name), self.out, []), Path(self.tmp.name) / 'exefs')

    def resources(self):
        res = Path(self.tmp.name) / 'res'
        res.mkdir(exist_ok=True)
        (res / 'gamelib-id.txt').write_text('id')
        return res

    def test_the_fingerprint_follows_the_modules_not_the_files_date(self):
        first = gb.fingerprint(self.path, self.resources(), {})
        os.utime(self.path, (1, 1))
        self.assertEqual(gb.fingerprint(self.path, self.resources(), {}), first)

    def test_a_package_that_cant_be_used_stops_with_code_2(self):
        make_nup(self.path, [BASE, UPDATE], compressed={'a.bin'})
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(gb.main(['--root', str(self.path), '--out', str(self.out)]), 2)


@unittest.skipUnless(os.name == 'posix' and shutil.which('sh'), 'AppRun is a POSIX shell script')
class AppRunPackage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.appdir = base / 'install'
        game = self.appdir / 'usr/bin/monado'
        game.parent.mkdir(parents=True)
        game.write_text('#!/bin/sh\nfor a in "$@"; do echo "arg $a"; done\n', encoding='utf-8')
        game.chmod(0o755)
        shutil.copy2(ROOT / 'packaging/linux/AppRun', self.appdir / 'AppRun')
        self.package = base / 'game.nup'
        make_nup(self.package, [BASE, UPDATE])

    def tearDown(self):
        self.tmp.cleanup()

    def run_apprun(self, *args):
        env = {'PATH': os.environ.get('PATH', '/usr/bin:/bin'), 'HOME': self.tmp.name, 'MONADO_DATA_ROOT': str(self.appdir)}
        done = subprocess.run(['sh', str(self.appdir / 'AppRun'), *args], env=env, text=True, capture_output=True)
        return done.returncode, done.stdout.splitlines()

    def test_game_plays_a_package(self):
        code, lines = self.run_apprun('--game', str(self.package))
        self.assertEqual(code, 0)
        self.assertEqual(lines, ['arg --game', f'arg {self.package.resolve()}'])

    def test_play_still_needs_a_folder(self):
        code, _ = self.run_apprun('--play', str(self.package))
        self.assertEqual(code, 2)


class RuntimeContracts(unittest.TestCase):
    @staticmethod
    def code(rel):
        return (ROOT / rel).read_text(encoding='utf-8')

    def test_the_loader_reads_through_the_game_files(self):
        self.assertIn('return gf_read(path, len);', self.code('runtime/loader.c'))

    def test_the_romfs_image_is_built_from_the_package(self):
        self.assertIn('gf_is_package() ? scan_package(gf_pkg_node(dir), dir, "", NULL, 0)',
                      self.code('runtime/services/romfs_virtual.c'))

    def test_romfs_reads_start_at_the_files_place_in_the_package(self):
        self.assertIn('gf_pread_at(fd, dst + done, n, f->base + in)', self.code('runtime/services/romfs_virtual.c'))

    def test_package_offsets_are_64_bit_on_windows(self):
        # The Windows build has a 32-bit off_t: a package's reads, size and detection must not go through it.
        game_files = self.code('runtime/services/game_files.c')
        self.assertIn('ov.OffsetHigh = (DWORD)(at >> 32)', game_files)
        self.assertIn('GetFileSizeEx(', game_files)
        self.assertNotIn('fstat(fd, &st) == 0 && pread', game_files)
        self.assertIn('gf_pread_at(p->fd', self.code('runtime/hle/movie.c'))

    def test_movies_are_read_in_place(self):
        movie = self.code('runtime/hle/movie.c')
        self.assertIn('avio_alloc_context(', movie)
        self.assertIn('AVFMT_FLAG_CUSTOM_IO', movie)

    def test_the_precompile_lists_the_package(self):
        self.assertIn('list_package(gf_pkg_node(romfs), romfs)', self.code('runtime/nvn/vulkan/precompile.c'))

    def test_the_game_opens_the_dump_before_reading_it(self):
        boot = self.code('runtime/host/main_boot.c')
        self.assertLess(boot.index('gf_init(root, why, sizeof why)'), boot.index('loader_load(exefs)'))

    def test_every_build_compiles_the_game_files(self):
        for rel in ('CMakeLists.txt', 'scripts/build_linux.sh', 'scripts/build_rt_gfx.sh', 'scripts/build_boot.sh'):
            self.assertIn('services/game_files', self.code(rel), rel)
        for rel in ('scripts/link_boot.sh', 'scripts/link_gfx.sh'):
            self.assertIn('services_game_files.o', self.code(rel), rel)


if __name__ == '__main__':
    unittest.main()

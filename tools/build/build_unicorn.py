#!/usr/bin/env python3
"""Build the unicorn library the Windows first-start builder ships, with tools/unicorn/*.patch applied.

    python tools/build/build_unicorn.py [--out build/unicorn] [--install] [--check] [--skip-build]

GitHub #104: monado-gamebuild.exe died with 0xC0000409 in VCRUNTIME140.dll at the lifter's first guest fault on a
PC where Control Flow Guard stayed active for it (despite the cleared CFG flag in its header, see freeze_gamebuild.py).
unicorn's MSVC build calls the CRT's _setjmp with a zero frame so longjmp does not unwind through generated code; the
CRT's longjmp fast-fails on such a buffer under CFG. windows-own-longjmp.patch gives unicorn its own setjmp/longjmp
pair, so CFG no longer matters. windows-commit-upfront.patch commits a 64 MiB code buffer up front instead of
committing it from a vectored exception handler (what upstream made the default after 2.1.4, unicorn#2364).

Only the AArch64 target is built (all the lifter emulates), which keeps the build to a minute or two. The library
must match the unicorn Python package (same version): --install replaces the package's own library in place, so
PyInstaller's --collect-all unicorn picks it up. --check runs a short emulation in a fresh interpreter with
faulthandler on: with the patch there is no first-touch access violation, so any reported fault fails the check.
On Windows it also runs cfgcheck.exe, built with /guard:cf so Control Flow Guard is active in it: it makes the guest
read unmapped memory, which leaves the emulation through longjmp. The stock wheel's library dies there (0xC0000409);
the build step proves that once, so the check cannot pass by accident.

Windows needs CMake and Visual Studio (the generator the official wheels use; CMake picks the newest); other hosts use Ninja and the
system compiler (useful only to test this script: their unicorn has no such handler).
"""
import argparse
import hashlib
import importlib.metadata
import importlib.util
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = '2.1.4'
URL = f'https://github.com/unicorn-engine/unicorn/archive/refs/tags/{VERSION}.tar.gz'
SHA256 = 'ea8863f095a0136388694e5a6063afd9bb7650e30243dd6251af59c5ce5601f4'
PATCHES = sorted((ROOT / 'tools' / 'unicorn').glob('*.patch'))
WINDOWS = os.name == 'nt'
LIBRARY = 'unicorn.dll' if WINDOWS else 'libunicorn.so.2'
FAST_FAIL = 0xC0000409

# A host with Control Flow Guard (the frozen builder's situation on the #104 PC), hitting unicorn's longjmp.
CFG_CHECK_C = r'''
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <stdio.h>
#include <unicorn/unicorn.h>

int main(void)
{
    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg = {0};
    GetProcessMitigationPolicy(GetCurrentProcess(), ProcessControlFlowGuardPolicy, &cfg, sizeof cfg);
    uc_engine *uc;
    uc_err err = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc);
    if (err != UC_ERR_OK) {
        printf("cfgcheck: uc_open: %s\n", uc_strerror(err));
        return 2;
    }
    const uint64_t base = 0x7000000000ull, x0 = 0x100000;   /* x0: unmapped */
    const unsigned char code[] = {0x01, 0x00, 0x40, 0xf9};   /* ldr x1, [x0] */
    uc_mem_map(uc, base, 0x200000, UC_PROT_ALL);
    uc_mem_write(uc, base, code, sizeof code);
    uc_reg_write(uc, UC_ARM64_REG_X0, &x0);
    fflush(stdout);
    err = uc_emu_start(uc, base, base + sizeof code, 0, 0);  /* the fault leaves through siglongjmp */
    printf("cfgcheck: Control Flow Guard %s, guest fault -> %s\n", cfg.EnableControlFlowGuard ? "on" : "OFF",
           uc_strerror(err));
    uc_close(uc);
    return cfg.EnableControlFlowGuard && err == UC_ERR_READ_UNMAPPED ? 0 : 1;
}
'''
CFG_CHECK_CMAKE = '''cmake_minimum_required(VERSION 3.16)
project(cfgcheck C)
add_executable(cfgcheck cfgcheck.c)
target_include_directories(cfgcheck PRIVATE "${UC_INCLUDE}")
target_link_libraries(cfgcheck PRIVATE "${UC_IMPLIB}")
target_compile_options(cfgcheck PRIVATE /guard:cf)
target_link_options(cfgcheck PRIVATE /guard:cf)
'''

CHECK = r'''
import faulthandler, io, os, sys, tempfile
log = tempfile.TemporaryFile(mode='w+')
faulthandler.enable(file=log, all_threads=True)
import unicorn as uc
from unicorn import arm64_const as A
from unicorn.unicorn_py3 import unicorn as binding
mu = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
mu.mem_map(0x7000000000, 0x200000)               # (the lifter's first call: unicorn initialises here)
mu.mem_write(0x7000000000, bytes.fromhex('400580d2'))   # mov x0, #42
mu.emu_start(0x7000000000, 0, count=1)
x0 = mu.reg_read(A.UC_ARM64_REG_X0)
size = mu.ctl_get_tcg_buffer_size()
faulthandler.disable()
log.seek(0)
faults = log.read()
print(f'unicorn {uc.__version__}: x0={x0}, code buffer {size >> 20} MiB, library {binding.uclib._name}')
if faults:
    print(faults)
patched = os.name != 'nt' or size == 64 << 20   # (the patch's Windows default: proves this library is the one loaded)
if not patched:
    print('the loaded unicorn is not the patched build (code buffer is not 64 MiB)')
sys.exit(0 if x0 == 42 and not faults and patched else 1)
'''


def run(cmd, **kw):
    print('+', ' '.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, **kw)


def fetch_source(work):
    request = urllib.request.Request(URL, headers={'User-Agent': 'Monado-build'})
    with urllib.request.urlopen(request, timeout=300) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise SystemExit(f'{URL}: SHA-256 {digest}, expected {SHA256}')
    archive = work / f'unicorn-{VERSION}.tar.gz'
    archive.write_bytes(data)
    with tarfile.open(archive) as tar:
        tar.extractall(work, filter='data')
    return work / f'unicorn-{VERSION}'


def apply_patches(src):
    # git apply works outside a repository (the work folder is a temporary one, never inside this checkout). The
    # patches are applied with LF line endings whatever the checkout did to them (core.autocrlf on Windows runners).
    for patch in PATCHES:
        normalized = src.parent / patch.name
        normalized.write_bytes(patch.read_bytes().replace(b'\r\n', b'\n'))
        run(['git', 'apply', '--verbose', str(normalized)], cwd=src)


def build(out):
    with tempfile.TemporaryDirectory(prefix='monado-unicorn-') as tmp:
        work = Path(tmp)
        src = fetch_source(work)
        apply_patches(src)
        build_dir = work / 'build'
        args = ['cmake', '-S', src, '-B', build_dir, '-DCMAKE_BUILD_TYPE=Release', '-DUNICORN_ARCH=aarch64',
                '-DUNICORN_BUILD_TESTS=OFF', '-DUNICORN_INSTALL=OFF', '-DBUILD_SHARED_LIBS=ON',
                '-DUNICORN_LEGACY_STATIC_ARCHIVE=OFF']
        # Windows: CMake's default generator, the newest Visual Studio installed (runner images move on: VS 2022, 2026)
        args += ['-A', 'x64'] if WINDOWS else ['-G', 'Ninja']
        run(args)
        run(['cmake', '--build', build_dir, '--config', 'Release', '--parallel'])
        found = [p for p in build_dir.rglob(LIBRARY) if p.is_file()]
        if not found:
            raise SystemExit(f'unicorn: the build produced no {LIBRARY}')
        out.mkdir(parents=True, exist_ok=True)
        shutil.copy2(found[0], out / LIBRARY)
        if WINDOWS:
            build_cfg_check(work, src, build_dir, out)
    (out / 'BUILD.txt').write_text(f'unicorn {VERSION} (aarch64 only) with: '
                                   + ', '.join(p.name for p in PATCHES) + '\n', encoding='utf-8')
    print(f'unicorn {VERSION}: {out / LIBRARY}')


def build_cfg_check(work, src, build_dir, out):
    implib = [p for p in build_dir.rglob('unicorn.lib') if p.is_file()]
    if not implib:
        raise SystemExit('unicorn: the build produced no unicorn.lib (import library) for cfgcheck')
    project = work / 'cfgcheck'
    project.mkdir()
    (project / 'cfgcheck.c').write_text(CFG_CHECK_C, encoding='utf-8')
    (project / 'CMakeLists.txt').write_text(CFG_CHECK_CMAKE, encoding='utf-8')
    run(['cmake', '-S', project, '-B', project / 'build', '-A', 'x64', f'-DUC_INCLUDE={(src / "include").as_posix()}',
         f'-DUC_IMPLIB={implib[0].as_posix()}'])
    run(['cmake', '--build', project / 'build', '--config', 'Release'])
    exe = [p for p in (project / 'build').rglob('cfgcheck.exe') if p.is_file()]
    if not exe:
        raise SystemExit('unicorn: cfgcheck.exe was not built')
    shutil.copy2(exe[0], out / 'cfgcheck.exe')


def run_cfg_check(folder, library):
    """cfgcheck.exe beside `library` (copied into `folder`); returns its exit code."""
    if library.resolve() != (folder / LIBRARY).resolve():
        shutil.copy2(library, folder / LIBRARY)
    result = subprocess.run([str(folder / 'cfgcheck.exe')], capture_output=True, text=True)
    print((result.stdout + result.stderr).strip() or '(no output)', f'[exit 0x{result.returncode & 0xFFFFFFFF:08X}]',
          flush=True)
    return result.returncode & 0xFFFFFFFF


def prove_stock_fails(out):
    """The stock wheel's library must die in cfgcheck, or cfgcheck does not test what #104 needs."""
    lib = package_lib_dir()
    if (lib / 'MONADO_BUILD.txt').is_file():
        print('+ cfgcheck with the stock library: skipped (the package already has the patched one)', flush=True)
        return
    print('+ cfgcheck with the stock wheel library (expected to die with 0xC0000409)', flush=True)
    with tempfile.TemporaryDirectory(prefix='monado-cfgcheck-') as tmp:
        shutil.copy2(out / 'cfgcheck.exe', Path(tmp) / 'cfgcheck.exe')
        code = run_cfg_check(Path(tmp), lib / LIBRARY)
    if code != FAST_FAIL:
        raise SystemExit(f'unicorn: the stock library passed cfgcheck (exit 0x{code:08X}): it no longer reproduces #104')


def package_lib_dir():
    """The installed unicorn package's lib/ folder, found without importing it (that would load and lock the DLL)."""
    installed = importlib.metadata.version('unicorn')
    if installed != VERSION:
        raise SystemExit(f'unicorn: the Python package is {installed}, the patched library is {VERSION}')
    spec = importlib.util.find_spec('unicorn')
    if spec is None or not spec.submodule_search_locations:
        raise SystemExit('unicorn: the Python package is not installed')
    lib = Path(list(spec.submodule_search_locations)[0]) / 'lib'
    if not (lib / LIBRARY).is_file():
        raise SystemExit(f'unicorn: {lib / LIBRARY} not found (unexpected package layout)')
    return lib


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'unicorn')
    ap.add_argument('--skip-build', action='store_true', help='use the library already in --out (a CI cache hit)')
    ap.add_argument('--install', action='store_true', help="replace the Python package's library with it")
    ap.add_argument('--check', action='store_true', help='emulate one instruction with faulthandler on')
    a = ap.parse_args(argv)
    out = a.out.resolve()
    if not a.skip_build:
        build(out)
        if WINDOWS and a.check:
            prove_stock_fails(out)
    elif not (out / LIBRARY).is_file():
        raise SystemExit(f'unicorn: --skip-build but {out / LIBRARY} is missing')
    if a.install:
        lib = package_lib_dir()
        shutil.copy2(out / LIBRARY, lib / LIBRARY)
        shutil.copy2(out / 'BUILD.txt', lib / 'MONADO_BUILD.txt')
        print(f'unicorn: installed {out / LIBRARY} -> {lib / LIBRARY}')
    if a.check:
        print('+ check: one ARM64 instruction with faulthandler on', flush=True)
        subprocess.run([sys.executable, '-c', CHECK], check=True)
        if WINDOWS:
            if not (out / 'cfgcheck.exe').is_file():
                raise SystemExit(f'unicorn: {out / "cfgcheck.exe"} is missing (rebuild without --skip-build)')
            print('+ cfgcheck: a guest fault in a process with Control Flow Guard', flush=True)
            if run_cfg_check(out, out / LIBRARY) != 0:
                raise SystemExit('unicorn: cfgcheck failed: the library does not survive a longjmp under CFG (#104)')
    return 0


if __name__ == '__main__':
    sys.exit(main())

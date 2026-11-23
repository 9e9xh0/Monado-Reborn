"""Validate a user-supplied dump against a game profile (FR-001..004, FR-006).

Accepts a folder holding exefs/ and romfs/ (or --exefs/--romfs separately), or a .nup package, whose exefs and romfs
are already decrypted and read in place (T280, tools/extract/nup.py). Never reads encrypted containers
(.nsp/.xci/.nca/.nsz/.xcz) and never opens key files (Constitution art. II).

The modules' build-ids are the version gate (FR-003). A romfs from another region has the same build-ids but
different localized data (bdat/menu), so by default its size and name differences are reported as warnings. Missing
folders or files are always errors. --strict requires the exact reference dump (FR-004).

Exit codes: 0 ok, 1 usage, 2 container, 3 exefs, 4 version, 5 romfs.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nup  # noqa: E402
from manifest import build_manifest, manifest_of  # noqa: E402

ERR_OK, ERR_USAGE, ERR_CONTAINER, ERR_EXEFS, ERR_VERSION, ERR_ROMFS = 0, 1, 2, 3, 4, 5
CONTAINER_EXT = ('.nsp', '.xci', '.nca', '.nsz', '.xcz')


class Result:
    def __init__(self):
        self.ok = True
        self.code = ERR_OK
        self.messages = []
        self.warnings = []

    def fail(self, code, *msgs):
        self.ok = False
        self.code = code
        self.messages.extend(msgs)
        return self


def _containers_in(path):
    try:
        return [n for n in os.listdir(path) if n.lower().endswith(CONTAINER_EXT)]
    except OSError:
        return []


def _read_build_id(path):
    """First 8 bytes of the NSO build id (offset 0x40), or None if the file is not an NSO."""
    try:
        with open(path, 'rb') as f:
            head = f.read(0x48)
    except OSError:
        return None
    return _build_id_of_head(head)


def _build_id_of_head(head):
    if len(head) < 0x48 or head[:4] != b'NSO0':
        return None
    return head[0x40:0x48].hex()


def _romfs_messages(got, exp, allow_variant):
    """Errors and warnings for a romfs that does not match the reference manifest.

    The build-id already proved the version. Missing folders and file-count differences are always errors: they mean
    an incomplete dump. Different sizes or names in the same set of files are how regional dumps differ (localized
    bdat/menu data), so with allow_variant they are warnings instead of errors.
    """
    errors, warnings = [], []
    for name, (n, b) in exp['dirs'].items():
        g = got['dirs'].get(name)
        if g is None:
            errors.append(f"  folder '{name}' is missing ({n} files)")
        elif g[0] != n:
            errors.append(f"  '{name}': {g[0]} files, {n} expected ({n - g[0]} missing)" if g[0] < n
                          else f"  '{name}': {g[0]} files, {n} expected ({g[0] - n} extra)")
        elif g[1] != b:
            text = f"  '{name}': the same number of files but {g[1]} bytes instead of {b}"
            if allow_variant:
                warnings.append(text + ' (another region?)')
            else:
                errors.append(text + ' (truncated or modified?)')
    for name in got['dirs']:
        if name not in exp['dirs']:
            (warnings if allow_variant else errors).append(f"  unexpected folder '{name}'")
    if not errors and not warnings:
        text = '  the same counts and sizes per folder, but different file names'
        if allow_variant:
            warnings.append(text + ' (another region or a repacked dump?)')
        else:
            errors.append(text + '.')
    return errors, warnings


def check_dump(path, profile, exefs=None, romfs=None, allow_romfs_variant=True):
    r = Result()
    if path is not None and os.path.isfile(path) and nup.is_package(path):
        return _check_package(r, path, profile, allow_romfs_variant)
    if path is not None:
        low = path.lower()
        if os.path.isfile(path):
            if low.endswith(CONTAINER_EXT):
                return r.fail(ERR_CONTAINER,
                              f'"{os.path.basename(path)}" is a container ({os.path.splitext(path)[1]}): '
                              'the extracted content is needed (exefs and romfs folders). The file was not read.')
            return r.fail(ERR_USAGE, 'Expected a folder with exefs/ and romfs/.')
        if not os.path.isdir(path):
            return r.fail(ERR_USAGE, f'No such folder: {path}')
        exefs = exefs or (os.path.join(path, 'exefs') if os.path.isdir(os.path.join(path, 'exefs')) else None)
        romfs = romfs or (os.path.join(path, 'romfs') if os.path.isdir(os.path.join(path, 'romfs')) else None)
        if exefs is None and os.path.isfile(os.path.join(path, 'main')):
            exefs = path  # the folder itself is the exefs
        if exefs is None and romfs is None and _containers_in(path):
            return r.fail(ERR_CONTAINER,
                          'The folder only holds encrypted containers (' + ', '.join(_containers_in(path)) + '). '
                          'The extracted content is needed (exefs and romfs).')
    if not exefs or not os.path.isdir(exefs):
        return r.fail(ERR_EXEFS, 'The exefs folder is missing.')
    have_romfs = romfs and os.path.isdir(romfs)
    return _check_contents(r, profile, allow_romfs_variant,
                           exefs_has=lambda name: os.path.isfile(os.path.join(exefs, name)),
                           build_id_of=lambda name: _read_build_id(os.path.join(exefs, name)),
                           romfs_manifest=lambda: build_manifest(romfs) if have_romfs else None)


def _check_package(r, path, profile, allow_romfs_variant):
    """A .nup package (T280): its newest title's exefs and romfs, read in place. Already decrypted: no keys."""
    try:
        package = nup.NupPackage(path)
    except (nup.NupError, OSError) as error:
        return r.fail(ERR_USAGE, str(error))
    return _check_contents(r, profile, allow_romfs_variant,
                           exefs_has=lambda name: package.find('exefs/' + name) is not None,
                           build_id_of=lambda name: _build_id_of_head(package.read('exefs/' + name, 0x48)),
                           romfs_manifest=lambda: manifest_of((rel, size) for rel, size, _ in package.walk('romfs')))


def _check_contents(r, profile, allow_romfs_variant, exefs_has, build_id_of, romfs_manifest):
    """The checks on a dump's contents, wherever they are: exefs files, the version by build id, the romfs manifest."""
    # exefs: required files, NSO magic
    missing = [n for n in profile['exefs_required'] if not exefs_has(n)]
    if missing:
        return r.fail(ERR_EXEFS, 'Files missing in exefs: ' + ', '.join(missing))
    ids = {}
    for m in profile['modules']:
        bid = build_id_of(m['name'])
        if bid is None:
            return r.fail(ERR_EXEFS, f'exefs/{m["name"]} is not a valid NSO.')
        ids[m['name']] = bid

    # version by build id
    bad = [(m['name'], ids[m['name']], m['build_id'][:16]) for m in profile['modules']
           if ids[m['name']] != m['build_id'][:16]]
    if bad:
        lines = [f'Unsupported version: {profile["version"]} is expected.']
        lines += [f'  {n}: build-id found {f}, expected {e}' for n, f, e in bad]
        return r.fail(ERR_VERSION, *lines)

    # romfs: aggregate manifest
    got = romfs_manifest()
    if got is None:
        return r.fail(ERR_ROMFS, 'The romfs folder is missing.')
    exp = profile['romfs_manifest']
    if got['sha256'] == exp['sha256']:
        r.messages.append(f'OK: version {profile["version"]}, {got["files"]} files in romfs.')
        return r
    errors, warnings = _romfs_messages(got, exp, allow_romfs_variant)
    if errors:
        return r.fail(ERR_ROMFS, 'The romfs does not match the expected version.', *(errors + warnings))
    r.warnings.extend(warnings)
    r.messages.append(f'OK: version {profile["version"]}, {got["files"]} files in romfs '
                      '(differences from the reference dump are listed below).')
    return r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dump', nargs='?')
    ap.add_argument('--exefs')
    ap.add_argument('--romfs')
    ap.add_argument('--profile', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                                                       'games', 'xcde', 'profile.json'))
    ap.add_argument('--strict', action='store_true',
                    help='require the exact romfs of the reference dump (default: report other-region differences '
                         'as warnings)')
    a = ap.parse_args(argv)
    with open(a.profile, encoding='utf-8') as f:
        profile = json.load(f)
    res = check_dump(a.dump, profile, exefs=a.exefs, romfs=a.romfs, allow_romfs_variant=not a.strict)
    for m in res.messages:
        print(m)
    for m in res.warnings:
        print('warning:' + m)
    return res.code


if __name__ == '__main__':
    sys.exit(main())

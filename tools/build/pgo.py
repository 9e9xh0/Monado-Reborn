"""Profile-guided build: merge a training session's counts, publish the profile, and convert it for a compiler.

    python tools/build/pgo.py merge     # build/pgo/raw/*.profraw -> build/pgo/monado.profdata (+ .json record)
    python tools/build/pgo.py publish   # build/pgo/monado.profdata -> data/pgo/monado.proftext.gz (+ .json), committed
    python tools/build/pgo.py index --tool llvm-profdata-19 --out build/pgo/monado.profdata   # data/pgo -> indexed
    python tools/build/pgo.py check     # does the published profile still fit the lifter and headers?

An instrumented build (cmake -DMONADO_PGO=gen) writes build/pgo/raw/default_<id>.profraw; `merge` turns every .profraw
there into one .profdata, next to a record (<name>.json) of what it was trained on: the git tree of the lifter and the
digests of the runtime headers every lifted function inlines. clang drops a function's counts without a word when its
code changed, so a profile goes stale after a lifter or header change; `check` says whether that happened.

The committed copy is LLVM's text format: a compiler only reads an indexed profile written by its own LLVM version or
an older one (zig 0.17.1's clang 22 rejects one from clang 23), while the text format reads anywhere. `index` writes
the indexed file with the llvm-profdata it is given; the release workflow uses LLVM 19's, the oldest compiler there.
The profile holds function names, control-flow hashes and counts - no game code.
See docs/guides/PGO.md.
"""
import argparse
import datetime
import gzip
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tokenize
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# inlined into every lifted function: a change here changes the generated code's control flow everywhere
HEADERS = ('runtime/cpu.h', 'runtime/simd.h', 'runtime/simd_fp.h')
PUBLISHED = ROOT / 'data/pgo/monado.proftext.gz'


def _py_code_digest(data):
    """Hash Python code after stripping comments, standalone docstrings, and blank lines.

    Operates on raw source spans rather than numeric token IDs so the digest is
    identical across Python versions (e.g. 3.12 on CI vs 3.14 locally).
    """
    text = data.decode('utf-8').replace('\r\n', '\n').replace('\r', '\n')
    lines = text.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))

    def pos(row_col):
        return offsets[row_col[0] - 1] + row_col[1]

    raw_tokens = list(tokenize.generate_tokens(io.StringIO(text).readline))
    strip = []
    for t in raw_tokens:
        if t.type == tokenize.COMMENT:
            strip.append((pos(t.start), pos(t.end)))
    code_tokens = [t for t in raw_tokens
                   if t.type not in (tokenize.COMMENT, tokenize.NL, tokenize.ENDMARKER)]
    stmt_start = True
    for i, t in enumerate(code_tokens):
        nxt = code_tokens[i + 1].type if i + 1 < len(code_tokens) else tokenize.NEWLINE
        if stmt_start and t.type == tokenize.STRING and nxt == tokenize.NEWLINE:
            strip.append((pos(t.start), pos(t.end)))
            stmt_start = False
            continue
        if t.type in (tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT):
            stmt_start = True
        else:
            stmt_start = False

    strip.sort()
    out = []
    cur = 0
    for s, e in strip:
        if s > cur:
            out.append(text[cur:s])
        cur = max(cur, e)
    out.append(text[cur:])

    normalized = '\n'.join(line.rstrip() for line in ''.join(out).splitlines() if line.strip())
    return hashlib.sha256(normalized.encode('utf-8')).hexdigest()


def _lifter_code_digest():
    """Combined digest of top-level tools/lifter/*.py codegen sources (excludes tests, comments, docstrings)."""
    h = hashlib.sha256()
    for p in sorted((ROOT / 'tools/lifter').glob('*.py')):
        h.update(f'{p.name}:{_py_code_digest(p.read_bytes())}\n'.encode())
    return h.hexdigest()


def fingerprint():
    tree = subprocess.run(['git', 'rev-parse', 'HEAD:tools/lifter'], cwd=ROOT, capture_output=True, text=True)
    return {'lifter_code': _lifter_code_digest(),
            'lifter_tree': tree.stdout.strip() or None,
            'headers': {h: hashlib.sha256((ROOT / h).read_bytes()).hexdigest() for h in HEADERS}}


def profdata_tool(explicit=None):
    if explicit:
        path = shutil.which(explicit) or (explicit if Path(explicit).is_file() else None)
        if not path:
            sys.exit(f'{explicit}: not found')
        return path
    for name in ('llvm-profdata', 'llvm-profdata.exe'):
        path = shutil.which(name)
        if path:
            return path
    sys.exit('llvm-profdata not found (it comes with clang / LLVM)')


def merge(args):
    raws = sorted(args.raw.glob('*.profraw'))
    if not raws:
        sys.exit(f'no .profraw files in {args.raw}: play an instrumented build first (cmake -DMONADO_PGO=gen)')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([profdata_tool(), 'merge', '-o', str(args.out), *map(str, raws)], check=True)
    record = fingerprint()
    record.update(created=datetime.datetime.now().isoformat(timespec='seconds'), profraw=[p.name for p in raws],
                  bytes=args.out.stat().st_size)
    args.out.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(f'{args.out} ({record["bytes"] // 1024} KiB) from {len(raws)} .profraw file(s)')


def publish(args):
    if not args.profile.is_file():
        sys.exit(f'{args.profile}: no such profile (tools/build/pgo.py merge first)')
    text = subprocess.run([profdata_tool(), 'merge', '--text', '-o', '-', str(args.profile)], check=True,
                          capture_output=True).stdout
    PUBLISHED.parent.mkdir(parents=True, exist_ok=True)
    with open(PUBLISHED, 'wb') as raw:  # (mtime 0, no name: the same profile gives the same bytes)
        with gzip.GzipFile(fileobj=raw, mode='wb', compresslevel=9, mtime=0, filename='') as out:
            out.write(text)
    record_path = args.profile.with_suffix('.json')
    record = json.loads(record_path.read_text(encoding='utf-8')) if record_path.is_file() else fingerprint()
    record['text_bytes'] = len(text)
    published_record(PUBLISHED).write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(f'{PUBLISHED.relative_to(ROOT)} ({PUBLISHED.stat().st_size // 1024} KiB): commit it with its .json')


def published_record(path):
    return path.with_name(path.name.split('.')[0] + '.json')


def index(args):
    if not args.input.is_file():
        sys.exit(f'{args.input}: no published profile')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    text = args.out.with_suffix('.proftext')
    text.write_bytes(gzip.decompress(args.input.read_bytes()))
    subprocess.run([profdata_tool(args.tool), 'merge', '-o', str(args.out), str(text)], check=True)
    text.unlink()
    print(f'{args.out} ({args.out.stat().st_size // 1024} KiB) from {args.input.name}')


def check(args):
    record_path = published_record(args.profile) if args.profile.name.endswith('.gz') else args.profile.with_suffix('.json')
    if not record_path.is_file():
        sys.exit(f'{record_path}: no record (merge the profile with tools/build/pgo.py merge)')
    record = json.loads(record_path.read_text(encoding='utf-8'))
    now = fingerprint()
    stale = []
    if 'lifter_code' in record:
        if record['lifter_code'] != now['lifter_code']:
            stale.append('the lifter changed since the training')
    elif record.get('lifter_tree') != now['lifter_tree']:
        stale.append('the lifter changed since the training')
    stale += [f'{h} changed since the training' for h, d in record['headers'].items() if now['headers'].get(h) != d]
    for line in stale:
        print('stale:', line)
    if stale:
        print('retrain: regenerate the game code, build with -DMONADO_PGO=gen, play, merge (docs/guides/PGO.md)')
        return 1
    print(f'{args.profile}: fits the current lifter and headers')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    m = sub.add_parser('merge', help='merge build/pgo/raw/*.profraw into a .profdata')
    m.add_argument('--raw', type=Path, default=ROOT / 'build/pgo/raw')
    m.add_argument('--out', type=Path, default=ROOT / 'build/pgo/monado.profdata')
    p = sub.add_parser('publish', help='write the committed text profile data/pgo/monado.proftext.gz')
    p.add_argument('--profile', type=Path, default=ROOT / 'build/pgo/monado.profdata')
    i = sub.add_parser('index', help='convert the committed text profile to an indexed one for a compiler')
    i.add_argument('--input', type=Path, default=PUBLISHED)
    i.add_argument('--tool', help='llvm-profdata of the oldest LLVM that will read the result (default: PATH)')
    i.add_argument('--out', type=Path, default=ROOT / 'build/pgo/monado.profdata')
    c = sub.add_parser('check', help='does a profile still fit the lifter and runtime headers?')
    c.add_argument('--profile', type=Path, default=PUBLISHED)
    args = parser.parse_args()
    if args.command == 'check':
        return check(args)
    {'merge': merge, 'publish': publish, 'index': index}[args.command](args)
    return 0


if __name__ == '__main__':
    sys.exit(main())

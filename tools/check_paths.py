#!/usr/bin/env python3
"""Check repo-relative path literals in build, CI and tooling files.

Usage:
    python tools/check_paths.py [--verbose]

Scans every tracked `.sh`, `.bat`, `.ps1`, `.yml`, `.yaml` and `.py` file plus `CMakeLists.txt` for literals that
start with one of the repository's tracked top-level directories (`tools/`, `scripts/`, `runtime/`, `tests/`,
`third_party/`, `docs/`, `cmake/`, `packaging/`, `games/`, `data/`) and reports the ones that do not exist as a
tracked file or a directory holding tracked files. It catches the path drift a move leaves behind: a renamed script,
a deleted tree, a command in CI or a build script that points at a file nobody has any more.

Not checked on purpose:
- work folders that are never tracked (`build/`, `targets/`, `generated/`, `cache/`, `save/`, `sdk/`, `dist/`, ...);
- placeholders (`<...>`, `{...}`, `*`, `$VAR`, `%VAR%`, environment variables, ...);
- paths belonging to other projects (upstream trees such as `tools/fsr4cap` of bbport) via `ALLOWED`;
- vendored trees (`third_party/`) and generated Markdown (documentation links are checked by `tools/check_docs.py`);
- the checker files themselves, which deliberately contain sample and allowlisted paths.

Exit status: 0 when clean, 1 when unresolved path literals are found.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Literals rooted at one of these are validated; anything else is a work folder or other project's path.
VALIDATED_TOPS = ('tools/', 'scripts/', 'runtime/', 'tests/', 'third_party/', 'docs/', 'cmake/',
                  'packaging/', 'games/', 'data/')
# Literals that are valid but do not name a file of this repository:
# - tools/fsr4cap: bbport's capture tooling (upstream project, described in third_party/fsr4cap);
# - docs/pgo.md: TotK Recomp's documentation, cited as design reference;
# - games/*: paths inside the staged gamebuild resource folder, not repository paths;
# - data/xcde*.ini, data/game-root.txt, data/targets: files of the data folder pgo_train.sh is given;
# - third_party/SPIRV-Tools/tools: inside the dxil-spirv build tree built under build/ by build_fsr4prep.py.
ALLOWED = {
    'tools/fsr4cap',
    'docs/pgo.md',
    'games/profile.json',
    'games/patches.py',
    'games/hle_symbols.txt',
    'games/compile_cost.json',
    'data/xcde.ini',
    'data/xcde_env.ini',
    'data/game-root.txt',
    'data/targets',
    'third_party/SPIRV-Tools/tools',
}
# Paths that are never tracked: their literals are runtime output, not build inputs.
WORK_DIRS = ('build/', 'targets/', 'generated/', 'cache/', 'crash/', 'save/', 'play/', 'sdk/', 'dist/',
             'reports/', 'mods/', 'saves/', 'ref/', 'dump/', '.venv/')
# Files with intentional sample/other-project paths (documentation checker data, this checker and its tests).
SKIP_FILES = {
    'tools/check_docs.py',
    'tools/check_paths.py',
    'tests/unit/test_check_paths.py',
    'tests/unit/test_check_docs.py',
}
TEXT_SUFFIXES = ('.sh', '.bat', '.ps1', '.yml', '.yaml', '.py')
# A path literal: a validated top-level directory followed by path characters. The lookbehind keeps URLs and
# longer paths (a/b/tools/...) from matching.
PATH_RE = re.compile(r'(?<![\w./-])((?:%s)[A-Za-z0-9_./+-]+)' % '|'.join(re.escape(t) for t in VALIDATED_TOPS))
# Characters that make a token a placeholder/expression rather than a literal path.
NOT_A_PATH = ('<', '>', '{', '}', '*', '|', '$', '%', '&', '~', '?', '!', '\\', '`', '"', "'", ' ')
ANCHOR_RE = re.compile(r'#.*$')
LINE_RE = re.compile(r':[0-9]+$')


def tracked_files(root=ROOT):
    """Tracked files as a set of repo-relative, forward-slash paths."""
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=root, capture_output=True, check=True)
    return {p for p in out.stdout.decode('utf8', 'replace').split('\0') if p}


def scanned_files(tracked):
    return sorted(f for f in tracked
                  if f not in SKIP_FILES
                  and not f.startswith('third_party/')
                  and (f.endswith(TEXT_SUFFIXES) or f == 'CMakeLists.txt'))


def _resolves(path, tracked):
    path = path.rstrip('/')
    if not path or path.startswith(('..', '/', '-')):
        return False
    return path in tracked or any(f.startswith(path + '/') for f in tracked)


def _clean(token):
    token = ANCHOR_RE.sub('', token)
    token = LINE_RE.sub('', token)
    return token.rstrip('.,;:!?)]')


def check_text(text, tracked, source='<text>'):
    """Return a list of unresolved path literals in one file. Exposed for the unit tests."""
    if source.endswith(('.bat', '.ps1')):  # Windows scripts use backslashes for repo paths
        text = text.replace('\\', '/')
    issues = []
    for number, line in enumerate(text.splitlines(), 1):
        if any(marker in line for marker in ('MANIFIESTO', 'placeholder')):
            continue
        for match in PATH_RE.finditer(line):
            token = _clean(match.group(1))
            following = line[match.end():match.end() + 1]
            if following in ('*', '<', '>', '{', '$', '%', '&', '~', '?'):
                continue  # a glob or placeholder such as runtime/hle/quickstep_*.c, docs/<out>/x.md
            if not token or any(marker in token for marker in NOT_A_PATH):
                continue
            if token.rstrip('/') in ALLOWED:
                continue
            if token.startswith(WORK_DIRS):
                continue
            if _resolves(token, tracked):
                continue
            issues.append(f'{source}:{number}: unresolved path literal -> {token}')
    return issues


def check_repository(root=ROOT):
    tracked = tracked_files(root)
    problems = []
    for name in scanned_files(tracked):
        path = os.path.join(root, name)
        try:
            text = open(path, encoding='utf-8', errors='replace').read()
        except OSError as error:
            problems.append(f'{name}: cannot read: {error}')
            continue
        problems.extend(check_text(text, tracked, name))
    return problems


def main(argv):
    verbose = '--verbose' in argv
    problems = check_repository()
    for problem in problems:
        print(problem)
    if problems:
        print(f'\n{len(problems)} unresolved path literal(s).', file=sys.stderr)
        return 1
    if verbose:
        print('repo path literals: clean')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

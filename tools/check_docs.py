#!/usr/bin/env python3
"""Check documentation links and repo paths (documentation integrity).

Usage:
    python tools/check_docs.py [--verbose]

Scans every tracked Markdown file outside `third_party/` and verifies:

- local Markdown links (`[text](target)`, `![alt](target)`) resolve to a tracked file or directory;
- backticked repo-relative paths resolve against `git ls-files`.

Allowlisted on purpose: runtime/work directories that are never tracked (`build/`, `targets/`, `generated/`, `cache/`,
`crash/`, `save/`, `play/`, `sdk/`), placeholders (`<...>`, `{...}`, `*`, `$...`), URLs, and documentation that
belongs to other projects (`EXTERNAL_DOCS`). Only paths that start with a tracked top-level directory are validated,
so work-folder and other-project paths are ignored rather than guessed at.

Exit status: 0 when clean, 1 when unresolved references are found.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Directories that are never tracked: their paths are runtime output, not documentation targets.
RUNTIME_DIRS = ('build/', 'targets/', 'generated/', 'cache/', 'crash/', 'save/', 'play/', 'sdk/', 'dist/', '.venv/')
# Only paths rooted in one of these are treated as repo-relative and validated.
VALIDATED_TOP = ('docs/', 'tests/', 'tools/', 'scripts/', 'runtime/', 'games/', 'data/', 'third_party/', '.github/')
# Docs that belong to other projects and are deliberately not in this repository.
EXTERNAL_DOCS = {
    'docs/pgo.md',                          # TotK Recomp
    'runtime/nvn_vk_m4.c',                  # ssbu-recomp (reviewed project)
    'runtime/nvn_vk_prewarm.h',             # ssbu-recomp (reviewed project)
    'runtime/hitch.c',                      # ssbu-recomp (reviewed project)
    'docs/HISTORICO.md',                    # raulzrrs/letsgo-recomp
    'docs/BUILDING.md',                     # WindowSmash
    'docs/TECHNICAL.md',                    # WindowSmash
    'docs/nvn/DEBUG.md',                    # Mario Wonder project
    'docs/nvn/DEBUG-2.md',                  # Mario Wonder project
    'docs/pipeline-prediction-stalls.md',   # TotK Recomp / other ports
}
# Inline-code spans that are syntax, not paths.
NOT_A_PATH = ('://', ' ', '\t', '<', '>', '{', '}', '*', '|', '="', '=>', '$', '\\', 'NNNN')

LINK_RE = re.compile(r'!?\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
CODE_RE = re.compile(r'`([^`\n]+)`')
SKIP_PREFIXES = ('http://', 'https://', 'mailto:', '#', '/', '<')


def tracked_files(root=ROOT):
    """Tracked files as a set of repo-relative, forward-slash paths."""
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=root, capture_output=True, check=True)
    return {p for p in out.stdout.decode('utf8', 'replace').split('\0') if p}


def documentation_files(tracked):
    return sorted(f for f in tracked
                  if f.endswith('.md') and not f.startswith(('third_party/', 'generated/')))


def _resolves(path, tracked):
    """True if `path` is a tracked file or a directory holding tracked files."""
    path = path.rstrip('/')
    if not path or path.startswith('..'):
        return False
    return path in tracked or any(f.startswith(path + '/') for f in tracked)


def _check_link(target, source, tracked):
    if target.startswith(SKIP_PREFIXES):
        return None
    target = target.split('#')[0]
    if not target:
        return None
    if target.startswith('//'):  # protocol-relative URL
        return None
    base = os.path.dirname(source)
    resolved = os.path.normpath(os.path.join(base, target)).replace(os.sep, '/')
    if _resolves(resolved, tracked):
        return None
    return f'broken Markdown link -> {target}'


def _check_code_span(token, source, tracked):
    t = token.strip()
    while t.startswith('`') and t.endswith('`') and len(t) > 2:
        t = t[1:-1].strip()
    if not t or any(marker in t for marker in NOT_A_PATH):
        return None
    if t.startswith(('-', '--', '#')):
        return None
    if t.endswith(':'):
        return None
    if t in EXTERNAL_DOCS:
        return None
    first = t.split('/')[0] + '/'
    if '/' in t:
        if first in RUNTIME_DIRS:
            return None
        if first not in VALIDATED_TOP and t not in ('README.md', 'CONTRIBUTING.md', 'NOTICE.md', 'LICENSE'):
            return None  # work-folder or other-project path: not ours to validate
    elif not t.endswith(('.md', '.txt')):
        return None
    # A bare doc name is accepted when a tracked file has that name (e.g. `tasks.md` in historical notes).
    base = os.path.dirname(source)
    candidates = [os.path.normpath(os.path.join(base, t)).replace(os.sep, '/'), t]
    candidates += [f for f in tracked if os.path.basename(f) == t] if '/' not in t else []
    if any(_resolves(c, tracked) and (c in tracked or os.path.isdir(os.path.join(ROOT, c)))
           for c in candidates):
        return None
    if '/' not in t and any(os.path.basename(f) == t for f in tracked):
        return None
    return f'unresolved path -> {t}'


def check_text(text, source, tracked):
    """Return a list of issues for one document. Exposed for the unit tests."""
    issues = []
    for m in LINK_RE.finditer(text):
        issue = _check_link(m.group(1), source, tracked)
        if issue:
            issues.append(issue)
    for m in CODE_RE.finditer(text):
        issue = _check_code_span(m.group(1), source, tracked)
        if issue:
            issues.append(issue)
    return issues


def check_repository(root=ROOT):
    tracked = tracked_files(root)
    problems = []
    for doc in documentation_files(tracked):
        path = os.path.join(root, doc)
        try:
            text = open(path, encoding='utf-8', errors='replace').read()
        except OSError as e:
            problems.append((doc, [f'cannot read: {e}']))
            continue
        issues = check_text(text, doc, tracked)
        if issues:
            problems.append((doc, issues))
    return problems


def main(argv):
    verbose = '--verbose' in argv
    problems = check_repository()
    total = sum(len(v) for _, v in problems)
    for doc, issues in problems:
        for issue in issues:
            print(f'{doc}: {issue}')
    if problems:
        print(f'\n{total} unresolved reference(s) in {len(problems)} document(s).', file=sys.stderr)
        return 1
    if verbose:
        print('documentation links: clean')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

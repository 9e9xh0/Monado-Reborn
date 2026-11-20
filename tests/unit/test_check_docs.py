import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import check_docs  # noqa: E402

TRACKED = {
    'README.md',
    'CONTRIBUTING.md',
    'docs/README.md',
    'docs/guides/BUILD-CMAKE.md',
    'docs/guides/LINUX.md',
    'docs/dev/tasks.md',
}


class CheckDocsTest(unittest.TestCase):
    def test_synthetic_broken_markdown_link_is_reported(self):
        issues = check_docs.check_text('See [the guide](docs/missing.md).', 'README.md', TRACKED)
        self.assertEqual(len(issues), 1)
        self.assertIn('docs/missing.md', issues[0])

    def test_synthetic_broken_backticked_path_is_reported(self):
        issues = check_docs.check_text('Run `docs/missing.md` next.', 'README.md', TRACKED)
        self.assertEqual(len(issues), 1)
        self.assertIn('docs/missing.md', issues[0])

    def test_valid_link_and_valid_path(self):
        text = ('See [the build guide](docs/guides/BUILD-CMAKE.md), '
                'the `docs/guides/LINUX.md` guide and the ledger `docs/dev/tasks.md`.')
        self.assertEqual(check_docs.check_text(text, 'README.md', TRACKED), [])

    def test_runtime_work_paths_and_placeholders_are_ignored(self):
        text = ('`build/xcde.exe`, `targets/exefs`, `cache/vk_pipelines.bin`, '
                '`docs/evidence/<out>/coverage.md` and `build/<name>_<frame>.png`')
        self.assertEqual(check_docs.check_text(text, 'README.md', TRACKED), [])

    def test_other_project_docs_are_ignored(self):
        self.assertEqual(check_docs.check_text('`docs/pgo.md` there', 'docs/README.md', TRACKED), [])

    def test_repository_documentation_is_clean(self):
        result = subprocess.run([sys.executable, '-X', 'utf8', os.path.join(ROOT, 'tools', 'check_docs.py')],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()

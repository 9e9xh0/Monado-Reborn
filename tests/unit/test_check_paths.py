"""Repo path literals in build, CI and tooling files (tools/check_paths.py)."""
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import check_paths  # noqa: E402

TRACKED = {
    'CMakeLists.txt',
    'tools/check_docs.py',
    'runtime/kernel.c',
    'runtime/nvn/vulkan/exec.c',
    'docs/README.md',
    'tests/native/test_fibers.c',
}


class CheckPathsTest(unittest.TestCase):
    def test_existing_files_and_directories_pass(self):
        text = ('Run `tools/check_docs.py`, `runtime/kernel.c`, `runtime/nvn/vulkan/exec.c` and the '
                '`tests/native` tree.')
        self.assertEqual(check_paths.check_text(text, TRACKED, 'README.md'), [])

    def test_moved_or_deleted_file_is_reported(self):
        text = 'The old command was `tools/moved_away.py`; the tree `runtime/gone.c` is gone.'
        issues = check_paths.check_text(text, TRACKED, 'README.md')
        self.assertEqual(len(issues), 2)
        self.assertIn('tools/moved_away.py', issues[0])
        self.assertIn('runtime/gone.c', issues[1])

    def test_work_folders_placeholders_and_urls_are_ignored(self):
        text = ('`build/xcde.exe` `targets/exefs` `generated/main/main.c` `cache/vk_pipelines.bin` '
                '`docs/evidence/<out>/coverage.md` `runtime/$file.c` `https://example.com/tools/page.py`')
        self.assertEqual(check_paths.check_text(text, TRACKED, 'README.md'), [])

    def test_glob_and_anchor_suffixes_are_ignored(self):
        text = '`runtime/hle/quickstep_*.c` and `runtime/nvn/vulkan/exec.c#L24`'
        self.assertEqual(check_paths.check_text(text, TRACKED, 'x.sh'), [])

    def test_windows_backslash_paths_are_checked(self):
        self.assertEqual(check_paths.check_text('tools\\check_docs.py', TRACKED, 'x.bat'), [])
        issues = check_paths.check_text('tools\\gone.py', TRACKED, 'x.bat')
        self.assertEqual(len(issues), 1)

    def test_repository_is_clean(self):
        result = subprocess.run([sys.executable, '-X', 'utf8', os.path.join(ROOT, 'tools', 'check_paths.py')],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()

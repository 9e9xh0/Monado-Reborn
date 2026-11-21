"""Lifter codegen fingerprint stability and PGO check (tools/build/pgo.py)."""
import argparse
import contextlib
import io
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'build'))
import pgo  # noqa: E402


class PgoFingerprintTest(unittest.TestCase):
    def test_ignores_comments_docstrings_whitespace_and_crlf(self):
        src_lf = (
            '"""Module docstring."""\n'
            'def emit(x):\n'
            '    """Function docstring."""\n'
            '    # leading comment\n'
            '    return f"a {{b}} {x + 1}"\n'
        ).encode('utf-8')
        src_crlf = (
            '"""Updated module docstring."""\r\n'
            '\r\n'
            'def emit(x):\r\n'
            '    """Different docstring\r\n'
            '    spanning lines."""\r\n'
            '    return f"a {{b}} {x + 1}"  # inline comment\r\n'
            '\r\n'
        ).encode('utf-8')
        self.assertEqual(pgo._py_code_digest(src_lf), pgo._py_code_digest(src_crlf))

    def test_detects_code_and_fstring_changes(self):
        base = 'def emit(x):\n    return f"a {{b}} {x + 1}"\n'.encode('utf-8')
        diff_op = 'def emit(x):\n    return f"a {{b}} {x - 1}"\n'.encode('utf-8')
        diff_const = 'def emit(x):\n    return f"a {{b}} {x + 2}"\n'.encode('utf-8')
        diff_fstr = 'def emit(x):\n    return f"a {{b}} {x + 1}!"\n'.encode('utf-8')
        base_digest = pgo._py_code_digest(base)
        self.assertNotEqual(base_digest, pgo._py_code_digest(diff_op))
        self.assertNotEqual(base_digest, pgo._py_code_digest(diff_const))
        self.assertNotEqual(base_digest, pgo._py_code_digest(diff_fstr))

    def test_published_profile_matches_current_lifter(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = pgo.check(argparse.Namespace(profile=pgo.PUBLISHED))
        self.assertEqual(rc, 0, buf.getvalue())


if __name__ == '__main__':
    unittest.main()

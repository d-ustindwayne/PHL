"""Checks that the C version behaves exactly like the pure Python version.

Skipped when the C version was not compiled.
"""

import math
import random
import sys
import unittest
from collections import OrderedDict
from pathlib import Path

from phl import PHLError, _parser, _writer

try:
    from phl import _speedups
except ImportError:
    _speedups = None

CONFORMANCE = Path(__file__).resolve().parents[3] / "conformance"

FRAGMENTS = [
    '"', "\\", "\n", ",", ";", ":", "[", "]", "{", "}", "(", ")", "-", ".", "1", "e", "//",
    '"true"', '"NIL"', "99999999999999999999", "-99999999999999999999", "0.5", "é", "😀",
    "﻿", "a", "\r\n", " ", "\t", "\x00", "\x01",
]


def outcome(function, *args, **kwargs):
    """The result, or the position of the error when it fails."""
    try:
        return ("ok", function(*args, **kwargs))
    except PHLError as error:
        return ("error", error.line, error.column)


def same(a, b):
    """Equal, and with the same types and key order all the way down."""
    pending = [(a, b)]
    while pending:
        a, b = pending.pop()
        if type(a) is not type(b):
            return False
        if isinstance(a, dict):
            if list(a) != list(b):
                return False
            pending.extend((a[key], b[key]) for key in a)
        elif isinstance(a, list):
            if len(a) != len(b):
                return False
            pending.extend(zip(a, b))
        elif isinstance(a, float):
            if a != b or math.copysign(1, a) != math.copysign(1, b):
                return False
        elif a != b:
            return False
    return True


@unittest.skipIf(_speedups is None, "the C version is not compiled")
class ParityTests(unittest.TestCase):
    def assert_same_read(self, text):
        for coerce in (True, False):
            for pure, native in (
                (_parser.loads, _speedups.loads),
                (_parser.loads_table, _speedups.loads_table),
            ):
                a = outcome(pure, text, coerce=coerce)
                b = outcome(native, text, coerce=coerce)
                self.assertEqual(a[0], b[0], repr(text))
                if a[0] == "error":
                    self.assertEqual(a, b, repr(text))
                else:
                    self.assertTrue(same(a[1], b[1]), repr(text))

    def assert_same_write(self, value, table=False):
        pure = _writer.dumps_table if table else _writer.dumps
        native = _speedups.dumps_table if table else _speedups.dumps
        for indent in (4, 2, 0, None, -1):
            a = outcome(pure, value, indent=indent)
            b = outcome(native, value, indent=indent)
            self.assertEqual(a, b)

    def test_conformance_files(self):
        for source in sorted(CONFORMANCE.glob("*/*/*.*")):
            if source.suffix in (".phl", ".hpl"):
                with self.subTest(source.name):
                    self.assert_same_read(source.read_text(encoding="utf-8"))

    def test_conformance_values_are_written_the_same(self):
        for source in sorted((CONFORMANCE / "phl" / "valid").glob("*.phl")):
            with self.subTest(source.name):
                self.assert_same_write(_parser.loads(source.read_text(encoding="utf-8")))
        for source in sorted((CONFORMANCE / "hpl" / "valid").glob("*.hpl")):
            with self.subTest(source.name):
                rows = _parser.loads_table(source.read_text(encoding="utf-8"))
                self.assert_same_write(rows, table=True)

    def test_random_changes_to_valid_files(self):
        rng = random.Random(20261110)
        sources = [
            path.read_text(encoding="utf-8")
            for path in sorted(CONFORMANCE.glob("*/valid/*.*"))
            if path.suffix in (".phl", ".hpl") and path.stem != "deep"
        ]
        for _ in range(4000):
            text = rng.choice(sources)
            for _ in range(rng.randint(1, 4)):
                at = rng.randrange(len(text) + 1)
                kind = rng.randrange(2)
                if kind == 0:
                    text = text[:at] + rng.choice(FRAGMENTS) + text[at:]
                else:
                    text = text[:at] + text[at + rng.randint(1, 8):]
            self.assert_same_read(text)

    def test_numbers(self):
        for text in [
            "a: 9223372036854775807",
            "a: 9223372036854775808",
            "a: -9223372036854775808",
            "a: -9223372036854775809",
            "a: 123456789012345678901234567890",
            "a: -0",
            "a: -0.0",
            "a: 007",
            "a: 0.1",
            "a: 149.50",
            "a: 1.7976931348623157",
            "a: 0." + "0" * 400 + "1",
            "a: " + "9" * 400 + ".0",
            "a: 1.",
            "a: 1.5x",
            "a: -x",
            "a: -",
            "1.5: 2",
            "-1: 2",
        ]:
            self.assert_same_read(text)

    def test_too_many_digits_for_python(self):
        if not hasattr(sys, "get_int_max_str_digits"):
            self.skipTest("this Python has no limit on digits")
        self.assert_same_read("a: " + "9" * (sys.get_int_max_str_digits() + 1))
        self.assert_same_read("a: -" + "9" * (sys.get_int_max_str_digits() + 1))

    def test_nesting_limit(self):
        for levels in (256, 257, 5000):
            self.assert_same_read("a: " + "[" * levels + "]" * levels)
            self.assert_same_read("a: " + "{a: " * levels + "1" + "}" * levels)
            self.assert_same_read("a: " + "(a) { " * levels + "1" + "; }" * levels)
            self.assert_same_read("(a) { " * levels + "1" + "; }" * levels)

    def test_writing_at_the_nesting_limit(self):
        for levels in (254, 255, 256, 257, 512, 600):
            lists = 1
            objects = 1
            tables = 1
            for _ in range(levels):
                lists = [lists]
                objects = {"a": objects}
                tables = [{"a": tables}]
            self.assert_same_write({"a": lists})
            self.assert_same_write({"a": objects})
            self.assert_same_write({"a": tables})
            self.assert_same_write(tables, table=True)

    def test_unusual_text(self):
        for text in [
            'a: "\ud800"',       # a lone surrogate
            '"\ud800": 1',
            "﻿a: 1",
            "﻿",
            'é: 1',
            'a: "é" oops',
            'a: "😀😀" oops',
            '"😀": 1 oops',
            'a: "x\x00y"',
            "a: \x00",
            'a: "\r"',
            "a: 1\r\nb: 2\r\n",
            '"": 1',
            '"a": 1, "a": 2',
            '"a\\nb": 1',
        ]:
            self.assert_same_read(text)

    def test_writing_values(self):
        cycle = []
        cycle.append(cycle)
        for value in [
            {},
            {"a": {}},
            {"a": []},
            {"": 1},
            {"a b": 'say "hi"\n'},
            {"é": "é"},
            {"s": "\ud800"},
            {"s": "\x00"},
            {"s": "\r"},
            {"s": "\t\\"},
            {"t": (1, 2, (3, 4))},
            {"t": [{"a": 1, "b": 2}, {"a": 3, "b": 4}]},
            {"t": [{"a": 1, "b": 2}, {"b": 3, "a": 4}]},
            {"t": [{"a": 1}, {"a": 2, "b": 3}]},
            {"t": [{}, {}]},
            {"t": [[{"a": 1}], [{"a": 2}]]},
            {"t": [{"a": [{"b": 1}], "c": {"d": [1, 2]}}]},
            {"t": [1, [2, [3, {"a": []}]]]},
            {"n": 2**70},
            {"n": -(2**70)},
            {"f": 1e22},
            {"f": 1e-7},
            {"f": 0.1},
            {"f": -0.0},
            {"f": 1.7976931348623157e308},
            {"f": 5e-324},
            {"f": float("nan")},
            {"f": float("inf")},
            {"b": True, "n": None},
            {"x": object()},
            {"x": {1, 2}},
            {1: "a"},
            {None: "a"},
            {"c": cycle},
            OrderedDict([("b", 1), ("a", 2)]),
            ["a"],
            "a",
            5,
        ]:
            self.assert_same_write(value)

    def test_floats_write_the_same(self):
        rng = random.Random(7)
        for _ in range(3000):
            value = rng.choice(
                [
                    rng.random(),
                    rng.uniform(-1e6, 1e6),
                    round(rng.uniform(-1000, 1000), 2),
                    rng.random() * 10.0 ** rng.randint(-300, 300),
                ]
            )
            self.assert_same_write({"f": value})

    def test_writing_tables(self):
        for value in [
            [],
            [{}],
            [{"a": 1}, {"a": 2}],
            [{"a": 1}, {"b": 2}],
            [1, 2],
            ({"a": 1}, {"a": 2}),
            iter([{"a": 1}]),
        ]:
            if isinstance(value, type(iter([]))):
                self.assertEqual(
                    _speedups.dumps_table(iter([{"a": 1}])), _writer.dumps_table(iter([{"a": 1}]))
                )
            else:
                self.assert_same_write(value, table=True)

    def test_error_messages(self):
        for text in [
            "a: 1\nb: oops",
            "a: 1 b: 2",
            "a: 1\na: 2",
            "t: (ID, ID) { 1, 2; }",
            "t: (ID, Name) { 1; }",
            "x: [a: 1]",
            "x: {a: 1, 2}",
            'a: "bad \\q"',
            'a: "unterminated',
            "a: -x",
            "name: Ann",
        ]:
            with self.subTest(text):
                with self.assertRaises(PHLError) as pure:
                    _parser.loads(text)
                with self.assertRaises(PHLError) as native:
                    _speedups.loads(text)
                self.assertEqual(str(pure.exception), str(native.exception))
                self.assertEqual(pure.exception.message, native.exception.message)


if __name__ == "__main__":
    unittest.main()

import io
import json
import unittest
from pathlib import Path

import phl

CONFORMANCE = Path(__file__).resolve().parents[3] / "conformance"


def read(path):
    return path.read_text(encoding="utf-8")


class ConformanceTests(unittest.TestCase):
    def test_valid_phl(self):
        for source in sorted((CONFORMANCE / "phl" / "valid").glob("*.phl")):
            with self.subTest(source.name):
                expected = json.loads(read(source.with_suffix(".json")))
                self.assertEqual(phl.loads(read(source)), expected)

                raw = source.with_name(source.stem + ".nocoerce.json")
                if raw.exists():
                    self.assertEqual(
                        phl.loads(read(source), coerce=False), json.loads(read(raw))
                    )

    def test_invalid_phl(self):
        for source in sorted((CONFORMANCE / "phl" / "invalid").glob("*.phl")):
            with self.subTest(source.name):
                with self.assertRaises(phl.PHLError):
                    phl.loads(read(source))

    def test_valid_hpl(self):
        for source in sorted((CONFORMANCE / "hpl" / "valid").glob("*.hpl")):
            with self.subTest(source.name):
                expected = json.loads(read(source.with_suffix(".json")))
                self.assertEqual(phl.loads_table(read(source)), expected)

    def test_invalid_hpl(self):
        for source in sorted((CONFORMANCE / "hpl" / "invalid").glob("*.hpl")):
            with self.subTest(source.name):
                with self.assertRaises(phl.PHLError):
                    phl.loads_table(read(source))

    def test_round_trip(self):
        for source in sorted((CONFORMANCE / "phl" / "valid").glob("*.phl")):
            with self.subTest(source.name):
                data = phl.loads(read(source))
                for indent in (4, 0):
                    self.assertEqual(phl.loads(phl.dumps(data, indent=indent)), data)

        for source in sorted((CONFORMANCE / "hpl" / "valid").glob("*.hpl")):
            with self.subTest(source.name):
                rows = phl.loads_table(read(source))
                self.assertEqual(phl.loads_table(phl.dumps_table(rows)), rows)


class TypeTests(unittest.TestCase):
    def test_number_types(self):
        data = phl.loads("whole: 42\ndecimal: 34.00\nnegative: -0.5")
        self.assertIs(type(data["whole"]), int)
        self.assertIs(type(data["decimal"]), float)
        self.assertEqual(data["negative"], -0.5)

    def test_coerced_values_are_real_booleans_and_none(self):
        data = phl.loads('a: "True"\nb: "NULL"')
        self.assertIs(data["a"], True)
        self.assertIsNone(data["b"])

    def test_keys_are_never_coerced(self):
        self.assertEqual(phl.loads('"true": 1'), {"true": 1})

    def test_table_index(self):
        data = phl.loads('product: (ID, Name) { 101, "a"; 102, "b"; 103, "c"; 104, "d"; }')
        self.assertEqual(data["product"][3]["ID"], 104)

    def test_error_position(self):
        with self.assertRaises(phl.PHLError) as caught:
            phl.loads("a: 1\nb: oops")
        self.assertEqual((caught.exception.line, caught.exception.column), (2, 4))


class WriterTests(unittest.TestCase):
    def test_dumps_layout(self):
        data = {
            "name": "Ann",
            "tags": ["a", "b"],
            "server": {"Host": "db", "Port": 1341},
            "rows": [{"ID": 1, "Name": "x"}, {"ID": 2, "Name": "y"}],
        }
        self.assertEqual(
            phl.dumps(data),
            'name: "Ann"\n'
            'tags: ["a", "b"]\n'
            "server: {\n"
            '    Host: "db",\n'
            "    Port: 1341\n"
            "}\n"
            "rows: (ID, Name) {\n"
            '    1, "x";\n'
            '    2, "y";\n'
            "}\n",
        )

    def test_dumps_table(self):
        rows = [{"ID": 1, "full name": "Ann"}]
        self.assertEqual(phl.dumps_table(rows), '(ID, "full name") {\n    1, "Ann";\n}\n')

    def test_quotes_keys_and_escapes_strings(self):
        self.assertEqual(phl.dumps({"a b": 'say "hi"\n'}), '"a b": "say \\"hi\\"\\n"\n')

    def test_small_and_large_floats_have_no_exponent(self):
        text = phl.dumps({"small": 1e-7, "large": 1e22})
        self.assertNotIn("e", text.replace("large", "").replace("small", ""))
        self.assertEqual(phl.loads(text), {"small": 1e-7, "large": 1e22})

    def test_rejects_unwritable_values(self):
        for bad in ({"x": float("nan")}, {"x": object()}, {1: "a"}, ["a"]):
            with self.subTest(bad=bad):
                with self.assertRaises(phl.PHLError):
                    phl.dumps(bad)

    def test_empty_object_and_list(self):
        text = phl.dumps({"o": {}, "l": []})
        self.assertEqual(text, "o: {}\nl: []\n")
        self.assertEqual(phl.loads(text), {"o": {}, "l": []})

    def test_file_helpers(self):
        buffer = io.StringIO()
        phl.dump({"a": 1}, buffer)
        buffer.seek(0)
        self.assertEqual(phl.load(buffer), {"a": 1})


if __name__ == "__main__":
    unittest.main()

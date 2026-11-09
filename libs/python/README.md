# phl for Python

Reads and writes PHL documents (`.phl`, `.pll`) and HPL tables (`.hpl`,
`.hpll`). The interface follows the standard `json` module. No dependencies.

```
pip install phl-file
```

The package is named `phl-file` on PyPI and is imported as `phl`.

```python
import phl

doc = phl.loads('name: "Ann"\nproduct: (ID, Name) { 101, "Mouse"; 102, "Chair"; }')
doc["product"][1]["Name"]          # 'Chair'

with open("config.phl", encoding="utf-8") as f:
    config = phl.load(f)

text = phl.dumps({"name": "Ann", "tags": ["a", "b"]})

with open("products.hpl", encoding="utf-8") as f:
    rows = phl.load_table(f)       # list of dicts
print(phl.dumps_table(rows))
```

| Function | Does |
|---|---|
| `loads(text, coerce=True)` / `load(fp, ...)` | PHL text to a `dict` |
| `loads_table(text, coerce=True)` / `load_table(fp, ...)` | HPL text to a `list` of `dict`s |
| `dumps(obj, indent=4)` / `dump(obj, fp, ...)` | a `dict` to PHL text |
| `dumps_table(rows, indent=4)` / `dump_table(rows, fp, ...)` | a list of `dict`s with the same keys to HPL text |

Whole numbers become `int` and decimals `float`. With `coerce=True`, the
strings `"true"` and `"false"` become `True` and `False`, and `"nil"`,
`"null"` and `"none"` become `None`, ignoring case.

`indent` is the spaces per nesting level. `indent=0` writes nested values on
one line.

Errors raise `phl.PHLError` (a `ValueError`) with `line` and `column`
attributes when the error has a position. Nesting deeper than 256 levels is an
error when reading and writing.

Whole numbers can be any size, up to Python's limit on digits
(`sys.set_int_max_str_digits`). Other PHL libraries refuse whole numbers beyond 64
bits, so keep to that range if other languages read the same files.

## The C version

The package includes a C version of the parser and writer, built on
[the C library](../c). It reads about 10 times as fast as pure Python and writes
about twice as fast. It is used automatically when it was compiled, and gives the
same results and the same error positions. Error messages can differ in small ways,
such as how an unusual key is quoted.

Without a compiler, or on an interpreter it does not support, the package still
installs and uses the pure Python version. To check which one you have, look at
whether `phl._speedups` is `None`. To force pure Python, set the environment
variable `PHL_PURE_PYTHON=1`.

## Tests

```
PYTHONPATH=src python -m unittest discover -s tests
```

The tests run the shared cases in `../../conformance` as well.
`tests/test_parity.py` compares the C version with pure Python, including over
randomly changed files. It is skipped when the C version is not compiled, so run
the tests against an installed copy to include it:

```
pip install . && python -m unittest discover -s tests
```

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

`indent` is the number of spaces per nesting level. `indent=0` writes nested
values on one line.

Errors raise `phl.PHLError` (a `ValueError`) with `line` and `column`
attributes when the error has a position.

## Tests

```
PYTHONPATH=src python -m unittest discover -s tests
```

The tests run the shared cases in `../../conformance` as well.

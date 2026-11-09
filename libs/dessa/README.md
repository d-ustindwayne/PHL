# phl for Dessa

A riv package (`phl/`) that reads and writes PHL documents (`.phl`, `.pll`)
and HPL tables (`.hpl`, `.hpll`). Values are read into the `Json` type from
Dessa's `json` package, so PHL and JSON convert into each other.

```dessa
$("phl") { parsePhl, parsePhlTable, toPhl, toPhlTable, PhlError }
$("json") { Json }
$("os") { readFile }

var[Json] doc = parsePhl(readFile("config.phl"));
print(doc.get("product").at(3).get("ID").asInt());   // 104
print(toPhl(doc));

var[Json] rows = parsePhlTable(readFile("products.hpl"));
print(toPhlTable(rows));
```

| Function | Does |
|---|---|
| `parsePhl(text, coerce=true)` | PHL text to a `Json` object |
| `parsePhlTable(text, coerce=true)` | HPL text to a `Json` array of objects |
| `toPhl(document, indent=4)` | a `Json` object to PHL text |
| `toPhlTable(rows, indent=4)` | a `Json` array of objects with the same keys to HPL text |

`Json` stores every number as a `Dbl`; use `asInt()` or `asDbl()` to read it, and
whole numbers beyond 2^53 lose precision.
With `coerce` on, the strings `"true"` and `"false"` become booleans, and
`"nil"`, `"null"` and `"none"` become `nil`, ignoring case.

Mistakes throw `PhlError`, an `Error` with `line` and `column` fields (0 when
there is no position). Nesting deeper than 256 levels and numbers too large for a
`Dbl` are errors.

## Tests

From the repository root, with Dessa's bundled packages after this folder in
`DESSA_PACKAGES` (separated by `;` on Windows, `:` elsewhere):

```
DESSA_PACKAGES="libs/dessa;<Dessa>/packages" dessa run libs/dessa/tests/phl_test.ds
```

The test runs the shared cases in `conformance/` as well. Tested with Dessa
0.5.6. The test lists folders, which `--target wasm` does not support yet, so it
runs natively; the library itself also works under `--target wasm`.

# phl for Dessa

A riv package (`phl/`) that reads and writes PHL documents (`.phl`, `.pll`)
and HPL tables (`.hpl`, `.hpll`). Values are read into the `Json` type from
Dessa's `json` package, so PHL and JSON convert into each other.

This replaces the old `phl` package in the Dessa repository
(`tools/riv/packages/phl`), which reads the previous syntax.

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

`Json` stores every number as a `Dbl`; use `asInt()` or `asDbl()` to read it.
With `coerce` on, the strings `"true"` and `"false"` become booleans, and
`"nil"`, `"null"` and `"none"` become `nil`, ignoring case.

Mistakes throw `PhlError`, an `Error` with `line` and `column` fields (0 when
there is no position).

## Tests

From the repository root, with Dessa's bundled packages after this folder in
`DESSA_PACKAGES` (separated by `;` on Windows, `:` elsewhere):

```
DESSA_PACKAGES="libs/dessa;<Dessa>/tools/riv/packages" dessa run libs/dessa/tests/phl_test.ds
DESSA_PACKAGES="libs/dessa;<Dessa>/tools/riv/packages" dessa run libs/dessa/tests/phl_test.ds --target js
```

The test runs the shared cases in `conformance/` as well. Tested with Dessa
0.3.6.

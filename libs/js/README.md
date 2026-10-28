# phl for JavaScript and TypeScript

Reads and writes PHL documents (`.phl`, `.pll`) and HPL tables (`.hpl`,
`.hpll`). An ES module with TypeScript types. No dependencies. Node 18 or
newer, or any modern browser.

```js
import { parse, stringify, parseTable, stringifyTable } from "@d-ustindwayne/phl";

const doc = parse('name: "Ann"\nproduct: (ID, Name) { 101, "Mouse"; 102, "Chair"; }');
doc.product[1].Name; // "Chair"

const text = stringify({ name: "Ann", tags: ["a", "b"] });

const rows = parseTable(hplText); // array of objects
stringifyTable(rows);
```

| Function | Does |
|---|---|
| `parse(text, { coerce = true })` | PHL text to an object |
| `parseTable(text, { coerce = true })` | HPL text to an array of objects |
| `stringify(value, { indent = 4 })` | an object to PHL text |
| `stringifyTable(rows, { indent = 4 })` | an array of objects with the same keys to HPL text |
| `parseTree(text, { coerce = true, table = false })` | a syntax tree with line and column positions, for editors and other tools (the VS Code extension uses it) |

With `coerce` on, the strings `"true"` and `"false"` become booleans, and
`"nil"`, `"null"` and `"none"` become `null`, ignoring case.

Numbers are JavaScript numbers, so whole numbers beyond
`Number.MAX_SAFE_INTEGER` lose precision.

Errors throw `PHLError` with `line` and `column` properties when the error has
a position, and `reason` for the message without the position.

## Tests

```
npm test
```

The tests run the shared cases in `../../conformance` as well.

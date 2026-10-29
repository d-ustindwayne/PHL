# PHL

PHL is a small, readable data format for configuration files and tabular data.
It is a format, not a programming language. There is nothing to run, so there
is no command-line tool. You read and write it through a library in your own
language.

```phl
// Store settings
name: "Corner Shop"
open: "true"
taxRate: 0.12

product: (ID, Name, Price, Stock) {
    101, "Wireless Mouse", 25.99, 150;
    102, "Ergonomic Chair", 149.50, 45;
    103, "LED Desk Lamp", 34.00, 80;
}

tags: ["electronics", "furniture"]

database: {
    host: "localhost",
    port: 5432
}
```

## Why PHL

- **Tables without repetition.** JSON repeats every key on every row. A PHL
  table names its columns once and then lists the rows.
- **Less punctuation.** Keys don't need quotes, top-level entries need no
  surrounding braces, and commas between top-level entries are optional.
- **Comments.** `//` comments are allowed anywhere.

## Syntax

A document is a list of `key: value` entries, one per line, or separated by
commas.

| Value | Example | Notes |
|---|---|---|
| String | `"Wireless Mouse"` | Double quotes. Escapes: `\n` `\t` `\"` `\\`. One line only. |
| Number | `42`, `-3`, `149.50` | Whole or decimal. |
| Object | `{host: "db", port: 5432}` | `key: value` entries inside `{ }`. |
| List | `["apple", "banana"]` | Values inside `[ ]`, without keys. |
| Table | `(ID, Name) { 1, "a"; 2, "b"; }` | Columns inside `( )`, rows inside `{ }`. |

There are no boolean or null values. Write them as strings, `"true"`,
`"false"` and `"nil"`, and the libraries read them as booleans and null (see
[Type conversion](#type-conversion)). A bare `true` without quotes is an error.

Rules:

- Keys use letters, digits and underscore. Any other key is written as a
  quoted string, for example `"full name": "Ann"`.
- Every top-level value needs a key. A value on its own is an error.
- Two entries on the same line need a comma between them.
- Keys inside an object must be unique. `{}` is an empty object.
- List items can be any value. `[]` is an empty list.
- A trailing comma is allowed in objects and lists.
- In a table, values in a row are separated by `,` and every row ends with `;`.
  Each row has exactly one value per column, and column names must be unique.
- A table is read as a list of objects, counted from 0. In the example above,
  `product[2].Name` is `"LED Desk Lamp"`.
- `//` starts a comment that runs to the end of the line.
- Files are UTF-8.

The example at the top reads as this JSON:

```json
{
  "name": "Corner Shop",
  "open": true,
  "taxRate": 0.12,
  "product": [
    { "ID": 101, "Name": "Wireless Mouse", "Price": 25.99, "Stock": 150 },
    { "ID": 102, "Name": "Ergonomic Chair", "Price": 149.5, "Stock": 45 },
    { "ID": 103, "Name": "LED Desk Lamp", "Price": 34.0, "Stock": 80 }
  ],
  "tags": ["electronics", "furniture"],
  "database": { "host": "localhost", "port": 5432 }
}
```

## Table files

A table file holds one table and nothing else, like a CSV file. It uses the
same table syntax, without a key:

```hpl
(ID, Name, Category, Price) {
    101, "Wireless Mouse", "Electronics", 25.99;
    102, "Ergonomic Chair", "Furniture", 149.50;
}
```

| Extension | Contents |
|---|---|
| `.phl`, `.pll` | a document |
| `.hpl`, `.hpll` | a single table |

## Type conversion

Whole numbers are read as integers and decimals as floating point numbers
(`int` and `float` in Python; JavaScript and Dessa have one number type).

The libraries also convert these quoted strings by default, ignoring case:

| In the file | Read as |
|---|---|
| `"true"`, `"false"` | boolean |
| `"nil"`, `"null"`, `"none"` | null (`None` in Python, `nil` in Dessa) |

Other strings stay strings, including numbers in quotes such as `"007"`. To
turn the conversion off, pass `coerce=False` in Python, `{ coerce: false }` in
JavaScript or `coerce: false` in Dessa.

When writing, the libraries write booleans and null as `"true"`, `"false"` and
`"nil"`.

## Libraries

| Language | Package | Source |
|---|---|---|
| Python 3.8+ | `phl` | [libs/python](libs/python) |
| JavaScript and TypeScript (Node 18+, Bun) | `phl-file` | [libs/js](libs/js) |
| Dessa | `phl` (riv package) | [libs/dessa](libs/dessa) |

None of them has dependencies. The Dessa package uses Dessa's own `json`
package for its `Json` type.

Python:

```python
import phl

with open("shop.phl", encoding="utf-8") as f:
    shop = phl.load(f)

print(shop["product"][2]["Name"])   # LED Desk Lamp
print(phl.dumps(shop))
```

JavaScript:

```js
import { parse, stringify } from "phl-file";

const shop = parse(text);
console.log(shop.product[2].Name); // LED Desk Lamp
console.log(stringify(shop));
```

Dessa:

```dessa
$("phl") { parsePhl, toPhl }
$("os") { readFile }

var[Json] shop = parsePhl(readFile("shop.phl"));
print(shop.get("product").at(2).get("Name").asStr());   // LED Desk Lamp
print(toPhl(shop));
```

When writing, a list of objects that all have the same keys in the same order
is written as a table. Each library's README lists its full API.

## Editor support

[vscode-phl](vscode-phl) is the VS Code extension. It highlights `.phl` and
`.hpl` files, shows errors as you type, fills the outline, shows the path and
type of a value on hover (`product[2].Name`), and completes `"true"`, `"false"`,
`"nil"` and table, object and list snippets. Download the `.vsix` from the
[releases page](https://github.com/d-ustindwayne/PHL/releases) and install it
with **Install from VSIX...** in the Extensions view.

## Repository layout

| Folder | Contents |
|---|---|
| `libs/python` | the Python library |
| `libs/js` | the JavaScript library, with TypeScript types |
| `libs/dessa` | the Dessa package |
| `vscode-phl` | the VS Code extension |
| `conformance` | test files every library must pass |
| `examples` | sample files |

The files in `conformance` are the reference for how PHL is read. Each valid
`.phl` or `.hpl` file has a `.json` file with the data it must produce, and
each file in an `invalid` folder must be rejected. A library for a new
language should pass all of them.

## Running the tests

```
cd libs/python && PYTHONPATH=src python -m unittest discover -s tests
cd libs/js && bun test
cd vscode-phl && bun run test
```

For Dessa, run from the repository root with Dessa's bundled packages listed
after this one in `DESSA_PACKAGES` (separated by `;` on Windows and `:`
elsewhere):

```
DESSA_PACKAGES="libs/dessa;<Dessa>/tools/riv/packages" dessa run libs/dessa/tests/phl_test.ds
```

## Status

The format is new and may still change before version 1.0.

Planned:

- A final name for the format.
- Libraries for PHP, Java, C++ and C#.
- The Dessa package published through riv.

## License

Apache License 2.0. See [LICENSE](LICENSE).

# phl for PHP

Reads and writes PHL documents (`.phl`, `.pll`) and HPL tables (`.hpl`,
`.hpll`). PHP 8.1 or newer, no dependencies or extensions.

```
composer require d-ustindwayne/phl-file
```

```php
use Phl\Phl;

$doc = Phl::parse("name: \"Ann\"\nproduct: (ID, Name) { 101, \"Mouse\"; 102, \"Chair\"; }");
echo $doc['product'][1]['Name'];   // Chair

$text = Phl::stringify(['name' => 'Ann', 'tags' => ['a', 'b']]);

$rows = Phl::parseTable($hplText);   // a list of rows
echo Phl::stringifyTable($rows);
```

| Method | Does |
|---|---|
| `Phl::parse($text, coerce: true, objects: false)` | PHL text to an array |
| `Phl::parseTable($text, coerce: true, objects: false)` | HPL text to a list of rows |
| `Phl::stringify($document, indent: 4)` | an array or `stdClass` to PHL text |
| `Phl::stringifyTable($rows, indent: 4)` | rows with the same keys to HPL text |

With `coerce` on, the strings `"true"` and `"false"` become booleans, and `"nil"`,
`"null"` and `"none"` become `null`, ignoring case.

Values are read as arrays, `string`, `int`, `float`, `bool` and `null`. Whole numbers
beyond PHP's `int` (64 bits on 64-bit systems) and decimals too large for a `float`
are errors.

`indent` is the spaces per nesting level, and 0 writes nested values on one line.
Rows with the same keys in the same order are written as a table.

## Objects and arrays

As with `json_decode($text, true)`, PHP arrays cannot tell some things apart:

- `{}` and `[]` both read as an empty array, which is written as `[]`.
- `{0: "a"}` reads as a list, and is written as `["a"]`.
- A key like `"1"` becomes the integer `1`, though it is written back as `1`.

With `objects: true`, objects read as `stdClass` instead, and all of these are
kept exactly. `Phl::stringify` accepts both forms: an array with the keys 0, 1, 2...
is a list, any other array is an object, and a `stdClass` is always an object.

```php
$doc = Phl::parse('settings: {}', objects: true);
echo $doc->settings instanceof stdClass;   // 1
echo Phl::stringify($doc);                 // settings: {}
```

## Errors

Errors throw `Phl\PhlException`, a `RuntimeException`, with `errorLine`,
`errorColumn` and `reason` (the message without the position). The line and column
are 0 when there is no position, and columns count Unicode code points. The names
differ from `getLine()`, which PHP keeps for the line of the PHP source.

Text must be valid UTF-8. Nesting deeper than `Phl::MAX_DEPTH` (256) is an error
when reading and writing. Nothing depends on the current locale.

## Tests

```
php tests/run.php
```

The tests run the shared cases in `../../conformance` as well.

# phl for C#

Reads and writes PHL documents (`.phl`, `.pll`) and HPL tables (`.hpl`,
`.hpll`). .NET Standard 2.0, so .NET Framework 4.6.1, .NET Core and .NET 5 or
newer. No dependencies.

```
dotnet add package PhlFile
```

```csharp
using Phl;

PhlObject doc = PhlConvert.Parse("name: \"Ann\"\nproduct: (ID, Name) { 101, \"Mouse\"; 102, \"Chair\"; }");
var rows = (List<object?>)doc["product"]!;
Console.WriteLine(((PhlObject)rows[1]!)["Name"]);   // Chair

string text = PhlConvert.Stringify(new PhlObject
{
    ["name"] = "Ann",
    ["tags"] = new[] { "a", "b" },
});

List<PhlObject> table = PhlConvert.ParseTable(hplText);
PhlConvert.StringifyTable(table);
```

| Method | Does |
|---|---|
| `PhlConvert.Parse(text, coerce = true)` | PHL text, or a `TextReader`, to a `PhlObject` |
| `PhlConvert.ParseTable(text, coerce = true)` | HPL text, or a `TextReader`, to a `List<PhlObject>` |
| `PhlConvert.Stringify(document, indent = 4)` | a dictionary to PHL text |
| `PhlConvert.StringifyTable(rows, indent = 4)` | dictionaries with the same keys to HPL text |

With `coerce` on, the strings `"true"` and `"false"` become booleans, and `"nil"`,
`"null"` and `"none"` become `null`, ignoring case.

Values are read as `PhlObject`, `List<object?>`, `string`, `long`, `double`,
`bool` and `null`. Tables are lists of `PhlObject`s. `PhlObject` is an
`IDictionary<string, object?>` that promises to keep its keys in order. Whole
numbers beyond 64 bits and decimals too large for a `double` are errors.

When writing, values can also be any dictionary with string keys, any other
`IEnumerable`, and any whole number type that fits in 64 bits (including
`BigInteger`), `float` and `decimal`. `indent` is the spaces per nesting level, and
0 writes nested values on one line. Dictionaries with the same keys in the same
order are written as a table. Use `PhlObject` when the order matters, because
`Dictionary` does not promise one.

Errors throw `PhlException`, with `Line`, `Column` and `Reason` (the message
without the position). `Line` and `Column` are 0 when there is no position, and
columns count Unicode code points. Nesting deeper than `PhlConvert.MaxDepth` (256)
is an error when reading and writing. Nothing depends on the current culture.

## Tests

```
dotnet test Phl.Tests
```

The tests run the shared cases in `../../conformance` as well.

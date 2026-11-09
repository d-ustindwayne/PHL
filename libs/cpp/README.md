# phl for C++

Reads and writes PHL documents (`.phl`, `.pll`) and HPL tables (`.hpl`,
`.hpll`). C++17, a single header, no dependencies.

Copy `include/phl.hpp` into your project, or use CMake:

```cmake
add_subdirectory(libs/cpp)
target_link_libraries(your_program PRIVATE phl::cpp)
```

```cpp
#include <iostream>
#include "phl.hpp"

phl::value doc = phl::parse("name: \"Ann\"\nproduct: (ID, Name) { 101, \"Mouse\"; 102, \"Chair\"; }");
std::cout << doc["product"][1]["Name"].as_string() << "\n";   // Chair

phl::object shop;
shop["name"] = "Corner Shop";
shop["open"] = true;
shop["tags"] = phl::list{"a", "b"};
std::cout << phl::stringify(shop);

phl::value rows = phl::parse_table(hpl_text);   // a list of objects
std::cout << phl::stringify_table(rows);
```

| Function | Does |
|---|---|
| `phl::parse(text, coerce = true)` | PHL text to an object |
| `phl::parse_table(text, coerce = true)` | HPL text to a list of objects |
| `phl::stringify(document, indent = 4)` | an object to PHL text |
| `phl::stringify_table(rows, indent = 4)` | a list of objects with the same keys to HPL text |

With `coerce` on, the strings `"true"` and `"false"` become booleans, and
`"nil"`, `"null"` and `"none"` become nil, ignoring case. `indent` is the spaces
per nesting level, and 0 writes nested values on one line. A list of objects
with the same keys in the same order is written as a table.

## Values

`phl::value` holds nil, a `bool`, a whole number (`std::int64_t`), a decimal
(`double`), a `std::string`, a `phl::list` (a `std::vector` of values) or a
`phl::object`.

| To | Use |
|---|---|
| check the type | `is_nil()`, `is_bool()`, `is_int()`, `is_float()`, `is_number()`, `is_string()`, `is_list()`, `is_object()`, or `kind()` |
| read it | `as_bool()`, `as_int()`, `as_float()` (also accepts whole numbers), `as_string()`, `as_list()`, `as_object()` |
| look inside | `v["key"]`, `v[2]`, `v.size()` |

`phl::object` keeps its keys in the order they were read or added. Use `find(key)`
(a pointer, or null when missing), `contains(key)`, `at(key)` and `obj[key]`, and
a range-based `for` over its `key` and `item` members.

## Errors

Every mistake throws `phl::error`, a `std::runtime_error`: invalid text, values
that cannot be written, reading a value as the wrong type, and looking up a
missing key or an index out of range. `line()` and `column()` give the position,
counted from 1, and are 0 when there is none. Columns count Unicode code points.

## Limits

- Text must be valid UTF-8.
- Whole numbers beyond signed 64 bits are an error, as are decimals too large for
  a `double`.
- Nesting deeper than `phl::max_depth` (256) is an error when reading and
  writing. A debug build can need up to about 750 KB of stack at that depth, and
  an optimized build far less.
- Reading and writing do not depend on the current locale.

## Tests

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests run the shared cases in `../../conformance` as well.

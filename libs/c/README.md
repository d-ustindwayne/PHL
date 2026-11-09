# phl for C

Reads and writes PHL documents (`.phl`, `.pll`) and HPL tables (`.hpl`,
`.hpll`). C99, no dependencies. The Python package `phl-file` is built on this
same code.

```c
#include <stdio.h>
#include <string.h>
#include "phl.h"

const char *text = "name: \"Ann\"\nproduct: (ID, Name) { 101, \"Mouse\"; 102, \"Chair\"; }";

phl_error error;
phl_value *doc = phl_parse(text, strlen(text), PHL_DEFAULT, &error);
if (!doc) {
    fprintf(stderr, "%s (line %zu, column %zu)\n", error.message, error.line, error.column);
    return 1;
}

const phl_value *row = phl_at(phl_get(doc, "product"), 1);
puts(phl_as_string(phl_get(row, "Name"), NULL));   /* Chair */

char *written = phl_write(doc, 4, PHL_DEFAULT, NULL, &error);
fputs(written, stdout);
phl_free_text(written);
phl_free(doc);
```

To use it, add `src/value.c`, `src/parser.c` and `src/writer.c` to your build
with `include` and `src` on the include path, or use CMake:

```cmake
add_subdirectory(libs/c)
target_link_libraries(your_program PRIVATE phl::phl)
```

`-DBUILD_SHARED_LIBS=ON` builds a shared library. When using it as a Windows
DLL, define `PHL_SHARED`.

## Reading

| Function | Does |
|---|---|
| `phl_parse(text, length, flags, &error)` | PHL text to a `PHL_OBJECT` |
| `phl_parse_table(text, length, flags, &error)` | HPL text to a `PHL_LIST` of `PHL_OBJECT` rows |
| `phl_free(value)` | releases a value and everything in it |

Both return NULL on failure and fill in `error`, which may be NULL. Pass
`PHL_NO_COERCE` to keep `"true"`, `"nil"` and the like as strings.

Text must be valid UTF-8. Strings may contain NUL bytes, so use the length
that `phl_as_string` gives when that matters.

## Looking at values

`phl_type_of` gives `PHL_NIL`, `PHL_BOOL`, `PHL_INT`, `PHL_FLOAT`, `PHL_STRING`,
`PHL_LIST` or `PHL_OBJECT`. Then use `phl_as_bool`, `phl_as_int` (`long long`),
`phl_as_float` (also accepts whole numbers), `phl_as_string`, `phl_length`,
`phl_at`, `phl_get`, `phl_key_at` and `phl_value_at`.

They all accept NULL and values of the wrong type and give NULL or 0, so lookups
can be chained. `phl_get` searches keys in order. Objects keep the order of
their keys.

## Writing

| Function | Does |
|---|---|
| `phl_write(value, indent, flags, &length, &error)` | a `PHL_OBJECT` to PHL text |
| `phl_write_table(value, indent, flags, &length, &error)` | a list of objects with the same keys to HPL text |
| `phl_free_text(text)` | releases the text |

`indent` is the spaces per nesting level, and 0 writes nested values on one
line. `length` and `error` may be NULL. A list of objects with the same keys in
the same order is written as a table.

Build values with `phl_new_nil`, `phl_new_bool`, `phl_new_int`, `phl_new_float`,
`phl_new_string`, `phl_new_string_n`, `phl_new_list` and `phl_new_object`, and
add to them with `phl_list_push`, `phl_object_set` and `phl_object_set_n`.
Adding takes ownership of the item, even on failure, so calls can be nested:

```c
phl_value *doc = phl_new_object();
phl_object_set(doc, "name", phl_new_string("Ann"));
phl_object_set(doc, "age", phl_new_int(30));
```

## Limits

- Whole numbers beyond signed 64 bits are an error, as are decimals too large
  for a `double`.
- Nesting deeper than `PHL_MAX_DEPTH` (256) is an error when reading and writing.
- Errors have 1-based `line` and `column`, which are 0 when there is no position.
  Columns count Unicode code points.

Language bindings can pass `PHL_KEEP_BIG_INTEGERS` to read larger whole numbers
as `PHL_BIGINT` with the digits as a string, and `PHL_ASSUME_VALID_UTF8` to skip
the UTF-8 check on text they have checked already.

Values are not safe to change from several threads, but separate threads can
read the same value and parse different text at the same time. Reading and
writing decimals do not depend on the current locale.

## Tests

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests run the shared cases in `../../conformance` as well. `test_robust`
also makes every allocation fail in turn and parses randomly changed files,
checking for leaks; `PHL_FUZZ_ROUNDS` sets how many. It is most useful in a build
with `-fsanitize=address,undefined`.

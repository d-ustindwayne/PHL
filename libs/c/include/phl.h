/*
 * phl.h -- read and write PHL documents (.phl, .pll) and HPL tables
 * (.hpl, .hpll). C99, no dependencies.
 *
 *     phl_error error;
 *     phl_value *doc = phl_parse(text, length, PHL_DEFAULT, &error);
 *     if (!doc) {
 *         fprintf(stderr, "%s\n", error.message);
 *         return 1;
 *     }
 *     const phl_value *row = phl_at(phl_get(doc, "product"), 2);
 *     puts(phl_as_string(phl_get(row, "Name"), NULL));
 *     phl_free(doc);
 *
 * Text is UTF-8 with an explicit length. Strings may contain NUL bytes, and
 * every string the library returns also ends with a NUL byte.
 *
 * Functions that read a value accept NULL and a value of the wrong type, and
 * return NULL, 0 or false, so lookups can be chained without checks.
 *
 * Values are not thread-safe to change, but separate threads may read the same
 * value or parse different text at the same time.
 */

#ifndef PHL_H
#define PHL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Define PHL_SHARED when building or using libphl as a Windows DLL. */
#if defined(_WIN32) && defined(PHL_SHARED)
#  ifdef PHL_BUILDING
#    define PHL_API __declspec(dllexport)
#  else
#    define PHL_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(PHL_SHARED)
#  define PHL_API __attribute__((visibility("default")))
#else
#  define PHL_API
#endif

#define PHL_VERSION "0.1.0"

/* Lists, objects and tables nested deeper than this are an error. */
#define PHL_MAX_DEPTH 256

typedef struct phl_value phl_value;

typedef enum phl_type {
    PHL_NIL,
    PHL_BOOL,
    PHL_INT,    /* signed 64 bits */
    PHL_FLOAT,  /* double */
    PHL_STRING,
    PHL_LIST,
    PHL_OBJECT, /* keys keep the order they were read or added in */
    PHL_BIGINT  /* only with PHL_KEEP_BIG_INTEGERS; digits as a string */
} phl_type;

/* Filled in when a function fails. line and column are 1-based, and 0 when the
   error has no position. Columns count Unicode code points. */
typedef struct phl_error {
    size_t line;
    size_t column;
    char message[256];
} phl_error;

/* Flags for phl_parse, phl_parse_table, phl_write and phl_write_table. */
#define PHL_DEFAULT 0u
/* Keep "true", "false", "nil", "null" and "none" as strings when reading. */
#define PHL_NO_COERCE 1u
/* Read whole numbers beyond 64 bits as PHL_BIGINT instead of failing. For
   language bindings whose integers have no size limit. */
#define PHL_KEEP_BIG_INTEGERS 2u
/* Skip the UTF-8 check, for bindings whose strings are checked already. */
#define PHL_ASSUME_VALID_UTF8 4u

/* Reading. Both return NULL and fill in error (which may be NULL) on failure.
   The result is released with phl_free. */

/* A PHL document, as a PHL_OBJECT. */
PHL_API phl_value *phl_parse(const char *text, size_t length, unsigned flags, phl_error *error);
/* An HPL table, as a PHL_LIST of PHL_OBJECT rows. */
PHL_API phl_value *phl_parse_table(const char *text, size_t length, unsigned flags, phl_error *error);

/* Writing. indent is the spaces per nesting level; 0 writes nested values on
   one line. Both return NUL-terminated text to release with phl_free_text, or
   NULL on failure. length may be NULL.

   A list of objects that all have the same keys in the same order is written
   as a table. Booleans and nil are written as "true", "false" and "nil". */

/* A PHL_OBJECT as a PHL document. */
PHL_API char *phl_write(const phl_value *document, int indent, unsigned flags, size_t *length, phl_error *error);
/* A PHL_LIST of objects with the same keys as an HPL table. */
PHL_API char *phl_write_table(const phl_value *rows, int indent, unsigned flags, size_t *length, phl_error *error);

PHL_API void phl_free_text(char *text);

/* Inspecting values. */

/* The type of a value. NULL counts as PHL_NIL. */
PHL_API phl_type phl_type_of(const phl_value *value);
PHL_API int phl_as_bool(const phl_value *value);
PHL_API long long phl_as_int(const phl_value *value);
/* The number as a double. Also accepts PHL_INT. */
PHL_API double phl_as_float(const phl_value *value);
/* The text of a PHL_STRING or PHL_BIGINT. length may be NULL. */
PHL_API const char *phl_as_string(const phl_value *value, size_t *length);

/* The number of items in a list or entries in an object. */
PHL_API size_t phl_length(const phl_value *value);
/* An item of a list, counted from 0. */
PHL_API const phl_value *phl_at(const phl_value *list, size_t index);
/* An entry of an object by key. This searches the keys in order. */
PHL_API const phl_value *phl_get(const phl_value *object, const char *key);
/* An entry of an object by position, counted from 0. */
PHL_API const char *phl_key_at(const phl_value *object, size_t index, size_t *length);
PHL_API const phl_value *phl_value_at(const phl_value *object, size_t index);

/* Building values. Each returns NULL when out of memory. */

PHL_API phl_value *phl_new_nil(void);
PHL_API phl_value *phl_new_bool(int value);
PHL_API phl_value *phl_new_int(long long value);
PHL_API phl_value *phl_new_float(double value);
PHL_API phl_value *phl_new_string(const char *text);
PHL_API phl_value *phl_new_string_n(const char *text, size_t length);
PHL_API phl_value *phl_new_list(void);
PHL_API phl_value *phl_new_object(void);

/* Adds item to a list. Returns 0, or -1 when list is not a list or memory runs
   out. Takes ownership of item either way, and item may be NULL for chaining
   (which also returns -1). */
PHL_API int phl_list_push(phl_value *list, phl_value *item);
/* Sets a key of an object, replacing any value it had. Ownership works as in
   phl_list_push. */
PHL_API int phl_object_set(phl_value *object, const char *key, phl_value *item);
PHL_API int phl_object_set_n(phl_value *object, const char *key, size_t key_length, phl_value *item);

/* Releases a value and everything inside it. NULL is allowed. */
PHL_API void phl_free(phl_value *value);

#ifdef __cplusplus
}
#endif

#endif /* PHL_H */

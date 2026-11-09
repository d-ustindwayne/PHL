/* Shared between the parts of libphl and the Python extension built on it. */

#ifndef PHL_INTERNAL_H
#define PHL_INTERNAL_H

#include <stddef.h>

#include "phl.h"

/* The allocation tests replace these to count allocations and make them fail. */
#ifndef PHL_MALLOC
#  include <stdlib.h>
#  define PHL_MALLOC malloc
#  define PHL_REALLOC realloc
#  define PHL_FREE free
#endif

/* Keys are shared between the rows of a table, so they are reference counted. */
typedef struct phl_key {
    size_t refs;
    size_t length;
    char data[];
} phl_key;

typedef struct phl_member {
    phl_key *key;
    phl_value *value;
} phl_member;

struct phl_value {
    phl_type type;
    union {
        int boolean;
        long long integer;
        double number;
        struct {
            char *data;
            size_t length;
            size_t line;   /* where a PHL_BIGINT was read, for bindings */
            size_t column;
        } string;
        struct {
            phl_value **items;
            size_t count;
            size_t capacity;
        } list;
        struct {
            phl_member *members;
            size_t count;
            size_t capacity;
        } object;
    } as;
};

phl_key *phl__key_new(const char *data, size_t length);
phl_key *phl__key_retain(phl_key *key);
void phl__key_release(phl_key *key);

/* Unlike the public functions, these leave the item with the caller on failure,
   and phl__object_append takes its own reference to the key and does not look
   for an existing entry. */
int phl__list_append(phl_value *list, phl_value *item);
int phl__object_reserve(phl_value *object, size_t capacity);
int phl__object_append(phl_value *object, phl_key *key, phl_value *item);

/* A PHL_BIGINT from its digits, with an optional leading '-'. */
phl_value *phl__new_bigint(const char *text, size_t length, size_t line, size_t column);

/* Length of the valid UTF-8 prefix of text. */
size_t phl__utf8_valid_prefix(const char *text, size_t length);

void phl__set_error(phl_error *error, size_t line, size_t column, const char *format, ...);

#endif /* PHL_INTERNAL_H */

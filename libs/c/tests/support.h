/* Helpers for the C tests: reading files, a small JSON reader for the expected
   results of the conformance cases, and comparing values. */

#ifndef PHL_TEST_SUPPORT_H
#define PHL_TEST_SUPPORT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phl.h"

static char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    char *data;
    long size;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (char *)malloc((size_t)size + 1);
    if (data) {
        *length = fread(data, 1, (size_t)size, file);
        data[*length] = '\0';
    }
    fclose(file);
    return data;
}

static void json_space(const char **s) {
    while (**s == ' ' || **s == '\n' || **s == '\r' || **s == '\t') (*s)++;
}

/* Decodes a JSON string in place of the opening quote. The conformance files
   only use the escapes below. */
static char *json_text(const char **s, size_t *length) {
    char *out = (char *)malloc(strlen(*s) + 1);
    size_t n = 0;
    (*s)++;
    while (**s != '"') {
        char c = *(*s)++;
        if (c == '\\') {
            c = *(*s)++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
        }
        out[n++] = c;
    }
    (*s)++;
    out[n] = '\0';
    *length = n;
    return out;
}

static phl_value *json_value(const char **s) {
    json_space(s);
    if (**s == '{') {
        phl_value *object = phl_new_object();
        (*s)++;
        for (;;) {
            size_t length;
            char *key;
            json_space(s);
            if (**s == '}') { (*s)++; return object; }
            if (**s == ',') { (*s)++; continue; }
            key = json_text(s, &length);
            json_space(s);
            (*s)++; /* the colon */
            phl_object_set_n(object, key, length, json_value(s));
            free(key);
        }
    }
    if (**s == '[') {
        phl_value *list = phl_new_list();
        (*s)++;
        for (;;) {
            json_space(s);
            if (**s == ']') { (*s)++; return list; }
            if (**s == ',') { (*s)++; continue; }
            phl_list_push(list, json_value(s));
        }
    }
    if (**s == '"') {
        size_t length;
        char *text = json_text(s, &length);
        phl_value *value = phl_new_string_n(text, length);
        free(text);
        return value;
    }
    if (strncmp(*s, "true", 4) == 0) { *s += 4; return phl_new_bool(1); }
    if (strncmp(*s, "false", 5) == 0) { *s += 5; return phl_new_bool(0); }
    if (strncmp(*s, "null", 4) == 0) { *s += 4; return phl_new_nil(); }
    {
        const char *start = *s;
        int decimal = 0;
        while (**s == '-' || **s == '.' || (**s >= '0' && **s <= '9')) {
            if (**s == '.') decimal = 1;
            (*s)++;
        }
        return decimal ? phl_new_float(strtod(start, NULL)) : phl_new_int(strtoll(start, NULL, 10));
    }
}

static int values_equal(const phl_value *a, const phl_value *b) {
    size_t i;
    if (phl_type_of(a) != phl_type_of(b)) return 0;
    switch (phl_type_of(a)) {
        case PHL_NIL: return 1;
        case PHL_BOOL: return phl_as_bool(a) == phl_as_bool(b);
        case PHL_INT: return phl_as_int(a) == phl_as_int(b);
        case PHL_FLOAT: return phl_as_float(a) == phl_as_float(b);
        case PHL_STRING:
        case PHL_BIGINT: {
            size_t x, y;
            const char *p = phl_as_string(a, &x);
            const char *q = phl_as_string(b, &y);
            return x == y && memcmp(p, q, x) == 0;
        }
        case PHL_LIST:
            if (phl_length(a) != phl_length(b)) return 0;
            for (i = 0; i < phl_length(a); i++) {
                if (!values_equal(phl_at(a, i), phl_at(b, i))) return 0;
            }
            return 1;
        case PHL_OBJECT:
            /* Keys must also be in the same order. */
            if (phl_length(a) != phl_length(b)) return 0;
            for (i = 0; i < phl_length(a); i++) {
                size_t x, y;
                const char *p = phl_key_at(a, i, &x);
                const char *q = phl_key_at(b, i, &y);
                if (x != y || memcmp(p, q, x) != 0) return 0;
                if (!values_equal(phl_value_at(a, i), phl_value_at(b, i))) return 0;
            }
            return 1;
    }
    return 0;
}

#endif

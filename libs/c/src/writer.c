/* Writes values as PHL and HPL text. */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

typedef struct writer {
    char *data;
    size_t length;
    size_t capacity;
    int indent;
    unsigned flags;
    int nesting;
    phl_error *error;
} writer;

static int reserve(writer *w, size_t extra) {
    if (extra > SIZE_MAX - w->length - 1) {
        phl__set_error(w->error, 0, 0, "out of memory");
        return -1;
    }
    if (w->length + extra + 1 > w->capacity) {
        size_t capacity = w->capacity ? w->capacity : 256;
        char *data;
        while (capacity < w->length + extra + 1) {
            if (capacity > SIZE_MAX / 2) {
                capacity = w->length + extra + 1;
                break;
            }
            capacity *= 2;
        }
        data = (char *)PHL_REALLOC(w->data, capacity);
        if (!data) {
            phl__set_error(w->error, 0, 0, "out of memory");
            return -1;
        }
        w->data = data;
        w->capacity = capacity;
    }
    return 0;
}

static int put(writer *w, const char *text, size_t length) {
    if (reserve(w, length) != 0) return -1;
    if (length) memcpy(w->data + w->length, text, length);
    w->length += length;
    return 0;
}

static int puts_w(writer *w, const char *text) {
    return put(w, text, strlen(text));
}

static int put_pad(writer *w, int depth) {
    size_t count = (size_t)w->indent * (size_t)depth;
    if (reserve(w, count) != 0) return -1;
    if (count) memset(w->data + w->length, ' ', count);
    w->length += count;
    return 0;
}

static int write_string(writer *w, const char *text, size_t length) {
    size_t i, start = 0;
    if (!(w->flags & PHL_ASSUME_VALID_UTF8) && phl__utf8_valid_prefix(text, length) != length) {
        phl__set_error(w->error, 0, 0, "strings must be valid UTF-8");
        return -1;
    }
    if (put(w, "\"", 1) != 0) return -1;
    for (i = 0; i < length; i++) {
        const char *escape = NULL;
        unsigned char c = (unsigned char)text[i];
        switch (c) {
            case '\\': escape = "\\\\"; break;
            case '"': escape = "\\\""; break;
            case '\n': escape = "\\n"; break;
            case '\t': escape = "\\t"; break;
            default:
                if (c < 0x20) {
                    phl__set_error(w->error, 0, 0, "strings cannot contain the control character %d", (int)c);
                    return -1;
                }
                break;
        }
        if (escape) {
            if (put(w, text + start, i - start) != 0 || put(w, escape, 2) != 0) return -1;
            start = i + 1;
        }
    }
    if (put(w, text + start, length - start) != 0) return -1;
    return put(w, "\"", 1);
}

static int write_key(writer *w, const phl_key *key) {
    size_t i;
    int bare = key->length > 0;
    for (i = 0; bare && i < key->length; i++) {
        char c = key->data[i];
        bare = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }
    return bare ? put(w, key->data, key->length) : write_string(w, key->data, key->length);
}

/* The shortest digits that read back as the same number, without exponent
   notation, which PHL does not support. */
static int write_float(writer *w, double value) {
    char text[64];
    char digits[32];
    size_t count = 0;
    int precision, exponent, i;
    const char *s;

    if (!isfinite(value)) {
        phl__set_error(w->error, 0, 0, "%s cannot be written; PHL numbers must be finite",
                       isnan(value) ? "nan" : (value < 0 ? "-inf" : "inf"));
        return -1;
    }
    for (precision = 0; precision <= 16; precision++) {
        snprintf(text, sizeof(text), "%.*e", precision, value);
        if (strtod(text, NULL) == value) break;
    }

    s = text;
    if (*s == '-') {
        if (put(w, "-", 1) != 0) return -1;
        s++;
    }
    /* Digits, skipping whatever decimal point the current locale uses. */
    while (*s && *s != 'e' && *s != 'E') {
        if (*s >= '0' && *s <= '9' && count < sizeof(digits)) digits[count++] = *s;
        s++;
    }
    exponent = atoi(*s ? s + 1 : "0");

    if (exponent < 0) {
        if (put(w, "0.", 2) != 0) return -1;
        for (i = -1; i > exponent; i--) {
            if (put(w, "0", 1) != 0) return -1;
        }
        return put(w, digits, count);
    }
    for (i = 0; i <= exponent; i++) {
        char c = (size_t)i < count ? digits[i] : '0';
        if (put(w, &c, 1) != 0) return -1;
    }
    if (put(w, ".", 1) != 0) return -1;
    if ((size_t)exponent + 1 < count) return put(w, digits + exponent + 1, count - (size_t)exponent - 1);
    return put(w, "0", 1);
}

static int same_keys(const phl_value *a, const phl_value *b) {
    size_t i;
    if (a->as.object.count != b->as.object.count) return 0;
    for (i = 0; i < a->as.object.count; i++) {
        const phl_key *x = a->as.object.members[i].key;
        const phl_key *y = b->as.object.members[i].key;
        if (x != y && (x->length != y->length || memcmp(x->data, y->data, x->length) != 0)) return 0;
    }
    return 1;
}

/* A non-empty list of objects that all have the same keys in the same order. */
static int is_table(const phl_value *value) {
    size_t i;
    if (value->type != PHL_LIST || value->as.list.count == 0) return 0;
    for (i = 0; i < value->as.list.count; i++) {
        if (value->as.list.items[i]->type != PHL_OBJECT) return 0;
    }
    if (value->as.list.items[0]->as.object.count == 0) return 0;
    for (i = 1; i < value->as.list.count; i++) {
        if (!same_keys(value->as.list.items[0], value->as.list.items[i])) return 0;
    }
    return 1;
}

static int is_scalar(const phl_value *value) {
    return value->type != PHL_LIST && value->type != PHL_OBJECT;
}

static int write_value(writer *w, const phl_value *value, int depth, int inline_);

static int write_table(writer *w, const phl_value *rows, int depth, int inline_) {
    const phl_value *first = rows->as.list.items[0];
    int on_one_line = inline_ || w->indent == 0;
    size_t r, c;

    if (put(w, "(", 1) != 0) return -1;
    for (c = 0; c < first->as.object.count; c++) {
        if (c > 0 && put(w, ", ", 2) != 0) return -1;
        if (write_key(w, first->as.object.members[c].key) != 0) return -1;
    }
    if (puts_w(w, on_one_line ? ") { " : ") {\n") != 0) return -1;
    for (r = 0; r < rows->as.list.count; r++) {
        const phl_value *row = rows->as.list.items[r];
        if (on_one_line) {
            if (r > 0 && put(w, " ", 1) != 0) return -1;
        } else if (put_pad(w, depth + 1) != 0) {
            return -1;
        }
        for (c = 0; c < row->as.object.count; c++) {
            if (c > 0 && put(w, ", ", 2) != 0) return -1;
            if (write_value(w, row->as.object.members[c].value, 0, 1) != 0) return -1;
        }
        if (put(w, ";", 1) != 0) return -1;
        if (!on_one_line && put(w, "\n", 1) != 0) return -1;
    }
    if (on_one_line) return puts_w(w, " }");
    if (put_pad(w, depth) != 0) return -1;
    return put(w, "}", 1);
}

static int write_object(writer *w, const phl_value *value, int depth, int inline_) {
    int on_one_line = inline_ || w->indent == 0;
    size_t i;
    if (value->as.object.count == 0) return put(w, "{}", 2);
    if (puts_w(w, on_one_line ? "{" : "{\n") != 0) return -1;
    for (i = 0; i < value->as.object.count; i++) {
        const phl_member *member = &value->as.object.members[i];
        if (i > 0 && puts_w(w, on_one_line ? ", " : ",\n") != 0) return -1;
        if (!on_one_line && put_pad(w, depth + 1) != 0) return -1;
        if (write_key(w, member->key) != 0 || put(w, ": ", 2) != 0) return -1;
        if (write_value(w, member->value, depth + 1, inline_) != 0) return -1;
    }
    if (on_one_line) return put(w, "}", 1);
    if (put(w, "\n", 1) != 0 || put_pad(w, depth) != 0) return -1;
    return put(w, "}", 1);
}

static int write_list(writer *w, const phl_value *value, int depth, int inline_) {
    int on_one_line;
    size_t i;
    if (value->as.list.count == 0) return put(w, "[]", 2);
    if (!inline_) {
        inline_ = 1;
        for (i = 0; i < value->as.list.count; i++) {
            if (!is_scalar(value->as.list.items[i])) {
                inline_ = 0;
                break;
            }
        }
    }
    on_one_line = inline_ || w->indent == 0;
    if (puts_w(w, on_one_line ? "[" : "[\n") != 0) return -1;
    for (i = 0; i < value->as.list.count; i++) {
        if (i > 0 && puts_w(w, on_one_line ? ", " : ",\n") != 0) return -1;
        if (!on_one_line && put_pad(w, depth + 1) != 0) return -1;
        if (write_value(w, value->as.list.items[i], depth + 1, inline_) != 0) return -1;
    }
    if (on_one_line) return put(w, "]", 1);
    if (put(w, "\n", 1) != 0 || put_pad(w, depth) != 0) return -1;
    return put(w, "]", 1);
}

static int write_nested(writer *w, const phl_value *value, int depth, int inline_) {
    int result;
    if (w->nesting >= PHL_MAX_DEPTH) {
        phl__set_error(w->error, 0, 0, "nesting is deeper than the limit of %d levels", PHL_MAX_DEPTH);
        return -1;
    }
    w->nesting++;
    if (value->type == PHL_OBJECT) {
        result = write_object(w, value, depth, inline_);
    } else if (is_table(value)) {
        result = write_table(w, value, depth, inline_);
    } else {
        result = write_list(w, value, depth, inline_);
    }
    w->nesting--;
    return result;
}

static int write_value(writer *w, const phl_value *value, int depth, int inline_) {
    char number[32];
    switch (value->type) {
        /* PHL has no boolean or null values, so they are written as the strings
           that coercion reads back. */
        case PHL_NIL:
            return puts_w(w, "\"nil\"");
        case PHL_BOOL:
            return puts_w(w, value->as.boolean ? "\"true\"" : "\"false\"");
        case PHL_INT:
            snprintf(number, sizeof(number), "%lld", value->as.integer);
            return puts_w(w, number);
        case PHL_BIGINT:
            return put(w, value->as.string.data, value->as.string.length);
        case PHL_FLOAT:
            return write_float(w, value->as.number);
        case PHL_STRING:
            return write_string(w, value->as.string.data, value->as.string.length);
        case PHL_LIST:
        case PHL_OBJECT:
            return write_nested(w, value, depth, inline_);
    }
    phl__set_error(w->error, 0, 0, "cannot write a value of an unknown type");
    return -1;
}

static char *finish(writer *w, int ok, size_t *length) {
    if (ok != 0 || reserve(w, 0) != 0) {
        PHL_FREE(w->data);
        if (length) *length = 0;
        return NULL;
    }
    w->data[w->length] = '\0';
    if (length) *length = w->length;
    return w->data;
}

static void start(writer *w, int indent, unsigned flags, phl_error *error) {
    memset(w, 0, sizeof(*w));
    w->indent = indent > 0 ? indent : 0;
    w->flags = flags;
    w->error = error;
    if (error) {
        error->line = 0;
        error->column = 0;
        error->message[0] = '\0';
    }
}

char *phl_write(const phl_value *document, int indent, unsigned flags, size_t *length, phl_error *error) {
    writer w;
    int ok = 0;
    size_t i;
    start(&w, indent, flags, error);
    if (!document || document->type != PHL_OBJECT) {
        phl__set_error(error, 0, 0, "a PHL document must be an object of top-level entries");
        return finish(&w, -1, length);
    }
    for (i = 0; ok == 0 && i < document->as.object.count; i++) {
        const phl_member *member = &document->as.object.members[i];
        ok = write_key(&w, member->key);
        if (ok == 0) ok = put(&w, ": ", 2);
        if (ok == 0) ok = write_value(&w, member->value, 0, 0);
        if (ok == 0) ok = put(&w, "\n", 1);
    }
    return finish(&w, ok, length);
}

char *phl_write_table(const phl_value *rows, int indent, unsigned flags, size_t *length, phl_error *error) {
    writer w;
    int ok;
    start(&w, indent, flags, error);
    if (!rows || !is_table(rows)) {
        phl__set_error(error, 0, 0, "an HPL table must be a non-empty list of objects that share the same keys");
        return finish(&w, -1, length);
    }
    w.nesting = 1;
    ok = write_table(&w, rows, 0, 0);
    if (ok == 0) ok = put(&w, "\n", 1);
    return finish(&w, ok, length);
}

void phl_free_text(char *text) {
    PHL_FREE(text);
}

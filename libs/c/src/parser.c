/* Turns PHL and HPL text into tokens, and tokens into values. */

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

typedef enum token_kind {
    TOKEN_STRING,
    TOKEN_INTEGER, /* a negative whole number; plain digits are TOKEN_WORD */
    TOKEN_DECIMAL,
    TOKEN_WORD,
    TOKEN_PUNCT,
    TOKEN_NEWLINE,
    TOKEN_END
} token_kind;

/* Tokens point into the source text. Strings are decoded when they are used. */
typedef struct token {
    token_kind kind;
    char punct;
    size_t start;
    size_t end;
    size_t line;
    size_t line_start;
} token;

typedef struct parser {
    const char *text;
    size_t length;
    unsigned flags;
    token *tokens;
    size_t count;
    size_t capacity;
    size_t pos;
    int depth;
    phl_error *error;
} parser;

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_word_char(char c) {
    return is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int all_digits(const char *text, size_t length) {
    size_t i;
    if (length == 0) return 0;
    for (i = 0; i < length; i++) {
        if (!is_digit(text[i])) return 0;
    }
    return 1;
}

/* Columns count code points, so continuation bytes are skipped. */
static size_t column_of(const parser *p, size_t line_start, size_t offset) {
    size_t i, column = 1;
    for (i = line_start; i < offset; i++) {
        if (((unsigned char)p->text[i] & 0xC0) != 0x80) column++;
    }
    return column;
}

static void oom(parser *p) {
    phl__set_error(p->error, 0, 0, "out of memory");
}

/* Tokenizer */

static int add_token(parser *p, token_kind kind, size_t start, size_t end, size_t line, size_t line_start) {
    token *t;
    if (p->count == p->capacity) {
        size_t capacity = p->capacity ? p->capacity * 2 : 64;
        token *tokens;
        if (capacity > SIZE_MAX / sizeof(token)) {
            oom(p);
            return -1;
        }
        tokens = (token *)PHL_REALLOC(p->tokens, capacity * sizeof(token));
        if (!tokens) {
            oom(p);
            return -1;
        }
        p->tokens = tokens;
        p->capacity = capacity;
    }
    t = &p->tokens[p->count++];
    t->kind = kind;
    t->punct = kind == TOKEN_PUNCT ? p->text[start] : '\0';
    t->start = start;
    t->end = end;
    t->line = line;
    t->line_start = line_start;
    return 0;
}

static int read_string(parser *p, size_t *index, size_t line, size_t line_start) {
    size_t start = *index;
    size_t i = start + 1;
    for (;;) {
        char c;
        if (i >= p->length || p->text[i] == '\n') {
            phl__set_error(p->error, line, column_of(p, line_start, start), "unterminated string");
            return -1;
        }
        c = p->text[i];
        if (c == '"') {
            *index = i + 1;
            return add_token(p, TOKEN_STRING, start + 1, i, line, line_start);
        }
        if (c == '\\') {
            char e = i + 1 < p->length ? p->text[i + 1] : '\0';
            if (i + 1 >= p->length || (e != 'n' && e != 't' && e != '"' && e != '\\')) {
                /* Show the whole character after the backslash. */
                size_t width = 0;
                if (i + 1 < p->length) {
                    width = 1;
                    while (i + 1 + width < p->length && ((unsigned char)p->text[i + 1 + width] & 0xC0) == 0x80) width++;
                    if (p->text[i + 1] == '\n') width = 0;
                }
                phl__set_error(p->error, line, column_of(p, line_start, i), "invalid escape sequence '\\%.*s'",
                               (int)width, p->text + i + 1);
                return -1;
            }
            i += 2;
        } else {
            i++;
        }
    }
}

static int read_number_or_word(parser *p, size_t *index, size_t line, size_t line_start) {
    size_t start = *index;
    size_t i = start;
    int negative = p->text[i] == '-';
    size_t head_end;

    if (negative) {
        i++;
        if (i >= p->length || !is_digit(p->text[i])) {
            phl__set_error(p->error, line, column_of(p, line_start, start), "expected a digit after '-'");
            return -1;
        }
    }
    while (i < p->length && is_word_char(p->text[i])) i++;
    head_end = i;

    if (!all_digits(p->text + start + (negative ? 1 : 0), head_end - start - (negative ? 1 : 0))) {
        if (negative) {
            phl__set_error(p->error, line, column_of(p, line_start, start), "invalid number '%.*s'",
                           (int)(head_end - start > 64 ? 64 : head_end - start), p->text + start);
            return -1;
        }
        /* Keys may start with a digit, for example 2fa_enabled. */
        *index = i;
        return add_token(p, TOKEN_WORD, start, i, line, line_start);
    }

    if (i + 1 < p->length && p->text[i] == '.' && is_digit(p->text[i + 1])) {
        i++;
        while (i < p->length && is_digit(p->text[i])) i++;
        if (i < p->length && is_word_char(p->text[i])) {
            phl__set_error(p->error, line, column_of(p, line_start, start), "invalid number '%.*s'",
                           (int)(i + 1 - start > 64 ? 64 : i + 1 - start), p->text + start);
            return -1;
        }
        *index = i;
        return add_token(p, TOKEN_DECIMAL, start, i, line, line_start);
    }

    *index = i;
    /* A plain run of digits can be a key or a number; the parser decides. */
    return add_token(p, negative ? TOKEN_INTEGER : TOKEN_WORD, start, i, line, line_start);
}

static int tokenize(parser *p) {
    size_t i = 0;
    size_t line = 1;
    size_t line_start = 0;

    if (p->length >= 3 && memcmp(p->text, "\xEF\xBB\xBF", 3) == 0) {
        i = 3;
        line_start = 3;
    }

    while (i < p->length) {
        char c = p->text[i];
        if (c == '\n') {
            if (add_token(p, TOKEN_NEWLINE, i, i + 1, line, line_start) != 0) return -1;
            i++;
            line++;
            line_start = i;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            i++;
        } else if (c == '/' && i + 1 < p->length && p->text[i + 1] == '/') {
            const char *end = (const char *)memchr(p->text + i, '\n', p->length - i);
            i = end ? (size_t)(end - p->text) : p->length;
        } else if (c == '"') {
            if (read_string(p, &i, line, line_start) != 0) return -1;
        } else if (c == '-' || is_digit(c)) {
            if (read_number_or_word(p, &i, line, line_start) != 0) return -1;
        } else if (is_word_char(c)) {
            size_t start = i;
            while (i < p->length && is_word_char(p->text[i])) i++;
            if (add_token(p, TOKEN_WORD, start, i, line, line_start) != 0) return -1;
        } else if (c != '\0' && strchr(":,;[]{}()", c)) {
            if (add_token(p, TOKEN_PUNCT, i, i + 1, line, line_start) != 0) return -1;
            i++;
        } else {
            size_t width = 1;
            while (i + width < p->length && ((unsigned char)p->text[i + width] & 0xC0) == 0x80) width++;
            if ((unsigned char)c < 0x20) {
                phl__set_error(p->error, line, column_of(p, line_start, i), "unexpected control character %d", (int)c);
            } else {
                phl__set_error(p->error, line, column_of(p, line_start, i), "unexpected character '%.*s'", (int)width,
                               p->text + i);
            }
            return -1;
        }
    }
    return add_token(p, TOKEN_END, p->length, p->length, line, line_start);
}

/* Token helpers */

static const token *peek(const parser *p, size_t offset) {
    /* The last token is always TOKEN_END. */
    size_t last = p->count - 1;
    size_t i = p->pos + offset;
    return &p->tokens[i < last ? i : last];
}

static const token *next_token(parser *p) {
    const token *t = &p->tokens[p->pos];
    if (t->kind != TOKEN_END) p->pos++;
    return t;
}

static void skip_newlines(parser *p) {
    while (peek(p, 0)->kind == TOKEN_NEWLINE) p->pos++;
}

static int is_punct(const token *t, char c) {
    return t->kind == TOKEN_PUNCT && t->punct == c;
}

static int is_key_like(const token *t) {
    return t->kind == TOKEN_WORD || t->kind == TOKEN_STRING;
}

/* Text of a token for messages. Long tokens are cut short. */
static const char *describe(const parser *p, const token *t, char *buffer, size_t size) {
    if (t->kind == TOKEN_END) return "end of input";
    if (t->kind == TOKEN_NEWLINE) return "end of line";
    if (t->kind == TOKEN_STRING) return "string";
    snprintf(buffer, size, "'%.*s'", (int)(t->end - t->start > 64 ? 64 : t->end - t->start), p->text + t->start);
    return buffer;
}

static void error_at(parser *p, const token *t, const char *message) {
    phl__set_error(p->error, t->line, column_of(p, t->line_start, t->start - (t->kind == TOKEN_STRING ? 1 : 0)), "%s", message);
}

/* format has one %s, for the token that was found. */
static void error_found(parser *p, const token *t, const char *format) {
    char found[80];
    char message[200];
    snprintf(message, sizeof(message), format, describe(p, t, found, sizeof(found)));
    error_at(p, t, message);
}

static int is_key_ahead(const parser *p) {
    size_t offset = 1;
    if (!is_key_like(peek(p, 0))) return 0;
    while (peek(p, offset)->kind == TOKEN_NEWLINE) offset++;
    return is_punct(peek(p, offset), ':');
}

/* The text of a string or word token. Returns a new key. */
static phl_key *decode_key(parser *p, const token *t) {
    phl_key *key;
    size_t i, out = 0;
    if (t->kind != TOKEN_STRING) {
        key = phl__key_new(p->text + t->start, t->end - t->start);
        if (!key) oom(p);
        return key;
    }
    /* Decoding never makes a string longer. */
    key = phl__key_new(p->text + t->start, t->end - t->start);
    if (!key) {
        oom(p);
        return NULL;
    }
    for (i = t->start; i < t->end; i++) {
        char c = p->text[i];
        if (c == '\\') {
            i++;
            switch (p->text[i]) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                default: c = p->text[i]; break;
            }
        }
        key->data[out++] = c;
    }
    key->data[out] = '\0';
    key->length = out;
    return key;
}

/* Sets of keys, to find duplicates in large objects without comparing every
   pair. The first slots live on the stack so small objects need no allocation. */

#define KEY_SET_INLINE 32

typedef struct key_set {
    phl_key *inline_slots[KEY_SET_INLINE];
    phl_key **slots;
    size_t capacity;
    size_t count;
} key_set;

static void key_set_init(key_set *set) {
    memset(set->inline_slots, 0, sizeof(set->inline_slots));
    set->slots = set->inline_slots;
    set->capacity = KEY_SET_INLINE;
    set->count = 0;
}

static void key_set_free(key_set *set) {
    if (set->slots != set->inline_slots) PHL_FREE(set->slots);
}

static size_t hash_key(const phl_key *key) {
    /* FNV-1a */
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < key->length; i++) {
        hash ^= (unsigned char)key->data[i];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)(hash ^ (hash >> 32));
}

static int keys_equal(const phl_key *a, const phl_key *b) {
    return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}

/* Returns 0 when added, 1 when the key was already there, -1 out of memory. The
   set borrows the key, which must outlive it. */
static int key_set_add(key_set *set, phl_key *key) {
    size_t slot;
    if ((set->count + 1) * 2 > set->capacity) {
        size_t capacity = set->capacity * 2;
        size_t i;
        phl_key **slots;
        if (capacity > SIZE_MAX / sizeof(phl_key *)) return -1;
        slots = (phl_key **)PHL_MALLOC(capacity * sizeof(phl_key *));
        if (!slots) return -1;
        memset(slots, 0, capacity * sizeof(phl_key *));
        for (i = 0; i < set->capacity; i++) {
            if (set->slots[i]) {
                slot = hash_key(set->slots[i]) & (capacity - 1);
                while (slots[slot]) slot = (slot + 1) & (capacity - 1);
                slots[slot] = set->slots[i];
            }
        }
        key_set_free(set);
        set->slots = slots;
        set->capacity = capacity;
    }
    slot = hash_key(key) & (set->capacity - 1);
    while (set->slots[slot]) {
        if (keys_equal(set->slots[slot], key)) return 1;
        slot = (slot + 1) & (set->capacity - 1);
    }
    set->slots[slot] = key;
    set->count++;
    return 0;
}

/* Values */

static phl_value *parse_value(parser *p);

/* Tracks nesting. Returns -1 and sets the error when it goes too deep. */
static int enter(parser *p, const token *open) {
    if (p->depth >= PHL_MAX_DEPTH) {
        char message[64];
        snprintf(message, sizeof(message), "nesting is deeper than the limit of %d levels", PHL_MAX_DEPTH);
        error_at(p, open, message);
        return -1;
    }
    p->depth++;
    return 0;
}

static int equals_ignore_case(const char *text, size_t length, const char *lower) {
    size_t i;
    if (strlen(lower) != length) return 0;
    for (i = 0; i < length; i++) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lower[i]) return 0;
    }
    return 1;
}

static phl_value *string_value(parser *p, const token *t) {
    phl_key *decoded = decode_key(p, t);
    phl_value *value;
    if (!decoded) return NULL;
    if (!(p->flags & PHL_NO_COERCE)) {
        if (equals_ignore_case(decoded->data, decoded->length, "true")) {
            value = phl_new_bool(1);
        } else if (equals_ignore_case(decoded->data, decoded->length, "false")) {
            value = phl_new_bool(0);
        } else if (equals_ignore_case(decoded->data, decoded->length, "nil") ||
                   equals_ignore_case(decoded->data, decoded->length, "null") ||
                   equals_ignore_case(decoded->data, decoded->length, "none")) {
            value = phl_new_nil();
        } else {
            value = phl_new_string_n(decoded->data, decoded->length);
        }
    } else {
        value = phl_new_string_n(decoded->data, decoded->length);
    }
    phl__key_release(decoded);
    if (!value) oom(p);
    return value;
}

static phl_value *integer_value(parser *p, const token *t) {
    const char *text = p->text + t->start;
    size_t length = t->end - t->start;
    int negative = text[0] == '-';
    /* The largest magnitude allowed: 2^63 - 1, or 2^63 for negative numbers. */
    unsigned long long limit = negative ? (unsigned long long)INT64_MAX + 1u : (unsigned long long)INT64_MAX;
    unsigned long long magnitude = 0;
    size_t i;
    phl_value *value;

    for (i = negative ? 1 : 0; i < length; i++) {
        unsigned digit = (unsigned)(text[i] - '0');
        if (magnitude > (limit - digit) / 10) {
            size_t line = t->line, column = column_of(p, t->line_start, t->start);
            if (p->flags & PHL_KEEP_BIG_INTEGERS) {
                value = phl__new_bigint(text, length, line, column);
                if (!value) oom(p);
                return value;
            }
            phl__set_error(p->error, line, column, "the number %.*s%s does not fit in 64 bits",
                           (int)(length > 40 ? 40 : length), text, length > 40 ? "..." : "");
            return NULL;
        }
        magnitude = magnitude * 10 + digit;
    }
    if (negative) {
        /* Written this way so that -2^63 does not overflow. */
        value = phl_new_int(magnitude == 0 ? 0 : -(long long)(magnitude - 1) - 1);
    } else {
        value = phl_new_int((long long)magnitude);
    }
    if (!value) oom(p);
    return value;
}

static phl_value *decimal_value(parser *p, const token *t) {
    size_t length = t->end - t->start;
    const char *point = localeconv()->decimal_point;
    size_t point_length = point ? strlen(point) : 0;
    char small[64];
    char *buffer = small;
    size_t dot;
    double number;
    phl_value *value;

    /* strtod expects the decimal point of the current locale. */
    if (!point_length) {
        point = ".";
        point_length = 1;
    }
    if (length + point_length + 1 > sizeof(small)) {
        buffer = (char *)PHL_MALLOC(length + point_length + 1);
        if (!buffer) {
            oom(p);
            return NULL;
        }
    }
    dot = (size_t)((const char *)memchr(p->text + t->start, '.', length) - (p->text + t->start));
    memcpy(buffer, p->text + t->start, dot);
    memcpy(buffer + dot, point, point_length);
    memcpy(buffer + dot + point_length, p->text + t->start + dot + 1, length - dot - 1);
    buffer[length - 1 + point_length] = '\0';
    number = strtod(buffer, NULL);
    if (buffer != small) PHL_FREE(buffer);

    if (!isfinite(number)) {
        error_at(p, t, "this number is too large for a 64-bit decimal");
        return NULL;
    }
    value = phl_new_float(number);
    if (!value) oom(p);
    return value;
}

/* Reads `key : value` into target. */
static int parse_entry(parser *p, phl_value *target, key_set *seen) {
    const token *key_token = next_token(p);
    phl_key *key;
    phl_value *value;
    int added;

    if (!is_key_like(key_token)) {
        error_found(p, key_token, "expected a key, found %s");
        return -1;
    }
    key = decode_key(p, key_token);
    if (!key) return -1;

    skip_newlines(p);
    {
        const token *colon = next_token(p);
        if (!is_punct(colon, ':')) {
            char message[200];
            char found[80];
            snprintf(message, sizeof(message), "expected ':' after key '%.*s', found %s",
                     (int)(key->length > 64 ? 64 : key->length), key->data, describe(p, colon, found, sizeof(found)));
            error_at(p, colon, message);
            phl__key_release(key);
            return -1;
        }
    }
    skip_newlines(p);

    added = key_set_add(seen, key);
    if (added < 0) {
        oom(p);
        phl__key_release(key);
        return -1;
    }
    if (added > 0) {
        char message[120];
        snprintf(message, sizeof(message), "duplicate key '%.*s'", (int)(key->length > 64 ? 64 : key->length), key->data);
        error_at(p, key_token, message);
        phl__key_release(key);
        return -1;
    }

    /* The object's reference keeps the key alive for the set. */
    value = parse_value(p);
    if (!value) {
        phl__key_release(key);
        return -1;
    }
    if (phl__object_append(target, key, value) != 0) {
        oom(p);
        phl_free(value);
        phl__key_release(key);
        return -1;
    }
    phl__key_release(key);
    return 0;
}

/* Reads ',' (gives 1) or the closing bracket (gives 0). Failure gives -1. */
static int item_separator(parser *p, const char *kind, char closer) {
    const token *t;
    skip_newlines(p);
    t = next_token(p);
    if (is_punct(t, ',')) return 1;
    if (is_punct(t, closer)) return 0;
    {
        char found[80];
        char message[200];
        snprintf(message, sizeof(message), "expected ',' or '%c' in %s, found %s", closer, kind,
                 describe(p, t, found, sizeof(found)));
        error_at(p, t, message);
    }
    return -1;
}

static phl_value *parse_object(parser *p) {
    const token *open = next_token(p);
    phl_value *result;
    key_set seen;

    if (enter(p, open) != 0) return NULL;
    result = phl_new_object();
    if (!result) {
        oom(p);
        return NULL;
    }
    key_set_init(&seen);
    for (;;) {
        int separator;
        skip_newlines(p);
        if (is_punct(peek(p, 0), '}')) {
            next_token(p);
            break;
        }
        if (!is_key_ahead(p)) {
            error_at(p, peek(p, 0), "expected 'key: value' inside an object");
            goto fail;
        }
        if (parse_entry(p, result, &seen) != 0) goto fail;
        separator = item_separator(p, "object", '}');
        if (separator < 0) goto fail;
        if (separator == 0) break;
    }
    key_set_free(&seen);
    p->depth--;
    return result;

fail:
    key_set_free(&seen);
    phl_free(result);
    return NULL;
}

static phl_value *parse_list(parser *p) {
    const token *open = next_token(p);
    phl_value *result;

    if (enter(p, open) != 0) return NULL;
    result = phl_new_list();
    if (!result) {
        oom(p);
        return NULL;
    }
    for (;;) {
        phl_value *item;
        int separator;
        skip_newlines(p);
        if (is_punct(peek(p, 0), ']')) {
            next_token(p);
            break;
        }
        if (is_key_ahead(p)) {
            error_at(p, peek(p, 0), "a list cannot contain 'key: value' entries; use { } for an object");
            goto fail;
        }
        item = parse_value(p);
        if (!item) goto fail;
        if (phl__list_append(result, item) != 0) {
            oom(p);
            phl_free(item);
            goto fail;
        }
        separator = item_separator(p, "list", ']');
        if (separator < 0) goto fail;
        if (separator == 0) break;
    }
    p->depth--;
    return result;

fail:
    phl_free(result);
    return NULL;
}

/* (col, col) { value, value; value, value; } */
static phl_value *parse_table(parser *p) {
    const token *open = next_token(p);
    phl_value *result = NULL;
    phl_key **columns = NULL;
    size_t column_count = 0, column_capacity = 0;
    phl_value **cells = NULL;
    size_t cell_count = 0, cell_capacity = 0;
    key_set seen;
    size_t i;

    if (enter(p, open) != 0) return NULL;
    key_set_init(&seen);

    /* Column names */
    for (;;) {
        const token *t;
        phl_key *key;
        int added;
        skip_newlines(p);
        if (is_punct(peek(p, 0), ')') && column_count > 0) {
            next_token(p);
            break;
        }
        t = next_token(p);
        if (!is_key_like(t)) {
            error_found(p, t, "expected a column name, found %s");
            goto fail;
        }
        key = decode_key(p, t);
        if (!key) goto fail;
        added = key_set_add(&seen, key);
        if (added != 0) {
            if (added < 0) {
                oom(p);
            } else {
                char message[120];
                snprintf(message, sizeof(message), "duplicate column '%.*s'", (int)(key->length > 64 ? 64 : key->length), key->data);
                error_at(p, t, message);
            }
            phl__key_release(key);
            goto fail;
        }
        if (column_count == column_capacity) {
            size_t capacity = column_capacity ? column_capacity * 2 : 8;
            phl_key **grown = capacity <= SIZE_MAX / sizeof(phl_key *)
                                  ? (phl_key **)PHL_REALLOC(columns, capacity * sizeof(phl_key *))
                                  : NULL;
            if (!grown) {
                oom(p);
                phl__key_release(key);
                goto fail;
            }
            columns = grown;
            column_capacity = capacity;
        }
        columns[column_count++] = key;
        skip_newlines(p);
        t = next_token(p);
        if (is_punct(t, ')')) break;
        if (!is_punct(t, ',')) {
            error_found(p, t, "expected ',' or ')' in column list, found %s");
            goto fail;
        }
    }

    /* Rows */
    skip_newlines(p);
    {
        const token *t = next_token(p);
        if (!is_punct(t, '{')) {
            error_found(p, t, "expected '{' to start the table rows, found %s");
            goto fail;
        }
    }
    result = phl_new_list();
    if (!result) {
        oom(p);
        goto fail;
    }
    for (;;) {
        const token *row_token;
        phl_value *row;
        skip_newlines(p);
        if (is_punct(peek(p, 0), '}')) {
            next_token(p);
            break;
        }
        row_token = peek(p, 0);
        cell_count = 0;
        for (;;) {
            const token *t;
            phl_value *cell;
            skip_newlines(p);
            cell = parse_value(p);
            if (!cell) goto fail;
            if (cell_count == cell_capacity) {
                size_t capacity = cell_capacity ? cell_capacity * 2 : (column_count > 8 ? column_count : 8);
                phl_value **grown = capacity <= SIZE_MAX / sizeof(phl_value *)
                                        ? (phl_value **)PHL_REALLOC(cells, capacity * sizeof(phl_value *))
                                        : NULL;
                if (!grown) {
                    oom(p);
                    phl_free(cell);
                    goto fail;
                }
                cells = grown;
                cell_capacity = capacity;
            }
            cells[cell_count++] = cell;
            skip_newlines(p);
            t = next_token(p);
            if (is_punct(t, ';')) break;
            if (!is_punct(t, ',')) {
                error_found(p, t, "expected ',' or ';' in table row, found %s");
                goto fail;
            }
        }
        if (cell_count != column_count) {
            char message[120];
            snprintf(message, sizeof(message), "row has %lu values but the table has %lu columns",
                     (unsigned long)cell_count, (unsigned long)column_count);
            error_at(p, row_token, message);
            goto fail;
        }
        row = phl_new_object();
        if (!row || phl__object_reserve(row, column_count) != 0) {
            oom(p);
            phl_free(row);
            goto fail;
        }
        for (i = 0; i < column_count; i++) {
            /* Space was reserved, so this cannot fail. */
            phl__object_append(row, columns[i], cells[i]);
        }
        cell_count = 0;
        if (phl__list_append(result, row) != 0) {
            oom(p);
            phl_free(row);
            goto fail;
        }
    }

    for (i = 0; i < column_count; i++) phl__key_release(columns[i]);
    PHL_FREE(columns);
    PHL_FREE(cells);
    key_set_free(&seen);
    p->depth--;
    return result;

fail:
    for (i = 0; i < cell_count; i++) phl_free(cells[i]);
    PHL_FREE(cells);
    for (i = 0; i < column_count; i++) phl__key_release(columns[i]);
    PHL_FREE(columns);
    key_set_free(&seen);
    phl_free(result);
    return NULL;
}

static phl_value *parse_value(parser *p) {
    const token *t = peek(p, 0);
    switch (t->kind) {
        case TOKEN_STRING:
            next_token(p);
            return string_value(p, t);
        case TOKEN_INTEGER:
            next_token(p);
            return integer_value(p, t);
        case TOKEN_DECIMAL:
            next_token(p);
            return decimal_value(p, t);
        case TOKEN_WORD:
            next_token(p);
            if (all_digits(p->text + t->start, t->end - t->start)) return integer_value(p, t);
            {
                char message[140];
                snprintf(message, sizeof(message), "unexpected word '%.*s'; strings need double quotes",
                         (int)(t->end - t->start > 64 ? 64 : t->end - t->start), p->text + t->start);
                error_at(p, t, message);
            }
            return NULL;
        case TOKEN_PUNCT:
            if (t->punct == '{') return parse_object(p);
            if (t->punct == '[') return parse_list(p);
            if (t->punct == '(') return parse_table(p);
            break;
        case TOKEN_END:
            error_at(p, t, "expected a value, found end of input");
            return NULL;
        default:
            break;
    }
    error_found(p, t, "expected a value, found %s");
    return NULL;
}

/* Documents */

static phl_value *parse_document(parser *p) {
    phl_value *result = phl_new_object();
    key_set seen;
    if (!result) {
        oom(p);
        return NULL;
    }
    key_set_init(&seen);
    skip_newlines(p);
    while (peek(p, 0)->kind != TOKEN_END) {
        const token *t;
        if (parse_entry(p, result, &seen) != 0) goto fail;
        t = peek(p, 0);
        if (is_punct(t, ',')) {
            next_token(p);
            skip_newlines(p);
        } else if (t->kind == TOKEN_NEWLINE) {
            skip_newlines(p);
        } else if (t->kind != TOKEN_END) {
            error_found(p, t, "expected a new line or ',' between entries, found %s");
            goto fail;
        }
    }
    key_set_free(&seen);
    return result;

fail:
    key_set_free(&seen);
    phl_free(result);
    return NULL;
}

static phl_value *parse_table_document(parser *p) {
    const token *t;
    phl_value *rows;
    skip_newlines(p);
    t = peek(p, 0);
    if (!is_punct(t, '(')) {
        error_found(p, t, "expected '(' to start the column list, found %s");
        return NULL;
    }
    rows = parse_table(p);
    if (!rows) return NULL;
    skip_newlines(p);
    t = peek(p, 0);
    if (t->kind != TOKEN_END) {
        error_found(p, t, "unexpected %s after the table");
        phl_free(rows);
        return NULL;
    }
    return rows;
}

static phl_value *run(const char *text, size_t length, unsigned flags, phl_error *error, int table) {
    parser p;
    phl_value *result = NULL;

    if (error) {
        error->line = 0;
        error->column = 0;
        error->message[0] = '\0';
    }
    if (!text) {
        if (length != 0) {
            phl__set_error(error, 0, 0, "text is NULL");
            return NULL;
        }
        text = "";
    }
    memset(&p, 0, sizeof(p));
    p.text = text;
    p.length = length;
    p.flags = flags;
    p.error = error;

    if (!(flags & PHL_ASSUME_VALID_UTF8)) {
        size_t valid = phl__utf8_valid_prefix(text, length);
        if (valid != length) {
            size_t i, line = 1, line_start = 0;
            for (i = 0; i < valid; i++) {
                if (text[i] == '\n') {
                    line++;
                    line_start = i + 1;
                }
            }
            phl__set_error(error, line, column_of(&p, line_start, valid), "the text is not valid UTF-8");
            return NULL;
        }
    }

    if (tokenize(&p) == 0) {
        result = table ? parse_table_document(&p) : parse_document(&p);
    }
    PHL_FREE(p.tokens);
    return result;
}

phl_value *phl_parse(const char *text, size_t length, unsigned flags, phl_error *error) {
    return run(text, length, flags, error, 0);
}

phl_value *phl_parse_table(const char *text, size_t length, unsigned flags, phl_error *error) {
    return run(text, length, flags, error, 1);
}

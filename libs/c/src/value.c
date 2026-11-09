/* The value tree: building it, reading it and releasing it. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "internal.h"

/* Keys */

phl_key *phl__key_new(const char *data, size_t length) {
    phl_key *key;
    if (length > SIZE_MAX - sizeof(phl_key) - 1) return NULL;
    key = (phl_key *)PHL_MALLOC(sizeof(phl_key) + length + 1);
    if (!key) return NULL;
    key->refs = 1;
    key->length = length;
    if (length) memcpy(key->data, data, length);
    key->data[length] = '\0';
    return key;
}

phl_key *phl__key_retain(phl_key *key) {
    key->refs++;
    return key;
}

void phl__key_release(phl_key *key) {
    if (key && --key->refs == 0) PHL_FREE(key);
}

/* Creating values */

static phl_value *new_value(phl_type type) {
    phl_value *value = (phl_value *)PHL_MALLOC(sizeof(phl_value));
    if (!value) return NULL;
    memset(value, 0, sizeof(*value));
    value->type = type;
    return value;
}

phl_value *phl_new_nil(void) {
    return new_value(PHL_NIL);
}

phl_value *phl_new_bool(int boolean) {
    phl_value *value = new_value(PHL_BOOL);
    if (value) value->as.boolean = boolean != 0;
    return value;
}

phl_value *phl_new_int(long long integer) {
    phl_value *value = new_value(PHL_INT);
    if (value) value->as.integer = integer;
    return value;
}

phl_value *phl_new_float(double number) {
    phl_value *value = new_value(PHL_FLOAT);
    if (value) value->as.number = number;
    return value;
}

static phl_value *new_text(phl_type type, const char *text, size_t length) {
    phl_value *value;
    if (length == SIZE_MAX) return NULL;
    value = new_value(type);
    if (!value) return NULL;
    value->as.string.data = (char *)PHL_MALLOC(length + 1);
    if (!value->as.string.data) {
        PHL_FREE(value);
        return NULL;
    }
    if (length) memcpy(value->as.string.data, text, length);
    value->as.string.data[length] = '\0';
    value->as.string.length = length;
    return value;
}

phl_value *phl_new_string_n(const char *text, size_t length) {
    return new_text(PHL_STRING, text, length);
}

phl_value *phl_new_string(const char *text) {
    return text ? new_text(PHL_STRING, text, strlen(text)) : NULL;
}

phl_value *phl__new_bigint(const char *text, size_t length, size_t line, size_t column) {
    phl_value *value = new_text(PHL_BIGINT, text, length);
    if (value) {
        value->as.string.line = line;
        value->as.string.column = column;
    }
    return value;
}

phl_value *phl_new_list(void) {
    return new_value(PHL_LIST);
}

phl_value *phl_new_object(void) {
    return new_value(PHL_OBJECT);
}

/* Adding to lists and objects */

int phl__list_append(phl_value *list, phl_value *item) {
    if (list->as.list.count == list->as.list.capacity) {
        size_t capacity = list->as.list.capacity ? list->as.list.capacity * 2 : 4;
        phl_value **items;
        if (capacity > SIZE_MAX / sizeof(phl_value *)) return -1;
        items = (phl_value **)PHL_REALLOC(list->as.list.items, capacity * sizeof(phl_value *));
        if (!items) return -1;
        list->as.list.items = items;
        list->as.list.capacity = capacity;
    }
    list->as.list.items[list->as.list.count++] = item;
    return 0;
}

int phl__object_reserve(phl_value *object, size_t capacity) {
    phl_member *members;
    if (capacity <= object->as.object.capacity) return 0;
    if (capacity > SIZE_MAX / sizeof(phl_member)) return -1;
    members = (phl_member *)PHL_REALLOC(object->as.object.members, capacity * sizeof(phl_member));
    if (!members) return -1;
    object->as.object.members = members;
    object->as.object.capacity = capacity;
    return 0;
}

int phl__object_append(phl_value *object, phl_key *key, phl_value *item) {
    if (object->as.object.count == object->as.object.capacity) {
        size_t capacity = object->as.object.capacity ? object->as.object.capacity * 2 : 4;
        if (phl__object_reserve(object, capacity) != 0) return -1;
    }
    object->as.object.members[object->as.object.count].key = phl__key_retain(key);
    object->as.object.members[object->as.object.count].value = item;
    object->as.object.count++;
    return 0;
}

int phl_list_push(phl_value *list, phl_value *item) {
    if (!item) return -1;
    if (!list || list->type != PHL_LIST || phl__list_append(list, item) != 0) {
        phl_free(item);
        return -1;
    }
    return 0;
}

int phl_object_set_n(phl_value *object, const char *key, size_t key_length, phl_value *item) {
    phl_key *stored;
    size_t i;
    int result;
    if (!item) return -1;
    if (!object || object->type != PHL_OBJECT || !key) {
        phl_free(item);
        return -1;
    }
    for (i = 0; i < object->as.object.count; i++) {
        phl_member *member = &object->as.object.members[i];
        if (member->key->length == key_length && memcmp(member->key->data, key, key_length) == 0) {
            phl_free(member->value);
            member->value = item;
            return 0;
        }
    }
    stored = phl__key_new(key, key_length);
    if (!stored) {
        phl_free(item);
        return -1;
    }
    result = phl__object_append(object, stored, item);
    phl__key_release(stored);
    if (result != 0) phl_free(item);
    return result;
}

int phl_object_set(phl_value *object, const char *key, phl_value *item) {
    if (!key) {
        phl_free(item);
        return -1;
    }
    return phl_object_set_n(object, key, strlen(key), item);
}

void phl_free(phl_value *value) {
    size_t i;
    if (!value) return;
    switch (value->type) {
        case PHL_STRING:
        case PHL_BIGINT:
            PHL_FREE(value->as.string.data);
            break;
        case PHL_LIST:
            for (i = 0; i < value->as.list.count; i++) phl_free(value->as.list.items[i]);
            PHL_FREE(value->as.list.items);
            break;
        case PHL_OBJECT:
            for (i = 0; i < value->as.object.count; i++) {
                phl__key_release(value->as.object.members[i].key);
                phl_free(value->as.object.members[i].value);
            }
            PHL_FREE(value->as.object.members);
            break;
        default:
            break;
    }
    PHL_FREE(value);
}

/* Reading values */

phl_type phl_type_of(const phl_value *value) {
    return value ? value->type : PHL_NIL;
}

int phl_as_bool(const phl_value *value) {
    return value && value->type == PHL_BOOL ? value->as.boolean : 0;
}

long long phl_as_int(const phl_value *value) {
    return value && value->type == PHL_INT ? value->as.integer : 0;
}

double phl_as_float(const phl_value *value) {
    if (!value) return 0.0;
    if (value->type == PHL_FLOAT) return value->as.number;
    if (value->type == PHL_INT) return (double)value->as.integer;
    return 0.0;
}

const char *phl_as_string(const phl_value *value, size_t *length) {
    if (!value || (value->type != PHL_STRING && value->type != PHL_BIGINT)) {
        if (length) *length = 0;
        return NULL;
    }
    if (length) *length = value->as.string.length;
    return value->as.string.data;
}

size_t phl_length(const phl_value *value) {
    if (!value) return 0;
    if (value->type == PHL_LIST) return value->as.list.count;
    if (value->type == PHL_OBJECT) return value->as.object.count;
    return 0;
}

const phl_value *phl_at(const phl_value *list, size_t index) {
    if (!list || list->type != PHL_LIST || index >= list->as.list.count) return NULL;
    return list->as.list.items[index];
}

const phl_value *phl_get(const phl_value *object, const char *key) {
    size_t i, length;
    if (!object || object->type != PHL_OBJECT || !key) return NULL;
    length = strlen(key);
    for (i = 0; i < object->as.object.count; i++) {
        const phl_member *member = &object->as.object.members[i];
        if (member->key->length == length && memcmp(member->key->data, key, length) == 0) {
            return member->value;
        }
    }
    return NULL;
}

const char *phl_key_at(const phl_value *object, size_t index, size_t *length) {
    if (!object || object->type != PHL_OBJECT || index >= object->as.object.count) {
        if (length) *length = 0;
        return NULL;
    }
    if (length) *length = object->as.object.members[index].key->length;
    return object->as.object.members[index].key->data;
}

const phl_value *phl_value_at(const phl_value *object, size_t index) {
    if (!object || object->type != PHL_OBJECT || index >= object->as.object.count) return NULL;
    return object->as.object.members[index].value;
}

/* Errors and UTF-8 */

void phl__set_error(phl_error *error, size_t line, size_t column, const char *format, ...) {
    va_list args;
    size_t length;
    if (!error) return;
    error->line = line;
    error->column = column;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
    /* Do not leave half a character behind if the message was cut short. */
    length = phl__utf8_valid_prefix(error->message, strlen(error->message));
    error->message[length] = '\0';
}

size_t phl__utf8_valid_prefix(const char *text, size_t length) {
    const unsigned char *s = (const unsigned char *)text;
    size_t i = 0;
    while (i < length) {
        unsigned char c = s[i];
        size_t extra, k;
        unsigned char low = 0x80, high = 0xBF;
        if (c < 0x80) {
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
            if (c == 0xE0) low = 0xA0;   /* overlong */
            if (c == 0xED) high = 0x9F;  /* surrogate */
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
            if (c == 0xF0) low = 0x90;   /* overlong */
            if (c == 0xF4) high = 0x8F;  /* above U+10FFFF */
        } else {
            return i;
        }
        if (length - i <= extra) return i;
        if (s[i + 1] < low || s[i + 1] > high) return i;
        for (k = 2; k <= extra; k++) {
            if (s[i + k] < 0x80 || s[i + k] > 0xBF) return i;
        }
        i += extra + 1;
    }
    return i;
}

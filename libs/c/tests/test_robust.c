/* Checks that bad input and failed allocations cause neither crashes nor leaks.
   Built with the library sources and with PHL_MALLOC, PHL_REALLOC and PHL_FREE
   set to the counting functions below; run it under a sanitizer as well.

       test_robust ../../conformance/phl/valid/x.phl ...
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support.h"

static long live = 0;
static long fail_at = -1;
static long allocations = 0;

static int should_fail(void) {
    long current = allocations++;
    return fail_at >= 0 && current == fail_at;
}

void *test_malloc(size_t size) {
    void *p;
    if (should_fail()) return NULL;
    p = malloc(size);
    if (p) live++;
    return p;
}

void *test_realloc(void *old, size_t size) {
    void *p;
    if (should_fail()) return NULL;
    p = realloc(old, size);
    if (p && !old) live++;
    return p;
}

void test_free(void *p) {
    if (p) live--;
    free(p);
}

static unsigned long long state = 0x9E3779B97F4A7C15ULL;

static unsigned random_below(unsigned limit) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return (unsigned)((state >> 16) % limit);
}

/* Parses and, when that works, writes and parses again. */
static void exercise(const char *text, size_t length, unsigned flags, int table) {
    phl_error error;
    phl_value *value = table ? phl_parse_table(text, length, flags, &error) : phl_parse(text, length, flags, &error);
    if (value) {
        size_t written_length;
        char *written = table ? phl_write_table(value, 4, flags, &written_length, &error)
                              : phl_write(value, 4, flags, &written_length, &error);
        if (written) {
            phl_value *again = table ? phl_parse_table(written, written_length, flags, &error)
                                     : phl_parse(written, written_length, flags, &error);
            phl_free(again);
            phl_free_text(written);
        }
        phl_free(value);
    } else if (strlen(error.message) >= sizeof(error.message)) {
        abort();
    }
}

static const char *const fragments[] = {
    "\"", "\\", "\n", ",", ";", ":", "[", "]", "{", "}", "(", ")", "-", ".", "1", "e", "//", "\"true\"",
    "99999999999999999999", "0.5", "\xC3\xA9", "\xFF", "\xEF\xBB\xBF", "a", "\r\n", " ",
};

static void mutate(char *out, size_t *length, size_t capacity) {
    unsigned kind = random_below(3);
    size_t at = *length ? random_below((unsigned)*length + 1) : 0;
    if (kind == 0 && *length) {
        out[random_below((unsigned)*length)] = (char)random_below(256);
    } else if (kind == 1 && at < *length) {
        size_t count = 1 + random_below(8);
        if (count > *length - at) count = *length - at;
        memmove(out + at, out + at + count, *length - at - count);
        *length -= count;
    } else {
        const char *piece = fragments[random_below((unsigned)(sizeof(fragments) / sizeof(fragments[0])))];
        size_t count = strlen(piece);
        if (*length + count < capacity) {
            memmove(out + at + count, out + at, *length - at);
            memcpy(out + at, piece, count);
            *length += count;
        }
    }
}

int main(int argc, char **argv) {
    int i, rounds = 20000, failures = 0;
    const char *env = getenv("PHL_FUZZ_ROUNDS");
    if (env) rounds = atoi(env);

    for (i = 1; i < argc; i++) {
        size_t length, mutated_length;
        char *text = read_file(argv[i], &length);
        int table;
        int round;
        long failing;
        char *buffer;
        size_t capacity;

        if (!text) continue;
        table = strstr(argv[i], ".hpl") != NULL;
        capacity = length + 256;
        buffer = (char *)malloc(capacity);

        /* Every allocation in turn fails, for both kinds of text handling. */
        for (failing = 0;; failing++) {
            long used;
            fail_at = failing;
            allocations = 0;
            exercise(text, length, PHL_DEFAULT, table);
            used = allocations;
            fail_at = -1;
            if (live != 0) {
                printf("LEAK %s when allocation %ld fails: %ld blocks\n", argv[i], failing, live);
                failures++;
                live = 0;
            }
            if (failing >= used) break;
        }

        /* Random changes to the text. */
        for (round = 0; round < rounds / (argc - 1 ? argc - 1 : 1); round++) {
            int changes = 1 + (int)random_below(4), c;
            memcpy(buffer, text, length);
            mutated_length = length;
            for (c = 0; c < changes; c++) mutate(buffer, &mutated_length, capacity);
            exercise(buffer, mutated_length, round % 3 == 0 ? PHL_KEEP_BIG_INTEGERS : PHL_DEFAULT, table);
            exercise(buffer, mutated_length, PHL_NO_COERCE, !table);
            if (live != 0) {
                printf("LEAK %s in round %d: %ld blocks\n", argv[i], round, live);
                failures++;
                live = 0;
            }
        }
        free(buffer);
        free(text);
    }
    printf("%s\n", failures ? "robustness failures" : "robustness checks passed");
    return failures ? 1 : 0;
}

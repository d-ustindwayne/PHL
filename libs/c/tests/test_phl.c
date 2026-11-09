/* Runs the shared conformance cases and checks of the C library. The case files
   are given as arguments:

       test_phl ../../conformance/phl/valid/x.phl ../../conformance/phl/invalid/y.phl ...
*/

#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phl.h"
#include "support.h"

static int passed = 0;
static int failed = 0;

static void check(const char *name, int ok) {
    if (ok) {
        passed++;
    } else {
        failed++;
        printf("FAIL %s\n", name);
    }
}

static int ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

static phl_value *read_json(const char *path) {
    size_t length;
    char *text = read_file(path, &length);
    const char *cursor = text;
    phl_value *value;
    if (!text) return NULL;
    value = json_value(&cursor);
    free(text);
    return value;
}

/* Replaces the extension of path. */
static void with_extension(char *out, size_t size, const char *path, const char *extension) {
    const char *dot = strrchr(path, '.');
    snprintf(out, size, "%.*s%s", (int)(dot - path), path, extension);
}

static void round_trip(const char *name, const phl_value *data, int table) {
    int indents[] = {4, 0};
    size_t i;
    for (i = 0; i < 2; i++) {
        phl_error error;
        size_t length;
        char *text = table ? phl_write_table(data, indents[i], PHL_DEFAULT, &length, &error)
                           : phl_write(data, indents[i], PHL_DEFAULT, &length, &error);
        phl_value *again = NULL;
        if (text) {
            again = table ? phl_parse_table(text, length, PHL_DEFAULT, &error)
                          : phl_parse(text, length, PHL_DEFAULT, &error);
        }
        check(name, again && values_equal(data, again));
        phl_free(again);
        phl_free_text(text);
    }
}

static void run_case(const char *path) {
    int table = ends_with(path, ".hpl");
    int invalid = strstr(path, "invalid") != NULL;
    size_t length;
    char *text = read_file(path, &length);
    phl_error error;
    phl_value *data;

    if (!text) {
        check(path, 0);
        return;
    }
    data = table ? phl_parse_table(text, length, PHL_DEFAULT, &error) : phl_parse(text, length, PHL_DEFAULT, &error);
    if (invalid) {
        check(path, data == NULL && error.message[0] != '\0');
    } else {
        char other[1024];
        phl_value *expected;
        with_extension(other, sizeof(other), path, ".json");
        expected = read_json(other);
        if (!data) printf("%s: %s\n", path, error.message);
        check(path, data && expected && values_equal(data, expected));
        phl_free(expected);

        with_extension(other, sizeof(other), path, ".nocoerce.json");
        expected = read_json(other);
        if (expected) {
            phl_value *raw = phl_parse(text, length, PHL_NO_COERCE, &error);
            check(path, raw && values_equal(raw, expected));
            phl_free(raw);
            phl_free(expected);
        }
        if (data) round_trip(path, data, table);
    }
    phl_free(data);
    free(text);
}

static phl_value *parse_text(const char *text) {
    return phl_parse(text, strlen(text), PHL_DEFAULT, NULL);
}

static int parse_fails(const char *text) {
    phl_value *value = parse_text(text);
    int failed_to_parse = value == NULL;
    phl_free(value);
    return failed_to_parse;
}

static int writes_as(const phl_value *value, const char *expected) {
    char *text = phl_write(value, 4, PHL_DEFAULT, NULL, NULL);
    int same = text && strcmp(text, expected) == 0;
    if (text && !same) printf("wrote: %s\nwanted: %s\n", text, expected);
    phl_free_text(text);
    return same;
}

static void test_types(void) {
    phl_value *doc = parse_text("whole: 42\ndecimal: 34.00\nnegative: -0.5\na: \"True\"\nb: \"NULL\"\n\"true\": 1");
    check("whole number", phl_type_of(phl_get(doc, "whole")) == PHL_INT && phl_as_int(phl_get(doc, "whole")) == 42);
    check("decimal", phl_type_of(phl_get(doc, "decimal")) == PHL_FLOAT && phl_as_float(phl_get(doc, "decimal")) == 34.0);
    check("negative", phl_as_float(phl_get(doc, "negative")) == -0.5);
    check("coerced boolean", phl_type_of(phl_get(doc, "a")) == PHL_BOOL && phl_as_bool(phl_get(doc, "a")));
    check("coerced nil", phl_type_of(phl_get(doc, "b")) == PHL_NIL && phl_get(doc, "b") != NULL);
    check("keys are not coerced", phl_as_int(phl_get(doc, "true")) == 1);
    phl_free(doc);

    check("bare true", parse_fails("a: true"));
    check("bare false", parse_fails("a: false"));
    check("bare nil", parse_fails("a: nil"));

    doc = parse_text("product: (ID, Name) { 101, \"a\"; 102, \"b\"; 103, \"c\"; 104, \"d\"; }");
    check("table index", phl_as_int(phl_get(phl_at(phl_get(doc, "product"), 3), "ID")) == 104);
    check("lookups can be chained", phl_get(phl_at(phl_get(doc, "nothing"), 3), "ID") == NULL);
    phl_free(doc);
}

static void test_errors(void) {
    phl_error error;
    phl_value *doc = phl_parse("a: 1\nb: oops", 12, PHL_DEFAULT, &error);
    check("error position", !doc && error.line == 2 && error.column == 4);

    doc = phl_parse("a: \"\xC3\xA9\xC3\xA9\" oops", 14, PHL_DEFAULT, &error);
    check("columns count characters", !doc && error.line == 1 && error.column == 9);

    doc = phl_parse("\xEF\xBB\xBF" "a: oops", 10, PHL_DEFAULT, &error);
    check("a byte order mark is not a column", !doc && error.column == 4);

    doc = phl_parse("a: \"\xFF\"", 6, PHL_DEFAULT, &error);
    check("invalid UTF-8", !doc && error.line == 1 && error.column == 5);

    doc = phl_parse("a: \"\xED\xA0\x80\"", 8, PHL_DEFAULT, &error);
    check("surrogates are invalid UTF-8", !doc);
    check("error may be NULL", phl_parse("oops", 4, PHL_DEFAULT, NULL) == NULL);
    doc = phl_parse(NULL, 0, PHL_DEFAULT, &error);
    check("NULL with no length is empty", doc && phl_length(doc) == 0);
    phl_free(doc);
}

static void test_numbers(void) {
    phl_error error;
    phl_value *doc = parse_text("max: 9223372036854775807\nmin: -9223372036854775808\nzeros: 007\nnegzero: -0");
    const char *big = "a: 9223372036854775808";
    const char *small = "a: -9223372036854775809";
    check("largest 64-bit number", phl_as_int(phl_get(doc, "max")) == LLONG_MAX);
    check("smallest 64-bit number", phl_as_int(phl_get(doc, "min")) == LLONG_MIN);
    check("leading zeros", phl_as_int(phl_get(doc, "zeros")) == 7);
    check("negative zero whole number", phl_as_int(phl_get(doc, "negzero")) == 0);
    phl_free(doc);

    check("beyond 64 bits", !phl_parse(big, strlen(big), PHL_DEFAULT, &error) && error.line == 1 && error.column == 4);
    check("below 64 bits", !phl_parse(small, strlen(small), PHL_DEFAULT, &error) && error.column == 4);
    check("long key is fine", (doc = parse_text("123456789012345678901234567890: 1")) != NULL);
    phl_free(doc);

    {
        const double numbers[] = {5e-324, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e308, 0.1 + 0.2, 1.0 / 3.0, 123456789012345680.0};
        size_t i;
        for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
            phl_value *back;
            char *written;
            doc = phl_new_object();
            phl_object_set(doc, "a", phl_new_float(numbers[i]));
            written = phl_write(doc, 4, PHL_DEFAULT, NULL, NULL);
            back = written ? parse_text(written) : NULL;
            check("decimals read back the same", back && phl_as_float(phl_get(back, "a")) == numbers[i]);
            phl_free(back);
            phl_free_text(written);
            phl_free(doc);
        }
    }

    doc = phl_parse(big, strlen(big), PHL_KEEP_BIG_INTEGERS, &error);
    check("big integers can be kept", doc && phl_type_of(phl_get(doc, "a")) == PHL_BIGINT &&
                                          strcmp(phl_as_string(phl_get(doc, "a"), NULL), "9223372036854775808") == 0);
    check("big integers are written back", writes_as(doc, "a: 9223372036854775808\n"));
    phl_free(doc);
}

static void test_writer(void) {
    phl_value *doc = phl_new_object();
    phl_value *tags = phl_new_list();
    phl_value *server = phl_new_object();
    phl_value *rows = phl_new_list();
    phl_value *row;
    char *text;

    phl_object_set(doc, "name", phl_new_string("Ann"));
    phl_list_push(tags, phl_new_string("a"));
    phl_list_push(tags, phl_new_string("b"));
    phl_object_set(doc, "tags", tags);
    phl_object_set(server, "Host", phl_new_string("db"));
    phl_object_set(server, "Port", phl_new_int(1341));
    phl_object_set(doc, "server", server);
    row = phl_new_object();
    phl_object_set(row, "ID", phl_new_int(1));
    phl_object_set(row, "Name", phl_new_string("x"));
    phl_list_push(rows, row);
    row = phl_new_object();
    phl_object_set(row, "ID", phl_new_int(2));
    phl_object_set(row, "Name", phl_new_string("y"));
    phl_list_push(rows, row);
    phl_object_set(doc, "rows", rows);
    check("layout", writes_as(doc,
                              "name: \"Ann\"\n"
                              "tags: [\"a\", \"b\"]\n"
                              "server: {\n"
                              "    Host: \"db\",\n"
                              "    Port: 1341\n"
                              "}\n"
                              "rows: (ID, Name) {\n"
                              "    1, \"x\";\n"
                              "    2, \"y\";\n"
                              "}\n"));
    text = phl_write(doc, 0, PHL_DEFAULT, NULL, NULL);
    check("indent 0", text && strcmp(text,
                                     "name: \"Ann\"\n"
                                     "tags: [\"a\", \"b\"]\n"
                                     "server: {Host: \"db\", Port: 1341}\n"
                                     "rows: (ID, Name) { 1, \"x\"; 2, \"y\"; }\n") == 0);
    phl_free_text(text);

    phl_object_set(doc, "name", phl_new_int(5));
    check("setting a key replaces its value", phl_as_int(phl_get(doc, "name")) == 5 && phl_length(doc) == 4);
    check("pushing to a non-list fails", phl_list_push(doc, phl_new_int(1)) == -1);
    check("a NULL item fails", phl_object_set(doc, "x", NULL) == -1);
    phl_free(doc);

    doc = phl_new_object();
    phl_object_set(doc, "a b", phl_new_string("say \"hi\"\n"));
    check("quotes keys and escapes strings", writes_as(doc, "\"a b\": \"say \\\"hi\\\"\\n\"\n"));
    phl_free(doc);

    doc = phl_new_object();
    phl_object_set(doc, "a", phl_new_bool(1));
    phl_object_set(doc, "b", phl_new_bool(0));
    phl_object_set(doc, "c", phl_new_nil());
    check("booleans and nil are strings", writes_as(doc, "a: \"true\"\nb: \"false\"\nc: \"nil\"\n"));
    phl_free(doc);

    doc = phl_new_object();
    phl_object_set(doc, "small", phl_new_float(1e-7));
    phl_object_set(doc, "large", phl_new_float(1e22));
    phl_object_set(doc, "whole", phl_new_float(34.0));
    phl_object_set(doc, "tenth", phl_new_float(0.1));
    phl_object_set(doc, "neg", phl_new_float(-25.99));
    phl_object_set(doc, "zero", phl_new_float(-0.0));
    check("floats have no exponent", writes_as(doc,
                                               "small: 0.0000001\n"
                                               "large: 10000000000000000000000.0\n"
                                               "whole: 34.0\n"
                                               "tenth: 0.1\n"
                                               "neg: -25.99\n"
                                               "zero: -0.0\n"));
    phl_free(doc);

    doc = phl_new_object();
    phl_object_set(doc, "x", phl_new_float(HUGE_VAL));
    check("infinity is refused", phl_write(doc, 4, PHL_DEFAULT, NULL, NULL) == NULL);
    phl_free(doc);

    doc = phl_new_object();
    phl_object_set(doc, "x", phl_new_string("\x01"));
    check("control characters are refused", phl_write(doc, 4, PHL_DEFAULT, NULL, NULL) == NULL);
    phl_free(doc);

    doc = phl_new_object();
    phl_object_set(doc, "x", phl_new_string("\xFF"));
    check("invalid UTF-8 is refused", phl_write(doc, 4, PHL_DEFAULT, NULL, NULL) == NULL);
    phl_free(doc);

    doc = phl_new_list();
    check("a document must be an object", phl_write(doc, 4, PHL_DEFAULT, NULL, NULL) == NULL);
    check("a table must not be empty", phl_write_table(doc, 4, PHL_DEFAULT, NULL, NULL) == NULL);
    phl_free(doc);

    doc = parse_text("o: {}\nl: []");
    check("empty object and list", writes_as(doc, "o: {}\nl: []\n"));
    phl_free(doc);

    doc = phl_parse_table("(ID, \"full name\") { 1, \"Ann\"; }", 31, PHL_DEFAULT, NULL);
    text = phl_write_table(doc, 4, PHL_DEFAULT, NULL, NULL);
    check("HPL table", text && strcmp(text, "(ID, \"full name\") {\n    1, \"Ann\";\n}\n") == 0);
    phl_free_text(text);
    phl_free(doc);
}

static char *nested(int levels, const char *open, const char *close) {
    size_t a = strlen(open), b = strlen(close);
    char *text = (char *)malloc(3 + (a + b) * (size_t)levels + 1);
    char *out = text;
    int i;
    memcpy(out, "a: ", 3);
    out += 3;
    for (i = 0; i < levels; i++, out += a) memcpy(out, open, a);
    for (i = 0; i < levels; i++, out += b) memcpy(out, close, b);
    *out = '\0';
    return text;
}

static void test_depth(void) {
    /* A deep document fails with an error and does not overflow the stack. */
    char *text = nested(PHL_MAX_DEPTH, "[", "]");
    phl_value *doc = parse_text(text);
    char *written;
    check("deepest allowed list", doc != NULL);
    written = phl_write(doc, 4, PHL_DEFAULT, NULL, NULL);
    check("deepest allowed list is written", written != NULL);
    phl_free_text(written);
    phl_free(doc);
    free(text);

    text = nested(PHL_MAX_DEPTH + 1, "[", "]");
    check("too deep list", parse_fails(text));
    free(text);
    text = nested(PHL_MAX_DEPTH + 1, "{a:", "}");
    check("too deep object", parse_fails(text));
    free(text);
    text = nested(PHL_MAX_DEPTH + 1, "(a){", ";}");
    check("too deep table", parse_fails(text));
    free(text);
    text = nested(200000, "[", "");
    check("very deep list", parse_fails(text));
    free(text);

    /* Too deep to write, built by hand. */
    {
        phl_value *value = phl_new_int(1);
        int i;
        for (i = 0; i <= PHL_MAX_DEPTH; i++) {
            phl_value *list = phl_new_list();
            phl_list_push(list, value);
            value = list;
        }
        doc = phl_new_object();
        phl_object_set(doc, "a", value);
        check("too deep to write", phl_write(doc, 4, PHL_DEFAULT, NULL, NULL) == NULL);
        phl_free(doc);
    }
}

static void test_large(void) {
    /* Many keys, which must not take quadratic time to check for duplicates. */
    size_t capacity = 2000000, used = 0;
    char *text = (char *)malloc(capacity);
    int i;
    phl_value *doc;
    for (i = 0; i < 100000; i++) used += (size_t)snprintf(text + used, capacity - used, "k%d: %d\n", i, i);
    doc = phl_parse(text, used, PHL_DEFAULT, NULL);
    check("many keys", doc && phl_length(doc) == 100000);
    phl_free(doc);
    used += (size_t)snprintf(text + used, capacity - used, "k50000: 1\n");
    check("duplicate among many keys", phl_parse(text, used, PHL_DEFAULT, NULL) == NULL);
    free(text);

    /* Strings with NUL bytes keep their length. */
    doc = phl_parse("a: \"x\0y\"", 8, PHL_DEFAULT, NULL);
    {
        size_t length = 0;
        phl_as_string(phl_get(doc, "a"), &length);
        check("NUL inside a string", length == 3);
    }
    phl_free(doc);
}

static void test_locale(void) {
    /* A locale with a decimal comma must change neither reading nor writing. */
    const char *names[] = {"de_DE.UTF-8", "de_DE", "German_Germany", "de-DE", NULL};
    int i;
    for (i = 0; names[i]; i++) {
        if (setlocale(LC_NUMERIC, names[i])) {
            phl_value *doc = parse_text("a: 1.5\nb: 0.25");
            check("reads decimals in a comma locale", doc && phl_as_float(phl_get(doc, "a")) == 1.5);
            check("writes decimals in a comma locale", writes_as(doc, "a: 1.5\nb: 0.25\n"));
            phl_free(doc);
            setlocale(LC_NUMERIC, "C");
            return;
        }
    }
    printf("note: no comma locale available, skipped the locale check\n");
}

int main(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; i++) {
        if (!ends_with(argv[i], ".json")) run_case(argv[i]);
    }
    test_types();
    test_errors();
    test_numbers();
    test_writer();
    test_depth();
    test_large();
    test_locale();
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

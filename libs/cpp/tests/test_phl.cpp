// Runs the shared conformance cases and checks of the C++ library. The folder
// that holds the conformance cases is given as the first argument.

#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "phl.hpp"

namespace fs = std::filesystem;

static int passed = 0;
static int failed = 0;

static void check(const std::string& name, bool ok) {
    if (ok) {
        passed++;
    } else {
        failed++;
        std::cout << "FAIL " << name << "\n";
    }
}

static std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// A small JSON reader for the expected results of the conformance cases.
class json {
public:
    explicit json(std::string text) : text_(std::move(text)) {}

    phl::value read() {
        space();
        const char c = text_[pos_];
        if (c == '{') {
            pos_++;
            phl::object result;
            for (;;) {
                space();
                if (text_[pos_] == '}') {
                    pos_++;
                    return result;
                }
                if (text_[pos_] == ',') {
                    pos_++;
                    continue;
                }
                std::string key = string();
                space();
                pos_++;  // the colon
                result.append(std::move(key), read());
            }
        }
        if (c == '[') {
            pos_++;
            phl::list result;
            for (;;) {
                space();
                if (text_[pos_] == ']') {
                    pos_++;
                    return result;
                }
                if (text_[pos_] == ',') {
                    pos_++;
                    continue;
                }
                result.push_back(read());
            }
        }
        if (c == '"') return string();
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return false;
        }
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return nullptr;
        }
        const std::size_t start = pos_;
        bool decimal = false;
        while (text_[pos_] == '-' || text_[pos_] == '.' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
            if (text_[pos_] == '.') decimal = true;
            pos_++;
        }
        const std::string number = text_.substr(start, pos_ - start);
        if (decimal) return std::stod(number);
        return static_cast<long long>(std::stoll(number));
    }

private:
    void space() {
        while (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t') pos_++;
    }

    std::string string() {
        std::string out;
        pos_++;
        while (text_[pos_] != '"') {
            char c = text_[pos_++];
            if (c == '\\') {
                c = text_[pos_++];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
            }
            out.push_back(c);
        }
        pos_++;
        return out;
    }

    std::string text_;
    std::size_t pos_ = 0;
};

static phl::value read_json(const fs::path& path) {
    return json(read_file(path)).read();
}

static void run_valid(const fs::path& path, bool table) {
    const std::string name = path.filename().string();
    try {
        const std::string text = read_file(path);
        fs::path other = path;
        const phl::value data = table ? phl::parse_table(text) : phl::parse(text);
        check(name, data == read_json(other.replace_extension(".json")));

        other = path;
        other.replace_extension(".nocoerce.json");
        if (fs::exists(other)) {
            const phl::value raw = table ? phl::parse_table(text, false) : phl::parse(text, false);
            check(name + " (coerce off)", raw == read_json(other));
        }

        for (int indent : {4, 0}) {
            const std::string written = table ? phl::stringify_table(data, indent) : phl::stringify(data, indent);
            const phl::value again = table ? phl::parse_table(written) : phl::parse(written);
            check(name + " (round trip)", again == data);
        }
    } catch (const std::exception& e) {
        check(name + ": " + e.what(), false);
    }
}

static void run_invalid(const fs::path& path, bool table) {
    const std::string text = read_file(path);
    bool threw = false;
    try {
        if (table) {
            phl::parse_table(text);
        } else {
            phl::parse(text);
        }
    } catch (const phl::error&) {
        threw = true;
    }
    check(path.filename().string(), threw);
}

static void run_conformance(const fs::path& root) {
    for (const auto& entry : fs::directory_iterator(root / "phl" / "valid")) {
        if (entry.path().extension() == ".phl") run_valid(entry.path(), false);
    }
    for (const auto& entry : fs::directory_iterator(root / "phl" / "invalid")) {
        if (entry.path().extension() == ".phl") run_invalid(entry.path(), false);
    }
    for (const auto& entry : fs::directory_iterator(root / "hpl" / "valid")) {
        if (entry.path().extension() == ".hpl") run_valid(entry.path(), true);
    }
    for (const auto& entry : fs::directory_iterator(root / "hpl" / "invalid")) {
        if (entry.path().extension() == ".hpl") run_invalid(entry.path(), true);
    }
}

static bool fails(const std::string& text) {
    try {
        phl::parse(text);
        return false;
    } catch (const phl::error&) {
        return true;
    }
}

static bool fails_at(std::string_view text, std::size_t line, std::size_t column) {
    try {
        phl::parse(text);
        return false;
    } catch (const phl::error& e) {
        if (e.line() != line || e.column() != column) std::cout << e.what() << "\n";
        return e.line() == line && e.column() == column;
    }
}

static void test_types() {
    const phl::value doc = phl::parse("whole: 42\ndecimal: 34.00\nnegative: -0.5\na: \"True\"\nb: \"NULL\"\n\"true\": 1");
    check("whole number", doc["whole"].is_int() && doc["whole"].as_int() == 42);
    check("decimal", doc["decimal"].is_float() && doc["decimal"].as_float() == 34.0);
    check("negative", doc["negative"].as_float() == -0.5);
    check("whole number as a double", doc["whole"].as_float() == 42.0);
    check("coerced boolean", doc["a"].is_bool() && doc["a"].as_bool());
    check("coerced nil", doc["b"].is_nil());
    check("keys are not coerced", doc["true"].as_int() == 1);
    check("not lower case with other letters", phl::parse("a: \"n\xC4\xB0l\"")["a"].is_string());

    check("bare true", fails("a: true"));
    check("bare false", fails("a: false"));
    check("bare nil", fails("a: nil"));

    const phl::value table = phl::parse("product: (ID, Name) { 101, \"a\"; 102, \"b\"; 103, \"c\"; 104, \"d\"; }");
    check("table index", table["product"][3]["ID"].as_int() == 104);
    check("size", table["product"].size() == 4);

    bool threw = false;
    try {
        (void)doc["whole"].as_string();
    } catch (const phl::error&) {
        threw = true;
    }
    check("wrong type throws", threw);
    threw = false;
    try {
        (void)table["product"][99];
    } catch (const phl::error&) {
        threw = true;
    }
    check("out of range throws", threw);
    threw = false;
    try {
        (void)doc["nothing"];
    } catch (const phl::error&) {
        threw = true;
    }
    check("missing key throws", threw);
}

static void test_errors() {
    check("error position", fails_at("a: 1\nb: oops", 2, 4));
    check("columns count characters", fails_at("a: \"\xC3\xA9\xC3\xA9\" oops", 1, 9));
    check("a byte order mark is not a column", fails_at("\xEF\xBB\xBF" "a: oops", 1, 4));
    check("invalid UTF-8", fails_at("a: \"\xFF\"", 1, 5));
    check("surrogates are invalid UTF-8", fails("a: \"\xED\xA0\x80\""));
    check("string position", fails_at("a: \"x\" \"y\"", 1, 8));
    check("empty text", phl::parse("").size() == 0);

    try {
        phl::parse("a: oops");
    } catch (const phl::error& e) {
        check("message", std::string(e.what()) == "unexpected word 'oops'; strings need double quotes (line 1, column 4)");
    }
}

static void test_numbers() {
    const phl::value doc = phl::parse("max: 9223372036854775807\nmin: -9223372036854775808\nzeros: 007\nnegzero: -0");
    check("largest 64-bit number", doc["max"].as_int() == INT64_MAX);
    check("smallest 64-bit number", doc["min"].as_int() == INT64_MIN);
    check("leading zeros", doc["zeros"].as_int() == 7);
    check("negative zero whole number", doc["negzero"].as_int() == 0);
    check("beyond 64 bits", fails_at("a: 9223372036854775808", 1, 4));
    check("below 64 bits", fails_at("a: -9223372036854775809", 1, 4));
    check("long key is fine", phl::parse("123456789012345678901234567890: 1").size() == 1);
    check("too large decimal", fails_at("a: " + std::string(400, '9') + ".0", 1, 4));
    check("too small decimal is zero", phl::parse("a: 0." + std::string(400, '0') + "1")["a"].as_float() == 0.0);
    check("too small negative decimal is zero", phl::parse("a: -0." + std::string(400, '0') + "1")["a"].as_float() == 0.0);
    check("a small decimal is read", phl::parse("a: 0." + std::string(300, '0') + "1")["a"].as_float() == 1e-301);
    check("the smallest decimal is read", phl::parse("a: 0." + std::string(323, '0') + "5")["a"].as_float() == 5e-324);
    check("the largest decimal is read", phl::parse("a: 179769313486231570000" + std::string(288, '0') + ".0")["a"].as_float() == 1.7976931348623157e308);
    for (double number : {5e-324, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e308, 0.1 + 0.2, 1.0 / 3.0, 123456789012345680.0}) {
        const phl::value written = phl::parse(phl::stringify(phl::object{{"a", number}}));
        check("decimals read back the same", written["a"].as_float() == number);
    }
}

static void test_writer() {
    phl::object server;
    server["Host"] = "db";
    server["Port"] = 1341;
    phl::object first;
    first["ID"] = 1;
    first["Name"] = "x";
    phl::object second;
    second["ID"] = 2;
    second["Name"] = "y";
    phl::object doc;
    doc["name"] = "Ann";
    doc["tags"] = phl::list{"a", "b"};
    doc["server"] = server;
    doc["rows"] = phl::list{first, second};

    check("layout", phl::stringify(doc) ==
                        "name: \"Ann\"\n"
                        "tags: [\"a\", \"b\"]\n"
                        "server: {\n"
                        "    Host: \"db\",\n"
                        "    Port: 1341\n"
                        "}\n"
                        "rows: (ID, Name) {\n"
                        "    1, \"x\";\n"
                        "    2, \"y\";\n"
                        "}\n");
    check("indent 0", phl::stringify(doc, 0) ==
                          "name: \"Ann\"\n"
                          "tags: [\"a\", \"b\"]\n"
                          "server: {Host: \"db\", Port: 1341}\n"
                          "rows: (ID, Name) { 1, \"x\"; 2, \"y\"; }\n");

    phl::object strings;
    strings["a b"] = "say \"hi\"\n";
    check("quotes keys and escapes strings", phl::stringify(strings) == "\"a b\": \"say \\\"hi\\\"\\n\"\n");

    phl::object scalars;
    scalars["a"] = true;
    scalars["b"] = false;
    scalars["c"] = nullptr;
    check("booleans and nil are strings", phl::stringify(scalars) == "a: \"true\"\nb: \"false\"\nc: \"nil\"\n");

    phl::object numbers;
    numbers["small"] = 1e-7;
    numbers["large"] = 1e22;
    numbers["whole"] = 34.0;
    numbers["tenth"] = 0.1;
    numbers["neg"] = -25.99;
    numbers["zero"] = -0.0;
    check("floats have no exponent", phl::stringify(numbers) ==
                                         "small: 0.0000001\n"
                                         "large: 10000000000000000000000.0\n"
                                         "whole: 34.0\n"
                                         "tenth: 0.1\n"
                                         "neg: -25.99\n"
                                         "zero: -0.0\n");

    const auto refuses = [](const phl::value& v) {
        try {
            phl::stringify(v);
            return false;
        } catch (const phl::error&) {
            return true;
        }
    };
    check("nan is refused", refuses(phl::object{{"x", std::nan("")}}));
    check("infinity is refused", refuses(phl::object{{"x", HUGE_VAL}}));
    check("control characters are refused", refuses(phl::object{{"x", "\x01"}}));
    check("invalid UTF-8 is refused", refuses(phl::object{{"x", "\xFF"}}));
    check("a document must be an object", refuses(phl::list{}));

    bool threw = false;
    try {
        phl::stringify_table(phl::list{});
    } catch (const phl::error&) {
        threw = true;
    }
    check("a table must not be empty", threw);
    threw = false;
    try {
        phl::value too_big(UINT64_MAX);
    } catch (const phl::error&) {
        threw = true;
    }
    check("unsigned numbers beyond 64 bits are refused", threw);

    check("empty object and list", phl::stringify(phl::parse("o: {}\nl: []")) == "o: {}\nl: []\n");
    check("HPL table", phl::stringify_table(phl::parse_table("(ID, \"full name\") { 1, \"Ann\"; }")) ==
                           "(ID, \"full name\") {\n    1, \"Ann\";\n}\n");
    check("different keys are not a table",
          phl::stringify(phl::parse("x: [{a: 1}, {b: 2}]")) == "x: [\n    {\n        a: 1\n    },\n    {\n        b: 2\n    }\n]\n");
    check("key order matters for a table", phl::parse("x: [{a: 1, b: 2}, {b: 3, a: 4}]") != phl::parse("x: [{a: 1, b: 2}, {a: 4, b: 3}]"));
}

static void test_depth() {
    const auto nested = [](int levels, const std::string& open, const std::string& close) {
        std::string text = "a: ";
        for (int i = 0; i < levels; i++) text += open;
        for (int i = 0; i < levels; i++) text += close;
        return text;
    };
    const phl::value deep = phl::parse(nested(phl::max_depth, "[", "]"));
    check("deepest allowed list is written", !phl::stringify(deep).empty());
    check("too deep list", fails(nested(phl::max_depth + 1, "[", "]")));
    check("too deep object", fails(nested(phl::max_depth + 1, "{a:", "}")));
    check("too deep table", fails(nested(phl::max_depth + 1, "(a){", ";}")));
    check("very deep list", fails(nested(200000, "[", "")));

    phl::value value = 1;
    for (int i = 0; i <= phl::max_depth; i++) value = phl::list{value};
    bool threw = false;
    try {
        phl::stringify(phl::object{{"a", value}});
    } catch (const phl::error&) {
        threw = true;
    }
    check("too deep to write", threw);
}

static void test_locale() {
    // A locale with a decimal comma must change neither reading nor writing.
    for (const char* name : {"de_DE.UTF-8", "de_DE", "German_Germany", "de-DE"}) {
        if (std::setlocale(LC_NUMERIC, name)) {
            const phl::value doc = phl::parse("a: 1.5\nb: 0.25");
            check("reads decimals in a comma locale", doc["a"].as_float() == 1.5);
            check("writes decimals in a comma locale", phl::stringify(doc) == "a: 1.5\nb: 0.25\n");
            std::setlocale(LC_NUMERIC, "C");
            return;
        }
    }
    std::cout << "note: no comma locale available, skipped the locale check\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "usage: test_phl <conformance folder>\n";
        return 2;
    }
    run_conformance(argv[1]);
    test_types();
    test_errors();
    test_numbers();
    test_writer();
    test_depth();
    test_locale();
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}

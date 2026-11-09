// phl.hpp -- read and write PHL documents (.phl, .pll) and HPL tables
// (.hpl, .hpll). C++17, header only, no dependencies.
//
//     phl::value doc = phl::parse(text);
//     std::cout << doc["product"][2]["Name"].as_string() << "\n";
//     std::cout << phl::stringify(doc);
//
// Errors throw phl::error, which has line() and column().

#ifndef PHL_HPP
#define PHL_HPP

#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// Older standard libraries do not convert floating point with <charconv>.
#ifndef PHL_FLOAT_CHARCONV
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define PHL_FLOAT_CHARCONV 1
#else
#define PHL_FLOAT_CHARCONV 0
#endif
#endif

// Error messages are built outside the functions that recurse, so that their
// temporary strings do not take up stack space at every level of nesting.
#if defined(__GNUC__) || defined(__clang__)
#define PHL_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define PHL_NOINLINE __declspec(noinline)
#else
#define PHL_NOINLINE
#endif

namespace phl {

/// Lists, objects and tables nested deeper than this are an error.
inline constexpr int max_depth = 256;

/// Thrown for text that is not valid PHL or HPL, for values that cannot be
/// written, and for using a value as the wrong type. line() and column() are
/// 1-based and 0 when the error has no position. Columns count Unicode code
/// points.
class error : public std::runtime_error {
public:
    explicit error(const std::string& message, std::size_t line = 0, std::size_t column = 0)
        : std::runtime_error(line ? message + " (line " + std::to_string(line) + ", column " + std::to_string(column) + ")"
                                  : message),
          line_(line),
          column_(column) {}

    std::size_t line() const noexcept { return line_; }
    std::size_t column() const noexcept { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

class value;

/// One `key: value` entry of an object.
struct member;

/// A list is a std::vector of values.
using list = std::vector<value>;

/// An object. Keys keep the order they were read or added in, and find()
/// searches them in that order.
class object {
public:
    object() = default;
    object(std::initializer_list<member> entries);

    std::size_t size() const noexcept { return members_.size(); }
    bool empty() const noexcept { return members_.empty(); }
    auto begin() const noexcept { return members_.begin(); }
    auto end() const noexcept { return members_.end(); }
    auto begin() noexcept { return members_.begin(); }
    auto end() noexcept { return members_.end(); }

    const value* find(std::string_view key) const noexcept;
    value* find(std::string_view key) noexcept;
    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    /// The value of a key. The const version throws phl::error when it is
    /// missing; the other version adds a nil value.
    const value& at(std::string_view key) const;
    value& operator[](std::string_view key);

    /// Adds an entry without looking for an existing one with the same key.
    void append(std::string key, value item);

    void reserve(std::size_t count) { members_.reserve(count); }

private:
    std::vector<member> members_;
};

/// A value read from or written to PHL: nil, a boolean, a 64-bit whole number,
/// a double, a string, a list or an object.
class value {
public:
    enum class type { nil, boolean, integer, decimal, string, list_value, object_value };

    value() noexcept : data_(nullptr) {}
    value(std::nullptr_t) noexcept : data_(nullptr) {}
    value(bool v) noexcept : data_(v) {}
    value(double v) noexcept : data_(v) {}
    value(float v) noexcept : data_(static_cast<double>(v)) {}
    value(const char* v) : data_(std::string(v)) {}
    value(std::string v) noexcept : data_(std::move(v)) {}
    value(std::string_view v) : data_(std::string(v)) {}
    value(phl::list items) noexcept : data_(std::move(items)) {}
    value(phl::object entries) noexcept : data_(std::move(entries)) {}

    /// Any other whole number type. Numbers beyond 64 bits are refused.
    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    value(T v) {
        if constexpr (std::is_unsigned_v<T> && sizeof(T) >= sizeof(std::int64_t)) {
            if (v > static_cast<T>(std::numeric_limits<std::int64_t>::max())) {
                throw error("the number " + std::to_string(v) + " does not fit in 64 bits");
            }
        }
        data_ = static_cast<std::int64_t>(v);
    }

    type kind() const noexcept { return static_cast<type>(data_.index()); }
    bool is_nil() const noexcept { return kind() == type::nil; }
    bool is_bool() const noexcept { return kind() == type::boolean; }
    bool is_int() const noexcept { return kind() == type::integer; }
    bool is_float() const noexcept { return kind() == type::decimal; }
    /// True for whole numbers and decimals.
    bool is_number() const noexcept { return is_int() || is_float(); }
    bool is_string() const noexcept { return kind() == type::string; }
    bool is_list() const noexcept { return kind() == type::list_value; }
    bool is_object() const noexcept { return kind() == type::object_value; }

    // These throw phl::error for a value of another type.
    bool as_bool() const { return get<bool>("a boolean"); }
    std::int64_t as_int() const { return get<std::int64_t>("a whole number"); }
    /// The number as a double. Also accepts whole numbers.
    double as_float() const {
        if (is_int()) return static_cast<double>(std::get<std::int64_t>(data_));
        return get<double>("a number");
    }
    const std::string& as_string() const { return get<std::string>("a string"); }
    const phl::list& as_list() const { return get<phl::list>("a list"); }
    phl::list& as_list() { return get_mutable<phl::list>("a list"); }
    const phl::object& as_object() const { return get<phl::object>("an object"); }
    phl::object& as_object() { return get_mutable<phl::object>("an object"); }

    /// An item of a list, counted from 0. Throws phl::error when out of range.
    const value& operator[](std::size_t index) const {
        const phl::list& items = as_list();
        if (index >= items.size()) {
            throw error("index " + std::to_string(index) + " is out of range for a list of " + std::to_string(items.size()));
        }
        return items[index];
    }
    /// Same as above, for int literals: v[2] would be ambiguous otherwise.
    const value& operator[](int index) const {
        if (index < 0) throw error("a list index cannot be negative");
        return (*this)[static_cast<std::size_t>(index)];
    }
    /// An entry of an object by key. Throws phl::error when it is missing.
    const value& operator[](std::string_view key) const { return as_object().at(key); }
    const value& operator[](const char* key) const { return as_object().at(key); }

    /// The number of items in a list or entries in an object, otherwise 0.
    std::size_t size() const noexcept {
        if (is_list()) return std::get<phl::list>(data_).size();
        if (is_object()) return std::get<phl::object>(data_).size();
        return 0;
    }

    /// Equal values of the same type. Objects also need the same key order.
    friend bool operator==(const value& a, const value& b);
    friend bool operator!=(const value& a, const value& b) { return !(a == b); }

private:
    template <typename T>
    const T& get(const char* wanted) const {
        const T* found = std::get_if<T>(&data_);
        if (!found) throw error(std::string("this value is not ") + wanted);
        return *found;
    }
    template <typename T>
    T& get_mutable(const char* wanted) {
        T* found = std::get_if<T>(&data_);
        if (!found) throw error(std::string("this value is not ") + wanted);
        return *found;
    }

    // The order matches enum class type.
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, phl::list, phl::object> data_;
};

struct member {
    std::string key;
    value item;
};

inline object::object(std::initializer_list<member> entries) : members_(entries) {}

inline const value* object::find(std::string_view key) const noexcept {
    for (const member& m : members_) {
        if (m.key == key) return &m.item;
    }
    return nullptr;
}

inline value* object::find(std::string_view key) noexcept {
    for (member& m : members_) {
        if (m.key == key) return &m.item;
    }
    return nullptr;
}

inline const value& object::at(std::string_view key) const {
    const value* found = find(key);
    if (!found) throw error("no entry named '" + std::string(key) + "'");
    return *found;
}

inline value& object::operator[](std::string_view key) {
    if (value* found = find(key)) return *found;
    members_.push_back(member{std::string(key), value()});
    return members_.back().item;
}

inline void object::append(std::string key, value item) {
    members_.push_back(member{std::move(key), std::move(item)});
}

inline bool operator==(const object& a, const object& b) {
    if (a.size() != b.size()) return false;
    auto x = a.begin();
    auto y = b.begin();
    for (; x != a.end(); ++x, ++y) {
        if (x->key != y->key || !(x->item == y->item)) return false;
    }
    return true;
}

inline bool operator==(const value& a, const value& b) {
    return a.data_ == b.data_;
}

namespace detail {

enum class token_kind { string, integer, decimal, word, punct, newline, end };

// Tokens point into the source text. Strings are decoded when they are used.
struct token {
    token_kind kind;
    char punct;
    std::size_t start;
    std::size_t end;
    std::size_t line;
    std::size_t line_start;
};

inline bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

inline bool is_word_char(char c) noexcept {
    return is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

inline bool all_digits(std::string_view text) noexcept {
    if (text.empty()) return false;
    for (char c : text) {
        if (!is_digit(c)) return false;
    }
    return true;
}

inline bool equals_ignore_case(std::string_view text, std::string_view lower) noexcept {
    if (text.size() != lower.size()) return false;
    for (std::size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != lower[i]) return false;
    }
    return true;
}

// The length of the valid UTF-8 at the start of text.
inline std::size_t utf8_valid_prefix(std::string_view text) noexcept {
    std::size_t i = 0;
    const std::size_t length = text.size();
    while (i < length) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        unsigned char low = 0x80, high = 0xBF;
        if (c < 0x80) {
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
            if (c == 0xE0) low = 0xA0;   // overlong
            if (c == 0xED) high = 0x9F;  // surrogate
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
            if (c == 0xF0) low = 0x90;   // overlong
            if (c == 0xF4) high = 0x8F;  // above U+10FFFF
        } else {
            return i;
        }
        if (length - i <= extra) return i;
        const auto first = static_cast<unsigned char>(text[i + 1]);
        if (first < low || first > high) return i;
        for (std::size_t k = 2; k <= extra; k++) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if (next < 0x80 || next > 0xBF) return i;
        }
        i += extra + 1;
    }
    return i;
}

inline double parse_decimal(std::string_view text, bool& ok) {
    double number = 0.0;
#if PHL_FLOAT_CHARCONV
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, number);
    ok = result.ec == std::errc() && std::isfinite(number);
    if (result.ec == std::errc::result_out_of_range) {
        // Too small rounds to zero; too large is an error.
        const auto point = text.find('.');
        ok = all_digits(text.substr(text[0] == '-' ? 1 : 0, point - (text[0] == '-' ? 1 : 0))) &&
             text.substr(text[0] == '-' ? 1 : 0, point - (text[0] == '-' ? 1 : 0)).find_first_not_of('0') == std::string_view::npos;
        number = 0.0;
        if (ok && text[0] == '-') number = -0.0;
    }
#else
    // strtod expects the decimal point of the current locale.
    std::string copy(text);
    const std::lconv* conventions = std::localeconv();
    if (conventions && conventions->decimal_point && *conventions->decimal_point) {
        copy.replace(copy.find('.'), 1, conventions->decimal_point);
    }
    number = std::strtod(copy.c_str(), nullptr);
    ok = std::isfinite(number);
#endif
    return number;
}

class parser {
public:
    parser(std::string_view text, bool coerce) : text_(text), coerce_(coerce) {
        const std::size_t valid = utf8_valid_prefix(text);
        if (valid != text.size()) {
            std::size_t line = 1, line_start = 0;
            for (std::size_t i = 0; i < valid; i++) {
                if (text[i] == '\n') {
                    line++;
                    line_start = i + 1;
                }
            }
            throw error("the text is not valid UTF-8", line, column_of(line_start, valid));
        }
        tokenize();
    }

    value document() {
        object result;
        std::unordered_set<std::string> seen;
        skip_newlines();
        while (peek().kind != token_kind::end) {
            entry(result, seen);
            const token& t = peek();
            if (is_punct(t, ',')) {
                next();
                skip_newlines();
            } else if (t.kind == token_kind::newline) {
                skip_newlines();
            } else if (t.kind != token_kind::end) {
                fail_found("expected a new line or ',' between entries, found ", t);
            }
        }
        return value(std::move(result));
    }

    value table_document() {
        skip_newlines();
        if (!is_punct(peek(), '(')) {
            fail_found("expected '(' to start the column list, found ", peek());
        }
        value rows = table();
        skip_newlines();
        if (peek().kind != token_kind::end) {
            fail(std::string("unexpected ") + describe(peek()) + " after the table", peek());
        }
        return rows;
    }

private:
    // Columns count code points, so continuation bytes are skipped.
    std::size_t column_of(std::size_t line_start, std::size_t offset) const noexcept {
        std::size_t column = 1;
        for (std::size_t i = line_start; i < offset; i++) {
            if ((static_cast<unsigned char>(text_[i]) & 0xC0) != 0x80) column++;
        }
        return column;
    }

    void add(token_kind kind, std::size_t start, std::size_t end, std::size_t line, std::size_t line_start) {
        tokens_.push_back(token{kind, kind == token_kind::punct ? text_[start] : '\0', start, end, line, line_start});
    }

    void tokenize() {
        std::size_t i = 0, line = 1, line_start = 0;
        const std::size_t length = text_.size();

        if (text_.substr(0, 3) == "\xEF\xBB\xBF") {
            i = 3;
            line_start = 3;
        }
        while (i < length) {
            const char c = text_[i];
            if (c == '\n') {
                add(token_kind::newline, i, i + 1, line, line_start);
                i++;
                line++;
                line_start = i;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                i++;
            } else if (c == '/' && i + 1 < length && text_[i + 1] == '/') {
                const std::size_t end = text_.find('\n', i);
                i = end == std::string_view::npos ? length : end;
            } else if (c == '"') {
                read_string(i, line, line_start);
            } else if (c == '-' || is_digit(c)) {
                read_number_or_word(i, line, line_start);
            } else if (is_word_char(c)) {
                const std::size_t start = i;
                while (i < length && is_word_char(text_[i])) i++;
                add(token_kind::word, start, i, line, line_start);
            } else if (c != '\0' && std::strchr(":,;[]{}()", c)) {
                add(token_kind::punct, i, i + 1, line, line_start);
                i++;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                throw error("unexpected control character " + std::to_string(static_cast<int>(c)), line, column_of(line_start, i));
            } else {
                std::size_t width = 1;
                while (i + width < length && (static_cast<unsigned char>(text_[i + width]) & 0xC0) == 0x80) width++;
                throw error("unexpected character '" + std::string(text_.substr(i, width)) + "'", line, column_of(line_start, i));
            }
        }
        add(token_kind::end, length, length, line, line_start);
    }

    void read_string(std::size_t& index, std::size_t line, std::size_t line_start) {
        const std::size_t start = index;
        std::size_t i = start + 1;
        const std::size_t length = text_.size();
        for (;;) {
            if (i >= length || text_[i] == '\n') {
                throw error("unterminated string", line, column_of(line_start, start));
            }
            const char c = text_[i];
            if (c == '"') {
                add(token_kind::string, start + 1, i, line, line_start);
                index = i + 1;
                return;
            }
            if (c == '\\') {
                const char e = i + 1 < length ? text_[i + 1] : '\0';
                if (i + 1 >= length || (e != 'n' && e != 't' && e != '"' && e != '\\')) {
                    std::size_t width = 0;
                    if (i + 1 < length && e != '\n') {
                        width = 1;
                        while (i + 1 + width < length && (static_cast<unsigned char>(text_[i + 1 + width]) & 0xC0) == 0x80) width++;
                    }
                    throw error("invalid escape sequence '\\" + std::string(text_.substr(i + 1, width)) + "'", line,
                                column_of(line_start, i));
                }
                i += 2;
            } else {
                i++;
            }
        }
    }

    void read_number_or_word(std::size_t& index, std::size_t line, std::size_t line_start) {
        const std::size_t start = index;
        const std::size_t length = text_.size();
        std::size_t i = start;
        const bool negative = text_[i] == '-';
        if (negative) {
            i++;
            if (i >= length || !is_digit(text_[i])) {
                throw error("expected a digit after '-'", line, column_of(line_start, start));
            }
        }
        while (i < length && is_word_char(text_[i])) i++;
        const std::string_view head = text_.substr(start, i - start);

        if (!all_digits(negative ? head.substr(1) : head)) {
            if (negative) throw error("invalid number '" + shorten(head) + "'", line, column_of(line_start, start));
            // Keys may start with a digit, for example 2fa_enabled.
            add(token_kind::word, start, i, line, line_start);
            index = i;
            return;
        }
        if (i + 1 < length && text_[i] == '.' && is_digit(text_[i + 1])) {
            i++;
            while (i < length && is_digit(text_[i])) i++;
            if (i < length && is_word_char(text_[i])) {
                throw error("invalid number '" + shorten(text_.substr(start, i + 1 - start)) + "'", line, column_of(line_start, start));
            }
            add(token_kind::decimal, start, i, line, line_start);
            index = i;
            return;
        }
        // A plain run of digits can be a key or a number; the parser decides.
        add(negative ? token_kind::integer : token_kind::word, start, i, line, line_start);
        index = i;
    }

    static std::string shorten(std::string_view text) {
        return std::string(text.substr(0, 64));
    }

    const token& peek(std::size_t offset = 0) const noexcept {
        const std::size_t last = tokens_.size() - 1;
        const std::size_t i = pos_ + offset;
        return tokens_[i < last ? i : last];
    }

    const token& next() noexcept {
        const token& t = tokens_[pos_];
        if (t.kind != token_kind::end) pos_++;
        return t;
    }

    void skip_newlines() noexcept {
        while (peek().kind == token_kind::newline) pos_++;
    }

    static bool is_punct(const token& t, char c) noexcept {
        return t.kind == token_kind::punct && t.punct == c;
    }

    static bool is_key_like(const token& t) noexcept {
        return t.kind == token_kind::word || t.kind == token_kind::string;
    }

    std::string describe(const token& t) const {
        if (t.kind == token_kind::end) return "end of input";
        if (t.kind == token_kind::newline) return "end of line";
        if (t.kind == token_kind::string) return "string";
        return "'" + shorten(text_.substr(t.start, t.end - t.start)) + "'";
    }

    [[noreturn]] PHL_NOINLINE void fail(std::string_view message, const token& t) const {
        const std::size_t at = t.start - (t.kind == token_kind::string ? 1 : 0);
        throw error(std::string(message), t.line, column_of(t.line_start, at));
    }

    // "<before> <the token that was found>"
    [[noreturn]] PHL_NOINLINE void fail_found(std::string_view before, const token& t) const {
        fail(std::string(before) + describe(t), t);
    }

    // "<before><name, cut short><after>"
    [[noreturn]] PHL_NOINLINE void fail_named(std::string_view before, std::string_view name, std::string_view after, const token& t) const {
        fail(std::string(before) + shorten(name) + std::string(after), t);
    }

    [[noreturn]] PHL_NOINLINE void fail_too_deep(const token& t) const {
        fail("nesting is deeper than the limit of " + std::to_string(max_depth) + " levels", t);
    }

    [[noreturn]] PHL_NOINLINE void fail_row_size(std::size_t cells, std::size_t columns, const token& t) const {
        fail("row has " + std::to_string(cells) + " values but the table has " + std::to_string(columns) + " columns", t);
    }

    [[noreturn]] PHL_NOINLINE void fail_no_colon(std::string_view key, const token& t) const {
        fail("expected ':' after key '" + shorten(key) + "', found " + describe(t), t);
    }

    [[noreturn]] PHL_NOINLINE void fail_separator(const char* kind, char closer, const token& t) const {
        fail(std::string("expected ',' or '") + closer + "' in " + kind + ", found " + describe(t), t);
    }

    [[noreturn]] PHL_NOINLINE void fail_too_big(std::string_view text, const token& t) const {
        fail("the number " + std::string(text.substr(0, 40)) + (text.size() > 40 ? "..." : "") + " does not fit in 64 bits", t);
    }

    bool is_key_ahead() const noexcept {
        if (!is_key_like(peek())) return false;
        std::size_t offset = 1;
        while (peek(offset).kind == token_kind::newline) offset++;
        return is_punct(peek(offset), ':');
    }

    std::string decode(const token& t) const {
        const std::string_view raw = text_.substr(t.start, t.end - t.start);
        if (t.kind != token_kind::string || raw.find('\\') == std::string_view::npos) return std::string(raw);
        std::string out;
        out.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); i++) {
            char c = raw[i];
            if (c == '\\') {
                c = raw[++i];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
            }
            out.push_back(c);
        }
        return out;
    }

    // Counts one level of nesting for as long as it lives.
    class nesting {
    public:
        nesting(parser& owner, const token& open) : owner_(owner) {
            if (owner_.depth_ >= max_depth) {
                owner_.fail_too_deep(open);
            }
            owner_.depth_++;
        }
        ~nesting() { owner_.depth_--; }
        nesting(const nesting&) = delete;
        nesting& operator=(const nesting&) = delete;

    private:
        parser& owner_;
    };

    void entry(object& target, std::unordered_set<std::string>& seen) {
        const token& key_token = next();
        if (!is_key_like(key_token)) fail_found("expected a key, found ", key_token);
        std::string key = decode(key_token);
        skip_newlines();
        const token& colon = next();
        if (!is_punct(colon, ':')) {
            fail_no_colon(key, colon);
        }
        skip_newlines();
        if (!seen.insert(key).second) fail_named("duplicate key '", key, "'", key_token);
        target.append(std::move(key), parse_value());
    }

    value string_value(const token& t) {
        std::string text = decode(t);
        if (coerce_) {
            if (equals_ignore_case(text, "true")) return value(true);
            if (equals_ignore_case(text, "false")) return value(false);
            if (equals_ignore_case(text, "nil") || equals_ignore_case(text, "null") || equals_ignore_case(text, "none")) {
                return value();
            }
        }
        return value(std::move(text));
    }

    value integer_value(const token& t) const {
        const std::string_view text = text_.substr(t.start, t.end - t.start);
        const bool negative = text[0] == '-';
        // The largest magnitude allowed: 2^63 - 1, or 2^63 for negative numbers.
        const std::uint64_t limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negative ? 1u : 0u);
        std::uint64_t magnitude = 0;
        for (std::size_t i = negative ? 1 : 0; i < text.size(); i++) {
            const auto digit = static_cast<std::uint64_t>(text[i] - '0');
            if (magnitude > (limit - digit) / 10) {
                fail_too_big(text, t);
            }
            magnitude = magnitude * 10 + digit;
        }
        if (negative) {
            // Written this way so that -2^63 does not overflow.
            return value(magnitude == 0 ? std::int64_t{0} : -static_cast<std::int64_t>(magnitude - 1) - 1);
        }
        return value(static_cast<std::int64_t>(magnitude));
    }

    value decimal_value(const token& t) const {
        bool ok = false;
        const double number = parse_decimal(text_.substr(t.start, t.end - t.start), ok);
        if (!ok) fail("this number is too large for a 64-bit decimal", t);
        return value(number);
    }

    value parse_value() {
        const token& t = peek();
        switch (t.kind) {
            case token_kind::string:
                next();
                return string_value(t);
            case token_kind::integer:
                next();
                return integer_value(t);
            case token_kind::decimal:
                next();
                return decimal_value(t);
            case token_kind::word:
                next();
                if (all_digits(text_.substr(t.start, t.end - t.start))) return integer_value(t);
                fail_named("unexpected word '", text_.substr(t.start, t.end - t.start), "'; strings need double quotes", t);
            case token_kind::punct:
                if (t.punct == '{') return parse_object();
                if (t.punct == '[') return parse_list();
                if (t.punct == '(') return table();
                break;
            default:
                break;
        }
        fail_found("expected a value, found ", t);
    }

    // Reads ',' (gives true) or the closing bracket (gives false).
    bool item_separator(const char* kind, char closer) {
        skip_newlines();
        const token& t = next();
        if (is_punct(t, ',')) return true;
        if (is_punct(t, closer)) return false;
        fail_separator(kind, closer, t);
    }

    value parse_object() {
        nesting level(*this, next());
        object result;
        std::unordered_set<std::string> seen;
        for (;;) {
            skip_newlines();
            if (is_punct(peek(), '}')) {
                next();
                break;
            }
            if (!is_key_ahead()) fail("expected 'key: value' inside an object", peek());
            entry(result, seen);
            if (!item_separator("object", '}')) break;
        }
        return value(std::move(result));
    }

    value parse_list() {
        nesting level(*this, next());
        phl::list result;
        for (;;) {
            skip_newlines();
            if (is_punct(peek(), ']')) {
                next();
                break;
            }
            if (is_key_ahead()) fail("a list cannot contain 'key: value' entries; use { } for an object", peek());
            result.push_back(parse_value());
            if (!item_separator("list", ']')) break;
        }
        return value(std::move(result));
    }

    // (col, col) { value, value; value, value; }
    value table() {
        nesting level(*this, next());
        std::vector<std::string> columns;
        std::unordered_set<std::string> seen;
        for (;;) {
            skip_newlines();
            if (is_punct(peek(), ')') && !columns.empty()) {
                next();
                break;
            }
            const token& name = next();
            if (!is_key_like(name)) fail_found("expected a column name, found ", name);
            std::string column = decode(name);
            if (!seen.insert(column).second) fail_named("duplicate column '", column, "'", name);
            columns.push_back(std::move(column));
            skip_newlines();
            const token& t = next();
            if (is_punct(t, ')')) break;
            if (!is_punct(t, ',')) fail_found("expected ',' or ')' in column list, found ", t);
        }

        skip_newlines();
        const token& open = next();
        if (!is_punct(open, '{')) fail_found("expected '{' to start the table rows, found ", open);

        phl::list rows;
        std::vector<value> cells;
        for (;;) {
            skip_newlines();
            if (is_punct(peek(), '}')) {
                next();
                break;
            }
            const token& row_token = peek();
            cells.clear();
            for (;;) {
                skip_newlines();
                cells.push_back(parse_value());
                skip_newlines();
                const token& t = next();
                if (is_punct(t, ';')) break;
                if (!is_punct(t, ',')) fail_found("expected ',' or ';' in table row, found ", t);
            }
            if (cells.size() != columns.size()) {
                fail_row_size(cells.size(), columns.size(), row_token);
            }
            object row;
            row.reserve(columns.size());
            for (std::size_t i = 0; i < columns.size(); i++) row.append(columns[i], std::move(cells[i]));
            rows.emplace_back(std::move(row));
        }
        return value(std::move(rows));
    }

    std::string_view text_;
    bool coerce_;
    std::vector<token> tokens_;
    std::size_t pos_ = 0;
    int depth_ = 0;
};

// The shortest digits that read back as the same number, and the exponent of
// the first digit.
inline void shortest_digits(double positive, std::string& digits, int& exponent) {
    char buffer[64];
#if PHL_FLOAT_CHARCONV
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), positive, std::chars_format::scientific);
    const std::string_view text(buffer, static_cast<std::size_t>(result.ptr - buffer));
#else
    // Both directions use the current locale, so its decimal point does not matter.
    for (int precision = 0; precision <= 16; precision++) {
        std::snprintf(buffer, sizeof(buffer), "%.*e", precision, positive);
        if (std::strtod(buffer, nullptr) == positive) break;
    }
    const std::string_view text(buffer);
#endif
    const std::size_t e = text.find('e');
    digits.clear();
    for (char c : text.substr(0, e)) {
        if (is_digit(c)) digits.push_back(c);
    }
    exponent = std::atoi(std::string(text.substr(e + 1)).c_str());
}

class writer {
public:
    explicit writer(int indent) : indent_(indent > 0 ? indent : 0) {}

    std::string document(const value& doc) {
        if (!doc.is_object()) throw error("a PHL document must be an object of top-level entries");
        for (const member& m : doc.as_object()) {
            key(m.key);
            out_ += ": ";
            write(m.item, 0, false);
            out_ += '\n';
        }
        return std::move(out_);
    }

    std::string table_document(const value& rows) {
        if (!is_table(rows)) throw error("an HPL table must be a non-empty list of objects that share the same keys");
        nesting_ = 1;
        table(rows.as_list(), 0, false);
        out_ += '\n';
        return std::move(out_);
    }

private:
    void pad(int depth) {
        out_.append(static_cast<std::size_t>(indent_) * static_cast<std::size_t>(depth), ' ');
    }

    void string(std::string_view text) {
        if (utf8_valid_prefix(text) != text.size()) throw error("strings must be valid UTF-8");
        out_ += '"';
        std::size_t start = 0;
        for (std::size_t i = 0; i < text.size(); i++) {
            const char c = text[i];
            const char* escape = nullptr;
            switch (c) {
                case '\\': escape = "\\\\"; break;
                case '"': escape = "\\\""; break;
                case '\n': escape = "\\n"; break;
                case '\t': escape = "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        throw error("strings cannot contain the control character " + std::to_string(static_cast<int>(c)));
                    }
                    break;
            }
            if (escape) {
                out_.append(text.substr(start, i - start));
                out_ += escape;
                start = i + 1;
            }
        }
        out_.append(text.substr(start));
        out_ += '"';
    }

    void key(std::string_view name) {
        bool bare = !name.empty();
        for (char c : name) {
            if (!is_word_char(c)) bare = false;
        }
        if (bare) {
            out_.append(name);
        } else {
            string(name);
        }
    }

    // No exponent notation, which PHL does not support.
    void decimal(double number) {
        if (!std::isfinite(number)) {
            throw error(std::string(std::isnan(number) ? "nan" : (number < 0 ? "-inf" : "inf")) +
                        " cannot be written; PHL numbers must be finite");
        }
        if (std::signbit(number)) out_ += '-';
        std::string digits;
        int exponent = 0;
        shortest_digits(std::fabs(number), digits, exponent);
        if (exponent < 0) {
            out_ += "0.";
            out_.append(static_cast<std::size_t>(-exponent - 1), '0');
            out_ += digits;
            return;
        }
        const auto whole = static_cast<std::size_t>(exponent) + 1;
        if (digits.size() <= whole) {
            out_ += digits;
            out_.append(whole - digits.size(), '0');
            out_ += ".0";
        } else {
            out_.append(digits, 0, whole);
            out_ += '.';
            out_.append(digits, whole, std::string::npos);
        }
    }

    // A non-empty list of objects that all have the same keys in the same order.
    static bool is_table(const value& v) {
        if (!v.is_list() || v.size() == 0) return false;
        const phl::list& items = v.as_list();
        for (const value& item : items) {
            if (!item.is_object()) return false;
        }
        const object& first = items[0].as_object();
        if (first.empty()) return false;
        for (std::size_t i = 1; i < items.size(); i++) {
            const object& row = items[i].as_object();
            if (row.size() != first.size()) return false;
            auto x = first.begin();
            auto y = row.begin();
            for (; x != first.end(); ++x, ++y) {
                if (x->key != y->key) return false;
            }
        }
        return true;
    }

    [[noreturn]] PHL_NOINLINE static void too_deep() {
        throw error("nesting is deeper than the limit of " + std::to_string(max_depth) + " levels");
    }

    void write(const value& v, int depth, bool one_line) {
        switch (v.kind()) {
            // PHL has no boolean or null values, so they are written as the
            // strings that coercion reads back.
            case value::type::nil:
                out_ += "\"nil\"";
                return;
            case value::type::boolean:
                out_ += v.as_bool() ? "\"true\"" : "\"false\"";
                return;
            case value::type::integer:
                out_ += std::to_string(v.as_int());
                return;
            case value::type::decimal:
                decimal(v.as_float());
                return;
            case value::type::string:
                string(v.as_string());
                return;
            default:
                break;
        }
        if (nesting_ >= max_depth) too_deep();
        nesting_++;
        if (v.is_object()) {
            write_object(v.as_object(), depth, one_line);
        } else if (is_table(v)) {
            table(v.as_list(), depth, one_line);
        } else {
            write_list(v.as_list(), depth, one_line);
        }
        nesting_--;
    }

    void write_object(const object& o, int depth, bool one_line) {
        if (o.empty()) {
            out_ += "{}";
            return;
        }
        const bool flat = one_line || indent_ == 0;
        out_ += flat ? "{" : "{\n";
        bool first = true;
        for (const member& m : o) {
            if (!first) out_ += flat ? ", " : ",\n";
            first = false;
            if (!flat) pad(depth + 1);
            key(m.key);
            out_ += ": ";
            write(m.item, depth + 1, one_line);
        }
        if (!flat) {
            out_ += '\n';
            pad(depth);
        }
        out_ += '}';
    }

    void write_list(const phl::list& items, int depth, bool one_line) {
        if (items.empty()) {
            out_ += "[]";
            return;
        }
        if (!one_line) {
            one_line = true;
            for (const value& item : items) {
                if (item.is_list() || item.is_object()) one_line = false;
            }
        }
        const bool flat = one_line || indent_ == 0;
        out_ += flat ? "[" : "[\n";
        bool first = true;
        for (const value& item : items) {
            if (!first) out_ += flat ? ", " : ",\n";
            first = false;
            if (!flat) pad(depth + 1);
            write(item, depth + 1, one_line);
        }
        if (!flat) {
            out_ += '\n';
            pad(depth);
        }
        out_ += ']';
    }

    void table(const phl::list& rows, int depth, bool one_line) {
        const bool flat = one_line || indent_ == 0;
        out_ += '(';
        bool first = true;
        for (const member& m : rows[0].as_object()) {
            if (!first) out_ += ", ";
            first = false;
            key(m.key);
        }
        out_ += flat ? ") { " : ") {\n";
        first = true;
        for (const value& row : rows) {
            if (flat) {
                if (!first) out_ += ' ';
                first = false;
            } else {
                pad(depth + 1);
            }
            bool first_cell = true;
            for (const member& m : row.as_object()) {
                if (!first_cell) out_ += ", ";
                first_cell = false;
                write(m.item, 0, true);
            }
            out_ += ';';
            if (!flat) out_ += '\n';
        }
        if (flat) {
            out_ += " }";
        } else {
            pad(depth);
            out_ += '}';
        }
    }

    int indent_;
    int nesting_ = 0;
    std::string out_;
};

}  // namespace detail

/// Reads a PHL document (UTF-8) into an object. With coerce, the quoted strings
/// "true" and "false" become booleans and "nil", "null" and "none" become nil,
/// ignoring case. Whole numbers beyond 64 bits are an error.
inline value parse(std::string_view text, bool coerce = true) {
    return detail::parser(text, coerce).document();
}

/// Reads an HPL table (UTF-8) into a list of objects.
inline value parse_table(std::string_view text, bool coerce = true) {
    return detail::parser(text, coerce).table_document();
}

/// Writes an object as a PHL document. indent is the spaces per nesting level; 0
/// writes nested values on one line. A list of objects that all have the same
/// keys in the same order is written as a table.
inline std::string stringify(const value& document, int indent = 4) {
    return detail::writer(indent).document(document);
}

/// Writes a list of objects with the same keys as an HPL table.
inline std::string stringify_table(const value& rows, int indent = 4) {
    return detail::writer(indent).table_document(rows);
}

}  // namespace phl

#endif  // PHL_HPP

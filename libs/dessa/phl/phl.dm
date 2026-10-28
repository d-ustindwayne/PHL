/***
 * $("phl") -- read and write PHL documents and HPL tables. Written in Dessa.
 *
 *     $("phl");
 *     var[Json] doc = parsePhl(readFile("config.phl"));
 *     print(doc.get("product").at(3).get("ID").asInt());   // 104
 *     print(toPhl(doc));                                   // back to PHL text
 *
 *     var[Json] rows = parsePhlTable(readFile("products.hpl"));
 *     print(toPhlTable(rows));
 *
 * Objects are {key: value}, lists [value, value] and tables
 * (col, col) { value, value; }. The full syntax is in the PHL README.
 *
 * Values are read into the same Json type as $("json"), so PHL and JSON
 * convert into each other.
 *
 * With coerce (on by default), the quoted strings "true" and "false" become
 * booleans and "nil", "null" and "none" become nil, ignoring case. Pass
 * coerce: false to keep them as strings.
 */

$("json") { Json, JsonKind, jsonNull, jsonBool, jsonNum, jsonStr, jsonArray, jsonObject }

// Thrown for text that is not valid PHL or HPL, and for values that cannot be
// written. line and column are 0 when the error has no position.
class PhlError(Error) {
    var[Int] line = 0;
    var[Int] column = 0;
    init(message[Str], line[Int]=0, column[Int]=0) {
        super(line > 0 ? $"{message} (line {line}, column {column})" : message);
        this.line = line;
        this.column = column;
    }
}

// ============================================================
// Tokens
// ============================================================

enum _Kind {
    STRING,
    NUMBER,
    WORD,
    PUNCT,
    NEWLINE,
    END
}

class _Token {
    var[_Kind] kind = _Kind.END;
    var[Str] text = "";
    var[Dbl] number = 0.0;
    var[Int] line = 0;
    var[Int] column = 0;

    init(kind[_Kind], text[Str], line[Int], column[Int]) {
        this.kind = kind;
        this.text = text;
        this.line = line;
        this.column = column;
    }

    func[Bul] isPunct(c[Str]) {
        return this.kind == _Kind.PUNCT && this.text == c;
    }

    func[Bul] isKeyLike() {
        return this.kind == _Kind.WORD || this.kind == _Kind.STRING;
    }

    func[Str] describe() {
        if (this.kind == _Kind.END) return "end of input";
        if (this.kind == _Kind.NEWLINE) return "end of line";
        if (this.kind == _Kind.STRING) return "string";
        return $"'{this.text}'";
    }
}

func[Bul] _isDigit(c[Chr]) {
    return c.code() >= 48 && c.code() <= 57;
}

// Keys are ASCII letters, digits and underscore.
func[Bul] _isWordChar(c[Chr]) {
    var[Int] code = c.code();
    return _isDigit(c) || (code >= 65 && code <= 90) || (code >= 97 && code <= 122) || c == '_';
}

func[Bul] _allDigits(text[Str]) {
    if (text.isEmpty()) return false;
    for (var[Chr] c in text) {
        if (!_isDigit(c)) return false;
    }
    return true;
}

class _Lexer {
    var[Chr<>] chars = <>;
    var[Int] pos = 0;
    var[Int] line = 1;
    var[Int] lineStart = 0;
    var[_Token<>] tokens = <>;

    init(text[Str]) {
        this.chars = text.chars();
        if (this.chars.length() > 0 && this.chars<0>.code() == 0xfeff) {
            this.pos = 1;
            this.lineStart = 1;
        }
    }

    func[Chr] at(i[Int]) {
        if (i >= this.chars.length()) return '\0';
        return this.chars<i>;
    }

    func[Str] slice(start[Int], end[Int]) {
        return this.chars.slice(start, end).join("");
    }

    func[Int] columnOf(i[Int]) {
        return i - this.lineStart + 1;
    }

    func[_Token] add(kind[_Kind], text[Str], column[Int]) {
        var[_Token] token = new _Token(kind, text, this.line, column);
        this.tokens.push(token);
        return token;
    }

    func[_Token<>] run() {
        var[Int] length = this.chars.length();
        while (this.pos < length) {
            var[Chr] c = this.chars<this.pos>;
            var[Int] column = this.columnOf(this.pos);
            if (c == '\n') {
                this.add(_Kind.NEWLINE, "\n", column);
                this.pos++;
                this.line++;
                this.lineStart = this.pos;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                this.pos++;
            } else if (c == '/' && this.at(this.pos + 1) == '/') {
                while (this.pos < length && this.chars<this.pos> != '\n') this.pos++;
            } else if (c == '"') {
                this.add(_Kind.STRING, this.string(), column);
            } else if (c == '-' || _isDigit(c)) {
                this.numberOrWord(column);
            } else if (_isWordChar(c)) {
                var[Int] start = this.pos;
                while (this.pos < length && _isWordChar(this.chars<this.pos>)) this.pos++;
                this.add(_Kind.WORD, this.slice(start, this.pos), column);
            } else if (":,;[]{}()".includes(Str(c))) {
                this.add(_Kind.PUNCT, Str(c), column);
                this.pos++;
            } else {
                throw new PhlError($"unexpected character '{c}'", this.line, column);
            }
        }
        this.add(_Kind.END, "", this.columnOf(this.pos));
        return this.tokens;
    }

    func[Str] string() {
        var[Int] column = this.columnOf(this.pos);
        this.pos++;
        var[Str] out = "";
        loop {
            var[Chr] c = this.at(this.pos);
            if (this.pos >= this.chars.length() || c == '\n') {
                throw new PhlError("unterminated string", this.line, column);
            }
            this.pos++;
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            var[Chr] e = this.at(this.pos);
            switch (e) {
                case 'n' := { out += "\n"; }
                case 't' := { out += "\t"; }
                case '"' := { out += "\""; }
                case '\\' := { out += "\\"; }
                default := {
                    var[Str] shown = e == '\0' ? "" : Str(e);
                    throw new PhlError($"invalid escape sequence '\\{shown}'", this.line, this.columnOf(this.pos - 1));
                }
            }
            this.pos++;
        }
    }

    func[Emp] numberOrWord(column[Int]) {
        var[Int] length = this.chars.length();
        var[Int] start = this.pos;
        var[Bul] negative = this.chars<this.pos> == '-';
        if (negative) {
            this.pos++;
            if (!_isDigit(this.at(this.pos))) {
                throw new PhlError("expected a digit after '-'", this.line, column);
            }
        }
        while (this.pos < length && _isWordChar(this.chars<this.pos>)) this.pos++;
        var[Str] head = this.slice(start, this.pos);
        var[Str] digits = negative ? this.slice(start + 1, this.pos) : head;

        if (!_allDigits(digits)) {
            if (negative) throw new PhlError($"invalid number '{head}'", this.line, column);
            // Keys may start with a digit, for example 2fa_enabled.
            this.add(_Kind.WORD, head, column);
            return;
        }

        if (this.at(this.pos) == '.' && _isDigit(this.at(this.pos + 1))) {
            this.pos++;
            while (this.pos < length && _isDigit(this.chars<this.pos>)) this.pos++;
            if (this.pos < length && _isWordChar(this.chars<this.pos>)) {
                throw new PhlError($"invalid number '{this.slice(start, this.pos + 1)}'", this.line, column);
            }
            var[_Token] token = this.add(_Kind.NUMBER, this.slice(start, this.pos), column);
            token.number = Dbl(token.text);
            return;
        }

        if (negative) {
            var[_Token] token = this.add(_Kind.NUMBER, head, column);
            token.number = Dbl(head);
            return;
        }
        // A plain run of digits can be a key or a number; the parser decides.
        this.add(_Kind.WORD, head, column);
    }
}

// ============================================================
// Reading
// ============================================================

class _Parser {
    var[_Token<>] tokens = <>;
    var[Int] pos = 0;
    var[Bul] coerce = true;

    init(text[Str], coerce[Bul]) {
        this.tokens = new _Lexer(text).run();
        this.coerce = coerce;
    }

    // ---- Token helpers ----

    func[_Token] peek(offset[Int]=0) {
        var[Int] i = this.pos + offset;
        if (i >= this.tokens.length()) i = this.tokens.length() - 1;
        return this.tokens<i>;
    }

    func[_Token] next() {
        var[_Token] token = this.tokens<this.pos>;
        if (token.kind != _Kind.END) this.pos++;
        return token;
    }

    func[Emp] skipNewlines() {
        while (this.peek().kind == _Kind.NEWLINE) this.pos++;
    }

    func[PhlError] error(message[Str], token[_Token]) {
        return new PhlError(message, token.line, token.column);
    }

    func[Emp] expect(c[Str], context[Str]) {
        var[_Token] token = this.next();
        if (!token.isPunct(c)) {
            throw this.error($"expected '{c}' {context}, found {token.describe()}", token);
        }
    }

    // True when the next tokens are `key :`, ignoring line breaks after the key.
    func[Bul] isKeyAhead() {
        if (!this.peek().isKeyLike()) return false;
        var[Int] offset = 1;
        while (this.peek(offset).kind == _Kind.NEWLINE) offset++;
        return this.peek(offset).isPunct(":");
    }

    // ---- Documents ----

    func[Json] document() {
        var[Json] result = jsonObject();
        this.skipNewlines();
        while (this.peek().kind != _Kind.END) {
            this.entry(result);
            var[_Token] token = this.peek();
            if (token.isPunct(",")) {
                this.next();
                this.skipNewlines();
            } else if (token.kind == _Kind.NEWLINE) {
                this.skipNewlines();
            } else if (token.kind != _Kind.END) {
                throw this.error($"expected a new line or ',' between entries, found {token.describe()}", token);
            }
        }
        return result;
    }

    func[Json] tableDocument() {
        this.skipNewlines();
        var[_Token] first = this.peek();
        if (!first.isPunct("(")) {
            throw this.error($"expected '(' to start the column list, found {first.describe()}", first);
        }
        var[Json] rows = this.table();
        this.skipNewlines();
        var[_Token] token = this.peek();
        if (token.kind != _Kind.END) {
            throw this.error($"unexpected {token.describe()} after the table", token);
        }
        return rows;
    }

    // ---- Entries and values ----

    func[Emp] entry(target[Json]) {
        var[_Token] keyToken = this.next();
        if (!keyToken.isKeyLike()) {
            throw this.error($"expected a key, found {keyToken.describe()}", keyToken);
        }
        var[Str] key = keyToken.text;
        this.skipNewlines();
        this.expect(":", $"after key '{key}'");
        this.skipNewlines();
        if (target.has(key)) throw this.error($"duplicate key '{key}'", keyToken);
        target.set(key, this.value());
    }

    func[Json] value() {
        var[_Token] token = this.peek();
        if (token.kind == _Kind.STRING) {
            this.next();
            if (this.coerce) {
                var[Str] lowered = token.text.lower();
                if (lowered == "true") return jsonBool(true);
                if (lowered == "false") return jsonBool(false);
                if (lowered == "nil" || lowered == "null" || lowered == "none") return jsonNull();
            }
            return jsonStr(token.text);
        }
        if (token.kind == _Kind.NUMBER) {
            this.next();
            return jsonNum(token.number);
        }
        if (token.kind == _Kind.WORD) {
            this.next();
            if (token.text == "true") return jsonBool(true);
            if (token.text == "false") return jsonBool(false);
            if (token.text == "nil") return jsonNull();
            if (_allDigits(token.text)) return jsonNum(Dbl(token.text));
            throw this.error($"unexpected word '{token.text}'; strings need double quotes", token);
        }
        if (token.isPunct("{")) return this.object();
        if (token.isPunct("[")) return this.list();
        if (token.isPunct("(")) return this.table();
        throw this.error($"expected a value, found {token.describe()}", token);
    }

    func[Json] object() {
        this.next();
        var[Json] result = jsonObject();
        loop {
            this.skipNewlines();
            if (this.peek().isPunct("}")) {
                this.next();
                return result;
            }
            if (!this.isKeyAhead()) throw this.error("expected 'key: value' inside an object", this.peek());
            this.entry(result);
            if (!this.itemSeparator("object", "}")) return result;
        }
    }

    func[Json] list() {
        this.next();
        var[Json] result = jsonArray();
        loop {
            this.skipNewlines();
            if (this.peek().isPunct("]")) {
                this.next();
                return result;
            }
            if (this.isKeyAhead()) {
                throw this.error("a list cannot contain 'key: value' entries; use { } for an object", this.peek());
            }
            result.push(this.value());
            if (!this.itemSeparator("list", "]")) return result;
        }
    }

    // Reads ',' (gives true) or the closing bracket (gives false).
    func[Bul] itemSeparator(kind[Str], closer[Str]) {
        this.skipNewlines();
        var[_Token] token = this.next();
        if (token.isPunct(",")) return true;
        if (token.isPunct(closer)) return false;
        throw this.error($"expected ',' or '{closer}' in {kind}, found {token.describe()}", token);
    }

    // ---- Tables ----

    // (col, col) { value, value; value, value; }
    func[Json] table() {
        this.next();
        var[Str<>] columns = this.columns(")");
        this.skipNewlines();
        this.expect("{", "to start the table rows");
        return this.rows(columns, "}");
    }

    func[Str<>] columns(closer[Str]) {
        var[Str<>] columns = <>;
        loop {
            this.skipNewlines();
            if (this.peek().isPunct(closer) && columns.length() > 0) {
                this.next();
                return columns;
            }
            var[_Token] token = this.next();
            if (!token.isKeyLike()) {
                throw this.error($"expected a column name, found {token.describe()}", token);
            }
            if (columns.includes(token.text)) {
                throw this.error($"duplicate column '{token.text}'", token);
            }
            columns.push(token.text);
            this.skipNewlines();
            token = this.next();
            if (token.isPunct(closer)) return columns;
            if (!token.isPunct(",")) {
                throw this.error($"expected ',' or '{closer}' in column list, found {token.describe()}", token);
            }
        }
    }

    func[Json] rows(columns[Str<>], closer[Str]) {
        var[Json] rows = jsonArray();
        loop {
            this.skipNewlines();
            if (this.peek().isPunct(closer)) {
                this.next();
                return rows;
            }
            var[_Token] rowToken = this.peek();
            var[Json<>] values = <>;
            loop {
                this.skipNewlines();
                values.push(this.value());
                this.skipNewlines();
                var[_Token] token = this.next();
                if (token.isPunct(";")) break;
                if (!token.isPunct(",")) {
                    throw this.error($"expected ',' or ';' in table row, found {token.describe()}", token);
                }
            }
            if (values.length() != columns.length()) {
                throw this.error($"row has {values.length()} values but the table has {columns.length()} columns", rowToken);
            }
            var[Json] row = jsonObject();
            for (var[Int] i = 0; i < columns.length(); i++) row.set(columns<i>, values<i>);
            rows.push(row);
        }
    }
}

// Reads a PHL document into a Json object. Throws PhlError on mistakes.
func[Json] parsePhl(text[Str], coerce[Bul]=true) {
    return new _Parser(text, coerce).document();
}

// Reads an HPL table document into a Json array of objects.
func[Json] parsePhlTable(text[Str], coerce[Bul]=true) {
    return new _Parser(text, coerce).tableDocument();
}

// ============================================================
// Writing
// ============================================================

func[Str] _phlString(text[Str]) {
    var[Str] out = "\"";
    for (var[Chr] c in text) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c.code() < 32) {
            throw new PhlError($"strings cannot contain the control character {c.code()}");
        } else {
            out += c;
        }
    }
    return out + "\"";
}

func[Str] _phlKey(name[Str]) {
    if (name.isEmpty()) return "\"\"";
    for (var[Chr] c in name) {
        if (!_isWordChar(c)) return _phlString(name);
    }
    return name;
}

// A number without exponent notation, which PHL does not support.
func[Str] _phlNumber(value[Dbl]) {
    var[Str] text = Str(value);
    if (text == "nan" || text == "inf" || text == "-inf" || text == "NaN" || text == "Infinity" || text == "-Infinity") {
        throw new PhlError($"{text} cannot be written; PHL numbers must be finite");
    }
    var[Int] e = text.indexOf("e");
    if (e < 0) return text;

    var[Str] mantissa = text.substr(0, e);
    var[Int] exponent = Int(text.substr(e + 1, text.length() - e - 1).replace("+", ""));
    var[Str] sign = "";
    if (mantissa.startsWith("-")) {
        sign = "-";
        mantissa = mantissa.substr(1, mantissa.length() - 1);
    }
    var[Int] dot = mantissa.indexOf(".");
    var[Int] whole = dot < 0 ? mantissa.length() : dot;
    var[Str] digits = mantissa.replace(".", "");
    var[Int] point = whole + exponent;
    if (point <= 0) return sign + "0." + "0".repeat(-point) + digits;
    if (point >= digits.length()) return sign + digits + "0".repeat(point - digits.length());
    return sign + digits.substr(0, point) + "." + digits.substr(point, digits.length() - point);
}

// A non-empty array of objects that all have the same keys in the same order.
func[Bul] _isTable(value[Json]) {
    if (!value.isArray() || value.items.isEmpty()) return false;
    var[Str<>] columns = <>;
    for (var[Json] item in value.items) {
        if (!item.isObject()) return false;
        if (columns.isEmpty()) {
            columns = item.keys();
            if (columns.isEmpty()) return false;
        } else if (item.keys() != columns) {
            return false;
        }
    }
    return true;
}

func[Bul] _isScalar(value[Json]) {
    return !value.isArray() && !value.isObject();
}

class _Writer {
    var[Str] indent = "";

    init(indent[Int]) {
        this.indent = " ".repeat(indent);
    }

    func[Str] value(value[Json], depth[Int], inline[Bul]=false) {
        switch (value.kind) {
            case JsonKind.NULL := { return "nil"; }
            case JsonKind.BOOL := { return value.boolValue ? "true" : "false"; }
            case JsonKind.NUMBER := { return _phlNumber(value.numberValue); }
            case JsonKind.STRING := { return _phlString(value.stringValue); }
            case JsonKind.OBJECT := { return this.object(value, depth, inline); }
            default := {}
        }
        if (_isTable(value)) return this.table(value, depth, inline);
        return this.list(value, depth, inline);
    }

    func[Str] block(items[Str<>], depth[Int], inline[Bul], open[Str], close[Str]) {
        if (inline || this.indent.isEmpty()) return open + items.join(", ") + close;
        var[Str] pad = this.indent.repeat(depth + 1);
        var[Str<>] lines = items.map((item[Str]) := pad + item);
        return open + "\n" + lines.join(",\n") + "\n" + this.indent.repeat(depth) + close;
    }

    func[Str] object(value[Json], depth[Int], inline[Bul]) {
        if (value.fields.length() == 0) return "{}";
        var[Str<>] items = <>;
        for (var[Str] name in value.fields) {
            items.push($"{_phlKey(name)}: {this.value(value.fields<name>, depth + 1, inline)}");
        }
        return this.block(items, depth, inline, "{", "}");
    }

    func[Str] list(value[Json], depth[Int], inline[Bul]) {
        if (value.items.isEmpty()) return "[]";
        var[Bul] flat = inline;
        if (!flat) {
            flat = true;
            for (var[Json] item in value.items) {
                if (!_isScalar(item)) flat = false;
            }
        }
        var[Str<>] items = <>;
        for (var[Json] item in value.items) items.push(this.value(item, depth + 1, flat));
        return this.block(items, depth, flat, "[", "]");
    }

    func[Str] row(row[Json]) {
        var[Str<>] cells = <>;
        for (var[Str] name in row.fields) cells.push(this.value(row.fields<name>, 0, true));
        return cells.join(", ") + ";";
    }

    func[Str] header(value[Json]) {
        var[Str<>] columns = <>;
        for (var[Str] name in value.items<0>.fields) columns.push(_phlKey(name));
        return columns.join(", ");
    }

    func[Str] table(value[Json], depth[Int], inline[Bul]) {
        var[Str<>] lines = <>;
        for (var[Json] item in value.items) lines.push(this.row(item));
        var[Str] head = "(" + this.header(value) + ")";
        if (inline || this.indent.isEmpty()) return head + " { " + lines.join(" ") + " }";
        var[Str] pad = this.indent.repeat(depth + 1);
        var[Str<>] padded = lines.map((line[Str]) := pad + line);
        return head + " {\n" + padded.join("\n") + "\n" + this.indent.repeat(depth) + "}";
    }
}

// Writes a Json object as a PHL document, one `key: value` per line. indent
// sets the spaces for nested objects, lists and table rows; 0 keeps nested
// values on one line.
func[Str] toPhl(document[Json], indent[Int]=4) {
    if (!document.isObject()) {
        throw new PhlError("a PHL document must be an object of key: value entries");
    }
    var[_Writer] writer = new _Writer(indent);
    var[Str] out = "";
    for (var[Str] name in document.fields) {
        out += $"{_phlKey(name)}: {writer.value(document.fields<name>, 0)}\n";
    }
    return out;
}

// Writes a Json array of objects with the same keys as an HPL table document.
func[Str] toPhlTable(rows[Json], indent[Int]=4) {
    if (!_isTable(rows)) {
        throw new PhlError("an HPL table must be a non-empty array of objects that share the same keys");
    }
    return new _Writer(indent).table(rows, 0, false) + "\n";
}

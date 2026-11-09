package io.github.dustindwayne.phl;

import io.github.dustindwayne.phl.Lexer.Kind;
import io.github.dustindwayne.phl.Lexer.Token;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Builds Java values from PHL and HPL tokens. */
final class Parser {
    private final Lexer lexer;
    private final List<Token> tokens;
    private final boolean coerce;
    private int pos = 0;
    private int depth = 0;

    Parser(String text, boolean coerce) {
        this.lexer = new Lexer(text);
        this.tokens = lexer.run();
        this.coerce = coerce;
    }

    private Token peek() {
        return peek(0);
    }

    private Token peek(int offset) {
        return tokens.get(Math.min(pos + offset, tokens.size() - 1));
    }

    private Token next() {
        Token token = tokens.get(pos);
        if (token.kind != Kind.END) {
            pos++;
        }
        return token;
    }

    private void skipNewlines() {
        while (peek().kind == Kind.NEWLINE) {
            pos++;
        }
    }

    private PhlException error(String message, Token token) {
        return new PhlException(message, token.line, lexer.columnOf(token.lineStart, token.start));
    }

    private boolean isKeyAhead() {
        if (!peek().isKeyLike()) {
            return false;
        }
        int offset = 1;
        while (peek(offset).kind == Kind.NEWLINE) {
            offset++;
        }
        return peek(offset).isPunct(':');
    }

    private void enter(Token open) {
        if (depth >= Phl.MAX_DEPTH) {
            throw error("nesting is deeper than the limit of " + Phl.MAX_DEPTH + " levels", open);
        }
        depth++;
    }

    // Documents

    Map<String, Object> document() {
        Map<String, Object> result = new LinkedHashMap<>();
        skipNewlines();
        while (peek().kind != Kind.END) {
            entry(result);
            Token token = peek();
            if (token.isPunct(',')) {
                next();
                skipNewlines();
            } else if (token.kind == Kind.NEWLINE) {
                skipNewlines();
            } else if (token.kind != Kind.END) {
                throw error("expected a new line or ',' between entries, found " + token.describe(), token);
            }
        }
        return result;
    }

    List<Map<String, Object>> tableDocument() {
        skipNewlines();
        Token first = peek();
        if (!first.isPunct('(')) {
            throw error("expected '(' to start the column list, found " + first.describe(), first);
        }
        List<Map<String, Object>> rows = table();
        skipNewlines();
        Token token = peek();
        if (token.kind != Kind.END) {
            throw error("unexpected " + token.describe() + " after the table", token);
        }
        return rows;
    }

    // Entries and values

    private void entry(Map<String, Object> target) {
        Token keyToken = next();
        if (!keyToken.isKeyLike()) {
            throw error("expected a key, found " + keyToken.describe(), keyToken);
        }
        String key = keyToken.text;
        skipNewlines();
        Token colon = next();
        if (!colon.isPunct(':')) {
            throw error("expected ':' after key '" + Lexer.shorten(key) + "', found " + colon.describe(), colon);
        }
        skipNewlines();
        if (target.containsKey(key)) {
            throw error("duplicate key '" + Lexer.shorten(key) + "'", keyToken);
        }
        target.put(key, value());
    }

    /** ASCII only: equalsIgnoreCase would also accept letters such as dotless i. */
    private static boolean equalsAsciiIgnoreCase(String text, String lower) {
        if (text.length() != lower.length()) {
            return false;
        }
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c >= 'A' && c <= 'Z') {
                c = (char) (c - 'A' + 'a');
            }
            if (c != lower.charAt(i)) {
                return false;
            }
        }
        return true;
    }

    private Object stringValue(Token token) {
        String text = token.text;
        if (coerce && text.length() >= 3 && text.length() <= 5) {
            if (equalsAsciiIgnoreCase(text, "true")) {
                return Boolean.TRUE;
            }
            if (equalsAsciiIgnoreCase(text, "false")) {
                return Boolean.FALSE;
            }
            if (equalsAsciiIgnoreCase(text, "nil") || equalsAsciiIgnoreCase(text, "null") || equalsAsciiIgnoreCase(text, "none")) {
                return null;
            }
        }
        return text;
    }

    private Long integerValue(Token token) {
        try {
            return Long.parseLong(token.text);
        } catch (NumberFormatException e) {
            String shown = token.text.length() > 40 ? token.text.substring(0, 40) + "..." : token.text;
            throw error("the number " + shown + " does not fit in 64 bits", token);
        }
    }

    private Double decimalValue(Token token) {
        double number = Double.parseDouble(token.text);
        if (Double.isInfinite(number)) {
            throw error("this number is too large for a 64-bit decimal", token);
        }
        return number;
    }

    private Object value() {
        Token token = peek();
        switch (token.kind) {
            case STRING:
                next();
                return stringValue(token);
            case INTEGER:
                next();
                return integerValue(token);
            case DECIMAL:
                next();
                return decimalValue(token);
            case WORD:
                next();
                if (Lexer.allDigits(token.text, 0)) {
                    return integerValue(token);
                }
                throw error("unexpected word '" + Lexer.shorten(token.text) + "'; strings need double quotes", token);
            case PUNCT:
                if (token.isPunct('{')) {
                    return object();
                }
                if (token.isPunct('[')) {
                    return list();
                }
                if (token.isPunct('(')) {
                    return table();
                }
                break;
            default:
                break;
        }
        throw error("expected a value, found " + token.describe(), token);
    }

    private Map<String, Object> object() {
        enter(next());
        Map<String, Object> result = new LinkedHashMap<>();
        while (true) {
            skipNewlines();
            if (peek().isPunct('}')) {
                next();
                break;
            }
            if (!isKeyAhead()) {
                throw error("expected 'key: value' inside an object", peek());
            }
            entry(result);
            if (!itemSeparator("object", '}')) {
                break;
            }
        }
        depth--;
        return result;
    }

    private List<Object> list() {
        enter(next());
        List<Object> result = new ArrayList<>();
        while (true) {
            skipNewlines();
            if (peek().isPunct(']')) {
                next();
                break;
            }
            if (isKeyAhead()) {
                throw error("a list cannot contain 'key: value' entries; use { } for an object", peek());
            }
            result.add(value());
            if (!itemSeparator("list", ']')) {
                break;
            }
        }
        depth--;
        return result;
    }

    /** Reads ',' (gives true) or the closing bracket (gives false). */
    private boolean itemSeparator(String kind, char closer) {
        skipNewlines();
        Token token = next();
        if (token.isPunct(',')) {
            return true;
        }
        if (token.isPunct(closer)) {
            return false;
        }
        throw error("expected ',' or '" + closer + "' in " + kind + ", found " + token.describe(), token);
    }

    // Tables

    /** (col, col) { value, value; value, value; } */
    private List<Map<String, Object>> table() {
        enter(next());
        List<String> columns = columns();
        skipNewlines();
        Token open = next();
        if (!open.isPunct('{')) {
            throw error("expected '{' to start the table rows, found " + open.describe(), open);
        }
        List<Map<String, Object>> rows = new ArrayList<>();
        List<Object> cells = new ArrayList<>();
        while (true) {
            skipNewlines();
            if (peek().isPunct('}')) {
                next();
                break;
            }
            Token rowToken = peek();
            cells.clear();
            while (true) {
                skipNewlines();
                cells.add(value());
                skipNewlines();
                Token token = next();
                if (token.isPunct(';')) {
                    break;
                }
                if (!token.isPunct(',')) {
                    throw error("expected ',' or ';' in table row, found " + token.describe(), token);
                }
            }
            if (cells.size() != columns.size()) {
                throw error("row has " + cells.size() + " values but the table has " + columns.size() + " columns", rowToken);
            }
            Map<String, Object> row = new LinkedHashMap<>();
            for (int i = 0; i < columns.size(); i++) {
                row.put(columns.get(i), cells.get(i));
            }
            rows.add(row);
        }
        depth--;
        return rows;
    }

    private List<String> columns() {
        List<String> columns = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        while (true) {
            skipNewlines();
            if (peek().isPunct(')') && !columns.isEmpty()) {
                next();
                return columns;
            }
            Token token = next();
            if (!token.isKeyLike()) {
                throw error("expected a column name, found " + token.describe(), token);
            }
            if (!seen.add(token.text)) {
                throw error("duplicate column '" + Lexer.shorten(token.text) + "'", token);
            }
            columns.add(token.text);
            skipNewlines();
            token = next();
            if (token.isPunct(')')) {
                return columns;
            }
            if (!token.isPunct(',')) {
                throw error("expected ',' or ')' in column list, found " + token.describe(), token);
            }
        }
    }
}

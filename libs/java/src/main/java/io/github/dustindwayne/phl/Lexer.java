package io.github.dustindwayne.phl;

import java.util.ArrayList;
import java.util.List;

/** Turns PHL and HPL text into tokens. */
final class Lexer {
    enum Kind {
        STRING,
        INTEGER, // a negative whole number; plain digits are WORD tokens
        DECIMAL,
        WORD,
        PUNCT,
        NEWLINE,
        END
    }

    static final class Token {
        final Kind kind;
        final String text;
        final int line;
        final int lineStart;
        final int start;

        Token(Kind kind, String text, int line, int lineStart, int start) {
            this.kind = kind;
            this.text = text;
            this.line = line;
            this.lineStart = lineStart;
            this.start = start;
        }

        boolean isPunct(char c) {
            return kind == Kind.PUNCT && text.charAt(0) == c;
        }

        boolean isKeyLike() {
            return kind == Kind.WORD || kind == Kind.STRING;
        }

        String describe() {
            switch (kind) {
                case END:
                    return "end of input";
                case NEWLINE:
                    return "end of line";
                case STRING:
                    return "string";
                default:
                    return "'" + text + "'";
            }
        }
    }

    private final String text;
    private final int length;
    private final List<Token> tokens = new ArrayList<>();
    private int pos = 0;
    private int line = 1;
    private int lineStart = 0;

    Lexer(String text) {
        this.text = text;
        this.length = text.length();
    }

    static boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    static boolean isWordChar(char c) {
        return isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }

    static boolean allDigits(String s, int from) {
        if (from >= s.length()) {
            return false;
        }
        for (int i = from; i < s.length(); i++) {
            if (!isDigit(s.charAt(i))) {
                return false;
            }
        }
        return true;
    }

    /** Columns count code points, so a surrogate pair is one column. */
    int columnOf(int lineBegin, int offset) {
        return text.codePointCount(lineBegin, offset) + 1;
    }

    private PhlException error(String message, int offset) {
        return new PhlException(message, line, columnOf(lineStart, offset));
    }

    private void add(Kind kind, String value, int start) {
        tokens.add(new Token(kind, value, line, lineStart, start));
    }

    List<Token> run() {
        if (length > 0 && text.charAt(0) == '﻿') {
            pos = 1;
            lineStart = 1;
        }
        while (pos < length) {
            char c = text.charAt(pos);
            if (c == '\n') {
                add(Kind.NEWLINE, "\n", pos);
                pos++;
                line++;
                lineStart = pos;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                pos++;
            } else if (c == '/' && pos + 1 < length && text.charAt(pos + 1) == '/') {
                int end = text.indexOf('\n', pos);
                pos = end < 0 ? length : end;
            } else if (c == '"') {
                readString();
            } else if (c == '-' || isDigit(c)) {
                readNumberOrWord();
            } else if (isWordChar(c)) {
                int start = pos;
                while (pos < length && isWordChar(text.charAt(pos))) {
                    pos++;
                }
                add(Kind.WORD, text.substring(start, pos), start);
            } else if (":,;[]{}()".indexOf(c) >= 0) {
                add(Kind.PUNCT, String.valueOf(c), pos);
                pos++;
            } else if (c < 0x20) {
                throw error("unexpected control character " + (int) c, pos);
            } else {
                int width = Character.charCount(text.codePointAt(pos));
                throw error("unexpected character '" + text.substring(pos, pos + width) + "'", pos);
            }
        }
        add(Kind.END, "", length);
        return tokens;
    }

    private void readString() {
        int start = pos;
        int i = start + 1;
        StringBuilder out = null;
        int runStart = i;
        while (true) {
            if (i >= length || text.charAt(i) == '\n') {
                throw error("unterminated string", start);
            }
            char c = text.charAt(i);
            if (c == '"') {
                String value = out == null ? text.substring(runStart, i) : out.append(text, runStart, i).toString();
                add(Kind.STRING, value, start);
                pos = i + 1;
                return;
            }
            if (c == '\\') {
                char e = i + 1 < length ? text.charAt(i + 1) : '\0';
                char decoded;
                switch (i + 1 < length ? e : '?') {
                    case 'n':
                        decoded = '\n';
                        break;
                    case 't':
                        decoded = '\t';
                        break;
                    case '"':
                        decoded = '"';
                        break;
                    case '\\':
                        decoded = '\\';
                        break;
                    default:
                        String shown = "";
                        if (i + 1 < length && e != '\n') {
                            shown = text.substring(i + 1, i + 1 + Character.charCount(text.codePointAt(i + 1)));
                        }
                        throw error("invalid escape sequence '\\" + shown + "'", i);
                }
                if (out == null) {
                    out = new StringBuilder();
                }
                out.append(text, runStart, i).append(decoded);
                i += 2;
                runStart = i;
            } else {
                i++;
            }
        }
    }

    private void readNumberOrWord() {
        int start = pos;
        int i = start;
        boolean negative = text.charAt(i) == '-';
        if (negative) {
            i++;
            if (i >= length || !isDigit(text.charAt(i))) {
                throw error("expected a digit after '-'", start);
            }
        }
        while (i < length && isWordChar(text.charAt(i))) {
            i++;
        }
        String head = text.substring(start, i);
        if (!allDigits(head, negative ? 1 : 0)) {
            if (negative) {
                throw error("invalid number '" + shorten(head) + "'", start);
            }
            // Keys may start with a digit, for example 2fa_enabled.
            add(Kind.WORD, head, start);
            pos = i;
            return;
        }
        if (i + 1 < length && text.charAt(i) == '.' && isDigit(text.charAt(i + 1))) {
            i++;
            while (i < length && isDigit(text.charAt(i))) {
                i++;
            }
            if (i < length && isWordChar(text.charAt(i))) {
                throw error("invalid number '" + shorten(text.substring(start, i + 1)) + "'", start);
            }
            add(Kind.DECIMAL, text.substring(start, i), start);
            pos = i;
            return;
        }
        // A plain run of digits can be a key or a number; the parser decides.
        add(negative ? Kind.INTEGER : Kind.WORD, head, start);
        pos = i;
    }

    static String shorten(String s) {
        return s.length() > 64 ? s.substring(0, 64) : s;
    }
}

"""Turns PHL and HPL text into tokens."""

from .errors import PHLError

# Token kinds
STRING = "string"
NUMBER = "number"
WORD = "word"
PUNCT = "punct"
NEWLINE = "newline"
EOF = "eof"

_PUNCTUATION = set(":,;[]{}()")
_ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}


def _is_word_char(ch):
    return ch.isascii() and (ch.isalnum() or ch == "_")


class Token:
    __slots__ = ("kind", "value", "line", "column")

    def __init__(self, kind, value, line, column):
        self.kind = kind
        self.value = value
        self.line = line
        self.column = column

    def is_punct(self, char):
        return self.kind == PUNCT and self.value == char

    def describe(self):
        if self.kind == EOF:
            return "end of input"
        if self.kind == NEWLINE:
            return "end of line"
        if self.kind == STRING:
            return "string"
        return repr(self.value)


def tokenize(text):
    tokens = []
    i = 0
    line = 1
    line_start = 0
    length = len(text)

    if text.startswith("﻿"):
        i = 1
        line_start = 1

    while i < length:
        ch = text[i]
        column = i - line_start + 1

        if ch == "\n":
            tokens.append(Token(NEWLINE, "\n", line, column))
            i += 1
            line += 1
            line_start = i
        elif ch in " \t\r":
            i += 1
        elif ch == "/" and text.startswith("//", i):
            end = text.find("\n", i)
            i = length if end == -1 else end
        elif ch == '"':
            value, i = _read_string(text, i, line, line_start)
            tokens.append(Token(STRING, value, line, column))
        elif ch == "-" or ch.isdigit() and ch.isascii():
            token, i = _read_number_or_word(text, i, line, column)
            tokens.append(token)
        elif _is_word_char(ch):
            start = i
            while i < length and _is_word_char(text[i]):
                i += 1
            tokens.append(Token(WORD, text[start:i], line, column))
        elif ch in _PUNCTUATION:
            tokens.append(Token(PUNCT, ch, line, column))
            i += 1
        else:
            raise PHLError(f"unexpected character {ch!r}", line, column)

    tokens.append(Token(EOF, None, line, length - line_start + 1))
    return tokens


def _read_string(text, i, line, line_start):
    start_column = i - line_start + 1
    i += 1
    parts = []
    length = len(text)
    while True:
        if i >= length or text[i] == "\n":
            raise PHLError("unterminated string", line, start_column)
        ch = text[i]
        if ch == '"':
            return "".join(parts), i + 1
        if ch == "\\":
            nxt = text[i + 1] if i + 1 < length else ""
            if nxt not in _ESCAPES:
                raise PHLError(
                    f"invalid escape sequence '\\{nxt}'", line, i - line_start + 1
                )
            parts.append(_ESCAPES[nxt])
            i += 2
        else:
            parts.append(ch)
            i += 1


def _read_number_or_word(text, i, line, column):
    length = len(text)
    start = i
    negative = text[i] == "-"
    if negative:
        i += 1
        if i >= length or not (text[i].isascii() and text[i].isdigit()):
            raise PHLError("expected a digit after '-'", line, column)

    while i < length and _is_word_char(text[i]):
        i += 1
    head = text[start:i]
    digits = head[1:] if negative else head

    if not digits.isdigit():
        if negative:
            raise PHLError(f"invalid number {head!r}", line, column)
        # Keys may start with a digit, for example 2fa_enabled.
        return Token(WORD, head, line, column), i

    if i + 1 < length and text[i] == "." and text[i + 1].isdigit():
        i += 1
        while i < length and text[i].isascii() and text[i].isdigit():
            i += 1
        if i < length and _is_word_char(text[i]):
            raise PHLError(f"invalid number {text[start:i + 1]!r}", line, column)
        return Token(NUMBER, float(text[start:i]), line, column), i

    if negative:
        return Token(NUMBER, int(head), line, column), i
    # A plain run of digits can be a key or a number; the parser decides.
    return Token(WORD, head, line, column), i

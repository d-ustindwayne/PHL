"""Builds Python values from PHL and HPL tokens."""

from ._lexer import EOF, NEWLINE, NUMBER, PUNCT, STRING, WORD, tokenize
from .errors import PHLError

_COERCE_STRINGS = {"true": True, "false": False, "nil": None, "null": None, "none": None}


class _Parser:
    def __init__(self, text, coerce):
        self.tokens = tokenize(text)
        self.pos = 0
        self.coerce = coerce

    # Token helpers

    def peek(self, offset=0):
        return self.tokens[min(self.pos + offset, len(self.tokens) - 1)]

    def next(self):
        token = self.tokens[self.pos]
        if token.kind != EOF:
            self.pos += 1
        return token

    def skip_newlines(self):
        while self.peek().kind == NEWLINE:
            self.pos += 1

    def error(self, message, token=None):
        token = token or self.peek()
        return PHLError(message, token.line, token.column)

    def expect(self, char, context):
        token = self.next()
        if not token.is_punct(char):
            raise self.error(f"expected '{char}' {context}, found {token.describe()}", token)
        return token

    def is_key_ahead(self):
        """True when the next tokens are `key :`, ignoring line breaks after the key."""
        if self.peek().kind not in (WORD, STRING):
            return False
        offset = 1
        while self.peek(offset).kind == NEWLINE:
            offset += 1
        return self.peek(offset).is_punct(":")

    # Documents

    def parse_document(self):
        result = {}
        self.skip_newlines()
        while self.peek().kind != EOF:
            self.parse_entry(result)
            token = self.peek()
            if token.is_punct(","):
                self.next()
                self.skip_newlines()
            elif token.kind == NEWLINE:
                self.skip_newlines()
            elif token.kind != EOF:
                raise self.error(
                    f"expected a new line or ',' between entries, found {token.describe()}"
                )
        return result

    def parse_table_document(self):
        self.skip_newlines()
        if not self.peek().is_punct("("):
            token = self.peek()
            raise self.error(f"expected '(' to start the column list, found {token.describe()}")
        rows = self.parse_table()
        self.skip_newlines()
        token = self.peek()
        if token.kind != EOF:
            raise self.error(f"unexpected {token.describe()} after the table", token)
        return rows

    # Entries and values

    def parse_key(self):
        token = self.next()
        if token.kind not in (WORD, STRING):
            raise self.error(f"expected a key, found {token.describe()}", token)
        return token

    def parse_entry(self, target):
        key_token = self.parse_key()
        key = key_token.value
        self.skip_newlines()
        self.expect(":", f"after key {key!r}")
        self.skip_newlines()
        if key in target:
            raise self.error(f"duplicate key {key!r}", key_token)
        target[key] = self.parse_value()

    def parse_value(self):
        token = self.peek()
        if token.kind == STRING:
            self.next()
            if self.coerce:
                lowered = token.value.lower()
                if lowered in _COERCE_STRINGS:
                    return _COERCE_STRINGS[lowered]
            return token.value
        if token.kind == NUMBER:
            self.next()
            return token.value
        if token.kind == WORD:
            self.next()
            if token.value.isdigit():
                return int(token.value)
            raise self.error(
                f"unexpected word {token.value!r}; strings need double quotes", token
            )
        if token.is_punct("{"):
            return self.parse_object()
        if token.is_punct("["):
            return self.parse_list()
        if token.is_punct("("):
            return self.parse_table()
        if token.kind == EOF:
            raise self.error("expected a value, found end of input", token)
        raise self.error(f"expected a value, found {token.describe()}", token)

    def parse_object(self):
        self.next()
        result = {}
        while True:
            self.skip_newlines()
            if self.peek().is_punct("}"):
                self.next()
                return result
            if not self.is_key_ahead():
                raise self.error("expected 'key: value' inside an object")
            self.parse_entry(result)
            if not self.parse_item_separator("object", "}"):
                return result

    def parse_list(self):
        self.next()
        result = []
        while True:
            self.skip_newlines()
            if self.peek().is_punct("]"):
                self.next()
                return result
            if self.is_key_ahead():
                raise self.error("a list cannot contain 'key: value' entries; use { } for an object")
            result.append(self.parse_value())
            if not self.parse_item_separator("list", "]"):
                return result

    def parse_item_separator(self, kind, closer):
        """Consumes ',' (returns True) or the closing bracket (returns False)."""
        self.skip_newlines()
        token = self.next()
        if token.is_punct(","):
            return True
        if token.is_punct(closer):
            return False
        raise self.error(
            f"expected ',' or '{closer}' in {kind}, found {token.describe()}", token
        )

    # Tables

    def parse_table(self):
        self.next()
        columns = self.parse_columns(")")
        self.skip_newlines()
        self.expect("{", "to start the table rows")
        return self.parse_rows(columns, "}")

    def parse_columns(self, closer):
        columns = []
        seen = set()
        while True:
            self.skip_newlines()
            if self.peek().is_punct(closer) and columns:
                self.next()
                return columns
            token = self.next()
            if token.kind not in (WORD, STRING):
                raise self.error(f"expected a column name, found {token.describe()}", token)
            if token.value in seen:
                raise self.error(f"duplicate column {token.value!r}", token)
            seen.add(token.value)
            columns.append(token.value)
            self.skip_newlines()
            token = self.next()
            if token.is_punct(closer):
                return columns
            if not token.is_punct(","):
                raise self.error(
                    f"expected ',' or '{closer}' in column list, found {token.describe()}",
                    token,
                )

    def parse_rows(self, columns, closer):
        rows = []
        while True:
            self.skip_newlines()
            if self.peek().is_punct(closer):
                self.next()
                return rows
            row_token = self.peek()
            values = []
            while True:
                self.skip_newlines()
                values.append(self.parse_value())
                self.skip_newlines()
                token = self.next()
                if token.is_punct(";"):
                    break
                if not token.is_punct(","):
                    raise self.error(
                        f"expected ',' or ';' in table row, found {token.describe()}", token
                    )
            if len(values) != len(columns):
                raise self.error(
                    f"row has {len(values)} values but the table has {len(columns)} columns",
                    row_token,
                )
            rows.append(dict(zip(columns, values)))


def loads(text, *, coerce=True):
    """Parse a PHL document into a dict.

    With coerce=True (the default), quoted strings "true" and "false" become
    booleans and "nil", "null" and "none" become None, ignoring case.
    """
    return _Parser(text, coerce).parse_document()


def loads_table(text, *, coerce=True):
    """Parse an HPL table document into a list of dicts."""
    return _Parser(text, coerce).parse_table_document()

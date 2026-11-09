"""Writes Python values as PHL and HPL text."""

import math
import re
from collections.abc import Mapping
from decimal import Decimal

from ._parser import _TOO_DEEP, MAX_DEPTH
from .errors import PHLError

_BARE_KEY = re.compile(r"[A-Za-z0-9_]+\Z")
_ESCAPES = {"\\": "\\\\", '"': '\\"', "\n": "\\n", "\t": "\\t"}


def _string(value):
    for ch in value:
        if ch not in _ESCAPES and ord(ch) < 0x20:
            raise PHLError(f"strings cannot contain the control character {ch!r}")
    return '"' + "".join(_ESCAPES.get(ch, ch) for ch in value) + '"'


def _key(key):
    if not isinstance(key, str):
        raise PHLError(f"keys must be strings, not {type(key).__name__}")
    return key if _BARE_KEY.match(key) else _string(key)


def _number(value):
    if isinstance(value, int):
        return str(value)
    if not math.isfinite(value):
        raise PHLError(f"{value} cannot be written; PHL numbers must be finite")
    text = repr(value)
    if "e" in text or "E" in text:
        text = format(Decimal(text), "f")
        if "." not in text:
            text += ".0"
    return text


def _is_table(value):
    """A non-empty list of dicts that all have the same keys in the same order."""
    if not value or not all(isinstance(row, Mapping) for row in value):
        return False
    columns = list(value[0].keys())
    return bool(columns) and all(list(row.keys()) == columns for row in value)


def _is_scalar(value):
    return value is None or isinstance(value, (str, bool, int, float))


class _Writer:
    def __init__(self, indent):
        self.indent = " " * indent if indent else ""
        self.nesting = 0

    def value(self, value, depth, inline=False):
        # PHL has no boolean or null values, so they are written as the strings
        # that coercion reads back.
        if value is None:
            return '"nil"'
        if isinstance(value, bool):
            return '"true"' if value else '"false"'
        if isinstance(value, (int, float)):
            return _number(value)
        if isinstance(value, str):
            return _string(value)
        if not isinstance(value, (Mapping, list, tuple)):
            raise PHLError(f"cannot write a value of type {type(value).__name__}")

        # The limit also stops values that contain themselves. The methods below
        # use loops, as comprehensions need a stack frame of their own on older
        # Pythons, and the recursion limit must not be reached before this limit.
        if self.nesting >= MAX_DEPTH:
            raise PHLError(_TOO_DEEP)
        self.nesting += 1
        if isinstance(value, Mapping):
            result = self.object(value, depth, inline)
        else:
            value = list(value)
            if _is_table(value):
                result = self.table(value, depth, inline)
            else:
                result = self.list(value, depth, inline)
        self.nesting -= 1
        return result

    def block(self, items, depth, inline, opener, closer):
        if inline or not self.indent:
            return opener + ", ".join(items) + closer
        pad = self.indent * (depth + 1)
        inner = ",\n".join(pad + item for item in items)
        return opener + "\n" + inner + "\n" + self.indent * depth + closer

    def object(self, value, depth, inline):
        if not value:
            return "{}"
        items = []
        for k, v in value.items():
            items.append(f"{_key(k)}: {self.value(v, depth + 1, inline)}")
        return self.block(items, depth, inline, "{", "}")

    def list(self, value, depth, inline):
        if not value:
            return "[]"
        inline = inline or all(_is_scalar(item) for item in value)
        items = []
        for item in value:
            items.append(self.value(item, depth + 1, inline))
        return self.block(items, depth, inline, "[", "]")

    def row(self, row):
        cells = []
        for cell in row.values():
            cells.append(self.value(cell, 0, True))
        return ", ".join(cells) + ";"

    def table(self, rows, depth, inline):
        header = "(" + ", ".join(_key(k) for k in rows[0].keys()) + ")"
        lines = []
        for row in rows:
            lines.append(self.row(row))
        if inline or not self.indent:
            return header + " { " + " ".join(lines) + " }"
        pad = self.indent * (depth + 1)
        body = "\n".join(pad + line for line in lines)
        return header + " {\n" + body + "\n" + self.indent * depth + "}"


def dumps(obj, *, indent=4):
    """Write a dict as a PHL document. Each top-level entry goes on its own line.

    indent sets the spaces used for nested objects, lists and table rows.
    Use 0 or None to write nested values on one line.
    """
    if not isinstance(obj, Mapping):
        raise PHLError("a PHL document must be a dict of top-level entries")
    writer = _Writer(indent)
    lines = []
    try:
        for k, v in obj.items():
            lines.append(f"{_key(k)}: {writer.value(v, 0)}")
    except RecursionError:
        raise PHLError(_TOO_DEEP) from None
    return "\n".join(lines) + "\n" if lines else ""


def dumps_table(rows, *, indent=4):
    """Write a list of dicts with the same keys as an HPL table document."""
    rows = list(rows)
    if not _is_table(rows):
        raise PHLError(
            "an HPL table must be a non-empty list of dicts that share the same keys"
        )
    writer = _Writer(indent)
    writer.nesting = 1
    try:
        return writer.table(rows, 0, False) + "\n"
    except RecursionError:
        raise PHLError(_TOO_DEEP) from None

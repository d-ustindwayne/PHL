"""Writes Python values as PHL and HPL text."""

import math
import re
from collections.abc import Mapping
from decimal import Decimal

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

    def value(self, value, depth, inline=False):
        if value is None:
            return "nil"
        if isinstance(value, bool):
            return "true" if value else "false"
        if isinstance(value, (int, float)):
            return _number(value)
        if isinstance(value, str):
            return _string(value)
        if isinstance(value, Mapping):
            return self.object(value, depth, inline)
        if isinstance(value, (list, tuple)):
            value = list(value)
            if _is_table(value):
                return self.table(value, depth, inline)
            return self.list(value, depth, inline)
        raise PHLError(f"cannot write a value of type {type(value).__name__}")

    def block(self, items, depth, inline, opener, closer):
        if inline or not self.indent:
            return opener + ", ".join(items) + closer
        pad = self.indent * (depth + 1)
        inner = ",\n".join(pad + item for item in items)
        return opener + "\n" + inner + "\n" + self.indent * depth + closer

    def object(self, value, depth, inline):
        if not value:
            return "{}"
        items = [f"{_key(k)}: {self.value(v, depth + 1, inline)}" for k, v in value.items()]
        return self.block(items, depth, inline, "{", "}")

    def list(self, value, depth, inline):
        if not value:
            return "[]"
        inline = inline or all(_is_scalar(item) for item in value)
        items = [self.value(item, depth + 1, inline) for item in value]
        return self.block(items, depth, inline, "[", "]")

    def row(self, row):
        return ", ".join(self.value(cell, 0, inline=True) for cell in row.values()) + ";"

    def table(self, rows, depth, inline):
        header = "(" + ", ".join(_key(k) for k in rows[0].keys()) + ")"
        lines = [self.row(row) for row in rows]
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
    lines = [f"{_key(k)}: {writer.value(v, 0)}" for k, v in obj.items()]
    return "\n".join(lines) + "\n" if lines else ""


def dumps_table(rows, *, indent=4):
    """Write a list of dicts with the same keys as an HPL table document."""
    rows = list(rows)
    if not _is_table(rows):
        raise PHLError(
            "an HPL table must be a non-empty list of dicts that share the same keys"
        )
    return _Writer(indent).table(rows, 0, False) + "\n"

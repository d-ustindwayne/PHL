// Writes JavaScript values as PHL and HPL text.

import { PHLError } from "./errors.js";

const BARE_KEY = /^[A-Za-z0-9_]+$/;
const ESCAPES = { "\\": "\\\\", '"': '\\"', "\n": "\\n", "\t": "\\t" };

function writeString(value) {
  let out = '"';
  for (const ch of value) {
    if (ch in ESCAPES) {
      out += ESCAPES[ch];
    } else if (ch < " ") {
      throw new PHLError(`strings cannot contain the control character ${JSON.stringify(ch)}`);
    } else {
      out += ch;
    }
  }
  return out + '"';
}

function writeKey(key) {
  return BARE_KEY.test(key) ? key : writeString(key);
}

/** Formats a number without exponent notation, which PHL does not support. */
function writeNumber(value) {
  if (!Number.isFinite(value)) {
    throw new PHLError(`${value} cannot be written; PHL numbers must be finite`);
  }
  const text = String(value);
  const match = /^(-?)(\d+)(?:\.(\d+))?e([+-]\d+)$/.exec(text);
  if (!match) return text;

  const [, sign, whole, fraction = "", exponent] = match;
  const digits = whole + fraction;
  const point = whole.length + Number(exponent);
  if (point <= 0) return `${sign}0.${"0".repeat(-point)}${digits}`;
  if (point >= digits.length) return sign + digits + "0".repeat(point - digits.length);
  return `${sign}${digits.slice(0, point)}.${digits.slice(point)}`;
}

function isPlainObject(value) {
  if (value === null || typeof value !== "object") return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

/** A non-empty array of objects that all have the same keys in the same order. */
function isTable(value) {
  if (!value.length || !value.every(isPlainObject)) return false;
  const columns = Object.keys(value[0]);
  return (
    columns.length > 0 &&
    value.every((row) => {
      const keys = Object.keys(row);
      return keys.length === columns.length && keys.every((key, i) => key === columns[i]);
    })
  );
}

const isScalar = (value) => value === null || typeof value !== "object";

class Writer {
  constructor(indent) {
    this.indent = indent ? " ".repeat(indent) : "";
  }

  value(value, depth, inline = false) {
    // PHL has no boolean or null values, so they are written as the strings
    // that coercion reads back.
    if (value === null) return '"nil"';
    switch (typeof value) {
      case "boolean":
        return value ? '"true"' : '"false"';
      case "number":
        return writeNumber(value);
      case "string":
        return writeString(value);
    }
    if (Array.isArray(value)) {
      return isTable(value) ? this.table(value, depth, inline) : this.list(value, depth, inline);
    }
    if (isPlainObject(value)) return this.object(value, depth, inline);
    const type = value?.constructor?.name ?? typeof value;
    throw new PHLError(`cannot write a value of type ${type}`);
  }

  block(items, depth, inline, open, close) {
    if (inline || !this.indent) return `${open}${items.join(", ")}${close}`;
    const pad = this.indent.repeat(depth + 1);
    const inner = items.map((item) => pad + item).join(",\n");
    return `${open}\n${inner}\n${this.indent.repeat(depth)}${close}`;
  }

  object(value, depth, inline) {
    const entries = Object.entries(value);
    if (!entries.length) return "{}";
    const items = entries.map(
      ([key, item]) => `${writeKey(key)}: ${this.value(item, depth + 1, inline)}`,
    );
    return this.block(items, depth, inline, "{", "}");
  }

  list(value, depth, inline) {
    if (!value.length) return "[]";
    inline = inline || value.every(isScalar);
    const items = value.map((item) => this.value(item, depth + 1, inline));
    return this.block(items, depth, inline, "[", "]");
  }

  row(row) {
    return `${Object.values(row)
      .map((cell) => this.value(cell, 0, true))
      .join(", ")};`;
  }

  table(rows, depth, inline) {
    const header = `(${Object.keys(rows[0]).map(writeKey).join(", ")})`;
    const lines = rows.map((row) => this.row(row));
    if (inline || !this.indent) return `${header} { ${lines.join(" ")} }`;
    const pad = this.indent.repeat(depth + 1);
    const body = lines.map((line) => pad + line).join("\n");
    return `${header} {\n${body}\n${this.indent.repeat(depth)}}`;
  }
}

export function stringify(value, { indent = 4 } = {}) {
  if (!isPlainObject(value)) {
    throw new PHLError("a PHL document must be an object of top-level entries");
  }
  const writer = new Writer(indent);
  const lines = Object.entries(value).map(
    ([key, item]) => `${writeKey(key)}: ${writer.value(item, 0)}`,
  );
  return lines.length ? `${lines.join("\n")}\n` : "";
}

export function stringifyTable(rows, { indent = 4 } = {}) {
  if (!Array.isArray(rows) || !isTable(rows)) {
    throw new PHLError("an HPL table must be a non-empty array of objects that share the same keys");
  }
  return `${new Writer(indent).table(rows, 0, false)}\n`;
}

// Builds JavaScript values, or a syntax tree with positions, from PHL and HPL tokens.

import { PHLError } from "./errors.js";
import { EOF, NEWLINE, NUMBER, STRING, WORD, tokenize } from "./lexer.js";

const CONSTANTS = new Map([
  ["true", true],
  ["false", false],
  ["nil", null],
]);
const COERCE_STRINGS = new Map([
  ["true", true],
  ["false", false],
  ["nil", null],
  ["null", null],
  ["none", null],
]);

// defineProperty stores a "__proto__" key as data instead of changing the prototype.
function setKey(target, key, value) {
  Object.defineProperty(target, key, {
    value,
    writable: true,
    enumerable: true,
    configurable: true,
  });
}

const start = (token) => ({ line: token.line, column: token.column });
const end = (token) => ({ line: token.line, column: token.endColumn });

// The parser hands what it reads to a builder, which makes either plain
// values (parse) or tree nodes with positions (parseTree).
const valueBuilder = {
  scalar: (token, value) => value,
  object(open, entries) {
    const result = {};
    for (const entry of entries) setKey(result, entry.key, entry.value);
    return result;
  },
  list: (open, items) => items,
  table(open, columns, rows) {
    return rows.map((values) => {
      const row = {};
      columns.forEach((column, index) => setKey(row, column.name, values[index]));
      return row;
    });
  },
  document(entries) {
    return this.object(null, entries);
  },
};

const scalarType = (value) =>
  value === null ? "nil" : typeof value === "boolean" ? "boolean" : typeof value;

const treeBuilder = {
  scalar: (token, value) => ({
    type: scalarType(value),
    value,
    // A quoted "true", "nil" and so on that coercion turned into another type.
    coerced: token.kind === STRING && typeof value !== "string",
    start: start(token),
    end: end(token),
  }),
  object: (open, entries, close) => ({
    type: "object",
    entries,
    start: start(open),
    end: end(close),
  }),
  list: (open, items, close) => ({ type: "list", items, start: start(open), end: end(close) }),
  table: (open, columns, rows, close) => ({
    type: "table",
    columns,
    rows,
    start: start(open),
    end: end(close),
  }),
  document: (entries) => ({ type: "document", entries }),
};

class Parser {
  constructor(text, coerce, builder) {
    this.tokens = tokenize(text);
    this.pos = 0;
    this.coerce = coerce;
    this.builder = builder;
  }

  // Token helpers

  peek(offset = 0) {
    return this.tokens[Math.min(this.pos + offset, this.tokens.length - 1)];
  }

  next() {
    const token = this.tokens[this.pos];
    if (token.kind !== EOF) this.pos += 1;
    return token;
  }

  skipNewlines() {
    while (this.peek().kind === NEWLINE) this.pos += 1;
  }

  error(message, token = this.peek()) {
    return new PHLError(message, token.line, token.column);
  }

  expect(char, context) {
    const token = this.next();
    if (!token.isPunct(char)) {
      throw this.error(`expected '${char}' ${context}, found ${token.describe()}`, token);
    }
    return token;
  }

  /** True when the next tokens are `key :`, ignoring line breaks after the key. */
  isKeyAhead() {
    const kind = this.peek().kind;
    if (kind !== WORD && kind !== STRING) return false;
    let offset = 1;
    while (this.peek(offset).kind === NEWLINE) offset += 1;
    return this.peek(offset).isPunct(":");
  }

  // Documents

  parseDocument() {
    const entries = [];
    const seen = new Set();
    this.skipNewlines();
    while (this.peek().kind !== EOF) {
      entries.push(this.parseEntry(seen));
      const token = this.peek();
      if (token.isPunct(",")) {
        this.next();
        this.skipNewlines();
      } else if (token.kind === NEWLINE) {
        this.skipNewlines();
      } else if (token.kind !== EOF) {
        throw this.error(
          `expected a new line or ',' between entries, found ${token.describe()}`,
        );
      }
    }
    return this.builder.document(entries);
  }

  parseTableDocument() {
    this.skipNewlines();
    const token = this.peek();
    if (!token.isPunct("(")) {
      throw this.error(`expected '(' to start the column list, found ${token.describe()}`);
    }
    const rows = this.parseTable();
    this.skipNewlines();
    const after = this.peek();
    if (after.kind !== EOF) {
      throw this.error(`unexpected ${after.describe()} after the table`, after);
    }
    return rows;
  }

  // Entries and values

  parseEntry(seen) {
    const keyToken = this.next();
    if (keyToken.kind !== WORD && keyToken.kind !== STRING) {
      throw this.error(`expected a key, found ${keyToken.describe()}`, keyToken);
    }
    const key = keyToken.value;
    this.skipNewlines();
    this.expect(":", `after key '${key}'`);
    this.skipNewlines();
    if (seen.has(key)) throw this.error(`duplicate key '${key}'`, keyToken);
    seen.add(key);
    return { key, keyStart: start(keyToken), keyEnd: end(keyToken), value: this.parseValue() };
  }

  parseValue() {
    const token = this.peek();
    if (token.kind === STRING) {
      this.next();
      if (this.coerce) {
        const lowered = token.value.toLowerCase();
        if (COERCE_STRINGS.has(lowered)) {
          return this.builder.scalar(token, COERCE_STRINGS.get(lowered));
        }
      }
      return this.builder.scalar(token, token.value);
    }
    if (token.kind === NUMBER) {
      this.next();
      return this.builder.scalar(token, token.value);
    }
    if (token.kind === WORD) {
      this.next();
      if (CONSTANTS.has(token.value)) return this.builder.scalar(token, CONSTANTS.get(token.value));
      if (/^[0-9]+$/.test(token.value)) return this.builder.scalar(token, Number(token.value));
      throw this.error(
        `unexpected word '${token.value}'; strings need double quotes`,
        token,
      );
    }
    if (token.isPunct("{")) return this.parseObject();
    if (token.isPunct("[")) return this.parseList();
    if (token.isPunct("(")) return this.parseTable();
    throw this.error(`expected a value, found ${token.describe()}`, token);
  }

  parseObject() {
    const open = this.next();
    const entries = [];
    const seen = new Set();
    while (true) {
      this.skipNewlines();
      if (this.peek().isPunct("}")) {
        return this.builder.object(open, entries, this.next());
      }
      if (!this.isKeyAhead()) throw this.error("expected 'key: value' inside an object");
      entries.push(this.parseEntry(seen));
      const close = this.parseItemSeparator("object", "}");
      if (close) return this.builder.object(open, entries, close);
    }
  }

  parseList() {
    const open = this.next();
    const items = [];
    while (true) {
      this.skipNewlines();
      if (this.peek().isPunct("]")) {
        return this.builder.list(open, items, this.next());
      }
      if (this.isKeyAhead()) {
        throw this.error("a list cannot contain 'key: value' entries; use { } for an object");
      }
      items.push(this.parseValue());
      const close = this.parseItemSeparator("list", "]");
      if (close) return this.builder.list(open, items, close);
    }
  }

  /** Consumes ',' (returns null) or the closing bracket (returns its token). */
  parseItemSeparator(kind, closer) {
    this.skipNewlines();
    const token = this.next();
    if (token.isPunct(",")) return null;
    if (token.isPunct(closer)) return token;
    throw this.error(`expected ',' or '${closer}' in ${kind}, found ${token.describe()}`, token);
  }

  // Tables

  parseTable() {
    const open = this.next();
    const columns = this.parseColumns();
    this.skipNewlines();
    this.expect("{", "to start the table rows");
    const rows = [];
    while (true) {
      this.skipNewlines();
      if (this.peek().isPunct("}")) {
        return this.builder.table(open, columns, rows, this.next());
      }
      const rowToken = this.peek();
      const values = [];
      while (true) {
        this.skipNewlines();
        values.push(this.parseValue());
        this.skipNewlines();
        const token = this.next();
        if (token.isPunct(";")) break;
        if (!token.isPunct(",")) {
          throw this.error(
            `expected ',' or ';' in table row, found ${token.describe()}`,
            token,
          );
        }
      }
      if (values.length !== columns.length) {
        throw this.error(
          `row has ${values.length} values but the table has ${columns.length} columns`,
          rowToken,
        );
      }
      rows.push(values);
    }
  }

  parseColumns() {
    const columns = [];
    const seen = new Set();
    while (true) {
      this.skipNewlines();
      if (this.peek().isPunct(")") && columns.length) {
        this.next();
        return columns;
      }
      let token = this.next();
      if (token.kind !== WORD && token.kind !== STRING) {
        throw this.error(`expected a column name, found ${token.describe()}`, token);
      }
      if (seen.has(token.value)) {
        throw this.error(`duplicate column '${token.value}'`, token);
      }
      seen.add(token.value);
      columns.push({ name: token.value, start: start(token), end: end(token) });
      this.skipNewlines();
      token = this.next();
      if (token.isPunct(")")) return columns;
      if (!token.isPunct(",")) {
        throw this.error(
          `expected ',' or ')' in column list, found ${token.describe()}`,
          token,
        );
      }
    }
  }
}

export function parse(text, { coerce = true } = {}) {
  return new Parser(text, coerce, valueBuilder).parseDocument();
}

export function parseTable(text, { coerce = true } = {}) {
  return new Parser(text, coerce, valueBuilder).parseTableDocument();
}

/**
 * Parses a PHL document (or, with table: true, an HPL table) into a syntax
 * tree whose nodes carry 1-based start and end positions. For editors and
 * other tools.
 */
export function parseTree(text, { coerce = true, table = false } = {}) {
  const parser = new Parser(text, coerce, treeBuilder);
  return table ? parser.parseTableDocument() : parser.parseDocument();
}

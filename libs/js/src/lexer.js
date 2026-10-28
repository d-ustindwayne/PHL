// Turns PHL and HPL text into tokens.

import { PHLError } from "./errors.js";

export const STRING = "string";
export const NUMBER = "number";
export const WORD = "word";
export const PUNCT = "punct";
export const NEWLINE = "newline";
export const EOF = "eof";

const PUNCTUATION = new Set(":,;[]{}()");
const ESCAPES = { n: "\n", t: "\t", '"': '"', "\\": "\\" };

const isDigit = (ch) => ch >= "0" && ch <= "9";
const isWordChar = (ch) => /^[A-Za-z0-9_]$/.test(ch);

export class Token {
  constructor(kind, value, line, column) {
    this.kind = kind;
    this.value = value;
    this.line = line;
    this.column = column;
    // Column just after the token; tokens never span lines.
    this.endColumn = column + 1;
  }

  isPunct(char) {
    return this.kind === PUNCT && this.value === char;
  }

  describe() {
    if (this.kind === EOF) return "end of input";
    if (this.kind === NEWLINE) return "end of line";
    if (this.kind === STRING) return "string";
    return `'${this.value}'`;
  }
}

export function tokenize(text) {
  const tokens = [];
  let i = 0;
  let line = 1;
  let lineStart = 0;
  const length = text.length;

  if (text.startsWith("﻿")) {
    i = 1;
    lineStart = 1;
  }

  while (i < length) {
    const ch = text[i];
    const column = i - lineStart + 1;
    const count = tokens.length;

    if (ch === "\n") {
      tokens.push(new Token(NEWLINE, "\n", line, column));
      i += 1;
      line += 1;
      lineStart = i;
    } else if (ch === " " || ch === "\t" || ch === "\r") {
      i += 1;
    } else if (ch === "/" && text.startsWith("//", i)) {
      const end = text.indexOf("\n", i);
      i = end === -1 ? length : end;
    } else if (ch === '"') {
      const [value, next] = readString(text, i, line, lineStart);
      tokens.push(new Token(STRING, value, line, column));
      i = next;
    } else if (ch === "-" || isDigit(ch)) {
      const [token, next] = readNumberOrWord(text, i, line, column);
      tokens.push(token);
      i = next;
    } else if (isWordChar(ch)) {
      const start = i;
      while (i < length && isWordChar(text[i])) i += 1;
      tokens.push(new Token(WORD, text.slice(start, i), line, column));
    } else if (PUNCTUATION.has(ch)) {
      tokens.push(new Token(PUNCT, ch, line, column));
      i += 1;
    } else {
      throw new PHLError(`unexpected character '${ch}'`, line, column);
    }
    if (tokens.length > count && ch !== "\n") tokens[count].endColumn = i - lineStart + 1;
  }

  tokens.push(new Token(EOF, null, line, length - lineStart + 1));
  return tokens;
}

function readString(text, i, line, lineStart) {
  const startColumn = i - lineStart + 1;
  i += 1;
  let value = "";
  while (true) {
    if (i >= text.length || text[i] === "\n") {
      throw new PHLError("unterminated string", line, startColumn);
    }
    const ch = text[i];
    if (ch === '"') return [value, i + 1];
    if (ch === "\\") {
      const next = text[i + 1] ?? "";
      if (!(next in ESCAPES)) {
        throw new PHLError(`invalid escape sequence '\\${next}'`, line, i - lineStart + 1);
      }
      value += ESCAPES[next];
      i += 2;
    } else {
      value += ch;
      i += 1;
    }
  }
}

function readNumberOrWord(text, i, line, column) {
  const length = text.length;
  const start = i;
  const negative = text[i] === "-";
  if (negative) {
    i += 1;
    if (i >= length || !isDigit(text[i])) {
      throw new PHLError("expected a digit after '-'", line, column);
    }
  }

  while (i < length && isWordChar(text[i])) i += 1;
  const head = text.slice(start, i);
  const digits = negative ? head.slice(1) : head;

  if (!/^[0-9]+$/.test(digits)) {
    if (negative) throw new PHLError(`invalid number '${head}'`, line, column);
    // Keys may start with a digit, for example 2fa_enabled.
    return [new Token(WORD, head, line, column), i];
  }

  if (text[i] === "." && isDigit(text[i + 1] ?? "")) {
    i += 1;
    while (i < length && isDigit(text[i])) i += 1;
    if (i < length && isWordChar(text[i])) {
      throw new PHLError(`invalid number '${text.slice(start, i + 1)}'`, line, column);
    }
    return [new Token(NUMBER, Number(text.slice(start, i)), line, column), i];
  }

  if (negative) return [new Token(NUMBER, Number(head), line, column), i];
  // A plain run of digits can be a key or a number; the parser decides.
  return [new Token(WORD, head, line, column), i];
}

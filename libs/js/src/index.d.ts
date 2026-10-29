export type PHLValue = string | number | boolean | null | PHLValue[] | PHLObject;
export interface PHLObject {
  [key: string]: PHLValue;
}

export interface ParseOptions {
  /**
   * Convert the quoted strings "true" and "false" to booleans and "nil",
   * "null" and "none" to null, ignoring case. Defaults to true.
   */
  coerce?: boolean;
}

export interface StringifyOptions {
  /** Spaces per nesting level. 0 writes nested values on one line. Defaults to 4. */
  indent?: number;
}

export class PHLError extends Error {
  /** The message without the position suffix. */
  reason: string;
  line: number | null;
  column: number | null;
}

/** Parse a PHL document into an object. */
export function parse(text: string, options?: ParseOptions): PHLObject;

/** Parse an HPL table document into an array of row objects. */
export function parseTable(text: string, options?: ParseOptions): PHLObject[];

/** A 1-based position. `end` positions point just after the last character. */
export interface Position {
  line: number;
  column: number;
}

export interface Entry {
  key: string;
  keyStart: Position;
  keyEnd: Position;
  value: Node;
}

/** Booleans and nil come only from coercion of a quoted "true", "nil" and so on. */
export type Node =
  | { type: "string"; value: string; coerced: false; start: Position; end: Position }
  | { type: "number"; value: number; coerced: false; start: Position; end: Position }
  | { type: "boolean"; value: boolean; coerced: true; start: Position; end: Position }
  | { type: "nil"; value: null; coerced: true; start: Position; end: Position }
  | { type: "object"; entries: Entry[]; start: Position; end: Position }
  | { type: "list"; items: Node[]; start: Position; end: Position }
  | {
      type: "table";
      columns: { name: string; start: Position; end: Position }[];
      rows: Node[][];
      start: Position;
      end: Position;
    };

export interface TreeOptions extends ParseOptions {
  /** Parse an HPL table document instead of a PHL document. */
  table?: boolean;
}

/** Parse into a syntax tree with positions, for editors and other tools. */
export function parseTree(text: string, options?: TreeOptions & { table?: false }): { type: "document"; entries: Entry[] };
export function parseTree(text: string, options: TreeOptions & { table: true }): Node;

/** Write an object as a PHL document. Each top-level entry goes on its own line. */
export function stringify(value: PHLObject, options?: StringifyOptions): string;

/** Write an array of objects with the same keys as an HPL table document. */
export function stringifyTable(rows: PHLObject[], options?: StringifyOptions): string;

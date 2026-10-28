import assert from "node:assert/strict";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { PHLError, parse, parseTable, parseTree, stringify, stringifyTable } from "../src/index.js";

const conformance = fileURLToPath(new URL("../../../conformance/", import.meta.url));
const read = (path) => readFileSync(path, "utf8");
const cases = (dir, ext) =>
  readdirSync(join(conformance, dir))
    .filter((name) => name.endsWith(ext))
    .sort()
    .map((name) => join(conformance, dir, name));

test("valid PHL conformance cases", () => {
  for (const source of cases("phl/valid", ".phl")) {
    const expected = JSON.parse(read(source.replace(/\.phl$/, ".json")));
    assert.deepEqual(parse(read(source)), expected, source);

    const raw = source.replace(/\.phl$/, ".nocoerce.json");
    if (existsSync(raw)) {
      assert.deepEqual(parse(read(source), { coerce: false }), JSON.parse(read(raw)), raw);
    }
  }
});

test("invalid PHL conformance cases", () => {
  for (const source of cases("phl/invalid", ".phl")) {
    assert.throws(() => parse(read(source)), PHLError, source);
  }
});

test("valid HPL conformance cases", () => {
  for (const source of cases("hpl/valid", ".hpl")) {
    const expected = JSON.parse(read(source.replace(/\.hpl$/, ".json")));
    assert.deepEqual(parseTable(read(source)), expected, source);
  }
});

test("invalid HPL conformance cases", () => {
  for (const source of cases("hpl/invalid", ".hpl")) {
    assert.throws(() => parseTable(read(source)), PHLError, source);
  }
});

test("round trip", () => {
  for (const source of cases("phl/valid", ".phl")) {
    const data = parse(read(source));
    for (const indent of [4, 0]) {
      assert.deepEqual(parse(stringify(data, { indent })), data, source);
    }
  }
  for (const source of cases("hpl/valid", ".hpl")) {
    const rows = parseTable(read(source));
    assert.deepEqual(parseTable(stringifyTable(rows)), rows, source);
  }
});

test("coerced values are real booleans and null", () => {
  const data = parse('a: "True"\nb: "NULL"');
  assert.equal(data.a, true);
  assert.equal(data.b, null);
});

test("keys are never coerced", () => {
  assert.deepEqual(parse('"true": 1'), { true: 1 });
});

test("__proto__ is stored as a normal key", () => {
  const data = parse('"__proto__": {polluted: true}');
  assert.deepEqual(Object.keys(data), ["__proto__"]);
  assert.equal({}.polluted, undefined);
});

test("table rows are indexed from 0", () => {
  const data = parse('product: (ID, Name) { 101, "a"; 102, "b"; 103, "c"; 104, "d"; }');
  assert.equal(data.product[3].ID, 104);
});

test("errors report line and column", () => {
  assert.throws(() => parse("a: 1\nb: oops"), { line: 2, column: 4 });
});

test("stringify layout", () => {
  const data = {
    name: "Ann",
    tags: ["a", "b"],
    server: { Host: "db", Port: 1341 },
    rows: [
      { ID: 1, Name: "x" },
      { ID: 2, Name: "y" },
    ],
  };
  assert.equal(
    stringify(data),
    'name: "Ann"\n' +
      'tags: ["a", "b"]\n' +
      "server: {\n" +
      '    Host: "db",\n' +
      "    Port: 1341\n" +
      "}\n" +
      "rows: (ID, Name) {\n" +
      '    1, "x";\n' +
      '    2, "y";\n' +
      "}\n",
  );
});

test("stringifyTable", () => {
  assert.equal(
    stringifyTable([{ ID: 1, "full name": "Ann" }]),
    '(ID, "full name") {\n    1, "Ann";\n}\n',
  );
});

test("quotes keys and escapes strings", () => {
  assert.equal(stringify({ "a b": 'say "hi"\n' }), '"a b": "say \\"hi\\"\\n"\n');
});

test("small and large numbers are written without exponents", () => {
  const text = stringify({ small: 1e-7, large: 1e22, neg: -2.5e-8 });
  assert.equal(text, "small: 0.0000001\nlarge: 10000000000000000000000\nneg: -0.000000025\n");
  assert.deepEqual(parse(text), { small: 1e-7, large: 1e22, neg: -2.5e-8 });
});

test("rejects values that cannot be written", () => {
  for (const bad of [{ x: NaN }, { x: new Date() }, { x: undefined }, ["a"]]) {
    assert.throws(() => stringify(bad), PHLError);
  }
});

test("empty object and list", () => {
  const text = stringify({ o: {}, l: [] });
  assert.equal(text, "o: {}\nl: []\n");
  assert.deepEqual(parse(text), { o: {}, l: [] });
});

test("parseTree gives types and positions", () => {
  const tree = parseTree('name: "Ann"\nt: (ID) {\n    1;\n}');
  const [name, table] = tree.entries;
  assert.equal(name.key, "name");
  assert.deepEqual([name.keyStart, name.keyEnd], [{ line: 1, column: 1 }, { line: 1, column: 5 }]);
  assert.deepEqual(
    [name.value.type, name.value.start, name.value.end],
    ["string", { line: 1, column: 7 }, { line: 1, column: 12 }],
  );
  assert.equal(table.value.type, "table");
  assert.deepEqual([table.value.start, table.value.end], [{ line: 2, column: 4 }, { line: 4, column: 2 }]);
  assert.equal(table.value.rows[0][0].value, 1);
  assert.equal(parseTree("(A) { 1; }", { table: true }).type, "table");
});

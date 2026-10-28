import assert from 'node:assert/strict';
import { test } from 'node:test';

import { analyse, hover, inStringOrComment, symbols } from '../src/analysis.mjs';

const DOC = [
    'name: "Ann"',
    'flag: "true"',
    'product: (ID, Name) {',
    '    101, "Mouse";',
    '    102, "Chair";',
    '}',
    'Services: {',
    '    Database: {Port: 1341},',
    '    "full name": ["a", "b"]',
    '}',
].join('\n');

test('reports the first error with its position', () => {
    const { tree, error } = analyse('a: 1\nb: oops', false);
    assert.equal(tree, null);
    assert.deepEqual(error, { message: "unexpected word 'oops'; strings need double quotes", line: 2, column: 4 });
});

test('outline lists entries, rows and nested keys', () => {
    const outline = symbols(analyse(DOC, false).tree);
    assert.deepEqual(outline.map((s) => [s.name, s.kind, s.detail]), [
        ['name', 'String', '"Ann"'],
        ['flag', 'Boolean', 'true'],
        ['product', 'Struct', 'table, 2 rows'],
        ['Services', 'Object', 'object, 2 keys'],
    ]);
    const rows = outline[2].children;
    assert.deepEqual(rows.map((r) => r.name), ['[0]', '[1]']);
    assert.deepEqual(rows[1].children.map((c) => [c.name, c.detail]), [['ID', '102'], ['Name', '"Chair"']]);
    assert.deepEqual(outline[2].range, { start: { line: 3, column: 1 }, end: { line: 6, column: 2 } });
});

test('outline of an HPL table lists its rows', () => {
    const outline = symbols(analyse('(A, B) {\n    1, 2;\n    3, 4;\n}', true).tree);
    assert.deepEqual(outline.map((r) => r.name), ['[0]', '[1]']);
});

test('hover gives the path and type', () => {
    const tree = analyse(DOC, false).tree;
    assert.match(hover(tree, 5, 10).text, /^\*\*`product\[1\]\.Name`\*\*: string/);
    assert.match(hover(tree, 3, 2).text, /table[\s\S]*2 rows; columns `ID`, `Name`/);
    assert.match(hover(tree, 3, 12).text, /`ID`\*\*: column of `product`/);
    assert.match(hover(tree, 8, 23).text, /^\*\*`Services\.Database\.Port`\*\*: number/);
    assert.match(hover(tree, 9, 25).text, /^\*\*`Services\["full name"\]\[1\]`\*\*: string/);
    assert.match(hover(tree, 2, 8).text, /read as boolean because coercion is on/);
    assert.equal(hover(tree, 1, 6), null);
});

test('knows when the cursor is in a string or comment', () => {
    assert.equal(inStringOrComment('a: "tr'), true);
    assert.equal(inStringOrComment('a: "x\\"y" // no'), true);
    assert.equal(inStringOrComment('a: "x", b: '), false);
    assert.equal(inStringOrComment('url: "http://x", b: '), false);
});

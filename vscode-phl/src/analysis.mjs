// What the extension knows about a PHL or HPL document, as plain data.
// Kept free of the vscode module so it can be tested with Node alone.
// Positions are 1-based, as in the PHL library.

import { PHLError, parseTree } from './phl/index.js';

const BARE_KEY = /^[A-Za-z0-9_]+$/;

/** Parses a document. Gives { tree } or { error: { message, line, column } }. */
export function analyse(text, isTable) {
    try {
        return { tree: parseTree(text, { table: isTable }), error: null };
    } catch (e) {
        if (!(e instanceof PHLError)) throw e;
        return { tree: null, error: { message: e.reason, line: e.line, column: e.column } };
    }
}

// ---- Paths: product[3].ID, Services.Database, ["full name"] ----

function keySegment(key, first) {
    if (BARE_KEY.test(key)) return first ? key : `.${key}`;
    return `[${JSON.stringify(key)}]`;
}

const withKey = (path, key) => path + keySegment(key, path === '');
const withIndex = (path, index) => `${path}[${index}]`;

// ---- Describing values ----

const plural = (count, word) => `${count} ${word}${count === 1 ? '' : 's'}`;

function summary(node) {
    switch (node.type) {
        case 'object': return `object, ${plural(node.entries.length, 'key')}`;
        case 'list': return `list, ${plural(node.items.length, 'item')}`;
        case 'table': return `table, ${plural(node.rows.length, 'row')}`;
        case 'string': return JSON.stringify(node.value);
        case 'nil': return 'nil';
        default: return String(node.value);
    }
}

const SYMBOL_KINDS = {
    object: 'Object',
    list: 'Array',
    table: 'Struct',
    string: 'String',
    number: 'Number',
    boolean: 'Boolean',
    nil: 'Null',
};

// ---- Outline ----

function symbolFor(name, node, nameStart, nameEnd) {
    return {
        name,
        detail: summary(node),
        kind: SYMBOL_KINDS[node.type],
        range: { start: nameStart, end: node.end },
        selectionRange: { start: nameStart, end: nameEnd },
        children: childSymbols(node),
    };
}

function childSymbols(node) {
    if (node.type === 'object') {
        return node.entries.map((e) => symbolFor(e.key, e.value, e.keyStart, e.keyEnd));
    }
    if (node.type === 'list') {
        return node.items.map((item, i) => symbolFor(`[${i}]`, item, item.start, item.end));
    }
    if (node.type === 'table') {
        return node.rows.map((cells, i) => {
            const first = cells[0];
            const last = cells[cells.length - 1];
            return {
                name: `[${i}]`,
                detail: summary(first),
                kind: 'Object',
                range: { start: first.start, end: last.end },
                selectionRange: { start: first.start, end: first.end },
                children: cells.map((cell, j) =>
                    symbolFor(node.columns[j].name, cell, cell.start, cell.end)),
            };
        });
    }
    return [];
}

/** The outline: one symbol per top-level entry (or per row, for an HPL table). */
export function symbols(tree) {
    if (tree.type === 'document') {
        return tree.entries.map((e) => symbolFor(e.key, e.value, e.keyStart, e.keyEnd));
    }
    return childSymbols(tree);
}

// ---- Hover ----

const before = (a, b) => a.line < b.line || (a.line === b.line && a.column < b.column);
const inside = (pos, start, end) => !before(pos, start) && before(pos, end);

function hoverText(path, node) {
    const title = path ? `\`${path}\`` : 'table';
    const lines = [`**${title}**: ${node.type}`];
    if (node.type === 'object') {
        lines.push(plural(node.entries.length, 'key'));
    } else if (node.type === 'list') {
        lines.push(plural(node.items.length, 'item'));
    } else if (node.type === 'table') {
        lines.push(`${plural(node.rows.length, 'row')}; columns ${node.columns.map((c) => `\`${c.name}\``).join(', ')}`);
    } else if (node.coerced) {
        lines.push(`Written as a quoted string; read as ${node.type} because coercion is on.`);
    }
    return lines.join('\n\n');
}

function findInEntries(entries, path, pos) {
    for (const entry of entries) {
        const entryPath = withKey(path, entry.key);
        if (inside(pos, entry.keyStart, entry.keyEnd)) {
            return { text: hoverText(entryPath, entry.value), range: { start: entry.keyStart, end: entry.keyEnd } };
        }
        if (inside(pos, entry.value.start, entry.value.end)) return findInNode(entry.value, entryPath, pos);
    }
    return null;
}

function findInNode(node, path, pos) {
    if (node.type === 'object') return findInEntries(node.entries, path, pos);
    if (node.type === 'list') {
        for (const [i, item] of node.items.entries()) {
            if (inside(pos, item.start, item.end)) return findInNode(item, withIndex(path, i), pos);
        }
        return null;
    }
    if (node.type === 'table') {
        for (const column of node.columns) {
            if (inside(pos, column.start, column.end)) {
                const text = `**\`${column.name}\`**: column of ${path ? `\`${path}\`` : 'the table'}\n\n${plural(node.rows.length, 'row')}`;
                return { text, range: { start: column.start, end: column.end } };
            }
        }
        for (const [i, cells] of node.rows.entries()) {
            for (const [j, cell] of cells.entries()) {
                if (inside(pos, cell.start, cell.end)) {
                    return findInNode(cell, withKey(withIndex(path, i), node.columns[j].name), pos);
                }
            }
        }
        return null;
    }
    return { text: hoverText(path, node), range: { start: node.start, end: node.end } };
}

/** Hover for the key, column or value at a position: its path and type. */
export function hover(tree, line, column) {
    const pos = { line, column };
    if (tree.type === 'document') return findInEntries(tree.entries, '', pos);
    return findInNode(tree, '', pos);
}

// ---- Completion ----

export const VALUE_COMPLETIONS = [
    { label: 'true', kind: 'Constant', insert: 'true', detail: 'boolean' },
    { label: 'false', kind: 'Constant', insert: 'false', detail: 'boolean' },
    { label: 'nil', kind: 'Constant', insert: 'nil', detail: 'empty value' },
    { label: 'table', kind: 'Snippet', insert: '(${1:ID}, ${2:Name}) {\n\t${3:1}, ${4:"a"};\n}', detail: '(columns) { rows; }' },
    { label: 'object', kind: 'Snippet', insert: '{\n\t${1:key}: ${2:value}\n}', detail: '{ key: value }' },
    { label: 'list', kind: 'Snippet', insert: '[${1}]', detail: '[ value, value ]' },
];

/** True when the cursor is inside a string or a // comment on its line. */
export function inStringOrComment(linePrefix) {
    let inString = false;
    for (let i = 0; i < linePrefix.length; i++) {
        const ch = linePrefix[i];
        if (inString) {
            if (ch === '\\') i++;
            else if (ch === '"') inString = false;
        } else if (ch === '"') {
            inString = true;
        } else if (ch === '/' && linePrefix[i + 1] === '/') {
            return true;
        }
    }
    return inString;
}

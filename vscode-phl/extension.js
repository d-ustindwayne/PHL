// PHL for VS Code: errors as you type, outline, hover and completion for
// .phl / .pll documents and .hpl / .hpll tables.
//
// The language work is in src/analysis.mjs, which uses the PHL JavaScript
// library (copied into src/phl by scripts/copy-lib.js), so the editor reads
// files exactly as the library does.
'use strict';

const vscode = require('vscode');
const path = require('path');
const { pathToFileURL } = require('url');

const LANGUAGES = ['phl', 'hpl'];
const SELECTOR = LANGUAGES.map((language) => ({ language }));

let analysis;
const results = new Map();   // uri -> { version, result }

function resultFor(document) {
    const key = document.uri.toString();
    const cached = results.get(key);
    if (cached && cached.version === document.version) return cached.result;
    const result = analysis.analyse(document.getText(), document.languageId === 'hpl');
    results.set(key, { version: document.version, result });
    return result;
}

const toPosition = (p) => new vscode.Position(p.line - 1, p.column - 1);
const toRange = (r) => new vscode.Range(toPosition(r.start), toPosition(r.end));

function errorRange(document, error) {
    const line = Math.min(error.line - 1, document.lineCount - 1);
    const text = document.lineAt(line).text;
    const column = Math.min(error.column - 1, text.length);
    const word = document.getWordRangeAtPosition(new vscode.Position(line, column));
    if (word) return word;
    // At the end of a line (or of the file) mark the last character instead.
    if (column >= text.length) return new vscode.Range(line, Math.max(column - 1, 0), line, column);
    return new vscode.Range(line, column, line, column + 1);
}

function refreshDiagnostics(document, collection) {
    if (!LANGUAGES.includes(document.languageId)) return;
    const { error } = resultFor(document);
    if (!error) {
        collection.set(document.uri, []);
        return;
    }
    const diagnostic = new vscode.Diagnostic(errorRange(document, error), error.message, vscode.DiagnosticSeverity.Error);
    diagnostic.source = document.languageId.toUpperCase();
    collection.set(document.uri, [diagnostic]);
}

function toSymbol(symbol) {
    const result = new vscode.DocumentSymbol(
        symbol.name, symbol.detail, vscode.SymbolKind[symbol.kind],
        toRange(symbol.range), toRange(symbol.selectionRange));
    result.children = symbol.children.map(toSymbol);
    return result;
}

async function activate(context) {
    analysis = await import(pathToFileURL(path.join(__dirname, 'src', 'analysis.mjs')).href);

    const diagnostics = vscode.languages.createDiagnosticCollection('phl');
    context.subscriptions.push(
        diagnostics,
        vscode.workspace.onDidOpenTextDocument((d) => refreshDiagnostics(d, diagnostics)),
        vscode.workspace.onDidChangeTextDocument((e) => refreshDiagnostics(e.document, diagnostics)),
        vscode.workspace.onDidCloseTextDocument((d) => {
            diagnostics.delete(d.uri);
            results.delete(d.uri.toString());
        }),

        vscode.languages.registerDocumentSymbolProvider(SELECTOR, {
            provideDocumentSymbols(document) {
                const { tree } = resultFor(document);
                return tree ? analysis.symbols(tree).map(toSymbol) : [];
            },
        }),

        vscode.languages.registerHoverProvider(SELECTOR, {
            provideHover(document, position) {
                const { tree } = resultFor(document);
                if (!tree) return null;
                const found = analysis.hover(tree, position.line + 1, position.character + 1);
                if (!found) return null;
                return new vscode.Hover(new vscode.MarkdownString(found.text), toRange(found.range));
            },
        }),

        vscode.languages.registerCompletionItemProvider(SELECTOR, {
            provideCompletionItems(document, position) {
                const prefix = document.lineAt(position.line).text.slice(0, position.character);
                if (analysis.inStringOrComment(prefix)) return [];
                return analysis.VALUE_COMPLETIONS.map((c) => {
                    const item = new vscode.CompletionItem(c.label, vscode.CompletionItemKind[c.kind]);
                    item.insertText = c.kind === 'Snippet' ? new vscode.SnippetString(c.insert) : c.insert;
                    item.detail = c.detail;
                    return item;
                });
            },
        }),
    );

    vscode.workspace.textDocuments.forEach((d) => refreshDiagnostics(d, diagnostics));
}

function deactivate() {}

module.exports = { activate, deactivate };

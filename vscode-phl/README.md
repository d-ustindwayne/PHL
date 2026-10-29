# PHL for VS Code

Support for [PHL](../README.md) documents (`.phl`, `.pll`) and HPL tables
(`.hpl`, `.hpll`):

- **Highlighting** for keys, objects `{key: value}`, lists `[1, 2]`, tables
  `(columns) { row; row; }`, strings, numbers and `//` comments.
- **Errors as you type**, with the same messages the libraries give. Only the
  first error in a file is shown, because parsing stops there.
- **Outline and breadcrumbs**: every key, list item, table row and cell.
  `Ctrl+Shift+O` jumps to any of them.
- **Hover** on a key, column or value shows its path (`product[3].ID`), its
  type, and the size of objects, lists and tables. Quoted values such as
  `"true"` that are read as another type are pointed out.
- **Completion** for `"true"`, `"false"` and `"nil"`, and snippets for a table,
  an object and a list.

Brackets and quotes close automatically, and `//` toggles comments.

The language features use the PHL JavaScript library from `libs/js`, so the
editor accepts and rejects exactly what the library does.

## Install

Download `phl-<version>.vsix` from the
[releases page](https://github.com/d-ustindwayne/PHL/releases). In VS Code, open
the Extensions view, click the `...` menu at the top, choose
**Install from VSIX...** and pick the file.

## Install from source

The extension needs a copy of the library first:

```
cd vscode-phl
bun run copy-lib
```

Then copy or link this folder into your VS Code extensions folder as `phl-0.3.0`:

- Windows: `%USERPROFILE%\.vscode\extensions\phl-0.3.0`
- macOS / Linux: `~/.vscode/extensions/phl-0.3.0`

Restart VS Code. To make a `.vsix` instead, run
`bunx @vscode/vsce package --no-dependencies` in this folder (it copies the
library itself).

## Tests

```
bun run test
```

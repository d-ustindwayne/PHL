// Copies the PHL JavaScript library (libs/js/src) into src/phl, so the
// packaged extension carries the same parser the library uses.
'use strict';

const fs = require('fs');
const path = require('path');

const from = path.join(__dirname, '..', '..', 'libs', 'js', 'src');
const to = path.join(__dirname, '..', 'src', 'phl');

fs.rmSync(to, { recursive: true, force: true });
fs.mkdirSync(to, { recursive: true });
for (const name of fs.readdirSync(from)) {
    if (name.endsWith('.js')) fs.copyFileSync(path.join(from, name), path.join(to, name));
}
// The library is ES modules; this marks the folder as such for Node.
fs.writeFileSync(path.join(to, 'package.json'), '{ "type": "module" }\n');
console.log(`copied ${from} -> ${to}`);

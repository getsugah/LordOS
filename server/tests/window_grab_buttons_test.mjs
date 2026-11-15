// Execute the actual window mouse-down callback bodies with a small WM stub.
// No React build, compositor or display is needed. Includes maximized-window
// JS fallback and PiP grabs, which must not start after C rejects a side button.
import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';

const files = [
  ['../../themes/default/components/Window.jsx', false],
  ['../../themes/classic/components/Window.jsx', true],
  ['../../sdk/windowshell.jsx', false],
  ['../../sdk/components.jsx', false],
];
let callbacks = 0, checks = 0;
for (const [file, leftOnly] of files) {
  const source = readFileSync(new URL(file, import.meta.url), 'utf8');
  let found = 0;
  for (const match of source.matchAll(/onMouseDown=\{\(e\) => \{([\s\S]*?)\}\}/g)) {
    const body = match[1];
    if (!/wm\.beginMove|wm\.beginResize|frame: w\.pip/.test(body)) continue;
    found++; callbacks++;
    const callback = new Function('e', 'wm', 'win', 'geo', 'drag', 'w', 'edges', body);
    for (const nativeWorks of [false, true]) {
      for (let button = 0; button <= 4; button++) {
        let begins = 0;
        const originalDrag = { previous: true }; // e.g. another primary button is already held
        const drag = { current: originalDrag };
        const wm = { focus() {}, beginMove() { begins++; return nativeWorks; }, beginResize() { begins++; return nativeWorks; } };
        const event = { button, x: 120, y: 100, stopPropagation() {} };
        callback(event, wm, { id: 1 }, { x: 0, w: 400 }, drag, { id: 1, pip: {} }, 8);
        const allowed = leftOnly ? button === 0 : button <= 2;
        const started = begins > 0 || drag.current !== originalDrag;
        assert.equal(started, allowed, `${file}: button ${button}, native=${nativeWorks}`);
        checks++;
      }
    }
  }
  assert.equal(found, file.endsWith('components.jsx') ? 2 : 1, file);
}
console.log(`window-grab-buttons: ${callbacks} real callbacks, ${checks} checks passed`);

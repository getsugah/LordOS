import assert from 'node:assert/strict';
import { build } from 'esbuild';
const result = await build({ entryPoints: [new URL('../../runtime/events.js', import.meta.url).pathname], bundle: true, platform: 'node', format: 'esm', write: false });
const { register } = await import('data:text/javascript;base64,' + Buffer.from(result.outputFiles[0].text).toString('base64'));
globalThis.lord = { now: () => 1000 };
const seen = [];
const root = { id: 9001, props: { onNavigate: e => seen.push(e.direction), onClick: () => seen.push('click') } };
const child = { id: 9002, parent: root, props: {} };
register(root); register(child);
for (const b of [3, 4]) {
  __lordEvent('down', child.id, 0, 0, b);
  __lordEvent('up', child.id, 0, 0, b);
}
assert.deepEqual(seen, ['back', 'forward']);
__lordEvent('down', child.id, 0, 0, 0);
__lordEvent('down', child.id, 0, 0, 3);
__lordEvent('up', child.id, 0, 0, 3);
__lordEvent('up', child.id, 0, 0, 0);
assert.deepEqual(seen, ['back', 'forward', 'back', 'click']);
console.log('mouse-navigation: OK');

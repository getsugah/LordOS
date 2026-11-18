import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';

const source = readFileSync(new URL('../../sdk/microanimation.js', import.meta.url), 'utf8');
const { tweenStyle, leavingItems } = await import(`data:text/javascript,${encodeURIComponent(source)}`);
let now = 0, next = 0, writes = [], frames = new Map();
Object.defineProperty(globalThis, 'performance', { value: { now: () => now }, configurable: true });
globalThis.lord = { setStyle: (...args) => writes.push(args) };
globalThis.requestAnimationFrame = (fn) => { frames.set(++next, fn); return next; };
globalThis.cancelAnimationFrame = (id) => frames.delete(id);
function advance(time) {
  now = time;
  const pending = frames; frames = new Map();
  for (const fn of pending.values()) fn(now);
}
for (const hz of [60, 120, 144]) {
  now = 0; writes = [];
  tweenStyle(7, 'opacity', 0, 1, 140);
  for (let time = 1000 / hz; time < 140; time += 1000 / hz) advance(time);
  advance(140);
  assert.deepEqual(writes.at(-1), [7, 'opacity', 1]);
  assert.equal(frames.size, 0, 'no idle frame after completion');
  assert.ok(writes.every((w, i) => w[2] >= 0 && w[2] <= 1 && (!i || w[2] >= writes[i - 1][2])));
}
now = 0;
let shown = 0;
const cancel = tweenStyle(8, 'translateX', 0, 16, 120, (v) => { shown = v; });
advance(40); cancel();
assert.equal(frames.size, 0);
const start = shown;
tweenStyle(8, 'translateX', shown, 0, 120);
assert.equal(writes.at(-1)[2], start, 'replacement starts at current value');
advance(1000);
assert.deepEqual(writes.at(-1), [8, 'translateX', 0]);
assert.equal(frames.size, 0, 'long stall finishes immediately');
const stop = tweenStyle(9, 'opacity', 0, 1, 100);
stop(); const count = writes.length; advance(2000);
assert.equal(writes.length, count, 'unmount prevents writes');
const a = { id: 1 }, b = { id: 2 };
let rows = leavingItems([], [a, b], 0, false);
rows = leavingItems(rows, [b], 10, false);
assert.equal(rows.find((r) => r.item.id === 1).until, 90);
assert.equal(leavingItems(rows, [b], 40, false).find((r) => r.item.id === 1).until, 90, 'updates do not postpone expiry');
assert.equal(leavingItems(rows, [b], 90, false).length, 1);
assert.equal(leavingItems(rows, [a, b], 40, false).filter((r) => r.until).length, 0, 'returning item reverses exit');
assert.equal(leavingItems(rows, [], 40, true).length, 0, 'reduce motion removes tails immediately');
assert.equal(leavingItems(Array.from({ length: 100 }, (_, id) => ({ item: { id }, until: 0 })), [], 10, false).length, 4, 'bounded visual tails');
Object.defineProperty(globalThis, 'performance', { value: performance, configurable: true });
console.log('microanimation: finite pacing, interruption, cancellation and bounded exit retention passed');

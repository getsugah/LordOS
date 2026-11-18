// Finite native-style animation. No React render in a frame callback.
// Callers own the node/property and cancel before replacing or unmounting it.
export function tweenStyle(node, property, from, to, duration, update = () => {}) {
  let frame = 0, stopped = false;
  const start = performance.now();
  const write = (value) => { lord.setStyle(node, property, value); update(value); };
  write(from);
  const step = (now) => {
    if (stopped) return;
    const p = Math.max(0, Math.min(1, (now - start) / duration));
    write(p === 1 ? to : from + (to - from) * (1 - (1 - p) ** 3));
    if (p < 1) frame = requestAnimationFrame(step);
  };
  frame = requestAnimationFrame(step);
  return () => { stopped = true; cancelAnimationFrame(frame); };
}

// Keep only a bounded visual tail after the functional item has been removed.
// Returning items replace a leaving item with the same id, so it can reverse its fade.
export function leavingItems(previous, items, now, reduce, duration = 80, limit = 4) {
  const ids = new Set(items.map((item) => item.id));
  const live = items.map((item) => ({ item, until: 0 }));
  if (reduce) return live;
  const tail = previous.filter((row) => !ids.has(row.item.id) && (!row.until || row.until > now))
    .slice(0, limit).map((row) => ({ item: row.item, until: row.until || now + duration }));
  return [...live, ...tail];
}

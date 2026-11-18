// Existing minimization lifecycle, isolated here so it can be tested without starting the shell.
import { useEffect, useRef, useState } from 'react';
import { settings } from '../../sdk/settings.js';
import { useStore } from '../../sdk/store.js';
import { minimizeStarts } from './minimize.js';

// Minimize effect: a window that was just minimized stays drawn for MIN_MS, shrinking towards the
// Dock (transform: scale, painted from a cached raster by lord-server; layout doesn't change). With Reduce motion
// none starts (minimize.js).
const MIN_MS = 180;
export function useMinimizing(windows) {
  const reduce = !!useStore(settings).reduceMotion;
  const prev = useRef(new Map());
  const [anim, setAnim] = useState([]);                 // [{ id, t0 }]
  useEffect(() => {
    const started = minimizeStarts(windows, prev.current, { reduce, now: performance.now() });
    const live = new Set(windows.filter((w) => w.minimized && !w.fullscreen && !w.pip).map((w) => w.id));
    setAnim((a) => {
      const keep = reduce ? [] : a.filter((x) => live.has(x.id) && !started.some((y) => y.id === x.id));
      return !started.length && keep.length === a.length ? a : [...keep, ...started];
    });
  }, [windows, reduce]);
  const [, tick] = useState(0);
  useEffect(() => {
    if (!anim.length || reduce) return undefined;
    let raf = requestAnimationFrame(function step() {
      const now = performance.now();
      if (anim.some((x) => now - x.t0 >= MIN_MS)) setAnim((a) => a.filter((x) => now - x.t0 < MIN_MS));
      else { tick((n) => n + 1); raf = requestAnimationFrame(step); }
    });
    return () => cancelAnimationFrame(raf);
  }, [anim, reduce]);
  const now = performance.now();
  if (reduce) return [];
  return anim.map((x) => ({ win: windows.find((w) => w.id === x.id), p: Math.min(1, (now - x.t0) / MIN_MS) }))
    .filter((x) => x.win && x.win.minimized);
}


import { useLayoutEffect, useRef, useState } from 'react';
import { tweenStyle, leavingItems } from './microanimation.js';
import { settings } from './settings.js';
import { useStore } from './store.js';

// Shell panels receive theme colors as props, outside a ThemeProvider in some themes.
// Read the desktop preference directly rather than relying on those color tokens.
export function usePanelEntrance(ref) {
  useAnimatedStyle(ref, 'opacity', 1, false, 120, 0);
}

// Logical close is immediate; keep only a non-interactive menu painting for 80 ms.
// Reopening cancels removal and reverses the same native opacity animation.
export function useMenuPresence(open, reduce) {
  const desktopReduce = useStore(settings, (s) => !!s.reduceMotion);
  reduce = !!reduce || desktopReduce;
  const [retained, setRetained] = useState(open);
  useLayoutEffect(() => {
    if (open || reduce) { setRetained(open); return; }
    if (!retained) return;
    const timer = setTimeout(() => setRetained(false), 80);
    return () => clearTimeout(timer);
  }, [open, reduce, retained]);
  return open || (!reduce && retained);
}

// Keep removed notification data briefly for painting only. Logical dismissal and
// its action have already happened. The caller must disable pointer events on tails.
export function useLeavingItems(items, reduce) {
  const [state, setState] = useState(() => ({ items, reduce, rows: leavingItems([], items, performance.now(), reduce) }));
  let rows = state.rows;
  if (state.items !== items || state.reduce !== reduce) {
    rows = leavingItems(rows, items, performance.now(), reduce);
    setState({ items, reduce, rows });
  }
  useLayoutEffect(() => {
    const until = Math.min(...rows.filter((r) => r.until).map((r) => r.until));
    if (!Number.isFinite(until)) return;
    const timer = setTimeout(() => setState((s) => ({ ...s,
      rows: s.rows.filter((r) => !r.until || r.until > performance.now()),
    })), Math.max(1, until - performance.now()));
    return () => clearTimeout(timer);
  }, [rows]);
  return rows;
}

// Animate only a property this hook owns; interruption starts at the displayed value.
export function useAnimatedStyle(ref, property, target, reduce, duration = 110, initial = target, active = true) {
  // Also cover controls mounted outside a theme provider. Isolated apps carry
  // the preference through their theme tokens (the explicit reduce argument).
  const desktopReduce = useStore(settings, (s) => !!s.reduceMotion);
  reduce = !!reduce || desktopReduce;
  const value = useRef(initial);
  useLayoutEffect(() => {
    if (!active) { value.current = initial; return; }
    const node = ref.current?.id;
    if (!node) return;
    if (reduce || value.current === target) {
      lord.setStyle(node, property, target);
      value.current = target;
      return;
    }
    return tweenStyle(node, property, value.current, target, duration, (v) => { value.current = v; });
  }, [ref, property, target, reduce, duration, active, initial]);
}

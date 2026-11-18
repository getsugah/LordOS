// Existing Dock visibility lifecycle; isolated for deterministic scene tests.
import { useEffect, useRef, useState } from 'react';
import { addListener } from '@lordos/runtime';
const SLIDE_MS = 180;

// Auto-hide: shows when the pointer touches the edge (after a short delay) and hides
// when it leaves the Dock. State only changes when crossing those boundaries: moving the mouse around the rest of the
// screen doesn't redraw anything. The animation belongs to this component, not the desktop.
// force: shown right away (an app being dragged from the app launcher).
// mode (sdk/dockmodel.js, issue #46): 'never' hides nothing; 'always' as above; 'overlap' hides only
// while `covered` (a window is where the Dock is) and comes back as soon as nothing covers it; the
// pointer at the edge still brings it over the windows, and it goes again when the pointer leaves.
const COVER_MS = 300;         // a window just covered the Dock: it slides away after this
export function useAutohide(mode, position, rectRef, keepOpen, areaRef, force, covered, reduce) {
  const enabled = mode !== 'never';
  const may = enabled && (mode === 'always' || !!covered);          // may it hide now
  const [p, setP] = useState(may ? 0 : 1);     // 0 hidden … 1 in view
  const current = useRef(p);
  const reduced = useRef(reduce);
  reduced.current = reduce;
  const progress = (value) => { current.current = value; setP(value); };
  const want = useRef(!may);
  const keep = useRef(keepOpen);
  keep.current = keepOpen;
  const mayHide = useRef(may);
  mayHide.current = may;
  const api = useRef(null);
  useEffect(() => {
    if (!enabled) { want.current = true; progress(1); return undefined; }
    want.current = !mayHide.current;
    progress(want.current ? 1 : 0);
    let timer = 0, raf = 0, from = 0, t0 = 0, px = -1, py = -1;
    const animate = (to) => {
      cancelAnimationFrame(raf);
      if (reduced.current) { from = to; progress(to); return; }
      t0 = performance.now();
      const step = (now) => {
        if (reduced.current) { from = to; progress(to); return; }
        const k = Math.min(1, (now - t0) / SLIDE_MS), e = 1 - (1 - k) * (1 - k);
        progress(from + (to - from) * e);
        if (k < 1) raf = requestAnimationFrame(step);
        else from = to;
      };
      raf = requestAnimationFrame(step);
    };
    const set = (on) => {
      if (want.current === on) return;
      want.current = on;
      from = current.current;
      animate(on ? 1 : 0);
    };
    const stop = () => { if (timer) { clearTimeout(timer); timer = 0; } };
    const isOver = () => {
      const r = rectRef.current;
      return !!r && px >= r.x0 - 8 && px < r.x1 + 8 && py >= r.y0 - 8 && py < r.y1 + 8;
    };
    // hides after ms unless, by then, the pointer is on it, a menu of it is open or nothing covers it
    const hideSoon = (ms) => {
      if (!timer) timer = setTimeout(() => { timer = 0; if (!keep.current && mayHide.current && !isOver()) set(false); }, ms);
    };
    api.current = {
      finish: () => { cancelAnimationFrame(raf); progress(want.current ? 1 : 0); },
      show: () => { stop(); set(true); },
      // 'overlap': a window started or stopped covering the Dock ('always' ↔ 'overlap' too)
      cover: (on) => {
        stop();
        if (!on) set(true);
        else if (want.current && !keep.current && !isOver()) hideSoon(COVER_MS);
      },
    };
    const off = addListener('pointermove', (e) => {
      px = e.x; py = e.y;
      // the edge of the display the Dock is on (the main one: docs/DISPLAYS.md)
      const A = areaRef.current, inX = e.x >= A.x && e.x < A.x + A.w, inY = e.y >= A.y && e.y < A.y + A.h;
      const edge = position === 'bottom' ? inX && e.y >= A.y + A.h - 2 : position === 'left' ? inY && e.x <= A.x + 1 : inY && e.x >= A.x + A.w - 2;
      if (!want.current) {
        if (edge && !timer) timer = setTimeout(() => { timer = 0; set(true); }, 120);
        else if (!edge) stop();
      } else if (!isOver() && !keep.current && mayHide.current) hideSoon(500);
      else stop();
    });
    return () => { off(); stop(); cancelAnimationFrame(raf); api.current = null; };
  }, [enabled, position]);
  useEffect(() => { if (reduce) api.current?.finish(); }, [reduce]);
  useEffect(() => { if (force && enabled) api.current?.show(); }, [force, enabled]);
  useEffect(() => { if (enabled) api.current?.cover(may); }, [may, enabled]);
  return reduce ? (want.current ? 1 : 0) : p;
}


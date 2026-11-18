#!/usr/bin/env python3
"""Summarize bounded live-diag CSV without claiming missed frames at idle."""
import argparse
import collections
import csv
import math
from pathlib import Path
import statistics

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('capture', type=Path)
args = ap.parse_args()
events = collections.Counter()
metrics = collections.defaultdict(list)
images = {}
flips = {}
first = last = None
with args.capture.open() as f:
    print(next(f).strip())
    for row in csv.reader(f):
        ns, kind = int(row[0]), row[1]
        v = list(map(float, row[2:]))
        events[kind] += 1
        first = ns if first is None else first
        last = ns
        if kind in ('pointer_dispatch', 'wayland_dispatch', 'cursor_update', 'render_total', 'poll_wait', 'loop_work'):
            metrics[kind + '_ms'].append((v[1] - v[0]) / 1e6)
        elif kind == 'cursor_image':
            metrics['cursor_image_ioctl_ms'].append((v[1] - v[0]) / 1e6)
            # Success, previous software bookkeeping said hidden: candidate reactivation.
            if v[3] and not v[4]:
                images[int(v[2])] = v[1]
        elif kind == 'cursor_show':
            crtc = int(v[2])
            metrics['cursor_show_ioctl_ms'].append((v[1] - v[0]) / 1e6)
            if not v[3] and crtc in images:
                if not v[4]:
                    metrics[f'crtc_{crtc}_image_to_hide_ms'].append((v[1] - images[crtc]) / 1e6)
                del images[crtc]
        elif kind == 'output_gpu':
            for index, name in enumerate(('upload', 'drag_upload', 'swap', 'submit'), 1):
                if not v[7]:  # direct scanout has no composed GPU times
                    metrics[f'crtc_{int(v[0])}_{name}_ms'].append(v[index])
        elif kind == 'output_flip':
            crtc, flip, seq, hz, submit = v
            crtc = int(crtc)
            if crtc in flips and flip > flips[crtc][0]:
                # This is a presentation gap, not a missed-frame classification.
                metrics[f'crtc_{crtc}_flip_gap_ms'].append((flip - flips[crtc][0]) / 1e6)
                metrics[f'crtc_{crtc}_sequence_delta'].append(seq - flips[crtc][1])
            flips[crtc] = flip, seq
            if submit and flip >= submit:
                metrics[f'crtc_{crtc}_submit_to_flip_ms'].append((flip - submit) / 1e6)
        elif kind == 'phase':
            for index, name in ((6, 'paint'), (7, 'upload'), (8, 'drag_upload'), (9, 'swap'), (10, 'submit'), (11, 'feed')):
                metrics['main_' + name + '_ms'].append(v[index])
print('events:', dict(events))
if first is not None:
    print(f'recorded span: {(last - first) / 1e9:.3f} s')
for name, values in sorted(metrics.items()):
    values.sort()
    p = lambda q: values[max(0, math.ceil(q * len(values)) - 1)]
    print(f'{name}: n={len(values)} mean={statistics.mean(values):.4f} '
          f'p50={p(.5):.4f} p99={p(.99):.4f} max={values[-1]:.4f}')
print('Presentation gaps include idle time. Cursor intervals describe ioctl sequencing, not optical visibility.')

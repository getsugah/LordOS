#!/usr/bin/env python3
"""Create an isolated source snapshot; never launch or install its compositor."""
from pathlib import Path
import shutil
import subprocess
import time

root = Path(__file__).resolve().parents[2]
dest = root / 'server/build' / time.strftime('compositor-diag-%Y%m%d-%H%M%S')
dest.mkdir()
server = dest / 'server'
server.mkdir()
for name in ('src', 'mk'):
    shutil.copytree(root / 'server' / name, server / name)
shutil.copy2(root / 'server/Makefile', server / 'Makefile')
shutil.copytree(root / 'third_party', dest / 'third_party')
for name in ('gen', 'qjs', 'obj'):
    shutil.copytree(root / 'server/build' / name, server / 'build' / name)
for name in ('live-diag.h', 'live-diag.c'):
    shutil.copy2(Path(__file__).parent / name, server / 'src' / name)


def edit(file, old, new):
    path = server / file
    text = path.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f'Expected one anchor in {file}: {old[:90]!r}')
    path.write_text(text.replace(old, new))


edit('Makefile', 'include $(sort $(wildcard mk/*.mk))',
     'build/lord-server: build/obj/live-diag.o\ninclude $(sort $(wildcard mk/*.mk))')
edit('src/main.c', '#include "bench/lbframes.h"',
     '#include "bench/lbframes.h"\n#include "live-diag.h"')
edit('src/bench/lbframes.c', '#include "lbframes.h"',
     '#include "lbframes.h"\n#include "../live-diag.h"')
edit('src/bench/lbframes.c',
     'int lbf_recording(void) { return rec_on(&rec_lb) || rec_on(&rec_drag); }',
     'int lbf_recording(void) { return diag_active() || rec_on(&rec_lb) || rec_on(&rec_drag); }')
edit('src/bench/lbframes.c', 'void lbf_phase_built(const LbPhase *p) {', '''void lbf_phase_built(const LbPhase *p) {
    DIAG("phase", p->wake, p->render0, p->built, p->submit, p->prev_flip, p->period,
         p->paint_ms, p->up_ms, p->gdrag_ms, p->swap_ms, p->submit_ms,
         p->feed_ms, p->piped, p->gdrag, p->gdrag_why, p->unshown);
    DIAG("phase_extra", p->up_px, p->gdrag_px, p->feed_px, p->par,
         p->swake, p->sub0, p->sub1, p->from_flip, p->sfrom_flip, p->gdrag_sub);''')
edit('src/bench/lbframes.c',
     'void lbf_phase_shown(uint64_t flip_ns, uint64_t gpu_done_ns) {',
     '''void lbf_phase_shown(uint64_t flip_ns, uint64_t gpu_done_ns) {
    DIAG("main_shown", flip_ns, gpu_done_ns);''')
edit('src/main.c', '    double start = now_ms();\n    while (!s.quit',
     '''    diag_init();
    for (int d = 0; d < OUT_MAX; d++) if (s.out[d].on)
        DIAG("output_state", d, s.out[d].kms.crtc_id, s.out[d].v.pw, s.out[d].v.ph,
             s.out[d].kms.mode.vrefresh, s.hw_cursor, s.out[d].kms.cursor_w,
             s.out[d].kms.cursor_h, out_scale(&s.out[d]), s.out[d].gpu);
    double start = now_ms();
    while (!s.quit''')
edit('src/main.c', '    mirror_stop();\n    gpu_tex_free(s.cursor_tex);',
     '    diag_finish();\n    mirror_stop();\n    gpu_tex_free(s.cursor_tex);')
edit('src/main.c', '    s->motion_pending = 0;\n    {',
     '    uint64_t diag_input0 = diag_active() ? diag_ns() : 0;\n    s->motion_pending = 0;\n    {')
edit('src/main.c', '    s->moved = s->wheel = s->nbuttons = 0;\n}',
     '''    if (diag_input0) DIAG("pointer_dispatch", diag_input0, diag_ns(), s->moved,
                           s->nbuttons, s->wheel, grab_active(), s->shape);
    s->moved = s->wheel = s->nbuttons = 0;
}''')
edit('src/main.c', '    while (!s.quit && !ui.quit_requested) {',
     '''    while (!s.quit && !ui.quit_requested) {
        uint64_t diag_loop0 = diag_active() ? diag_ns() : 0;''')
edit('src/main.c', '        if (poll(pfd, n, wait) < 0 && errno != EINTR)',
     '''        uint64_t diag_poll0 = diag_active() ? diag_ns() : 0;
        if (diag_loop0) DIAG("loop_work", diag_loop0, diag_poll0, wait);
        if (poll(pfd, n, wait) < 0 && errno != EINTR)''')
edit('src/main.c', '        loop_wake = mono_ns();',
     '''        if (diag_poll0) DIAG("poll_wait", diag_poll0, diag_ns(), wait);
        loop_wake = mono_ns();''')
edit('src/main.c', '    s->cursor_gen = gen;\n    s->cursor_mode = mode;',
     '''    uint64_t diag_cursor0 = diag_active() ? diag_ns() : 0;
    s->cursor_gen = gen;
    s->cursor_mode = mode;''')
edit('src/main.c', '    s->cursor_tex_h = h;\n}',
     '''    s->cursor_tex_h = h;
    if (diag_cursor0) DIAG("cursor_update", diag_cursor0, diag_ns(), mode, gen,
                           w, h, s->hw_cursor, out_at_pointer(s));
}''')
edit('src/main.c', '        if (wbase >= 0 && pfd[wbase].revents) wl_dispatch();',
     '''        if (wbase >= 0 && pfd[wbase].revents) {
            uint64_t d0 = diag_active() ? diag_ns() : 0;
            wl_dispatch();
            if (d0) DIAG("wayland_dispatch", d0, diag_ns());
        }''')
edit('src/main.c', '    double t1 = now_ms();\n    uint64_t r1 = phases ? mono_ns() : 0;',
     '''    double t1 = now_ms();
    uint64_t r1 = phases ? mono_ns() : 0;
    DIAG("render_total", r0, r1, ui.layout_ms, ui.paint_ms, shown, call_piped);''')
edit('src/main.c', '    int gdragwhy = o->pgdragwhy,',
     '    uint64_t diag_out0 = diag_active() ? diag_ns() : 0;\n    int gdragwhy = o->pgdragwhy,')
edit('src/main.c', '        int failed = res == -1, unshown = res == -2;',
     '''        int failed = res == -1, unshown = res == -2;
        if (diag_active()) {
            GpuFrameTimes dt = {0};
            if (!scanned) gpu_frame_times(o->gpu, &dt);
            DIAG("output_gpu", o->kms.crtc_id, dt.up_ms, dt.gdrag_ms, dt.swap_ms,
                 dt.submit_ms, dt.up_px, dt.gdrag_px, scanned, res);
        }''')
edit('src/main.c', '    if (ph) {\n        ph->prev_flip = o->kms.flip_ns;',
     '''    if (diag_out0) DIAG("output_present_call", o->kms.crtc_id, diag_out0,
                           diag_ns(), o->kms.flip_submit_ns, o->kms.flip_pending);
    if (ph) {
        ph->prev_flip = o->kms.flip_ns;''')
# Capture every real head's flip while retaining the existing main-head phase instrumentation.
edit('src/main.c', '            if (nh) kms_dispatch_heads(heads, nh);',
     '''            uint64_t diag_before[OUT_MAX] = {0};
            if (diag_active()) for (int d = 0; d < nh; d++) diag_before[d] = heads[d]->flip_ns;
            if (nh) kms_dispatch_heads(heads, nh);
            if (diag_active()) for (int d = 0; d < nh; d++)
                if (diag_before[d] != heads[d]->flip_ns)
                    DIAG("output_flip", heads[d]->crtc_id, heads[d]->flip_ns,
                         heads[d]->flip_seq, heads[d]->mode.vrefresh, heads[d]->flip_submit_ns);''')
edit('src/kms.c', '#include "kms.h"', '#include "kms.h"\n#include "live-diag.h"')
edit('src/kms.c', '    k->cursor_ok = xioctl(k->fd, DRM_IOCTL_MODE_CURSOR2, &c) == 0;',
     '''    uint64_t diag_c0 = diag_active() ? diag_ns() : 0;
    k->cursor_ok = xioctl(k->fd, DRM_IOCTL_MODE_CURSOR2, &c) == 0;
    if (diag_c0) DIAG("cursor_image", diag_c0, diag_ns(), k->crtc_id, k->cursor_ok,
                      k->cursor_visible, w, h);''')
edit('src/kms.c', '    xioctl(k->fd, DRM_IOCTL_MODE_CURSOR2, &c);\n    k->cursor_visible = visible;',
     '''    uint64_t diag_c0 = diag_active() ? diag_ns() : 0;
    int diag_rc = xioctl(k->fd, DRM_IOCTL_MODE_CURSOR2, &c);
    if (diag_c0) DIAG("cursor_show", diag_c0, diag_ns(), k->crtc_id, visible, diag_rc);
    k->cursor_visible = visible;''')
diff = subprocess.run(['diff', '-ruN', str(root / 'server/src'), str(server / 'src')],
                      text=True, stdout=subprocess.PIPE, check=False)
if diff.returncode not in (0, 1):
    raise RuntimeError('diff failed')
(dest / 'instrumentation.patch').write_text(diff.stdout)
(dest / 'source-status.txt').write_text(subprocess.check_output(
    ['git', '-C', str(root), 'status', '--short'], text=True))
print(dest)

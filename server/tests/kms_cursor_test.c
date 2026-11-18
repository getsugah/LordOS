/* Exercise production cursor ioctls without a DRM device or a running desktop. */
#include "../src/kms.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, calls, fail;
static struct drm_mode_cursor2 last;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

int __wrap_ioctl(int fd, unsigned long req, ...) {
    (void)fd;
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    CHECK(req == DRM_IOCTL_MODE_CURSOR2);
    last = *(struct drm_mode_cursor2 *)arg;
    calls++;
    if (fail) { errno = EIO; return -1; }
    return 0;
}

int main(void) {
    uint32_t storage[64 * 64], image[4] = { 0xffffffff, 0xff112233, 0xff445566, 0xff778899 };
    Kms a = { .fd = 42, .crtc_id = 7, .cursor_handle = 19, .cursor_w = 64, .cursor_h = 64,
        .cursor_pitch = 64, .cursor_size = sizeof storage, .cursor_map = storage,
        .cursor_ok = 1, .cursor_visible = 0 };
    CHECK(kms_cursor_image(&a, image, 2, 2, 1, 2) == 0);
    CHECK(last.handle == 0 && !a.cursor_visible && a.cursor_ok);
    CHECK(storage[0] == image[0] && storage[64] == image[2] && storage[2] == 0);
    kms_cursor_show(&a, 1);
    CHECK(last.handle == 19 && last.hot_x == 1 && last.hot_y == 2 && a.cursor_visible);
    CHECK(kms_cursor_image(&a, image, 2, 2, 3, 4) == 0);
    CHECK(last.handle == 19 && a.cursor_visible);
    fail = 1;
    int before = calls;
    kms_cursor_show(&a, 0);
    CHECK(a.cursor_visible && calls == before + 1);
    kms_cursor_show(&a, 0);
    CHECK(a.cursor_visible && calls == before + 2); /* failed hide must be retried */
    CHECK(kms_cursor_image(&a, image, 2, 2, 8, 9) == -1);
    CHECK(a.cursor_ok && a.cursor_visible && a.cursor_hot_x == 3 && a.cursor_hot_y == 4);
    fail = 0;
    kms_cursor_show(&a, 0);
    CHECK(!a.cursor_visible && last.handle == 0);
    fail = 1;
    kms_cursor_show(&a, 1);
    CHECK(!a.cursor_visible);
    fail = 0;
    kms_cursor_show(&a, 1);
    CHECK(a.cursor_visible && last.hot_x == 3 && last.hot_y == 4);
    before = calls;
    kms_cursor_show(&a, 1);
    CHECK(calls == before);
    puts("kms-cursor: visibility, hotspots and ioctl failure retries passed");
    printf("%d checks\n", checks);
    return 0;
}

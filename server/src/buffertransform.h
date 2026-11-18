/* wl_output.transform values. Map displayed surface coordinates to the raw
 * buffer: the client applied this transform, the compositor displays its inverse.
 * Coordinates are top-down. Keep this independent of GL and libwayland. */
#ifndef LORD_BUFFERTRANSFORM_H
#define LORD_BUFFERTRANSFORM_H

static inline void buffer_transform_size(int transform, int w, int h, int *ow, int *oh) {
    *ow = transform & 1 ? h : w;
    *oh = transform & 1 ? w : h;
}

static inline void buffer_transform_uv(int transform, float u, float v, float *x, float *y) {
    switch (transform) {
    case 1: *x = v; *y = 1 - u; break;
    case 2: *x = 1 - u; *y = 1 - v; break;
    case 3: *x = 1 - v; *y = u; break;
    case 4: *x = 1 - u; *y = v; break;
    case 5: *x = v; *y = u; break;
    case 6: *x = u; *y = 1 - v; break;
    case 7: *x = 1 - v; *y = 1 - u; break;
    default: *x = u; *y = v; break;
    }
}

static inline void buffer_transform_pixel(int transform, int x, int y, int w, int h, int *rx, int *ry) {
    switch (transform) {
    case 1: *rx = y; *ry = h - 1 - x; break;
    case 2: *rx = w - 1 - x; *ry = h - 1 - y; break;
    case 3: *rx = w - 1 - y; *ry = x; break;
    case 4: *rx = w - 1 - x; *ry = y; break;
    case 5: *rx = y; *ry = x; break;
    case 6: *rx = x; *ry = h - 1 - y; break;
    case 7: *rx = w - 1 - y; *ry = h - 1 - x; break;
    default: *rx = x; *ry = y; break;
    }
}
#endif

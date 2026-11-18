/* Real offscreen GLES test of the production drag/upload code. No DRM modeset,
 * compositor, input injection or connection to the user's desktop. */
#include "../src/gpu/gpu.c"

static int checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } checks++; } while (0)

static int import_created, import_destroyed, reject_2d, reject_external, bind_2d, bind_external;
static int reject_query;
static EGLBoolean fake_modifiers(EGLDisplay display, EGLint format, EGLint max,
                                  EGLuint64KHR *mods, EGLBoolean *external, EGLint *count) {
    (void)display; (void)format;
    CHECK(max >= 2);
    *count = 2;
    if (reject_query) return EGL_FALSE; /* leaves output arrays undefined */
    mods[0] = 17; external[0] = EGL_FALSE;
    mods[1] = 23; external[1] = EGL_TRUE;
    return EGL_TRUE;
}
static EGLImageKHR fake_import(EGLDisplay display, EGLContext context, EGLenum target,
                               EGLClientBuffer buffer, const EGLint *attrs) {
    (void)display; (void)context; (void)buffer; (void)attrs;
    CHECK(target == EGL_LINUX_DMA_BUF_EXT);
    import_created++;
    return (EGLImageKHR)(uintptr_t)123;
}
static EGLBoolean fake_destroy(EGLDisplay display, EGLImageKHR img) {
    (void)display;
    CHECK(img == (EGLImageKHR)(uintptr_t)123);
    import_destroyed++;
    return EGL_TRUE;
}
static void fake_bind(GLenum target, GLeglImageOES img) {
    CHECK(img == (GLeglImageOES)(uintptr_t)123);
    if (target == GL_TEXTURE_2D) bind_2d++; else bind_external++;
    if (target == GL_TEXTURE_2D ? reject_2d : reject_external) {
        glEnable(0xffffffffu); /* inject a driver rejecting this image target */
    } else if (target == GL_TEXTURE_2D) {
        const uint8_t rgba[16] = { 0x12, 0x34, 0x56, 0xff, 0x12, 0x34, 0x56, 0xff,
                                  0x12, 0x34, 0x56, 0xff, 0x12, 0x34, 0x56, 0xff };
        glTexImage2D(target, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
}

static void imports(void) {
    /* EGL import/driver binding are injected; allocation, error handling,
     * shader sampling, readback and texture lifetime use production GL code. */
    G.create_image = fake_import;
    G.destroy_image = fake_destroy;
    G.image_target = fake_bind;
    G.have_image2d = 1;
    G.have_external = 0;
    G.gen = 1;
    G.query_modifiers = fake_modifiers;
    GpuFormat formats[16];
    CHECK(gpu_formats(formats, 16) == 8);
    for (int i = 0; i < 8; i++) CHECK(formats[i].modifier != 23);
    G.have_external = 1;
    CHECK(gpu_formats(formats, 16) == 12);
    reject_query = 1;
    CHECK(gpu_formats(formats, 16) == 4);
    for (int i = 0; i < 4; i++) CHECK(formats[i].modifier == DRM_FORMAT_MOD_INVALID);
    G.query_modifiers = NULL;
    G.have_external = 0;
    CHECK(make_prog(0, fs_2d) == 0);
    GpuDmabuf d = { .width = 2, .height = 2, .nplanes = 1, .fourcc = DRM_FORMAT_ARGB8888,
                    .modifier = DRM_FORMAT_MOD_INVALID };
    CHECK(gpu_tex_dmabuf(NULL) == NULL);
    GpuTex *t = gpu_tex_dmabuf(&d);
    CHECK(t && !t->external && bind_2d == 1 && !bind_external);
    uint32_t px[4];
    CHECK(gpu_tex_read(t, px, 2, 2) == 0);
    for (int i = 0; i < 4; i++) CHECK(px[i] == 0xff123456);
    GLuint id = t->id;
    gpu_tex_free(t);
    CHECK(!glIsTexture(id) && import_destroyed == 1);
    reject_2d = 1;
    CHECK(gpu_tex_dmabuf(&d) == NULL);
    CHECK(import_destroyed == 2 && !bind_external);
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    if (has_ext(exts, "GL_OES_EGL_image_external")) {
        G.have_external = 1;
        t = gpu_tex_dmabuf(&d);
        CHECK(t && t->external && bind_external == 1);
        gpu_tex_free(t);
        reject_external = 1;
        CHECK(gpu_tex_dmabuf(&d) == NULL);
        CHECK(bind_external == 2);
        reject_external = 0;
        G.have_image2d = 0;
        t = gpu_tex_dmabuf(&d);
        CHECK(t && t->external);
        gpu_tex_free(t);
    }
    CHECK(import_created == import_destroyed);
    CHECK(glGetError() == GL_NO_ERROR);
    G.create_image = NULL;
    G.destroy_image = NULL;
    G.image_target = NULL;
    glDeleteProgram(G.prog[0]);
    G.prog[0] = 0;
}

static void pixels(int out, const GpuDrag *g, uint32_t inside) {
    const GOut *o = &G.out[out];
    uint8_t rd[64 * 48 * 4];
    glReadPixels(0, 0, o->W, o->H, GL_RGBA, GL_UNSIGNED_BYTE, rd);
    CHECK(glGetError() == GL_NO_ERROR);
    for (int y = 0; y < o->H; y++) for (int x = 0; x < o->W; x++) {
        const uint8_t *p = rd + ((o->H - 1 - y) * o->W + x) * 4;
        uint32_t actual = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        uint32_t expected = x >= g->w.x0 && x < g->w.x1 && y >= g->w.y0 && y < g->w.y1 ? inside : 0;
        if (actual != expected) {
            fprintf(stderr, "output %d pixel %d,%d: %08x != %08x\n", out, x, y, actual, expected);
            exit(1);
        }
    }
    checks++;
}

static int draw(int out, const GpuDrag *g) {
    GOut *o = &G.out[out];
    glViewport(0, 0, o->W, o->H);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    return gdrag_frame(o, out, g);
}

static uint32_t sample(int out, int x, int y) {
    uint8_t p[4];
    glReadPixels(x, G.out[out].H - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

static void transformed_pixels(void) {
    static const int expected[8][6] = {
        {1,2,3,4,5,6}, {4,1,5,2,6,3}, {6,5,4,3,2,1}, {3,6,2,5,1,4},
        {3,2,1,6,5,4}, {1,4,2,5,3,6}, {4,5,6,1,2,3}, {6,3,5,2,4,1}
    };
    CHECK(make_prog(0, fs_2d) == 0);
    uint32_t px[6];
    for (int i = 0; i < 6; i++) px[i] = 0xff000000u | (i + 1) * 0x111111u;
    GpuTex *t = gpu_tex_pixels(NULL, px, 3, 2, 1);
    CHECK(t);
    GOut out = { .W = 8, .H = 8 };
    G.cur = &out;
    glViewport(0, 0, 8, 8);
    for (int transform = 0; transform < 8; transform++) {
        int w = transform & 1 ? 2 : 3, h = transform & 1 ? 3 : 2;
        for (int crop = 0; crop <= 1; crop++) {
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            gpu_draw_transform(t, 1, 2, w - crop, h, crop, 0, w - crop, h,
                               (Rect){ 1, 2, 1 + w - crop, 2 + h }, 1, transform);
            CHECK(glGetError() == GL_NO_ERROR);
            for (int y = 0; y < h; y++) for (int x = 0; x < w - crop; x++) {
                uint8_t p[4];
                glReadPixels(x + 1, 8 - 1 - (y + 2), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
                int value = expected[transform][y * w + x + crop] * 0x11;
                CHECK(p[0] == value && p[1] == value && p[2] == value && p[3] == 255);
            }
        }
    }
    gpu_tex_free(t);
    G.cur = NULL;
    glDeleteProgram(G.prog[0]);
    G.prog[0] = 0;
    CHECK(glGetError() == GL_NO_ERROR);
}

static void pixel_damage(void) {
    CHECK(make_prog(0, fs_2d) == 0);
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    int subimage = has_ext(extensions, "GL_EXT_unpack_subimage");
    for (int mode = 0; mode <= subimage; mode++) {
        G.unpack_sub = mode;
        uint32_t px[12], rd[12];
        for (int i = 0; i < 12; i++) px[i] = 0xff102030;
        GpuTex *t = gpu_tex_pixels_damage(NULL, px, 4, 3, 0, (Rect){ 1, 1, 2, 2 });
        CHECK(t && gpu_tex_read(t, rd, 4, 3) == 0);
        for (int i = 0; i < 12; i++) CHECK(rd[i] == 0xff102030);
        for (int i = 0; i < 12; i++) px[i] = 0xff998877;
        px[5] = 0xff123456;
        t = gpu_tex_pixels_damage(t, px, 4, 3, 0, (Rect){ 1, 1, 2, 2 });
        CHECK(t && gpu_tex_read(t, rd, 4, 3) == 0);
        for (int i = 0; i < 12; i++) {
            uint32_t expected = i == 5 ? px[5] : !mode && i >= 4 && i < 8 ? px[i] : 0xff102030;
            CHECK(rd[i] == expected);
        }
        t = gpu_tex_pixels_damage(t, px, 4, 3, 0, (Rect){ -100, -100, 100, 100 });
        CHECK(t && gpu_tex_read(t, rd, 4, 3) == 0);
        for (int i = 0; i < 12; i++) CHECK(rd[i] == px[i]);
        /* Reallocation must ignore a tiny damage and initialize all pixels. */
        t = gpu_tex_pixels_damage(t, px, 3, 4, 0, (Rect){ 1, 1, 2, 2 });
        CHECK(t && gpu_tex_read(t, rd, 3, 4) == 0);
        for (int i = 0; i < 12; i++) CHECK(rd[i] == px[i]);
        gpu_tex_free(t);
        GLint maxtex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
        CHECK(gpu_tex_pixels_damage(NULL, px, maxtex + 1, 1, 0, (Rect){ 0 }) == NULL);
    }
    G.unpack_sub = 0;
    glDeleteProgram(G.prog[0]);
    G.prog[0] = 0;
    CHECK(glGetError() == GL_NO_ERROR);
}

int main(void) {
    G.dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint major, minor, n;
    CHECK(G.dpy != EGL_NO_DISPLAY && eglInitialize(G.dpy, &major, &minor));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint cfg[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    CHECK(eglChooseConfig(G.dpy, cfg, &G.cfg, 1, &n) && n == 1);
    const EGLint ctx[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    G.ctx = eglCreateContext(G.dpy, G.cfg, EGL_NO_CONTEXT, ctx);
    CHECK(G.ctx != EGL_NO_CONTEXT);
    const EGLint size[] = { EGL_WIDTH, 64, EGL_HEIGHT, 48, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(G.dpy, G.cfg, size);
    CHECK(surface != EGL_NO_SURFACE && eglMakeCurrent(G.dpy, surface, surface, G.ctx));
    snprintf(G.renderer, sizeof G.renderer, "%s", glGetString(GL_RENDERER));
    printf("renderer: %s\n", G.renderer);
    G.on = 1;
    imports();
    pixel_damage();
    transformed_pixels();
    G.out[0] = (GOut){ .used = 1, .W = 64, .H = 48, .surf = surface };
    G.out[1] = (GOut){ .used = 1, .W = 41, .H = 29, .surf = surface };
    G.gdrag = gdrag_setup();
    CHECK(G.gdrag == 1); /* includes production arithmetic and placement self-test */
    CHECK(!G.drag_test.tex[0] && !G.drag_test.tex[1]);
    CHECK(!gpu_gdrag_holds(-1) && !gpu_gdrag_holds(OUT_MAX));
    CHECK(!gpu_gdrag_holds(0) && !gpu_gdrag_holds(1));

    uint32_t below[2][64 * 48], layerpx[2][35];
    GpuDrag g[2];
    for (int o = 0; o < 2; o++) {
        for (int i = 0; i < 64 * 48; i++) below[o][i] = o ? 0xff334455 : 0xff112233;
        for (int i = 0; i < 35; i++) layerpx[o][i] = o ? 0xff18c761 : 0xffc93215;
        g[o] = (GpuDrag){ .w = { 3, 2, 10, 7 }, .lpx = layerpx[o], .lw = 7, .lh = 5, .lx = 3, .ly = 2,
            .bpx = below[o], .bw = G.out[o].W, .bh = G.out[o].H, .lfull = 1, .bfull = 1 };
        CHECK(draw(o, &g[o]));
        CHECK(G.out[o].times.gdrag_px == (long)g[o].bw * g[o].bh + 35);
        pixels(o, &g[o], layerpx[o][0]);
    }
    CHECK(gpu_gdrag_holds(0) && gpu_gdrag_holds(1));
    CHECK(G.out[0].drag.tex[0] != G.out[1].drag.tex[0]);
    CHECK(G.out[0].drag.tex[1] != G.out[1].drag.tex[1]);

    /* Both canvases are unchanged: alternating heads must not turn empty dirty
     * lists into full uploads just because the previous frame used another head. */
    for (int o = 0; o < 2; o++) g[o].bfull = g[o].lfull = 0;
    for (int i = 0; i < 20; i++) {
        int o = i % 2;
        CHECK(draw(o, &g[o]));
        CHECK(G.out[o].times.gdrag_px == 0 && !G.out[o].times.gdrag_full);
        pixels(o, &g[o], layerpx[o][0]);
    }

    /* No-pixel replay used when a head still displays the previous drag while
     * the other head receives newer content. This used to lose its owner. */
    for (int o = 0; o < 2; o++) { g[o].bpx = g[o].lpx = NULL; g[o].bfull = g[o].lfull = 0; }
    for (int i = 0; i < 20; i++) {
        int o = i % 2;
        G.out[o].times = (GpuFrameTimes){ 0 };
        CHECK(draw(o, &g[o]));
        CHECK(G.out[o].times.gdrag_px == 0);
        pixels(o, &g[o], layerpx[o][0]);
    }

    /* Incremental feed on one head must preserve the other's contents and its
     * storage, even when their resolutions and layer sizes are different. */
    layerpx[0][0] = 0xff51b219;
    CHECK(gpu_gdrag_feed(0, 1, layerpx[0], 7, 5, (Rect){ 0, 0, 1, 1 }) == 0);
    CHECK(draw(0, &g[0]));
    CHECK(sample(0, 3, 2) == layerpx[0][0]);
    CHECK(sample(0, 4, 2) == layerpx[0][1]);
    CHECK(draw(1, &g[1]));
    pixels(1, &g[1], layerpx[1][0]);
    for (int i = 0; i < 35; i++) layerpx[0][i] = layerpx[0][0];
    CHECK(gpu_gdrag_feed(0, 1, layerpx[0], 7, 5, (Rect){ 0, 0, 7, 5 }) == 0);
    CHECK(draw(0, &g[0]));
    pixels(0, &g[0], layerpx[0][0]);
    CHECK(gpu_gdrag_feed(1, 1, layerpx[1], 7, 5, (Rect){ 0 }) == 0);
    CHECK(gpu_gdrag_feed(0, 1, layerpx[0], 5, 7, (Rect){ 0 }) == 1);
    CHECK(!draw(0, &g[0])); /* mismatching cached dimensions cannot be replayed */
    CHECK(draw(1, &g[1]));
    pixels(1, &g[1], layerpx[1][0]);

    /* Tearing down an output cannot invalidate another. Re-created storage
     * must request a complete feed, not treat old pixels as current. */
    gdrag_release(&G.out[0].drag);
    CHECK(!gpu_gdrag_holds(0) && gpu_gdrag_holds(1));
    CHECK(gpu_gdrag_feed(0, 1, layerpx[0], 7, 5, (Rect){ 0 }) == 1);
    CHECK(!gpu_gdrag_holds(0));
    gdrag_release(&G.out[0].drag);
    gdrag_release(&G.out[1].drag);
    CHECK(!gpu_gdrag_holds(0) && !gpu_gdrag_holds(1));
    gdrag_teardown();
    CHECK(glGetError() == GL_NO_ERROR);
    eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(G.dpy, surface);
    eglDestroyContext(G.dpy, G.ctx);
    eglTerminate(G.dpy);
    printf("gpu-multioutput: %d checks, independent uploads and replay pixels passed\n", checks);
    return 0;
}

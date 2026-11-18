/* Wire integration against the complete compositor objects. The production
 * wl.c replaces its object so this test can inspect state after real requests.
 * --wrap=main selects this entry point, without starting the desktop/main loop.
 * wl_start opens only its own temporary runtime; no GPU/KMS/input is initialized. */
#include "../src/wl/wl.c"
#include <wayland-client.h>
#include <poll.h>
#include <assert.h>
#include <sys/socket.h>

typedef struct {
    struct wl_display *display;
    struct wl_client *peer;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_callback *callbacks[32];
    int frames;
} WireClient;
typedef struct {
    struct wl_buffer *buffer;
    uint32_t *px;
    size_t size;
    int releases;
} WireBuffer;
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "wire FAIL line %d: %s\n", __LINE__, #x); abort(); } } while (0)

static void barrier_done(void *data, struct wl_callback *cb, uint32_t serial) {
    (void)serial;
    *(int *)data = 1;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener barrier_listener = { .done = barrier_done };

/* One thread pumps both real socket endpoints; the sync reply orders all
 * preceding client requests and events without timing-dependent sleeps. */
static int wire_sync(WireClient *c) {
    int done = 0;
    struct wl_callback *cb = wl_display_sync(c->display);
    CHECK(cb && wl_callback_add_listener(cb, &barrier_listener, &done) == 0);
    if (wl_display_flush(c->display) < 0 && errno != EAGAIN) { wl_callback_destroy(cb); return -1; }
    for (int i = 0; i < 1000 && !done; i++) {
        CHECK(wl_event_loop_dispatch(loop, 0) >= 0);
        wl_display_flush_clients(dpy);
        CHECK(wl_display_dispatch_pending(c->display) >= 0);
        if (done) break;
        struct pollfd fd = { .fd = wl_display_get_fd(c->display), .events = POLLIN };
        int ready = poll(&fd, 1, 5);
        CHECK(ready >= 0);
        if (ready && wl_display_dispatch(c->display) < 0) { wl_callback_destroy(cb); return -1; }
        if (wl_display_flush(c->display) < 0 && errno != EAGAIN) { wl_callback_destroy(cb); return -1; }
    }
    CHECK(done);
    return 0;
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    WireClient *c = data;
    if (!strcmp(interface, "wl_compositor")) c->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 6 ? version : 6);
    else if (!strcmp(interface, "wl_subcompositor")) c->subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(interface, "wl_shm")) c->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
}
static void registry_removed(void *data, struct wl_registry *registry, uint32_t name) { (void)data; (void)registry; (void)name; }
static const struct wl_registry_listener registry_listener = { .global = registry_global, .global_remove = registry_removed };

static void wire_connect(WireClient *c) {
    memset(c, 0, sizeof *c);
    int sockets[2]; CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
    c->peer = wl_client_create(dpy, sockets[0]); CHECK(c->peer);
    c->display = wl_display_connect_to_fd(sockets[1]); CHECK(c->display);
    c->registry = wl_display_get_registry(c->display); CHECK(c->registry);
    CHECK(wl_registry_add_listener(c->registry, &registry_listener, c) == 0);
    CHECK(wire_sync(c) == 0 && wire_sync(c) == 0);
    CHECK(c->compositor && c->subcompositor && c->shm);
}
static void wire_disconnect(WireClient *c) {
    for (int i = 0; i < 32; i++) if (c->callbacks[i]) wl_callback_destroy(c->callbacks[i]);
    wl_display_disconnect(c->display);
    CHECK(wl_event_loop_dispatch(loop, 0) >= 0);
}
static Surf *wire_surface(WireClient *c, struct wl_surface *surface) {
    uint32_t id = wl_proxy_get_id((struct wl_proxy *)surface);
    for (int i = 0; i < MAX_SURF; i++)
        if (surfs[i] && surfs[i]->client == c->peer && wl_resource_get_id(surfs[i]->res) == id) return surfs[i];
    CHECK(0);
    return NULL;
}
static void buffer_released(void *data, struct wl_buffer *buffer) { (void)buffer; ((WireBuffer *)data)->releases++; }
static const struct wl_buffer_listener buffer_listener = { .release = buffer_released };
static void wire_buffer(WireClient *c, WireBuffer *b, int w, int h, uint32_t color) {
    memset(b, 0, sizeof *b);
    int fd = memfd_create("lordos-wire-test", MFD_CLOEXEC); CHECK(fd >= 0);
    b->size = (size_t)w * h * 4;
    CHECK(ftruncate(fd, (off_t)b->size) == 0);
    b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); CHECK(b->px != MAP_FAILED);
    for (int i = 0; i < w * h; i++) b->px[i] = color;
    struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, (int)b->size); CHECK(pool);
    b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888); CHECK(b->buffer);
    CHECK(wl_buffer_add_listener(b->buffer, &buffer_listener, b) == 0);
    wl_shm_pool_destroy(pool);
    close(fd);
}
static void wire_buffer_destroy(WireBuffer *b) {
    if (b->buffer) wl_buffer_destroy(b->buffer);
    CHECK(munmap(b->px, b->size) == 0);
    b->buffer = NULL;
}
static void frame_done(void *data, struct wl_callback *cb, uint32_t stamp) {
    (void)stamp;
    WireClient *c = data;
    for (int i = 0; i < 32; i++) if (c->callbacks[i] == cb) c->callbacks[i] = NULL;
    c->frames++;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener frame_listener = { .done = frame_done };
static void wire_frame(WireClient *c, struct wl_surface *surface) {
    for (int i = 0; i < 32; i++) if (!c->callbacks[i]) {
        c->callbacks[i] = wl_surface_frame(surface); CHECK(c->callbacks[i]);
        CHECK(wl_callback_add_listener(c->callbacks[i], &frame_listener, c) == 0);
        return;
    }
    CHECK(0);
}
static uint32_t wire_pixel(Surf *root, int x, int y) {
    uint32_t pixels[8 * 8] = {0};
    Canvas canvas;
    canvas_init(&canvas, pixels, 8, 8, 8);
    paint_tree(&canvas, root, 0, 0, 1, (Rect){0,0,8,8});
    return pixels[y * 8 + x];
}
static void attach_commit(struct wl_surface *surface, WireBuffer *buffer) {
    wl_surface_attach(surface, buffer ? buffer->buffer : NULL, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(surface);
}

static void commits_and_pixels(void) {
    WireClient c;
    wire_connect(&c);
    struct wl_surface *root = wl_compositor_create_surface(c.compositor);
    struct wl_surface *child = wl_compositor_create_surface(c.compositor);
    struct wl_surface *grand = wl_compositor_create_surface(c.compositor);
    struct wl_subsurface *sub = wl_subcompositor_get_subsurface(c.subcompositor, child, root);
    struct wl_subsurface *gsub = wl_subcompositor_get_subsurface(c.subcompositor, grand, child);
    WireBuffer red, blue, yellow, temporary, rotated;
    wire_buffer(&c, &red, 4, 3, 0xffff0000);
    wire_buffer(&c, &blue, 2, 2, 0xff0000ff);
    wire_buffer(&c, &yellow, 1, 1, 0xffffff00);
    wl_subsurface_set_position(sub, 1, 1);
    attach_commit(root, &red);
    CHECK(wire_sync(&c) == 0);
    Surf *r = wire_surface(&c, root), *s = wire_surface(&c, child), *g = wire_surface(&c, grand);
    CHECK(s->sub_sync && s->sx == 1 && s->sy == 1);
    wire_frame(&c, child);
    attach_commit(child, &blue);
    CHECK(wire_sync(&c) == 0);
    CHECK(s->cached && !s->content && blue.releases == 1);
    CHECK(wire_pixel(r, 1, 1) == 0xffff0000);
    wl_frame_done(10); CHECK(wire_sync(&c) == 0 && c.frames == 0);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0);
    CHECK(!s->cached && wire_pixel(r, 1, 1) == 0xff0000ff);
    wl_frame_done(20); CHECK(wire_sync(&c) == 0 && c.frames == 1);
    blue.px[0] = 0xff00ff00;
    wl_surface_attach(child, blue.buffer, 0, 0);
    wl_surface_damage_buffer(child, 0, 0, 1, 1);
    wl_surface_commit(child); CHECK(wire_sync(&c) == 0);
    CHECK(wire_pixel(r, 1, 1) == 0xff0000ff);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0);
    CHECK(wire_pixel(r, 1, 1) == 0xff00ff00 && wire_pixel(r, 2, 1) == 0xff0000ff);
    int frames_before_batch = c.frames;
    wire_frame(&c, child); attach_commit(child, &blue); CHECK(wire_sync(&c) == 0);
    for (int i = 0; i < 4; i++) blue.px[i] = 0xff00ff00;
    wire_frame(&c, child); attach_commit(child, &blue); CHECK(wire_sync(&c) == 0);
    wl_frame_done(21); CHECK(wire_sync(&c) == 0 && c.frames == frames_before_batch);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0);
    wl_frame_done(22); CHECK(wire_sync(&c) == 0 && c.frames == frames_before_batch + 2);
    wl_subsurface_set_desync(sub);
    for (int i = 0; i < 4; i++) blue.px[i] = 0xff00ff00;
    attach_commit(child, &blue); CHECK(wire_sync(&c) == 0);
    CHECK(!s->cached && wire_pixel(r, 1, 1) == 0xff00ff00);
    wl_subsurface_set_sync(sub);
    for (int i = 0; i < 4; i++) blue.px[i] = 0xff0000ff;
    attach_commit(child, &blue); CHECK(wire_sync(&c) == 0 && s->cached);
    wl_subsurface_set_desync(sub); CHECK(wire_sync(&c) == 0);
    CHECK(!s->cached && wire_pixel(r, 1, 1) == 0xff0000ff);
    wl_subsurface_set_sync(sub); wl_subsurface_set_desync(gsub);
    attach_commit(grand, &yellow); CHECK(wire_sync(&c) == 0);
    CHECK(g->cached && !g->content && !s->cached);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0);
    CHECK(g->content && !g->cached && wire_pixel(r, 1, 1) == 0xffffff00);
    wl_subsurface_set_position(gsub, 1, 0); wl_surface_commit(child);
    wl_subsurface_set_position(gsub, 0, 1);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0);
    CHECK(g->sx == 1 && g->sy == 0 && wire_pixel(r, 2, 1) == 0xffffff00);
    wl_subsurface_place_below(sub, root);
    CHECK(wire_sync(&c) == 0 && r->nbelow == 0);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0);
    CHECK(r->nbelow == 1 && wire_pixel(r, 1, 1) == 0xffff0000);
    wl_subsurface_place_above(sub, root); wl_surface_commit(root); CHECK(wire_sync(&c) == 0);
    CHECK(r->nbelow == 0 && wire_pixel(r, 1, 1) == 0xff0000ff);
    wl_surface_commit(child); CHECK(wire_sync(&c) == 0 && s->cached && s->cached->nkids == 1);
    wl_subsurface_destroy(gsub); wl_surface_destroy(grand);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0 && s->nkids == 0);
    wire_buffer(&c, &temporary, 2, 2, 0xffffffff);
    attach_commit(child, &temporary); CHECK(wire_sync(&c) == 0 && temporary.releases == 1);
    wire_buffer_destroy(&temporary);
    wl_surface_commit(root); CHECK(wire_sync(&c) == 0 && wire_pixel(r, 1, 1) == 0xffffffff);
    /* Exercise set_buffer_transform through the actual wire dispatcher. */
    struct wl_surface *ts = wl_compositor_create_surface(c.compositor);
    wire_buffer(&c, &rotated, 3, 2, 0);
    for (int i = 0; i < 6; i++) rotated.px[i] = 0xff000000u | (i + 1) * 0x111111u;
    wl_surface_set_buffer_transform(ts, WL_OUTPUT_TRANSFORM_90);
    attach_commit(ts, &rotated); CHECK(wire_sync(&c) == 0);
    Surf *t = wire_surface(&c, ts);
    CHECK(t->lw == 2 && t->lh == 3);
    CHECK(wire_pixel(t, 0, 0) == 0xff444444 && wire_pixel(t, 1, 2) == 0xff333333);
    int frames = c.frames;
    wire_frame(&c, child); wl_surface_commit(child); CHECK(wire_sync(&c) == 0 && s->cached);
    wl_subsurface_destroy(sub); CHECK(wire_sync(&c) == 0);
    wl_frame_done(30); CHECK(wire_sync(&c) == 0 && c.frames == frames && !s->cached);
    struct wl_surface *orphan_parent = wl_compositor_create_surface(c.compositor);
    struct wl_surface *orphan = wl_compositor_create_surface(c.compositor);
    struct wl_subsurface *orphan_sub = wl_subcompositor_get_subsurface(c.subcompositor, orphan, orphan_parent);
    wire_frame(&c, orphan); attach_commit(orphan, &blue); CHECK(wire_sync(&c) == 0);
    Surf *o = wire_surface(&c, orphan);
    CHECK(o->cached && !o->content);
    wl_surface_destroy(orphan_parent); CHECK(wire_sync(&c) == 0 && !o->parent);
    wl_frame_done(31); CHECK(wire_sync(&c) == 0 && c.frames == frames);
    wl_subsurface_destroy(orphan_sub); wl_surface_destroy(orphan);
    wl_surface_destroy(ts); wl_surface_destroy(child); wl_surface_destroy(root);
    wire_buffer_destroy(&red); wire_buffer_destroy(&blue); wire_buffer_destroy(&yellow); wire_buffer_destroy(&rotated);
    CHECK(wire_sync(&c) == 0);
    wire_disconnect(&c);
}

static void protocol_errors(void) {
    for (int kind = 0; kind < 5; kind++) {
        WireClient c;
        wire_connect(&c);
        struct wl_surface *a = wl_compositor_create_surface(c.compositor);
        if (kind == 0) {
            struct wl_surface *b = wl_compositor_create_surface(c.compositor);
            wl_subcompositor_get_subsurface(c.subcompositor, a, b);
            wl_subcompositor_get_subsurface(c.subcompositor, b, a);
        } else if (kind == 1) wl_surface_set_buffer_transform(a, 8);
        else if (kind == 2) {
            struct wl_surface *b = wl_compositor_create_surface(c.compositor);
            struct wl_subsurface *sub = wl_subcompositor_get_subsurface(c.subcompositor, a, b);
            wl_subsurface_place_above(sub, a);
        } else if (kind == 3) {
            struct wl_surface *b = wl_compositor_create_surface(c.compositor);
            wl_subcompositor_get_subsurface(c.subcompositor, a, b);
            wl_subcompositor_get_subsurface(c.subcompositor, a, b);
        } else for (int i = 0; i < 17; i++) {
            struct wl_surface *b = wl_compositor_create_surface(c.compositor);
            wl_subcompositor_get_subsurface(c.subcompositor, b, a);
        }
        CHECK(wire_sync(&c) == -1);
        /* libwayland maps wl_display.no_memory to ENOMEM, rather than EPROTO. */
        if (kind == 4) {
            CHECK(wl_display_get_error(c.display) == ENOMEM);
            wire_disconnect(&c);
            continue;
        }
        CHECK(wl_display_get_error(c.display) == EPROTO);
        const struct wl_interface *interface = NULL;
        uint32_t object = 0;
        uint32_t code = wl_display_get_protocol_error(c.display, &interface, &object);
        CHECK(interface && object);
        const char *name = kind == 1 ? "wl_surface" : kind == 2 ? "wl_subsurface" : "wl_subcompositor";
        CHECK(!strcmp(interface->name, name));
        CHECK(code == (kind == 1 ? WL_SURFACE_ERROR_INVALID_TRANSFORM : 0));
        wire_disconnect(&c);
    }
}

int __wrap_main(int argc, char **argv) {
    (void)argc; (void)argv;
    char runtime[] = "/tmp/lordos-wl-wire.XXXXXX";
    CHECK(mkdtemp(runtime) && chmod(runtime, 0700) == 0);
    CHECK(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
    ui.width = ui.height = 64;
    paint_view = (PaintView){ .k = 1 };
    CHECK(wl_start() == 0 && !gpu_active());
    commits_and_pixels();
    protocol_errors();
    for (int i = 0; i < MAX_SURF; i++) CHECK(!surfs[i]);
    wl_stop();
    CHECK(rmdir(runtime) == 0);
    printf("wl-wire: %d checks, real requests/SHM/releases/callbacks, CPU pixels and protocol errors passed\n", checks);
    return 0;
}

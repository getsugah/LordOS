/* Production SHM copy and damage requests, with an injected padded SHM buffer.
 * Resources and release events use libwayland; no desktop or device is opened. */
#include "../src/wl/wl.c"
#include <assert.h>
#include <sys/socket.h>

static uint32_t source[6 * 4];
static int width = 4, height = 3, accesses;
static uint32_t format = WL_SHM_FORMAT_ARGB8888;
static struct wl_shm_buffer *fake = (struct wl_shm_buffer *)(uintptr_t)1;
static int upload_fail, uploads;
static Rect uploaded;
static int protocol_errors;
Ui ui;
Node *node_get(int id) { (void)id; return NULL; }
void damage_add(Box b) { (void)b; }
static int buffer_releases, frame_events, discarded, import_dma;
static int readbacks;
static int gpu_enabled = 1;
void __wrap_wl_resource_post_event(struct wl_resource *r, uint32_t opcode, ...) {
    const char *name = wl_resource_get_class(r);
    if (!strcmp(name, "wl_buffer")) { assert(opcode == 0); buffer_releases++; }
    else if (!strcmp(name, "wl_callback")) frame_events++;
    else if (!strcmp(name, "wp_presentation_feedback")) { assert(opcode == 2); discarded++; }
}
void __wrap_wl_resource_post_error(struct wl_resource *r, uint32_t code, const char *message, ...) {
    (void)r; (void)message;
    assert(code == WL_SURFACE_ERROR_INVALID_TRANSFORM);
    protocol_errors++;
}
int gpu_active(void) { return gpu_enabled; }
unsigned gpu_generation(void) { return 1; }
int gpu_tex_current(const GpuTex *t) { return t != NULL; }
void gpu_tex_free(GpuTex *t) { (void)t; }
int gpu_tex_read(const GpuTex *t, uint32_t *px, int w, int h) {
    assert(t);
    for (int i = 0; i < w * h; i++) px[i] = 0xffa1b2c3;
    readbacks++;
    return 0;
}
GpuTex *gpu_tex_dmabuf(const GpuDmabuf *d) { (void)d; return import_dma ? (GpuTex *)(uintptr_t)1 : NULL; }
GpuTex *gpu_tex_pixels_damage(GpuTex *t, const uint32_t *px, int w, int h, int opaque, Rect damage) {
    (void)t; (void)px; (void)w; (void)h; (void)opaque;
    uploaded = damage;
    uploads++;
    return upload_fail ? NULL : (GpuTex *)(uintptr_t)1;
}
struct wl_shm_buffer *__wrap_wl_shm_buffer_get(struct wl_resource *r) { return r ? fake : NULL; }
int32_t __wrap_wl_shm_buffer_get_width(struct wl_shm_buffer *b) { (void)b; return width; }
int32_t __wrap_wl_shm_buffer_get_height(struct wl_shm_buffer *b) { (void)b; return height; }
int32_t __wrap_wl_shm_buffer_get_stride(struct wl_shm_buffer *b) { (void)b; return 6 * 4; }
uint32_t __wrap_wl_shm_buffer_get_format(struct wl_shm_buffer *b) { (void)b; return format; }
void *__wrap_wl_shm_buffer_get_data(struct wl_shm_buffer *b) { (void)b; return source; }
void __wrap_wl_shm_buffer_begin_access(struct wl_shm_buffer *b) { (void)b; accesses++; }
void __wrap_wl_shm_buffer_end_access(struct wl_shm_buffer *b) { (void)b; accesses--; }

static void transforms(struct wl_client *client, struct wl_resource *resource) {
    uint32_t raw[] = { 1, 2, 3, 4, 5, 6 };
    static const uint32_t expected[8][6] = {
        {1,2,3,4,5,6}, {4,1,5,2,6,3}, {6,5,4,3,2,1}, {3,6,2,5,1,4},
        {3,2,1,6,5,4}, {1,4,2,5,3,6}, {4,5,6,1,2,3}, {6,3,5,2,4,1}
    };
    Surf s = { .px = raw, .w = 3, .h = 2, .bscale = 1, .p_bscale = 1 };
    wl_resource_set_user_data(resource, &s);
    for (int t = 0; t < 8; t++) {
        srf_transform(client, resource, t);
        assert(s.p_transform == t && s.transform == 0); /* pending, not immediate */
        apply_buffer_state(&s);
        apply_viewport(&s);
        assert(s.lw == (t & 1 ? 2 : 3) && s.lh == (t & 1 ? 3 : 2));
        int w, h;
        const uint32_t *p = surface_pixels(&s, &w, &h);
        assert(p && w == s.lw && h == s.lh);
        for (int i = 0; i < 6; i++) assert(p[i] == expected[t][i]);
        assert(surface_pixels(&s, &w, &h) == p); /* cache reused */
        float x, y, sw, sh;
        source_rect(&s, &x, &y, &sw, &sh);
        assert(x == 0 && y == 0 && sw == w && sh == h);
        s.transform = 0;
    }
    srf_transform(client, resource, -1);
    srf_transform(client, resource, 8);
    assert(protocol_errors == 2 && s.p_transform == 7);
    s.transform = 1;
    s.oriented_dirty = 1;
    raw[0] = 99;
    int w, h;
    assert(surface_pixels(&s, &w, &h)[1] == 99);
    s.bscale = 2;
    s.w = 6; s.h = 4; /* size/viewport tests only; no reads from raw below */
    apply_viewport(&s);
    assert(s.lw == 2 && s.lh == 3);
    s.viewport = resource;
    s.vsx = 0.5f; s.vsy = 1; s.vsw = 1; s.vsh = 2;
    float x, y, sw, sh;
    source_rect(&s, &x, &y, &sw, &sh);
    assert(x == 1 && y == 2 && sw == 2 && sh == 4);
    apply_viewport(&s);
    assert(s.lw == 1 && s.lh == 2);
    s.vdw = 10; s.vdh = 20;
    apply_viewport(&s);
    assert(s.lw == 10 && s.lh == 20);
    s.p_transform = 0;
    apply_buffer_state(&s);
    assert(!s.oriented && !s.transform);
}

static void init_sync_surface(Surf *s, struct wl_client *client) {
    *s = (Surf){ .client = client, .bscale = 1, .p_bscale = 1, .vsw = -1, .p_vsw = -1 };
    s->res = wl_resource_create(client, &wl_surface_interface, 6, 0); assert(s->res);
    wl_resource_set_user_data(s->res, s);
    s->sub = wl_resource_create(client, &wl_subsurface_interface, 1, 0); assert(s->sub);
    wl_resource_set_user_data(s->sub, s);
    wl_list_init(&s->p_buf_destroy.link);
    wl_list_init(&s->p_frames); wl_list_init(&s->frames);
    wl_list_init(&s->p_feedback); wl_list_init(&s->feedback);
    wl_list_init(&s->sent_feedback);
}

static void sync_commits(struct wl_client *client, struct wl_resource *buffer) {
    Surf root, child, grand;
    init_sync_surface(&root, client); init_sync_surface(&child, client); init_sync_surface(&grand, client);
    child.role = grand.role = R_SUB;
    child.parent = &root; grand.parent = &child;
    root.nkids = child.nkids = 1;
    root.kids[0] = root.p_kids[0] = &child;
    child.kids[0] = child.p_kids[0] = &grand;
    child.sub_sync = 1; grand.sub_sync = 0;
    assert(subsurface_synchronized(&child) && subsurface_synchronized(&grand));
    width = 4; height = 3; format = WL_SHM_FORMAT_ARGB8888;
    for (int i = 0; i < 24; i++) source[i] = 0xff010203;
    int released = buffer_releases;
    srf_attach(client, child.res, buffer, 0, 0);
    srf_frame(client, child.res, 0);
    pres_feedback(client, NULL, child.res, 0);
    assert(commit_subsurface_state(&child) == 1);
    assert(child.cached && !child.content && !child.px && wl_list_empty(&child.frames));
    assert(wl_list_length(&child.cached->frames) == 1 && wl_list_length(&child.cached->feedback) == 1);
    assert(buffer_releases == released + 1 && !frame_events); /* copied SHM may be reused */
    /* New requests after commit must not leak into the cached state. */
    srf_scale(client, child.res, 2);
    srf_transform(client, child.res, 1);
    child.p_vp = 1; child.p_vsw = 1; child.p_vsh = 1;
    child.p_vdw = 7; child.p_vdh = 9;
    srf_input(client, child.res, NULL);
    grand.p_sx = 5; grand.p_sy = 6;
    source[0] = 0xffaabbcc; /* the released buffer can change */
    flush_synced_children(&root, 0);
    assert(!child.cached && child.content && child.px[0] == 0xff010203);
    assert(child.bscale == 1 && !child.transform && child.lw == 4 && child.lh == 3);
    assert(child.p_bscale == 2 && child.p_transform == 1 && child.p_vp && child.p_input_set);
    assert(grand.sx == 0 && grand.sy == 0);
    assert(wl_list_length(&child.frames) == 1 && wl_list_length(&child.feedback) == 1);
    WlRgn region = {0};
    assert(wlrgn_push(&region, 0, 1, 2, 3, 4) == 0);
    struct wl_resource *region_resource = wl_resource_create(client, &wl_region_interface, 1, 0);
    assert(region_resource);
    wl_resource_set_user_data(region_resource, &region);
    srf_input(client, child.res, region_resource);
    assert(commit_subsurface_state(&child) == 1);
    srf_input(client, child.res, NULL); /* a newer pending region is independent */
    flush_synced_children(&root, 0);
    assert(child.has_input && wlrgn_has(&child.input, 2, 3) && child.p_input_set);
    wl_resource_destroy(region_resource); wlrgn_free(&region);
    /* Two commits coalesce while callbacks accumulate. */
    srf_frame(client, child.res, 0);
    pres_feedback(client, NULL, child.res, 0);
    assert(commit_subsurface_state(&child) == 1);
    int discarded_before_replace = discarded;
    srf_frame(client, child.res, 0);
    pres_feedback(client, NULL, child.res, 0);
    assert(commit_subsurface_state(&child) == 1);
    assert(discarded == discarded_before_replace + 1 && wl_list_length(&child.cached->feedback) == 1);
    grand.p_sx = 30; /* uncommitted position after the cached parent commit */
    srf_scale(client, child.res, 3); /* likewise uncommitted */
    flush_synced_children(&root, 0);
    assert(child.bscale == 2 && child.p_bscale == 3 && child.transform == 1);
    assert(!child.has_input);
    assert(child.lw == 7 && child.lh == 9 && grand.sx == 5 && grand.sy == 6);
    assert(wl_list_length(&child.frames) == 3 && !frame_events);
    /* An inherited synchronized descendant flushes even if the intermediate
     * surface has no cached commit of its own. */
    srf_attach(client, grand.res, buffer, 0, 0);
    assert(commit_subsurface_state(&grand) == 1 && grand.cached && !grand.content);
    flush_synced_children(&root, 0);
    assert(!grand.cached && grand.content && grand.px[0] == 0xffaabbcc);
    /* Destroyed children must leave no pointers in a cached parent's snapshot.
     * Children created after that snapshot retain their initial position. */
    assert(commit_subsurface_state(&child) == 1);
    remove_subsurface(&grand);
    grand.parent = NULL;
    assert(child.nkids == 0 && child.cached->nkids == 0);
    flush_synced_children(&root, 0);
    assert(commit_subsurface_state(&child) == 1);
    child.nkids = 1; child.kids[0] = child.p_kids[0] = &grand;
    grand.parent = &child; grand.sx = grand.sy = 0; grand.p_sx = 77;
    flush_synced_children(&root, 0);
    assert(child.nkids == 1 && grand.sx == 0);
    sub_sync(client, grand.sub);
    srf_attach(client, grand.res, NULL, 0, 0);
    assert(commit_subsurface_state(&grand) == 1 && grand.content);
    sub_desync(client, grand.sub); /* inherited synchronization still holds */
    assert(grand.cached && grand.content);
    sub_desync(client, child.sub); /* both become independent, detach applies */
    assert(!grand.cached && !grand.content && !subsurface_synchronized(&grand));
    assert(commit_subsurface_state(&grand) == 0); /* ordinary desync path */
    /* A new synchronized child commit is applied immediately by set_desync. */
    sub_sync(client, child.sub);
    srf_attach(client, child.res, NULL, 0, 0);
    assert(commit_subsurface_state(&child) == 1 && child.content);
    sub_desync(client, child.sub);
    assert(!child.cached && !child.content);
    /* Cached callbacks are destroyed/discarded on teardown, never sent done. */
    sub_sync(client, child.sub);
    srf_frame(client, child.res, 0);
    pres_feedback(client, NULL, child.res, 0);
    assert(commit_subsurface_state(&child) == 1);
    int before_discard = discarded;
    sub_cache_destroy(&child);
    assert(discarded == before_discard + 1 && !frame_events);
    /* SHM cache survives wl_buffer destruction; a newer uncommitted attach
     * stays pending when the parent applies the already committed image. */
    struct wl_resource *temporary = wl_resource_create(client, &wl_buffer_interface, 1, 0);
    assert(temporary);
    srf_attach(client, child.res, temporary, 0, 0);
    assert(commit_subsurface_state(&child) == 1);
    wl_resource_destroy(temporary);
    srf_attach(client, child.res, buffer, 0, 0);
    flush_synced_children(&root, 0);
    assert(child.content && child.p_buf == buffer && child.p_attached);
    wl_list_remove(&child.p_buf_destroy.link); wl_list_init(&child.p_buf_destroy.link);
    child.p_buf = NULL; child.p_attached = 0;
    struct wl_resource *r, *tmp;
    wl_resource_for_each_safe(r, tmp, &child.frames) wl_resource_destroy(r);
    wl_resource_for_each_safe(r, tmp, &child.feedback) wl_resource_destroy(r);
    free(child.px); free(grand.px);
    free(child.oriented); free(grand.oriented);
    wlrgn_free(&child.input); wlrgn_free(&child.p_input);
    /* Remove user data before locals expire; these resource objects have no
     * surface destructors because we test the commit machinery in isolation. */
    wl_resource_set_user_data(root.res, NULL); wl_resource_set_user_data(root.sub, NULL);
    wl_resource_set_user_data(child.res, NULL); wl_resource_set_user_data(child.sub, NULL);
    wl_resource_set_user_data(grand.res, NULL); wl_resource_set_user_data(grand.sub, NULL);
}

static Dmabuf *test_dmabuf(struct wl_client *client) {
    Dmabuf *d = calloc(1, sizeof *d); assert(d);
    d->refs = 1;
    d->d = (GpuDmabuf){ .width = 4, .height = 3, .nplanes = 1, .fd = {-1,-1,-1,-1} };
    d->res = wl_resource_create(client, &wl_buffer_interface, 1, 0); assert(d->res);
    wl_resource_set_implementation(d->res, &dmabuf_buffer_impl, d, dmabuf_buffer_gone);
    return d;
}

static void drain_test_releases(void) {
    release_retired_buffers();
}

static void sync_dmabufs(struct wl_client *client) {
    Surf root, child, current;
    init_sync_surface(&root, client); init_sync_surface(&child, client); init_sync_surface(&current, client);
    child.role = R_SUB; child.sub_sync = 1; child.parent = &root;
    root.nkids = 1; root.kids[0] = root.p_kids[0] = &child;
    import_dma = 1;
    Dmabuf *d = test_dmabuf(client);
    take_buffer(&current, d->res);
    assert(d->refs == 2 && current.dmabuf == d);
    srf_attach(client, child.res, d->res, 0, 0);
    assert(commit_subsurface_state(&child) == 1);
    assert(d->refs == 3 && child.cached->dmabuf == d && !child.dmabuf);
    surfs[0] = &current; surfs[1] = &child;
    int before_reads = readbacks;
    wl_gpu_lost();
    assert(readbacks == before_reads + 2 && !d->tex);
    assert(current.px[0] == 0xffa1b2c3 && child.cached->px[0] == 0xffa1b2c3);
    surfs[0] = surfs[1] = NULL;
    int released = buffer_releases;
    take_buffer(&current, NULL);
    drain_test_releases();
    assert(d->refs == 2 && buffer_releases == released); /* cache still owns it */
    flush_synced_children(&root, 0);
    assert(child.dmabuf == d && d->refs == 2 && !child.cached);
    /* Reattaching the same DMA-BUF cannot release its new visible reference. */
    srf_attach(client, child.res, d->res, 0, 0);
    assert(commit_subsurface_state(&child) == 1);
    flush_synced_children(&root, 0);
    drain_test_releases();
    assert(child.dmabuf == d && d->refs == 2 && buffer_releases == released);
    srf_attach(client, child.res, NULL, 0, 0);
    assert(commit_subsurface_state(&child) == 1 && child.dmabuf == d);
    flush_synced_children(&root, 0);
    drain_test_releases();
    assert(!child.dmabuf && !child.content && d->refs == 1 && buffer_releases == released + 1);
    wl_resource_destroy(d->res);
    /* An import failure on one commit must not release a DMA-BUF still owned
     * by another surface, even when this commit cannot use a CPU mapping. */
    d = test_dmabuf(client); d->d.modifier = 17;
    take_buffer(&current, d->res);
    d->tex = NULL; d->no_import = 1; import_dma = 0;
    released = buffer_releases;
    srf_attach(client, child.res, d->res, 0, 0);
    assert(commit_subsurface_state(&child) == 1 && !child.cached->content);
    assert(buffer_releases == released && d->refs == 2);
    flush_synced_children(&root, 0);
    take_buffer(&current, NULL); drain_test_releases();
    assert(buffer_releases == released + 1 && d->refs == 1);
    wl_resource_destroy(d->res);
    import_dma = 1;
    /* The wl_buffer can disappear while the cache retains its DMA-BUF object. */
    d = test_dmabuf(client);
    srf_attach(client, child.res, d->res, 0, 0);
    assert(commit_subsurface_state(&child) == 1 && d->refs == 2);
    wl_resource_destroy(d->res);
    assert(d->refs == 1 && !d->res && child.cached->dmabuf == d);
    flush_synced_children(&root, 0);
    assert(child.dmabuf == d && child.content);
    take_buffer(&child, NULL);
    drain_test_releases(); /* final object/texture ownership ends here */
    /* With the GPU unavailable, an unreadable tiled buffer retains the last
     * good CPU frame in the synchronized cache instead of blanking it. */
    d = test_dmabuf(client); d->d.modifier = 17;
    child.w = 4; child.h = 3; child.content = 1;
    child.px = malloc(12 * sizeof *child.px); assert(child.px);
    for (int i = 0; i < 12; i++) child.px[i] = 0xff991122;
    gpu_enabled = 0;
    srf_attach(client, child.res, d->res, 0, 0);
    assert(commit_subsurface_state(&child) == 1);
    assert(child.cached->content && child.cached->px[0] == 0xff991122);
    flush_synced_children(&root, 0);
    assert(child.content && child.dmabuf == d && child.px[0] == 0xff991122);
    gpu_enabled = 1;
    take_buffer(&child, NULL); drain_test_releases();
    wl_resource_destroy(d->res);
    /* A GPU return before parent commit must use the actual buffer dimensions,
     * not those of the older CPU fallback image seeded into the cache. */
    d = test_dmabuf(client); d->d.modifier = 17; d->d.width = 8; d->d.height = 6;
    child.w = 4; child.h = 3; child.content = 1;
    child.px = calloc(12, sizeof *child.px); assert(child.px);
    gpu_enabled = 0;
    srf_attach(client, child.res, d->res, 0, 0);
    assert(commit_subsurface_state(&child) == 1 && child.cached->w == 4);
    gpu_enabled = 1;
    flush_synced_children(&root, 0);
    assert(child.w == 8 && child.h == 6 && child.lw == 8 && child.lh == 6 && !child.px);
    take_buffer(&child, NULL); drain_test_releases();
    wl_resource_destroy(d->res);
    import_dma = 0;
    wl_resource_set_user_data(root.res, NULL); wl_resource_set_user_data(root.sub, NULL);
    wl_resource_set_user_data(child.res, NULL); wl_resource_set_user_data(child.sub, NULL);
    wl_resource_set_user_data(current.res, NULL); wl_resource_set_user_data(current.sub, NULL);
}

int main(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct wl_display *display = wl_display_create(); assert(display);
    struct wl_client *client = wl_client_create(display, sockets[0]); assert(client);
    struct wl_resource *buffer = wl_resource_create(client, &wl_buffer_interface, 1, 0);
    struct wl_resource *surface = wl_resource_create(client, &wl_surface_interface, 6, 0);
    assert(buffer && surface);
    Surf s = { 0 };
    wl_resource_set_user_data(surface, &s);
    for (int i = 0; i < 24; i++) source[i] = 0xff010203;
    /* Even a tiny initial damage must initialize the complete retained image. */
    srf_damage_buffer(client, surface, 1, 1, 1, 1);
    copy_buffer(&s, buffer);
    assert(s.shm_valid && s.tex_dirty == 1 && s.w == 4 && s.h == 3);
    for (int i = 0; i < 12; i++) assert(s.px[i] == 0xff010203);
    assert(surface_tex(&s) && !s.tex_dirty && uploads == 1);
    assert(uploaded.x0 == 0 && uploaded.y0 == 0 && uploaded.x1 == 4 && uploaded.y1 == 3);
    s.transform = 1;
    int oriented_w, oriented_h;
    assert(surface_pixels(&s, &oriented_w, &oriented_h)[4] == 0xff010203);
    s.p_buffer_damage = (Rect){ 0 };
    for (int i = 0; i < 24; i++) source[i] = 0xffdeadbe;
    source[7] = 0xff112233;
    srf_damage_buffer(client, surface, 1, 1, 1, 1);
    copy_buffer(&s, buffer);
    assert(s.px[5] == 0xff112233 && s.px[0] == 0xff010203 && s.px[11] == 0xff010203);
    assert(surface_pixels(&s, &oriented_w, &oriented_h)[4] == 0xff112233);
    assert(s.tex_dirty == 2 && s.tex_damage.x0 == 1 && s.tex_damage.x1 == 2);
    s.p_buffer_damage = (Rect){ 0 };
    srf_damage_buffer(client, surface, 3, 2, 1, 1);
    copy_buffer(&s, buffer);
    assert(s.px[11] == 0xffdeadbe && s.px[0] == 0xff010203);
    assert(surface_pixels(&s, &oriented_w, &oriented_h)[9] == 0xffdeadbe);
    s.transform = 0;
    assert(s.tex_damage.x0 == 1 && s.tex_damage.y0 == 1 && s.tex_damage.x1 == 4 && s.tex_damage.y1 == 3);
    upload_fail = 1;
    assert(surface_tex(&s) == NULL && s.tex_dirty == 2);
    upload_fail = 0;
    assert(surface_tex(&s) && !s.tex_dirty);
    assert(uploaded.x0 == 1 && uploaded.y0 == 1 && uploaded.x1 == 4 && uploaded.y1 == 3);
    int before = uploads;
    assert(surface_tex(&s) && uploads == before); /* no redundant upload */
    /* Surface-coordinate damage conservatively requests all pixels. */
    srf_damage(client, surface, 0, 0, 1, 1);
    copy_buffer(&s, buffer);
    assert(s.tex_dirty == 1 && s.px[0] == source[0]);
    s.p_surface_damage = 0;
    s.tex_dirty = 0;
    format = WL_SHM_FORMAT_XRGB8888;
    for (int i = 0; i < 24; i++) source[i] = 0x00123456;
    copy_buffer(&s, buffer);
    for (int i = 0; i < 12; i++) assert(s.px[i] == 0xff123456);
    assert(s.tex_dirty == 1);
    width = 5; height = 4;
    copy_buffer(&s, buffer);
    assert(s.w == 5 && s.h == 4 && s.px[19] == 0xff123456);
    s.p_buffer_damage = (Rect){ 0 };
    srf_damage_buffer(client, surface, INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX);
    assert(!gsc_area(s.p_buffer_damage));
    srf_damage_buffer(client, surface, -1, -1, 2, 2);
    assert(gsc_area(s.p_buffer_damage) == 1);
    srf_damage_buffer(client, surface, 0, 0, -1, 2);
    assert(gsc_area(s.p_buffer_damage) == 1);
    copy_buffer(&s, NULL);
    assert(!s.px && !s.shm_valid && !s.content && accesses == 0);
    free(s.oriented);
    transforms(client, surface);
    sync_commits(client, buffer);
    sync_dmabufs(client);
    wl_display_destroy_clients(display);
    wl_display_destroy(display);
    close(sockets[1]);
    puts("wl-shm-damage: copies, transforms, synchronized commits, callbacks and DMA-BUF ownership passed");
    return 0;
}

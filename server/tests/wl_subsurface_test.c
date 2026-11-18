/* Real subsurface request handlers/resources; GPU calls record the production
 * draw_tree order. No compositor, display device or desktop connection. */
#include "../src/wl/wl.c"
#include <assert.h>
#include <sys/socket.h>

Ui ui;
PaintView paint_view = { .k = 1 };
static Node window_node = { .prev_box = { 0, 0, 4, 4 } };
static int damage_count;
Node *node_get(int id) { return id == 1 ? &window_node : NULL; }
void damage_add(Box b) { (void)b; damage_count++; }
static int errors, no_memory, drawn[32], ndrawn;
void __wrap_wl_resource_post_error(struct wl_resource *r, uint32_t code, const char *message, ...) {
    (void)r; (void)message; assert(code == 0); errors++;
}
void __wrap_wl_client_post_no_memory(struct wl_client *c) { (void)c; no_memory++; }
int gpu_active(void) { return 1; }
unsigned gpu_generation(void) { return 1; }
int gpu_tex_current(const GpuTex *t) { return t != NULL; }
void gpu_tex_free(GpuTex *t) { (void)t; }
GpuTex *gpu_tex_dmabuf(const GpuDmabuf *d) { (void)d; return NULL; }
GpuTex *gpu_tex_pixels_damage(GpuTex *t, const uint32_t *px, int w, int h, int opaque, Rect damage) {
    (void)t; (void)px; (void)w; (void)h; (void)opaque; (void)damage; return NULL;
}
void gpu_draw_transform(GpuTex *t, float dx, float dy, float dw, float dh, float sx, float sy,
                        float sw, float sh, Rect clip, float opacity, int transform) {
    (void)dx; (void)dy; (void)dw; (void)dh; (void)sx; (void)sy;
    (void)sw; (void)sh; (void)clip; (void)opacity; (void)transform;
    assert(ndrawn < 32);
    drawn[ndrawn++] = (int)(uintptr_t)t;
}
static void check_draw(Surf *root, const int *expected, int n) {
    ndrawn = 0;
    draw_tree(root, 0, 0, 1, 1, (Rect){0,0,100,100});
    assert(ndrawn == n);
    for (int i = 0; i < n; i++) assert(drawn[i] == expected[i]);
}

int main(void) {
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct wl_display *display = wl_display_create(); assert(display);
    struct wl_client *client = wl_client_create(display, sockets[0]); assert(client);
    struct wl_resource *manager = wl_resource_create(client, &wl_subcompositor_interface, 1, 0); assert(manager);
    Surf root = { .w = 4, .h = 4, .lw = 4, .lh = 4, .content = 1, .tex = (GpuTex *)(uintptr_t)1 };
    Surf children[17] = { 0 }, unrelated = { 0 };
    root.res = wl_resource_create(client, &wl_surface_interface, 6, 0); assert(root.res);
    wl_resource_set_user_data(root.res, &root);
    unrelated.res = wl_resource_create(client, &wl_surface_interface, 6, 0); assert(unrelated.res);
    wl_resource_set_user_data(unrelated.res, &unrelated);
    for (int i = 0; i < 17; i++) {
        children[i] = (Surf){ .w = 4, .h = 4, .lw = 4, .lh = 4, .content = 1, .tex = (GpuTex *)(uintptr_t)(i + 2) };
        children[i].res = wl_resource_create(client, &wl_surface_interface, 6, 0); assert(children[i].res);
        wl_resource_set_user_data(children[i].res, &children[i]);
    }
    for (int i = 0; i < 3; i++) subc_get(client, manager, 0, children[i].res, root.res);
    Surf *a = &children[0], *b = &children[1], *c = &children[2];
    assert(root.nkids == 3 && a->parent == &root && a->sub && a->sub_sync == 1);
    check_draw(&root, (int[]){1,2,3,4}, 4);
    sub_below(client, a->sub, root.res);
    assert(root.nbelow == 0 && root.p_nbelow == 1);
    check_draw(&root, (int[]){1,2,3,4}, 4); /* pending request changes nothing yet */
    apply_subsurface_state(&root);
    check_draw(&root, (int[]){2,1,3,4}, 4);
    float x, y;
    assert(surface_at(&root, 1, 1, &x, &y) == c);
    b->content = c->content = 0;
    assert(surface_at(&root, 1, 1, &x, &y) == &root); /* parent is above a */
    root.has_input = 1; /* empty input region lets the lower child receive input */
    assert(surface_at(&root, 1, 1, &x, &y) == a);
    root.has_input = 0;
    b->content = c->content = 1;
    sub_above(client, a->sub, c->res);
    sub_below(client, c->sub, b->res);
    apply_subsurface_state(&root);
    check_draw(&root, (int[]){1,4,3,2}, 4);
    assert(surface_at(&root, 1, 1, &x, &y) == a);
    sub_position(client, a->sub, -3, 5);
    assert(a->sx == 0 && a->sy == 0);
    apply_subsurface_state(a); /* a child's own commit cannot apply its position */
    assert(a->sx == 0 && a->sy == 0);
    apply_subsurface_state(&root);
    assert(a->sx == -3 && a->sy == 5);
    sub_above(client, a->sub, a->res);
    sub_below(client, a->sub, unrelated.res);
    assert(errors == 2);
    subc_get(client, manager, 0, root.res, a->res); /* ancestor cycle */
    assert(errors == 3 && !root.parent && !root.sub);
    unrelated.role = R_TOPLEVEL;
    subc_get(client, manager, 0, unrelated.res, root.res);
    assert(errors == 4 && unrelated.role == R_TOPLEVEL && !unrelated.sub);
    root.node = 1;
    root.hole_sig = hole_sig(&root);
    ui.dirty = need_frame = damage_count = 0;
    damage_node(&root);
    assert(need_frame && !ui.dirty && !damage_count);
    damage_node_mode(&root, 1);
    assert(ui.dirty && damage_count == 1); /* rebuild retained GPU lists/holes */
    a->sx = INT32_MAX; a->sy = INT32_MIN;
    assert(hole_sig(a) == ((4u * 2654435761u) ^ (4u * 40503u) ^ 0xfffffffbu ^ (1u << 30)));
    a->sx = -3; a->sy = 5;
    /* Removing a child cannot exchange the surviving siblings, even with a
     * different scheduled order. */
    sub_below(client, a->sub, root.res);
    wl_resource_destroy(b->sub);
    assert(root.nkids == 2 && root.kids[0] == c && root.kids[1] == a);
    apply_subsurface_state(&root);
    check_draw(&root, (int[]){2,1,4}, 3);
    for (int i = 3; i < 17; i++) subc_get(client, manager, 0, children[i].res, root.res);
    assert(root.nkids == 16);
    subc_get(client, manager, 0, b->res, root.res);
    assert(no_memory == 1 && !b->sub && !b->parent);
    wl_display_destroy_clients(display); /* includes real sub_gone callbacks */
    wl_display_destroy(display);
    close(sockets[1]);
    puts("wl-subsurface: deferred position/stacking, GPU draw order, hit testing, stable removal, cycles and limits passed");
    return 0;
}

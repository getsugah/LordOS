/* Real grab.c, with rendering/node/benchmark services replaced by inert stubs.
 * No compositor, device, graphical session or system service is started. */
#include "ui/grab.h"
#include "cursor.h"
#include "bench/lbinput.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

Ui ui;
static Node window;
static int layers, checks;
Node *node_get(int id) { return id == 1 ? &window : NULL; }
void node_moved(Node *n) { (void)n; }
void node_invalidate(Node *n) { (void)n; }
void paint_layer_begin(int node, int resize) { (void)node; (void)resize; layers++; }
void paint_layer_end(void) { layers--; }
void lbi_stop(void) {}
int lbi_start(float x, float y, float ms) { (void)x; (void)y; (void)ms; return 0; }
int lbi_fd(void) { return -1; }
int lbi_take(float *x, float *y) { (void)x; (void)y; return 0; }
void lbi_path(float rx, float ry, float ms, double t, float *x, float *y) {
    (void)rx; (void)ry; (void)ms; (void)t; *x = *y = 0;
}
int cursor_for_edges(int edges) { (void)edges; return CUR_DEFAULT; }
#define CHECK(x) do { assert(x); checks++; } while (0)
static const GrabLimits lim = { .min_w = 120, .min_h = 80 };

static void reset(void) {
    grab_cancel();
    CHECK(layers == 0);
    memset(&window, 0, sizeof window);
    window.s.absolute = 1;
    window.x = window.ax = 100; window.y = window.ay = 100;
    window.w = 400; window.h = 300;
    ui.width = 1280; ui.height = 720;
    grab_note_pointer(100, 100, 0);
}
static int finish(void) {
    int node, edges; float dx, dy, dw, dh;
    return grab_finish(&node, &edges, &dx, &dy, &dw, &dh);
}
int main(void) {
    const int edges[] = { 0, 1, 2, 4, 8, 5, 10, 9, 6 };
    for (unsigned e = 0; e < sizeof edges / sizeof *edges; e++) {
        /* Every edge and corner, plus moving: side buttons alone cannot start. */
        const int side_masks[] = { 0, 8, 16, 24 };
        for (unsigned s = 0; s < sizeof side_masks / sizeof *side_masks; s++) {
            reset();
            grab_note_pointer(100, 100, side_masks[s]);
            CHECK(grab_begin(1, edges[e], &lim) == -1);
            CHECK(!grab_active()); CHECK(layers == 0);
            grab_motion(150, 140);
            CHECK(window.s.pos[EDGE_L].unit != UNIT_PX);
        }
        /* All three original buttons and their combinations still behave as before. */
        for (int main = 1; main <= 7; main++) {
            for (int side = 0; side <= 24; side += 8) {
                reset();
                grab_note_pointer(100, 100, main | side);
                CHECK(grab_begin(1, edges[e], &lim) == 0);
                CHECK(grab_active()); CHECK(layers == 1);
                grab_motion(120, 115);
                CHECK(window.s.pos[EDGE_L].unit == UNIT_PX);
                CHECK(finish() == 0);
                /* A side press or release cannot cancel an original-button grab. */
                grab_note_pointer(120, 115, main | 24);
                CHECK(finish() == 0);
                /* The original three-button release policy is unchanged. */
                int remaining = main & (main - 1);
                if (remaining) {
                    grab_note_pointer(120, 115, remaining | side);
                    CHECK(finish() == 0);
                }
                /* Releasing the last original button finishes even with side buttons held. */
                grab_note_pointer(120, 115, side);
                CHECK(finish() == 1); CHECK(!grab_active()); CHECK(layers == 0);
                CHECK(finish() == 0);
            }
        }
    }
    reset();
    CHECK(grab_script_begin(1, &lim, 10, 10, 1000) == 0);
    CHECK(grab_scripted());
    grab_script_tick(0);
    grab_cancel(); CHECK(!grab_active()); CHECK(layers == 0);
    printf("grab-buttons: %d checks, 0 failures\n", checks);
    return 0;
}

/* Test the actual window-request policy in wl.c without starting a compositor.
 * Link-time section GC drops unrelated compositor services. Also round-trip the
 * side-button codes through real libwayland sockets, with no display/device. */
#include "../src/wl/wl.c"
#include <wayland-client.h>
#include <assert.h>
#include <sys/socket.h>

static int test_primary_held, test_received;
static const uint32_t test_codes[] = { BTN_SIDE, BTN_SIDE, BTN_EXTRA, BTN_EXTRA };
int grab_can_begin(void) { return test_primary_held; }
static void test_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                        uint32_t time, uint32_t code, uint32_t state) {
    (void)data; (void)pointer; (void)time;
    assert(test_received < 4);
    assert(code == test_codes[test_received]);
    assert(serial == (uint32_t)test_received + 10);
    assert(state == (test_received % 2 ? WL_POINTER_BUTTON_STATE_RELEASED : WL_POINTER_BUTTON_STATE_PRESSED));
    test_received++;
}
int main(void) {
    struct wl_client *client = (struct wl_client *)(uintptr_t)1;
    Surf surface = { .client = client };
    press_surf = &surface;
    npress = 1; press_serials[0] = 123;
    for (int held = 0; held <= 1; held++) {
        test_primary_held = held;
        for (int b = 0; b <= 4; b++) {
            press_buttons[0] = b;
            assert(window_press_allowed(client, 123) == (held && b <= 2));
        }
    }
    test_primary_held = 1; press_buttons[0] = 0;
    assert(!window_press_allowed(client, 456));
    assert(!window_press_allowed(NULL, 123));
    press_surf = NULL;
    assert(!window_press_allowed(client, 123));
    press_surf = &surface; npress = 0;
    assert(!window_press_allowed(client, 123));

    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct wl_display *server = wl_display_create(); assert(server);
    struct wl_client *peer = wl_client_create(server, sockets[0]); assert(peer);
    struct wl_display *connection = wl_display_connect_to_fd(sockets[1]); assert(connection);
    struct wl_proxy *proxy = wl_proxy_create((struct wl_proxy *)connection, &wl_pointer_interface); assert(proxy);
    struct wl_resource *resource = wl_resource_create(peer, &wl_pointer_interface, 1, wl_proxy_get_id(proxy));
    assert(resource);
    static const struct wl_pointer_listener listener = { .button = test_button };
    assert(wl_pointer_add_listener((struct wl_pointer *)proxy, &listener, NULL) == 0);
    for (int i = 0; i < 4; i++) {
        int dom = i < 2 ? 3 : 4;
        int code = mouse_button_to_evdev(dom);
        assert(code == (i < 2 ? 0x113 : 0x114));
        wl_pointer_send_button(resource, 10 + i, 100, code,
            i % 2 ? WL_POINTER_BUTTON_STATE_RELEASED : WL_POINTER_BUTTON_STATE_PRESSED);
    }
    wl_display_flush_clients(server);
    assert(wl_display_dispatch(connection) >= 0);
    assert(test_received == 4);
    wl_proxy_destroy(proxy);
    wl_display_disconnect(connection);
    wl_display_destroy_clients(server);
    wl_display_destroy(server);
    puts("wl-mousebuttons: window request policy and four real Wayland button events: ok");
    return 0;
}

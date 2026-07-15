/* Test what _flux_parse_key emits for various mouse byte sequences. */
#define FLUX_IMPL
#include "../flux.h"
#include <stdio.h>
#include <string.h>

/* Re-declare the static helper (HACK: we know it exists, just call via include). */
extern int _flux_parse_key(const char *buf, int len, FluxMsg *out);

static const char *evname(FluxMouseEvent e) {
    switch (e) {
        case FLUX_MOUSE_PRESS: return "PRESS";
        case FLUX_MOUSE_RELEASE: return "RELEASE";
        case FLUX_MOUSE_MOVE: return "MOVE";
        case FLUX_MOUSE_WHEEL_UP: return "WHEEL_UP";
        case FLUX_MOUSE_WHEEL_DOWN: return "WHEEL_DOWN";
    }
    return "?";
}

static const char *typename_(FluxMsgType t) {
    switch (t) {
        case MSG_NONE: return "NONE";
        case MSG_KEY: return "KEY";
        case MSG_WINSIZE: return "WINSIZE";
        case MSG_QUIT: return "QUIT";
        case MSG_TICK: return "TICK";
        case MSG_CUSTOM: return "CUSTOM";
        case MSG_PASTE: return "PASTE";
        case MSG_MOUSE: return "MOUSE";
    }
    return "?";
}

static void test(const char *label, const char *bytes, int len) {
    int off = 0;
    int iter = 0;
    printf("\n--- %s (len=%d) ---\n", label, len);
    /* dump bytes in hex */
    printf("  bytes: ");
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == 0x1b) printf("ESC ");
        else if (c >= 0x20 && c < 0x7f) printf("%c ", c);
        else printf("\\x%02x ", c);
    }
    printf("\n");
    while (off < len && iter < 10) {
        FluxMsg m;
        int consumed = _flux_parse_key(bytes + off, len - off, &m);
        if (m.type == MSG_MOUSE) {
            printf("  -> MSG_MOUSE %s @ (%d,%d) btn=%d  consumed=%d\n",
                   evname(m.u.mouse.event), m.u.mouse.x, m.u.mouse.y,
                   m.u.mouse.button, consumed);
        } else if (m.type == MSG_KEY) {
            printf("  -> MSG_KEY '%s'  consumed=%d\n", m.u.key.key, consumed);
        } else {
            printf("  -> %s  consumed=%d\n", typename_(m.type), consumed);
        }
        if (consumed <= 0) break;
        off += consumed;
        iter++;
    }
}

int main(void) {
    /* Left click at col 5, row 3 */
    test("LEFT PRESS @5,3",     "\x1b[<0;5;3M", 9);
    test("LEFT RELEASE @5,3",   "\x1b[<0;5;3m", 9);
    /* Mouse motion (no button) */
    test("MOTION no-button",    "\x1b[<35;10;5M", 11);
    test("MOTION while drag",   "\x1b[<32;10;5M", 11);
    /* Wheel up */
    test("WHEEL UP",            "\x1b[<64;10;5M", 11);
    test("WHEEL DOWN",          "\x1b[<65;10;5M", 11);
    /* Two motion events back to back */
    test("MOTION x2",
         "\x1b[<35;10;5M\x1b[<35;11;6M", 22);
    /* Motion then click */
    test("MOTION then PRESS",
         "\x1b[<35;10;5M\x1b[<0;15;7M", 21);
    /* PARTIAL: motion sequence missing terminator */
    test("PARTIAL no terminator", "\x1b[<35;10;5", 10);
    /* Just ESC alone (rare race condition) */
    test("BARE ESC", "\x1b", 1);
    return 0;
}

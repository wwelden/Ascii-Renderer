/* Phase 5: Doom in the terminal.

   This file is the doomgeneric "platform port" for the ASCII renderer: it
   implements the six DG_* callbacks the engine expects, wiring them onto the
   layers built in phases 0-4:

     - term.c   owns the terminal session (raw mode, alt screen, safe restore)
     - render.c presents a 320x200 RGB Framebuffer as half-block cells,
                diffing frames so cost scales with what moved

   The engine owns the game loop: main() hands control to doomgeneric, which
   calls DG_DrawFrame() once per rendered frame and DG_GetKey() once per tic
   drain. We never re-enter the engine. */

/* Feature-test macros, before any include: clock_gettime/nanosleep under
   strict -std=c11. _DEFAULT_SOURCE covers glibc; _DARWIN_C_SOURCE macOS. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#include "framebuffer.h"
#include "render.h"
#include "term.h"

#define DOOM_W DOOMGENERIC_RESX /* 320 (set in the Makefile) */
#define DOOM_H DOOMGENERIC_RESY /* 200 */

#define TARGET_FPS 35 /* vanilla Doom's tic rate */
#define NS_PER_SEC 1000000000LL

/* --- Time ---------------------------------------------------------------- */

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

uint32_t DG_GetTicksMs(void) {
    return (uint32_t)(now_ns() / 1000000LL);
}

void DG_SleepMs(uint32_t ms) {
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* --- Video --------------------------------------------------------------- */

static PixelRenderer *g_renderer;
static Framebuffer *g_native; /* the engine's 320x200 frame as RGB */
static Framebuffer *g_scaled; /* scaled-to-terminal copy (NULL when native) */
static int g_out_w, g_out_h;  /* current on-screen pixel size */
static int64_t g_next_frame_ns;
static int g_session_open; /* terminal session entered? (deferred) */

static int64_t g_first_frame_ns;
static long g_frames;
static int64_t g_bytes;

/* Runs at exit, after term_shutdown() has restored the screen (atexit is
   LIFO and term_init registers its handler later than us). */
static void print_stats(void) {
    if (g_frames <= 0) {
        return;
    }
    double elapsed = (double)(now_ns() - g_first_frame_ns) / NS_PER_SEC;
    fprintf(stderr, "rendered %ld frames in %.2fs = %.1f fps, %.0f bytes/frame\n",
            g_frames, elapsed, elapsed > 0.0 ? g_frames / elapsed : 0.0,
            g_frames > 0 ? (double)g_bytes / g_frames : 0.0);
}

/* Pick the on-screen pixel size for the current terminal: the largest
   width x height that fits cols x (2*rows) cells-as-pixels while preserving
   Doom's 320:200 aspect. Integer multiples when upscaling (crisp), fractional
   only when shrinking to fit. */
static void compute_output_size(void) {
    TermSize ts = term_size();
    double avail_w = ts.cols;
    double avail_h = ts.rows * 2.0;
    double scale = avail_w / DOOM_W;
    if (avail_h / DOOM_H < scale) {
        scale = avail_h / DOOM_H;
    }
    if (scale >= 1.0) {
        scale = (double)(int)scale; /* floor to an integer multiple */
    }
    if (scale <= 0.0) {
        scale = 0.1; /* degenerate terminal; render something */
    }
    g_out_w = (int)(DOOM_W * scale);
    g_out_h = (int)(DOOM_H * scale);
    if (g_out_w < 1) {
        g_out_w = 1;
    }
    if (g_out_h < 1) {
        g_out_h = 1;
    }
}

/* Make g_scaled match g_out_w x g_out_h, or free it when output is native. */
static void ensure_scaled_buffer(void) {
    if (g_out_w == DOOM_W && g_out_h == DOOM_H) {
        framebuffer_destroy(g_scaled);
        g_scaled = NULL;
        return;
    }
    if (g_scaled != NULL && g_scaled->width == g_out_w &&
        g_scaled->height == g_out_h) {
        return;
    }
    framebuffer_destroy(g_scaled);
    g_scaled = framebuffer_create(g_out_w, g_out_h);
    /* On OOM fall back to native; render_frame will clip via the terminal. */
    if (g_scaled == NULL) {
        g_out_w = DOOM_W;
        g_out_h = DOOM_H;
    }
}

/* Nearest-neighbor rescale of the native frame into g_scaled. Runs every
   frame, so it writes into the preallocated buffer -- no allocation here. */
static void rescale_native(void) {
    Framebuffer *dst = g_scaled;
    for (int y = 0; y < dst->height; ++y) {
        int sy = (int)((int64_t)y * DOOM_H / dst->height);
        const Color *src_row = &g_native->pixels[(size_t)sy * DOOM_W];
        Color *dst_row = &dst->pixels[(size_t)y * dst->width];
        for (int x = 0; x < dst->width; ++x) {
            int sx = (int)((int64_t)x * DOOM_W / dst->width);
            dst_row[x] = src_row[sx];
        }
    }
}

void DG_Init(void) {
    /* Allocate only; the terminal session is entered on the first frame so
       the engine's startup log (WAD detection, version banner, errors) still
       lands on the normal screen where the user can read it. */
    g_native = framebuffer_create(DOOM_W, DOOM_H);
    g_renderer = pixel_renderer_create(DOOM_W, DOOM_H);
    if (g_native == NULL || g_renderer == NULL) {
        fprintf(stderr, "error: failed to allocate renderer\n");
        exit(1);
    }
    atexit(print_stats);
}

void DG_DrawFrame(void) {
    if (!g_session_open) {
        if (term_init() == -1) {
            fprintf(stderr, "error: stdin/stdout must be a terminal\n");
            exit(1);
        }
        g_session_open = 1;
        compute_output_size();
        g_next_frame_ns = now_ns();
        g_first_frame_ns = g_next_frame_ns;
    }

    if (term_resized()) {
        compute_output_size();
        /* The old image may be larger than the new grid: clear our slate. */
        fputs("\x1b[2J", stdout);
        pixel_renderer_mark_dirty(g_renderer);
    }
    ensure_scaled_buffer();

    /* Engine pixels are 32-bit 0x00RRGGBB words (rgba8888 mode, scaling 1). */
    const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;
    Color *dst = g_native->pixels;
    for (size_t i = 0; i < (size_t)DOOM_W * DOOM_H; ++i) {
        uint32_t p = src[i];
        dst[i].r = (unsigned char)(p >> 16);
        dst[i].g = (unsigned char)(p >> 8);
        dst[i].b = (unsigned char)p;
    }

    const Framebuffer *frame = g_native;
    if (g_scaled != NULL) {
        rescale_native();
        frame = g_scaled;
    }
    g_bytes += (int64_t)render_frame(g_renderer, frame, stdout);
    ++g_frames;

    /* Pace at the tic rate. The engine renders as fast as we let it; sleeping
       here caps the whole loop at 35fps without touching engine code. */
    g_next_frame_ns += NS_PER_SEC / TARGET_FPS;
    int64_t remaining = g_next_frame_ns - now_ns();
    if (remaining > 0) {
        struct timespec ts = {(time_t)(remaining / NS_PER_SEC),
                              (long)(remaining % NS_PER_SEC)};
        nanosleep(&ts, NULL);
    } else {
        g_next_frame_ns = now_ns(); /* fell behind: resync, don't spiral */
    }
}

void DG_SetWindowTitle(const char *title) {
    /* OSC 2: set the terminal window/tab title. Harmless if unsupported. */
    printf("\x1b]2;%s\x07", title);
    fflush(stdout);
}

/* --- Keyboard ------------------------------------------------------------ */

/* A terminal delivers key-down bytes only -- there is no key-up event, and a
   held key just autorepeats. Doom needs press/release pairs. The standard
   trick: report a press when a byte arrives, remember when each key was last
   seen, and synthesize the release once the byte stream for that key has been
   quiet for longer than the autorepeat gap. KEY_HOLD_MS must exceed the OS's
   *initial* repeat delay (~400ms on stock macOS) or a held key stutters
   press/release/press before autorepeat kicks in. The cost of the margin:
   every input lingers ~half a second after the finger lifts. */
#define KEY_HOLD_MS 500

#define KEYQUEUE_SIZE 32

static unsigned short g_queue[KEYQUEUE_SIZE]; /* (pressed << 8) | doom key */
static unsigned int g_queue_write;
static unsigned int g_queue_read;

static uint32_t g_last_seen_ms[256];
static unsigned char g_key_down[256];

static void queue_event(int pressed, unsigned char key) {
    g_queue[g_queue_write % KEYQUEUE_SIZE] =
        (unsigned short)(((pressed & 1) << 8) | key);
    ++g_queue_write;
}

static void key_seen(unsigned char key, uint32_t now_ms) {
    if (!g_key_down[key]) {
        g_key_down[key] = 1;
        queue_event(1, key);
    }
    g_last_seen_ms[key] = now_ms;
}

/* Remap a plain terminal byte to the doom keycode for our control scheme.
   Vanilla binds fire/strafe/run to Ctrl/Alt/Shift, which a raw tty cannot
   deliver as bytes, so gameplay moves to WASD+EF: w/s forward/back, a/d
   strafe, e use, f fire. Turning stays on the real arrow keys. Returning 0
   leaves the byte as-is, so menu prompts ('y'/'n'), weapon numbers, and
   space-as-use keep their vanilla meaning. The letters consumed here no
   longer reach the engine as themselves -- cheat codes spelled with them
   (idkfa, iddqd) are the price of the scheme. */
static unsigned char remap_gameplay_key(unsigned char ch) {
    switch (ch) {
    case 'w': return KEY_UPARROW;
    case 's': return KEY_DOWNARROW;
    case 'a': return KEY_STRAFE_L;
    case 'd': return KEY_STRAFE_R;
    case 'e': return KEY_USE;
    case 'f': return KEY_FIRE;
    default:  return 0;
    }
}

/* Translate one non-escape byte from the tty into a doom keycode. */
static unsigned char map_byte(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        ch = (unsigned char)(ch - 'A' + 'a'); /* engine expects lowercase */
    }
    switch (ch) {
    case '\r':
    case '\n':
        return KEY_ENTER;
    case '\t':
        return KEY_TAB;
    case 0x7f:
    case '\b':
        return KEY_BACKSPACE;
    default: {
        unsigned char mapped = remap_gameplay_key(ch);
        return mapped != 0 ? mapped : ch;
    }
    }
}

/* Drain every byte currently waiting on the tty, translate to doom key
   presses, and expire held keys that have gone quiet. */
static void pump_input(void) {
    unsigned char buf[64];
    int n = 0;
    while (n < (int)sizeof(buf)) {
        unsigned char byte;
        if (term_read_byte(&byte) != 1) {
            break;
        }
        buf[n++] = byte;
    }

    uint32_t now_ms = DG_GetTicksMs();

    for (int i = 0; i < n; ++i) {
        unsigned char ch = buf[i];
        if (ch == 0x03) { /* Ctrl-C: hard quit; atexit restores the tty */
            exit(0);
        }
        if (ch == 0x1b) {
            /* CSI arrow sequences arrive as one atomic write from the
               terminal, so if this ESC starts one, its tail is already in
               buf. A bare ESC (no '[' behind it) is the Escape key. */
            if (i + 2 < n && buf[i + 1] == '[') {
                unsigned char final = buf[i + 2];
                i += 2;
                switch (final) {
                case 'A': key_seen(KEY_UPARROW, now_ms); break;
                case 'B': key_seen(KEY_DOWNARROW, now_ms); break;
                case 'C': key_seen(KEY_RIGHTARROW, now_ms); break;
                case 'D': key_seen(KEY_LEFTARROW, now_ms); break;
                default: break; /* other CSI: ignore the sequence */
                }
                continue;
            }
            key_seen(KEY_ESCAPE, now_ms);
            continue;
        }
        key_seen(map_byte(ch), now_ms);
    }

    for (int k = 0; k < 256; ++k) {
        if (g_key_down[k] && now_ms - g_last_seen_ms[k] > KEY_HOLD_MS) {
            g_key_down[k] = 0;
            queue_event(0, (unsigned char)k);
        }
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    /* The engine polls keys from the very first TryRunTics(), before any
       frame is drawn. Until DG_DrawFrame() has entered raw mode, the tty is
       still canonical and read() would block on a whole line -- report no
       input instead of touching stdin. */
    if (!g_session_open) {
        return 0;
    }
    /* The engine calls this in a drain loop once per tic; refill the queue
       from the tty on entry, then hand events back one at a time. */
    if (g_queue_read == g_queue_write) {
        pump_input();
    }
    if (g_queue_read == g_queue_write) {
        return 0;
    }
    unsigned short ev = g_queue[g_queue_read % KEYQUEUE_SIZE];
    ++g_queue_read;
    *pressed = ev >> 8;
    *doomKey = ev & 0xFF;
    return 1;
}

/* --- Entry point --------------------------------------------------------- */

int main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    for (;;) {
        doomgeneric_Tick();
    }
    return 0;
}

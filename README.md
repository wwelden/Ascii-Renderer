# ASCII Renderer

A terminal renderer in C, built up in phases toward running Doom in a terminal.
Currently: a raw-mode terminal session with signal-safe cleanup, plus a small
canvas library for drawing to a character grid.

## Requirements

- A C11 compiler (`cc`, `clang`, or `gcc`)
- `make`

## Building

```bash
make            # debug build (default): -O0 -g + AddressSanitizer/UBSan
make BUILD=release   # optimized build, no sanitizers
```

The binary is written to `build/ascii-renderer`.

## Running

```bash
make run             # or ./build/ascii-renderer
```

The default program is the Phase 0 milestone: it switches to the alternate
screen buffer, enters raw mode, hides the cursor, and draws a border sized to
the terminal. Resizing the window redraws the border; `q` (or Ctrl-C) quits.
The original terminal state is restored on any exit path — normal quit,
`exit()`, or a fatal signal (SIGINT/SIGTERM/SIGSEGV/…).

```bash
./build/ascii-renderer --gradient   # canvas demo: truecolor radial gradient
```

## Cleaning

```bash
make clean
```

## Project layout

```
.
├── include/        # public headers
│   ├── canvas.h
│   └── term.h
├── src/            # source files
│   ├── canvas.c    # canvas / drawing primitives
│   ├── term.c      # terminal lifecycle: raw mode, alt screen, resize, cleanup
│   └── main.c      # entry point: border milestone + --gradient demo
├── Makefile
└── README.md
```

## The terminal API

`term.h` manages the terminal session:

| Function         | Description                                              |
| ---------------- | -------------------------------------------------------- |
| `term_init`      | Raw mode + alternate screen + hidden cursor; registers `atexit` and fatal-signal handlers |
| `term_shutdown`  | Restore the original terminal state (also runs automatically at exit) |
| `term_size`      | Current size via `ioctl(TIOCGWINSZ)`                     |
| `term_resized`   | Poll-and-clear flag set by `SIGWINCH`                    |
| `term_read_byte` | Read one byte from stdin with a ~100ms timeout           |

Design notes (for the eventual "design" section):

- **Signal-safe restore.** The restore path uses only async-signal-safe calls
  (`write` of a single constant escape string, then `tcsetattr`), so the same
  function runs from `atexit` and from SIGSEGV/SIGBUS handlers. Fatal signals
  restore the terminal, reset the handler to `SIG_DFL`, and re-raise so the
  exit status still reflects the signal.
- **Raw mode with `ISIG` off.** Ctrl-C arrives as byte `0x03` and is handled
  as a quit key in the main loop; the SIGINT handler still exists for
  externally sent signals.
- **Poll-style reads.** `VMIN=0, VTIME=1` makes `read()` return within 100ms,
  so the loop notices `SIGWINCH` promptly without threads or `select()`.

## The canvas API

`canvas.h` exposes a minimal drawing surface:

| Function             | Description                                  |
| -------------------- | -------------------------------------------- |
| `canvas_create`      | Allocate a `width x height` canvas           |
| `canvas_destroy`     | Free a canvas                                |
| `canvas_clear`       | Fill the whole canvas with one character     |
| `canvas_set` / `_get`  | Write / read a cell's character (bounds-checked) |
| `canvas_set_color`     | Set a cell's RGB color                            |
| `canvas_put`           | Set a cell's character and color at once          |
| `canvas_draw_line`     | Draw a line (Bresenham's algorithm)               |
| `canvas_draw_rect`     | Draw a rectangle outline                          |
| `canvas_fill_rect`     | Draw a filled rectangle                           |
| `canvas_ramp_char`     | Map an intensity `[0,1]` to a ramp character      |
| `canvas_shade`         | Shade a cell from an intensity `[0,1]`            |
| `canvas_render`        | Print the canvas as plain text                    |
| `canvas_render_color`  | Print the canvas with ANSI truecolor             |

### Brightness ramp

`ascii_ramp` is the classic 70-level ASCII ramp, ordered from dimmest (space)
to densest (`$`). `canvas_ramp_char` and `canvas_shade` map an intensity in
`[0.0, 1.0]` onto it, so brighter values pick denser glyphs. The demo in
`main.c` uses this to draw a radial gradient.

### Color

Every cell also carries a 24-bit RGB `Color` (default white). Build colors with
`color_rgb(r, g, b)`, assign them with `canvas_set_color` or `canvas_put`, and
print with `canvas_render_color`, which emits ANSI 24-bit ("truecolor") escape
codes and resets at the end of each row. Use plain `canvas_render` when you want
uncolored output. Truecolor requires a capable terminal (most modern ones, e.g.
iTerm2, Kitty, Windows Terminal, and recent GNOME Terminal / VS Code). The demo
colors the gradient with a black→red→orange→yellow→white heat palette.

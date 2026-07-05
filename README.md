# ASCII Renderer

A small C library and demo for drawing to a character grid and printing it to
the terminal as ASCII art.

## Requirements

- A C11 compiler (`cc`, `clang`, or `gcc`)
- `make`

## Building

```bash
make            # debug build (default)
make BUILD=release   # optimized build
```

The binary is written to `build/ascii-renderer`.

## Running

```bash
make run
```

or run the binary directly:

```bash
./build/ascii-renderer
```

## Cleaning

```bash
make clean
```

## Project layout

```
.
├── include/        # public headers
│   └── canvas.h
├── src/            # source files
│   ├── canvas.c    # canvas / drawing primitives
│   └── main.c      # demo entry point
├── Makefile
└── README.md
```

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

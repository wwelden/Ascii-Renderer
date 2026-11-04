<img width="3416" height="2220" alt="CleanShot 2026-10-20 at 8 12 19 PM@2x" src="https://github.com/user-attachments/assets/5fa36a07-3040-4591-9944-fb207ff0cafc" />



# ASCII Renderer

A terminal renderer in C, built up in phases toward running Doom in a terminal.
Phase 5 is here: `make doom` builds a doomgeneric port that plays Doom in the
terminal at 35fps through the diffing half-block renderer.

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

## Doom (Phase 5)

```bash
make doom                            # builds build/ascii-doom
./build/ascii-doom -iwad doom1.wad   # any Doom IWAD works; shareware doom1.wad is free
```

The port renders Doom's native 320×200 frame as half-block cells
(320 columns × 100 rows), scaled to fit smaller terminals, paced at the
engine's 35fps tic rate. The terminal is restored on every exit path.

### Controls

| Key | Action |
| --- | --- |
| `W` / `S` (or `↑` / `↓`) | Move forward / back |
| `←` / `→` | Turn left / right |
| `A` / `D` | Strafe left / right |
| `F` | Fire (hold for continuous fire) |
| `E` | Use: open doors, flip switches |
| `1`–`7` | Select weapon |
| `Tab` | Toggle automap |
| `Esc` | Open / close the menu (pauses the game) |
| `↑` / `↓` + `Enter` | Navigate and select in menus |
| `Y` / `N` | Answer menu prompts (e.g. "quit game?") |
| `Ctrl-C` | Force-quit immediately |

There is no mouse support: the port reads keyboard bytes only, so aim by
turning with the arrow keys. Your scroll wheel may also be sent as arrow
keys in the alternate screen, which walks you forward or back, so leave it
alone while playing.

Why the layout differs from classic Doom: vanilla binds fire to Ctrl, strafe
to Alt, and run to Shift, and a raw terminal can't report those modifiers on
their own. Fire, use, and strafe move to `F`, `E`, and `A`/`D` instead, and
there is no run key. Space does nothing. Cheat codes containing `w`, `a`,
`s`, `d`, `e`, or `f` (e.g. `iddqd`) can't be typed because those letters
are game controls.

To start: `Esc` → New Game → pick an episode → pick a skill, each with
`Enter`. Or skip the menus with `./build/ascii-doom -iwad doom1.wad -warp 1 1`.

Design notes (Phase 5):

- **The engine owns the loop.** doomgeneric inverts the phase 3 structure:
  instead of our loop calling a `game_step()`, Doom calls our six `DG_*`
  callbacks. The whole port is `src/doom/doomgeneric_ascii.c`; the renderer is
  reused unmodified through the `render_frame()` API built in phase 4.
- **Deferred session entry.** Raw mode + alternate screen are entered on the
  *first frame*, not at startup, so the engine's boot log and fatal errors
  (missing WAD, bad args) stay readable on the normal screen. Lesson learned:
  the engine polls `DG_GetKey` before the first frame, so key reads must
  report "no input" until the session is open — the tty is still canonical,
  and a blocking `read()` there hangs the boot.
- **Key-up synthesis.** A tty delivers key-down bytes only. Presses are
  reported when a byte arrives; releases are synthesized once the key's byte
  stream goes quiet. A freshly pressed key waits ~500ms (longer than the OS
  initial autorepeat delay, or held keys stutter); once autorepeat is
  flowing, ~150ms of silence means the key was let go, so held turns stop
  close to where you release them. A byte that arrives after a gap too long
  for autorepeat counts as a new tap, so quick repeated presses (menu
  navigation, single shots) aren't swallowed.

## Running in the browser (WebAssembly)

The same C code compiles to WebAssembly with [Emscripten](https://emscripten.org)
and runs inside an [xterm.js](https://xtermjs.org) terminal on a static page,
with a small fake shell in front of it (`help`, `snake`, `plasma`, ...). The
shell ships a read-only replica of this repository, so `ls -la`, `cd`, `cat`,
and tab completion all work — along with a handful of unlisted commands left
as easter eggs.

```bash
make web      # needs emcc on PATH; writes web/public/ascii-renderer.{js,wasm}
make web-doom # Doom too: web/public/ascii-doom.{js,wasm,data} (needs doom1.wad)
make serve    # http://localhost:8000
```

`node web/smoke.mjs` (needs `playwright`) runs every demo in headless Chromium
and checks it draws and exits; CI runs it on each push.

Or `bash web/build.sh`, which installs a pinned Emscripten SDK into `.emsdk/`
first if `emcc` is missing.

How it works:

- `web/term_web.c` replaces `src/term.c`, implementing `term.h` on hooks the
  page provides (terminal size, resize flag, an input byte queue). Nothing
  else in `src/` changes beyond one `#ifdef` in `sleep_ns()`.
- The game loops stay as written: `-sASYNCIFY` lets `emscripten_sleep()`
  yield to the browser each frame.
- stdout bytes go straight into `term.write()`; xterm.js handles the
  truecolor SGR and the `▀` half-blocks.
- Doom is the same Phase 5 port (`src/doom/doomgeneric_ascii.c`) compiled
  with the engine into its own module; the two `nanosleep` pacing sites gain
  an `emscripten_sleep()` path, and the shareware `doom1.wad` is baked into
  `ascii-doom.data` and mounted at `/doom1.wad` by Emscripten's preloaded FS.
  `web/build.sh` downloads the WAD (pinned by SHA-256) when it's not on disk.

### Deploying to Vercel

Import the repo in Vercel; `vercel.json` takes care of the rest (build
command `bash web/build.sh`, output directory `web/public`, no framework).
The first build spends a minute or two installing the Emscripten SDK.

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
├── web/
│   ├── term_web.c  # term.h for the browser (replaces src/term.c)
│   ├── build.sh    # Vercel / local build script for the wasm site
│   └── public/     # static site: index.html, main.js (xterm.js host), style.css
├── vercel.json
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

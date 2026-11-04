/* Browser host for the WebAssembly build of ascii-renderer.

   An xterm.js terminal runs a tiny fake shell. Running a demo instantiates a
   fresh copy of the wasm module (createAsciiRenderer, from ascii-renderer.js)
   and wires it to the terminal:

     stdout bytes  -> batched, then term.write()
     keystrokes    -> byte queue drained by term_read_byte() (web/term_web.c)
     size/resize   -> term.cols / term.rows and a one-shot resize flag

   A fresh instance per run means every demo starts from clean C globals. */

(() => {
  "use strict";

  // The prompt tracks the fake shell's working directory (see cwd below).
  let promptPath = "~";
  const PROMPT = () =>
    "\x1b[1;32mguest@ascii-renderer\x1b[0m:\x1b[1;34m" + promptPath + "\x1b[0m$ ";

  const term = new Terminal({
    cursorBlink: true,
    convertEol: true, // like a cooked tty's ONLCR, for the shell and exit stats
    fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace',
    fontSize: 14,
    theme: { background: "#000000" },
    allowProposedApi: false,
  });
  const fit = new FitAddon.FitAddon();
  term.loadAddon(fit);
  term.open(document.getElementById("terminal"));
  fit.fit();

  // Renderer choice. xterm's default DOM renderer builds a styled element per
  // cell, which tops out around 10-15 fps on the plasma demo (every cell
  // changes color every frame); parsing the ANSI is ~50x faster than that, so
  // drawing is the bottleneck. The WebGL renderer is far faster on a real
  // GPU, but slower than DOM when WebGL is software-emulated (SwiftShader,
  // llvmpipe), so only use it when the GPU looks real.
  function hasHardwareWebgl() {
    try {
      const gl = document.createElement("canvas").getContext("webgl2");
      if (!gl) return false;
      const info = gl.getExtension("WEBGL_debug_renderer_info");
      const name = info ? String(gl.getParameter(info.UNMASKED_RENDERER_WEBGL)) : "";
      return !/swiftshader|llvmpipe|software|basic render/i.test(name);
    } catch {
      return false;
    }
  }

  let rendererName = "dom";
  if (window.WebglAddon && hasHardwareWebgl()) {
    try {
      const webgl = new WebglAddon.WebglAddon();
      webgl.onContextLoss(() => webgl.dispose()); // falls back to DOM
      term.loadAddon(webgl);
      rendererName = "webgl";
    } catch {
      rendererName = "dom";
    }
  }
  document.documentElement.dataset.renderer = rendererName;

  // ---------------------------------------------------------------- programs

  // `factory` names the global Emscripten module factory; demos share one
  // binary (createAsciiRenderer), doom is its own (createAsciiDoom, which
  // also pulls in ascii-doom.data, the preloaded shareware IWAD).
  const PROGRAMS = {
    doom:    { args: ["-iwad", "/doom1.wad"], factory: "createAsciiDoom",
               about: "DOOM (1993, shareware) — arrows turn/move, wasd move/strafe, f fire, e use, Enter/Esc menus" },
    snake:   { args: ["--snake"],   about: "play snake (wasd / arrows, r restarts, q quits)" },
    plasma:  { args: ["--pixels"],  about: "full-screen truecolor plasma via render_frame() — the diff's worst case" },
    animate: { args: ["--animate"], about: "bouncing box over a gradient, with a live fps HUD" },
    border:  { args: [],            about: "the phase 0 milestone: a border that tracks resizes" },
  };

  const FLAG_TO_PROGRAM = {
    "--snake": "snake", "--pixels": "plasma", "--animate": "animate", "": "border",
  };

  let running = false;
  let input = [];        // queued input bytes for the running program
  let resized = false;   // one-shot flag read by term_resized()

  // Batch stdout bytes and hand them to xterm once per event-loop turn. The
  // program yields every frame (emscripten_sleep), so this flushes per frame.
  let outBuf = new Uint8Array(1 << 16);
  let outLen = 0;
  let flushScheduled = false;

  function flushOut() {
    flushScheduled = false;
    if (outLen === 0) return;
    term.write(outBuf.slice(0, outLen));
    outLen = 0;
  }

  function pushOut(byte) {
    if (byte === null || byte === undefined) return;
    if (outLen === outBuf.length) {
      const bigger = new Uint8Array(outBuf.length * 2);
      bigger.set(outBuf);
      outBuf = bigger;
    }
    outBuf[outLen++] = byte & 0xff; // older Emscripten passes signed HEAP8 values
    if (!flushScheduled) {
      flushScheduled = true;
      setTimeout(flushOut, 0);
    }
  }

  function setButtonsDisabled(disabled) {
    document.querySelectorAll("nav button").forEach((b) => { b.disabled = disabled; });
  }

  function runProgram(name) {
    const prog = PROGRAMS[name];
    const factory = window[prog.factory || "createAsciiRenderer"];
    if (typeof factory !== "function") {
      term.writeln(name + ": module not loaded (" + (prog.factory || "createAsciiRenderer") + " missing)");
      term.write(PROMPT);
      return;
    }
    running = true;
    input = [];
    resized = false;
    setButtonsDisabled(true);

    let finished = false;
    const finish = () => {
      if (finished) return;
      finished = true;
      flushOut();
      running = false;
      setButtonsDisabled(false);
      term.write("\r\n" + PROMPT());
      term.focus();
    };

    factory({
      arguments: prog.args.slice(),
      stdin: () => null,
      stdout: pushOut,
      stderr: pushOut,
      termCols: () => term.cols,
      termRows: () => term.rows,
      termTakeResized: () => { const r = resized; resized = false; return r; },
      termReadByte: () => (input.length ? input.shift() : -1),
      onExit: finish,
      onAbort: (what) => {
        flushOut();
        term.write("\x1b[0m\x1b[?25h\x1b[?1049l\r\n\x1b[31mprogram aborted: " + what + "\x1b[0m");
        finish();
      },
    }).catch((err) => {
      // An ExitStatus from exit() is a normal termination, not an error.
      if (!(err && err.name === "ExitStatus")) {
        term.write("\r\n\x1b[31mfailed to start: " + err + "\x1b[0m");
      }
      finish();
    });
  }

  // ------------------------------------------------- fake filesystem (props)
  // Mirrors the real repository — names and byte sizes are the real ones.
  // Pure set dressing for ls / cd / cat / tab completion; nothing is writable.

  const GITHUB = "https://github.com/wwelden/Ascii-Renderer";
  const d = (children) => ({ kind: "dir", children });
  const f = (size, meta) => Object.assign({ kind: "file", size }, meta);
  const src = (size, desc) => f(size, { desc }); // cat prints a one-line summary

  const REPO = d({
    ".git": d({
      HEAD: f(23, { text: "ref: refs/heads/master" }),
    }),
    ".github": d({
      workflows: d({ "build.yml": src(1039, "CI: native build + wasm smoke test") }),
    }),
    ".gitignore": f(378, { text: "build/\n*.o\n*.wad\nweb/public/ascii-renderer.js\nweb/public/ascii-renderer.wasm" }),
    ".plan": f(178, {
      text: [
        "[x] phase 0  border that survives resizes",
        "[x] phase 1  double-buffered diff renderer",
        "[x] phase 2  input + snake",
        "[x] phase 3  half-block truecolor pixels",
        "[x] phase 4  images, zero-alloc hot loop",
        "[x] phase 5  DOOM",
        "[ ] phase 6  ???",
      ].join("\n"),
    }),
    Makefile: f(4768, {
      text: [
        "# the real Makefile is 4.8K; the parts that matter:",
        "all: build/ascii-renderer        # -O0 -g + ASan/UBSan by default",
        "doom: build/ascii-doom           # doomgeneric, 35fps in a terminal",
        "web:  web/public/ascii-renderer.wasm   # what you are running now",
        "",
        "# full version: " + GITHUB,
      ].join("\n"),
    }),
    "README.md": f(9962, { render: "about" }),
    "ascii-doom": { kind: "exec", size: 546862 },
    "ascii-renderer": { kind: "exec", size: 131072 },
    "doom1.wad": src(4196020, "the shareware episode — what 'doom' boots, straight from 1993"),
    include: d({
      "canvas.h": src(2848, "text canvas: glyphs + colors over the framebuffer"),
      "framebuffer.h": src(1920, "double-buffered cell grid, diffed per frame"),
      "render.h": src(4470, "half-block truecolor pixel renderer, 2 px per cell"),
      "snake.h": src(1776, "snake. you know snake."),
      "term.h": src(1438, "the tty seam — the only layer the browser swaps out"),
    }),
    src: d({
      "canvas.c": src(5547, "text canvas implementation"),
      doom: d({
        "doomgeneric_ascii.c": src(15086, "doomgeneric port: DOOM frames -> half-block cells"),
      }),
      "framebuffer.c": src(4789, "frame diffing: emit ANSI only for cells that changed"),
      "main.c": src(19937, "demos + arg parsing; what --snake and friends dispatch into"),
      "render.c": src(13377, "the pixel hot loop — zero heap allocation per frame"),
      "snake.c": src(5117, "snake, for real this time"),
      "stb_image_impl.c": src(384, "one #define and one #include"),
      "term.c": src(4858, "raw mode, resize signals, nonblocking reads (native build)"),
    }),
    third_party: d({
      "doomgeneric-src": d({
        LICENSE: src(18092, "GPL-2.0, as id intended"),
        "README.md": src(2028, "doomgeneric: port DOOM by filling in five functions"),
        doomgeneric: d({
          "d_main.c": src(44883, "D_DoomMain and the 1993 game loop"),
          "doomgeneric.c": src(390, "the five-function porting seam"),
          "g_game.c": src(53214, "53K of game state"),
          "info.c": src(139548, "every actor in DOOM, as one giant table"),
          "p_enemy.c": src(37354, "monster AI"),
          "r_main.c": src(15531, "the original software renderer"),
        }),
      }),
      "stb_image.h": src(283010, "stb single-header image loader"),
    }),
    "vercel.json": f(180, {
      text: '{\n  "framework": null,\n  "installCommand": "true",\n  "buildCommand": "bash web/build.sh",\n  "outputDirectory": "web/public"\n}',
    }),
    web: d({
      "build.sh": src(677, "emscripten build for the wasm module"),
      public: d({
        "index.html": src(1883, "the page around this terminal"),
        "main.js": src(28987, "the shell you are typing into right now"),
        "style.css": src(1390, "makes the green bits green"),
      }),
      "smoke.mjs": src(4043, "headless Playwright test that plays the demos"),
      "term_web.c": src(2767, "term.h backend for the browser: no tty, just xterm.js"),
    }),
  });

  const ROOT = d({
    bin: d({ fakesh: { kind: "exec", size: 0 } }),
    dev: d({
      null: f(0, { text: "" }),
      urandom: f(0, { special: "urandom" }),
    }),
    etc: d({
      motd: f(64, { text: "It is pitch black. You are likely to be eaten by a grue." }),
      passwd: f(92, {
        text: "root:x:0:0:root:/root:/bin/sh\nguest:x:1000:1000:guest:/home/guest:/bin/fakesh",
      }),
    }),
    home: d({ guest: REPO }),
    tmp: d({}),
  });

  const HOME = ["home", "guest"];
  let cwd = HOME.slice();
  let prevCwd = HOME.slice();

  function nodeAt(parts) {
    let node = ROOT;
    for (const part of parts) {
      if (node.kind !== "dir" || !Object.hasOwn(node.children, part)) return null;
      node = node.children[part];
    }
    return node;
  }

  // "foo", "./foo", "../foo", "~/foo", "/etc/foo" -> {parts, node|null}
  function resolve(path) {
    let parts;
    if (path === "~" || path.startsWith("~/")) {
      parts = HOME.slice();
      path = path.slice(1);
    } else if (path.startsWith("/")) {
      parts = [];
    } else {
      parts = cwd.slice();
    }
    for (const seg of path.split("/")) {
      if (seg === "" || seg === ".") continue;
      if (seg === "..") parts.pop();
      else parts.push(seg);
    }
    return { parts, node: nodeAt(parts) };
  }

  function pathString(parts) {
    const abs = "/" + parts.join("/");
    const home = "/" + HOME.join("/");
    if (abs === home) return "~";
    if (abs.startsWith(home + "/")) return "~" + abs.slice(home.length);
    return abs;
  }

  function setCwd(parts) {
    prevCwd = cwd;
    cwd = parts;
    promptPath = pathString(cwd);
  }

  // ------------------------------------------------------------------ shell

  let line = "";
  const history = [];
  let historyPos = 0;
  const bootTime = Date.now();

  function help() {
    term.writeln("\x1b[1mdemos\x1b[0m");
    for (const [name, p] of Object.entries(PROGRAMS)) {
      term.writeln("  \x1b[32m" + name.padEnd(9) + "\x1b[0m " + p.about);
    }
    term.writeln("\x1b[1mshell\x1b[0m");
    term.writeln("  \x1b[32mls\x1b[0m [-la]  list files        \x1b[32mcd\x1b[0m        change directory");
    term.writeln("  \x1b[32mcat\x1b[0m       read a file       \x1b[32mpwd\x1b[0m       where am I");
    term.writeln("  \x1b[32mabout\x1b[0m     what this is      \x1b[32mclear\x1b[0m     clear screen (or Ctrl-L)");
    term.writeln("  \x1b[32mhelp\x1b[0m      this message");
    term.writeln("Tab completes, ↑ recalls history, and a few commands are not listed here.");
    term.writeln("You can also run the binary directly: \x1b[33m./ascii-renderer --snake\x1b[0m");
  }

  function about() {
    term.writeln("ascii-renderer is a terminal renderer written in C11, built in phases");
    term.writeln("toward running Doom in a terminal. What you are seeing is the real C");
    term.writeln("code, unmodified apart from its tty layer, compiled to WebAssembly with");
    term.writeln("Emscripten. Its ANSI output is fed straight into this xterm.js terminal.");
    term.writeln("");
    term.writeln("  - double-buffered renderer that diffs frames and emits minimal ANSI");
    term.writeln("  - half-block (\u2580) truecolor pixel renderer: 2 pixels per cell");
    term.writeln("  - zero heap allocation in the per-frame hot loop");
    term.writeln("");
    term.writeln("source: \x1b[4mhttps://github.com/wwelden/Ascii-Renderer\x1b[0m");
  }

  function colorName(name, node, slash) {
    if (node.kind === "dir") return "\x1b[1;34m" + name + (slash ? "/" : "") + "\x1b[0m";
    if (node.kind === "exec") return "\x1b[1;32m" + name + "\x1b[0m";
    return name;
  }

  function ls(args) {
    let flags = "";
    let target = "";
    for (const a of args) {
      if (a.startsWith("-")) flags += a.slice(1);
      else target = a;
    }
    const { node } = resolve(target || ".");
    if (!node) { term.writeln("ls: cannot access '" + target + "': No such file or directory"); return; }
    const all = flags.includes("a");
    let entries;
    if (node.kind !== "dir") {
      entries = [[target, node]];
    } else {
      entries = Object.entries(node.children)
        .filter(([name]) => all || !name.startsWith("."))
        .sort(([a], [b]) => a.localeCompare(b));
      if (all) entries.unshift([".", node], ["..", node]);
    }
    if (!flags.includes("l")) {
      if (entries.length) term.writeln(entries.map(([n, e]) => colorName(n, e, true)).join("  "));
      return;
    }
    // Everything was last touched when the site was built, naturally.
    term.writeln("total " + entries.reduce((sum, [, e]) => sum + Math.ceil((e.size || 0) / 1024), 0));
    for (const [name, entry] of entries) {
      const mode =
        entry.kind === "dir" ? "drwxr-xr-x" : entry.kind === "exec" ? "-rwxr-xr-x" : "-rw-r--r--";
      const size = String(entry.kind === "dir" ? 4096 : entry.size || 0).padStart(7);
      term.writeln(mode + "  1 guest guest " + size + " Oct 20 20:12 " + colorName(name, entry, false));
    }
  }

  function cat(args) {
    if (args.length === 0) {
      term.writeln("cat: waiting for stdin... just kidding, there is no stdin. Try 'cat README.md'.");
      return;
    }
    for (const arg of args) {
      const { node } = resolve(arg);
      if (!node) { term.writeln("cat: " + arg + ": No such file or directory"); continue; }
      if (node.kind === "dir") { term.writeln("cat: " + arg + ": Is a directory"); continue; }
      if (node.kind === "exec") {
        term.writeln("\x1b[2mPK\u0003\u0004\u0000asm\u0001\u0000\u0000\u0000\x1b[0m... it's 128K of WebAssembly.");
        term.writeln("Try running it instead: \x1b[33m./ascii-renderer --snake\x1b[0m");
        continue;
      }
      if (node.special === "urandom") {
        const junk = () => Array.from({ length: 64 }, () => String.fromCharCode(33 + Math.floor(Math.random() * 93))).join("");
        term.writeln(junk()); term.writeln(junk()); term.writeln(junk());
        term.writeln("^C \x1b[2m(you're welcome)\x1b[0m");
        continue;
      }
      if (node.render === "about") { about(); continue; }
      if (node.text !== undefined) {
        if (node.text) term.writeln(node.text);
        continue;
      }
      const name = arg.split("/").filter(Boolean).pop();
      term.writeln("\x1b[2m/* " + name + " — " + (node.desc || "part of the renderer") + " */\x1b[0m");
      term.writeln("\x1b[2m/* " + node.size + " bytes. The real file lives at " + GITHUB + " */\x1b[0m");
    }
  }

  function cd(args) {
    let target = args[0] || "~";
    if (target === "-") {
      setCwd(prevCwd);
      term.writeln(pathString(cwd));
      return;
    }
    const { parts, node } = resolve(target);
    if (!node) term.writeln("cd: " + target + ": No such file or directory");
    else if (node.kind !== "dir") term.writeln("cd: " + target + ": Not a directory");
    else setCwd(parts);
  }

  function cowsay(args) {
    const msg = args.join(" ") || "moo. also, DOOM runs in a terminal now.";
    term.writeln(" " + "_".repeat(msg.length + 2));
    term.writeln("< " + msg + " >");
    term.writeln(" " + "-".repeat(msg.length + 2));
    term.writeln("        \\   ^__^");
    term.writeln("         \\  (oo)\\_______");
    term.writeln("            (__)\\       )\\/\\");
    term.writeln("                ||----w |");
    term.writeln("                ||     ||");
  }

  function neofetch() {
    const art = [
      "\x1b[1;32m        ▄▄▄▄▄▄        \x1b[0m",
      "\x1b[1;32m     ▄▀▀      ▀▀▄     \x1b[0m",
      "\x1b[1;32m    █   ▀█  █▀   █    \x1b[0m",
      "\x1b[1;32m    █    ▀▀▀▀    █    \x1b[0m",
      "\x1b[1;32m     ▀▄ ▀▄▄▄▄▀ ▄▀     \x1b[0m",
      "\x1b[1;32m        ▀▀▀▀▀▀        \x1b[0m",
      "                      ",
    ];
    const info = [
      "\x1b[1;32mguest\x1b[0m@\x1b[1;32mascii-renderer\x1b[0m",
      "-----------------",
      "\x1b[1mOS:\x1b[0m       WebAssembly (wasm32)",
      "\x1b[1mHost:\x1b[0m     a browser tab",
      "\x1b[1mShell:\x1b[0m    fakesh 1.0 (~200 lines of JS)",
      "\x1b[1mTerminal:\x1b[0m xterm.js, " + rendererName + " renderer, " + term.cols + "×" + term.rows,
      "\x1b[1mCPU:\x1b[0m      whatever you brought",
    ];
    for (let i = 0; i < Math.max(art.length, info.length); i++) {
      term.writeln((art[i] || " ".repeat(22)) + "  " + (info[i] || ""));
    }
  }

  function execute(cmdline) {
    const parts = cmdline.trim().split(/\s+/).filter(Boolean);
    if (parts.length === 0) return false;
    const [cmd, ...rest] = parts;

    // Own-property checks: plain objects inherit "constructor", "toString",
    // etc., which must not be dispatched as programs.
    if (Object.hasOwn(PROGRAMS, cmd)) { runProgram(cmd); return true; }

    if (cmd === "ascii-doom" || cmd.split("/").pop() === "ascii-doom") {
      if (cmd.includes("/") && (resolve(cmd).node || {}).kind !== "exec") {
        term.writeln("fakesh: " + cmd + ": No such file or directory");
        return false;
      }
      runProgram("doom");
      return true;
    }

    if (cmd === "ascii-renderer" || cmd.split("/").pop() === "ascii-renderer") {
      // Path invocations only work if the path actually leads to the binary.
      if (cmd.includes("/") && (resolve(cmd).node || {}).kind !== "exec") {
        term.writeln("fakesh: " + cmd + ": No such file or directory");
        return false;
      }
      const flag = rest[0] || "";
      if (Object.hasOwn(FLAG_TO_PROGRAM, flag)) { runProgram(FLAG_TO_PROGRAM[flag]); return true; }
      term.writeln("ascii-renderer: '" + flag + "' is not available in the browser build");
      term.writeln("try: --snake, --pixels, --animate, or no flag");
      return false;
    }

    switch (cmd) {
      case "help": help(); break;
      case "about": about(); break;
      case "clear": term.clear(); break;
      case "ls": ls(rest); break;
      case "cat": cat(rest); break;
      case "cd": cd(rest); break;
      case "pwd": term.writeln("/" + cwd.join("/")); break;
      case "whoami": term.writeln("guest"); break;
      case "echo":
        term.writeln(rest.join(" ").replace(/\$(\w+)/g, (_, v) =>
          ({ USER: "guest", HOME: "/home/guest", SHELL: "/bin/fakesh", TERM: "xterm-256color" }[v] ?? "")));
        break;
      case "date": term.writeln(new Date().toString()); break;
      case "uptime": {
        const mins = Math.floor((Date.now() - bootTime) / 60000);
        term.writeln(" up " + mins + " min,  1 user,  load average: 0.00, 0.00, 0.00 (it's wasm)");
        break;
      }
      case "history":
        history.forEach((h, i) => term.writeln(String(i + 1).padStart(5) + "  " + h));
        break;
      case "uname":
        term.writeln(rest.includes("-a")
          ? "Linux ascii-renderer 6.6.6-wasm32 #1 SMP PREEMPT_FAKE xterm.js wasm32 GNU/Emscripten"
          : "Linux (allegedly)");
        break;
      case "make":
        if (rest[0] === "doom") {
          term.writeln("make: 'ascii-doom' is up to date. Just type \x1b[33mdoom\x1b[0m.");
        } else if (rest[0] === "clean") {
          term.writeln("make: refusing to clean — you are standing inside the build artifact.");
        } else {
          term.writeln("make: 'ascii-renderer' is up to date. (CI compiled it to wasm before you got here.)");
        }
        break;
      case "git":
        if (rest[0] === "status") {
          term.writeln("HEAD detached at production");
          term.writeln("nothing to commit, working tree deployed");
        } else {
          term.writeln("git: this is the deployed artifact — the history lives at " + GITHUB);
        }
        break;
      case "ps":
        term.writeln("  PID TTY          TIME CMD");
        term.writeln("    1 tty1     00:00:00 fakesh");
        term.writeln("    2 tty1     00:00:13 xterm.js");
        term.writeln("  666 ?        00:00:00 doomd <sleeping>");
        break;
      case "kill": term.writeln("kill: (666): Operation not permitted — let him sleep"); break;
      case "top": term.writeln("top: you're looking at it. One process, and you're talking to it."); break;
      case "man":
        term.writeln("No manual entry for " + (rest[0] || "man") + " (the whole shell is the easter egg — try 'help')");
        break;
      case "vim": case "vi": case "nvim":
        term.writeln(cmd + ": refusing to open — in a fake shell, nobody can hear you :q!");
        break;
      case "emacs": term.writeln("emacs: not enough RAM in this tab. Try vim. (Sorry.)"); break;
      case "nano": term.writeln("nano: the filesystem is read-only, which saves us both some embarrassment"); break;
      case "ed": term.writeln("?"); break;
      case "rm":
        if (rest.join(" ").match(/-rf?\s+[/~*]/)) {
          term.writeln("rm: it takes a special kind of person to try that in somebody else's demo");
        } else {
          term.writeln("rm: cannot remove '" + (rest.filter((a) => !a.startsWith("-"))[0] || "") + "': Read-only file system (these files are load-bearing)");
        }
        break;
      case "mkdir": case "touch": case "mv": case "cp": case "chmod":
        term.writeln(cmd + ": Read-only file system");
        break;
      case "cowsay": cowsay(rest); break;
      case "sl":
        term.writeln("      ====        ________ ");
        term.writeln("  _D _|  |_______/        \\__I_I_____===__|_________|");
        term.writeln("   |(_)---  |   H\\________/ |   |        =|___ ___|  ");
        term.writeln("   /     |  |   H  |  |     |   |         ||_| |_||  ");
        term.writeln("  |      |  |   H  |__--------------------| [___] |  ");
        term.writeln("  | ________|___H__/__|_____/[][]~\\_______|       |  ");
        term.writeln("  |/ |   |-----------I_____I [][] []  D   |=======|__");
        term.writeln("you have been promoted to typo artist");
        break;
      case "neofetch": case "screenfetch": neofetch(); break;
      case "exit": term.writeln("nice try — there is nowhere to go"); break;
      case "sudo": term.writeln("guest is not in the sudoers file. This incident will be reported."); break;
      default: term.writeln(cmd + ": command not found (try 'help')");
    }
    return false;
  }

  function replaceLine(text) {
    term.write("\r\x1b[2K" + PROMPT() + text);
    line = text;
  }

  // ------------------------------------------------------------ tab complete

  // Only the documented commands complete; the easter eggs stay eggs.
  const COMPLETABLE = [
    ...Object.keys(PROGRAMS),
    "help", "about", "clear", "ls", "cd", "cat", "pwd", "echo", "make", "git",
    "./ascii-renderer",
  ];

  function completionCandidates(tok, isFirstWord, firstWord) {
    // Flags for the binary.
    if (tok.startsWith("-") && /ascii-renderer$/.test(firstWord)) {
      return ["--snake", "--pixels", "--animate"].map((flag) => ({ insert: flag, display: flag, suffix: " " }));
    }
    // Command position, unless it already looks like a path.
    if (isFirstWord && !tok.includes("/")) {
      return COMPLETABLE.map((name) => ({ insert: name, display: name, suffix: " " }));
    }
    // Otherwise complete a path against the fake filesystem.
    const slash = tok.lastIndexOf("/");
    const dirPart = slash >= 0 ? tok.slice(0, slash + 1) : "";
    const base = slash >= 0 ? tok.slice(slash + 1) : tok;
    const { node } = resolve(dirPart || ".");
    if (!node || node.kind !== "dir") return [];
    return Object.entries(node.children)
      .filter(([name]) => base.startsWith(".") || !name.startsWith("."))
      .map(([name, child]) => ({
        insert: dirPart + name,
        display: colorName(name, child, true),
        suffix: child.kind === "dir" ? "/" : " ",
      }));
  }

  function completeTab() {
    const tok = (/(?:^|\s)(\S*)$/.exec(line) || [, ""])[1];
    const firstWord = line.trim().split(/\s+/)[0] || "";
    const isFirstWord = line.slice(0, line.length - tok.length).trim() === "";
    const matches = completionCandidates(tok, isFirstWord, firstWord)
      .filter((c) => c.insert.startsWith(tok) && c.insert !== tok);
    if (matches.length === 0) return;
    if (matches.length === 1) {
      const extra = matches[0].insert.slice(tok.length) + matches[0].suffix;
      line += extra;
      term.write(extra);
      return;
    }
    // Several matches: extend to the longest common prefix, or list them.
    let prefix = matches[0].insert;
    for (const m of matches) {
      while (!m.insert.startsWith(prefix)) prefix = prefix.slice(0, -1);
    }
    if (prefix.length > tok.length) {
      const extra = prefix.slice(tok.length);
      line += extra;
      term.write(extra);
      return;
    }
    term.write("\r\n" + matches.map((m) => m.display).join("  ") + "\r\n");
    term.write(PROMPT() + line);
  }

  function onShellData(data) {
    // Arrow keys arrive as whole escape sequences.
    if (data === "\x1b[A") {
      if (historyPos > 0) replaceLine(history[--historyPos]);
      return;
    }
    if (data === "\x1b[B") {
      if (historyPos < history.length - 1) replaceLine(history[++historyPos]);
      else { historyPos = history.length; replaceLine(""); }
      return;
    }
    if (data.startsWith("\x1b")) return; // ignore other escapes

    for (const ch of data) {
      if (ch === "\r") {
        term.write("\r\n");
        const cmdline = line;
        line = "";
        if (cmdline.trim()) history.push(cmdline);
        historyPos = history.length;
        const started = execute(cmdline);
        if (!started) term.write(PROMPT());
        if (started) return; // drop anything typed after the command
      } else if (ch === "\x7f" || ch === "\b") {
        if (line.length > 0) {
          line = line.slice(0, -1);
          term.write("\b \b");
        }
      } else if (ch === "\t") {
        completeTab();
      } else if (ch === "\x03") {
        term.write("^C\r\n" + PROMPT());
        line = "";
      } else if (ch === "\x0c") {
        term.clear();
        replaceLine(line);
      } else if (ch >= " ") {
        line += ch;
        term.write(ch);
      }
    }
  }

  const encoder = new TextEncoder();

  function sendToProgram(data) {
    for (const b of encoder.encode(data)) input.push(b);
  }

  term.onData((data) => {
    if (running) sendToProgram(data);
    else onShellData(data);
  });

  term.onResize(() => { resized = true; });
  window.addEventListener("resize", () => fit.fit());
  new ResizeObserver(() => fit.fit()).observe(document.getElementById("terminal"));

  document.querySelectorAll("nav button[data-cmd]").forEach((btn) => {
    btn.addEventListener("click", () => {
      if (running) return;
      const cmd = btn.dataset.cmd;
      term.write("\r\x1b[2K" + PROMPT() + cmd + "\r\n");
      line = "";
      runProgram(cmd);
      term.focus();
    });
  });

  document.querySelectorAll("#touch-keys button").forEach((btn) => {
    btn.addEventListener("click", () => {
      if (running) sendToProgram(btn.dataset.key);
      term.focus();
    });
  });

  // ------------------------------------------------------------------ boot

  term.writeln("\x1b[1;32m+----------------------+\r\n| ascii-renderer / web |\r\n+----------------------+\x1b[0m");
  term.writeln("");
  term.writeln("C11 terminal renderer, compiled to WebAssembly. Type \x1b[32mhelp\x1b[0m, or try \x1b[32msnake\x1b[0m.");
  term.writeln("");
  term.write(PROMPT());
  term.focus();
})();

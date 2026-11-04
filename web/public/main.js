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

  const PROMPT = "\x1b[1;32mguest@ascii-renderer\x1b[0m:\x1b[1;34m~\x1b[0m$ ";

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

  const PROGRAMS = {
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
      term.write("\r\n" + PROMPT);
      term.focus();
    };

    createAsciiRenderer({
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

  // ------------------------------------------------------------------ shell

  let line = "";
  const history = [];
  let historyPos = 0;

  function help() {
    term.writeln("\x1b[1mdemos\x1b[0m");
    for (const [name, p] of Object.entries(PROGRAMS)) {
      term.writeln("  \x1b[32m" + name.padEnd(9) + "\x1b[0m " + p.about);
    }
    term.writeln("\x1b[1mshell\x1b[0m");
    term.writeln("  \x1b[32mls\x1b[0m        list files");
    term.writeln("  \x1b[32mabout\x1b[0m     what this is");
    term.writeln("  \x1b[32mclear\x1b[0m     clear the screen (or Ctrl-L)");
    term.writeln("  \x1b[32mhelp\x1b[0m      this message");
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

  function execute(cmdline) {
    const parts = cmdline.trim().split(/\s+/).filter(Boolean);
    if (parts.length === 0) return false;
    const [cmd, ...rest] = parts;

    // Own-property checks: plain objects inherit "constructor", "toString",
    // etc., which must not be dispatched as programs.
    if (Object.hasOwn(PROGRAMS, cmd)) { runProgram(cmd); return true; }

    if (cmd === "./ascii-renderer" || cmd === "ascii-renderer") {
      const flag = rest[0] || "";
      if (Object.hasOwn(FLAG_TO_PROGRAM, flag)) { runProgram(FLAG_TO_PROGRAM[flag]); return true; }
      term.writeln("ascii-renderer: '" + flag + "' is not available in the browser build");
      term.writeln("try: --snake, --pixels, --animate, or no flag");
      return false;
    }

    switch (cmd) {
      case "help": help(); break;
      case "about": case "cat": about(); break;
      case "clear": term.clear(); break;
      case "ls":
        term.writeln("\x1b[1;32mascii-renderer\x1b[0m  README.md  Makefile  \x1b[1;34minclude/\x1b[0m  \x1b[1;34msrc/\x1b[0m  \x1b[1;34mweb/\x1b[0m");
        break;
      case "exit": term.writeln("nice try — there is nowhere to go"); break;
      case "sudo": term.writeln("guest is not in the sudoers file. This incident will be reported."); break;
      default: term.writeln(cmd + ": command not found (try 'help')");
    }
    return false;
  }

  function replaceLine(text) {
    term.write("\r\x1b[2K" + PROMPT + text);
    line = text;
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
        if (!started) term.write(PROMPT);
        if (started) return; // drop anything typed after the command
      } else if (ch === "\x7f" || ch === "\b") {
        if (line.length > 0) {
          line = line.slice(0, -1);
          term.write("\b \b");
        }
      } else if (ch === "\x03") {
        term.write("^C\r\n" + PROMPT);
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
      term.write("\r\x1b[2K" + PROMPT + cmd + "\r\n");
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
  term.write(PROMPT);
  term.focus();
})();

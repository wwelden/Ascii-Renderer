// Headless smoke test for the WebAssembly site: serves web/public, drives the
// shell in Chromium, and checks that each demo starts, draws, and exits back
// to the prompt. Run after `make web`:  node web/smoke.mjs
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("./public/", import.meta.url));
const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm" };

const server = createServer(async (req, res) => {
  const path = normalize(decodeURIComponent(new URL(req.url, "http://x").pathname)).replace(/^([/\\])+/, "");
  const file = path || "index.html";
  try {
    const body = await readFile(join(root, file));
    res.writeHead(200, { "content-type": types[extname(file)] || "application/octet-stream" });
    res.end(body);
  } catch {
    res.writeHead(404).end();
  }
});
await new Promise((r) => server.listen(0, r));
const url = `http://localhost:${server.address().port}/`;

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1100, height: 750 } });
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));

const screen = () =>
  page.evaluate(() => [...document.querySelectorAll(".xterm-rows > div")].map((d) => d.textContent).join("\n"));

async function waitForScreen(pred, what, timeout = 10000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    const s = await screen();
    if (pred(s)) return s;
    await page.waitForTimeout(100);
  }
  throw new Error(`timed out waiting for ${what}. Screen:\n${await screen()}`);
}

async function run(cmd, { running, exited }) {
  await page.keyboard.type(cmd);
  await page.keyboard.press("Enter");
  await waitForScreen(running, `${cmd} to draw`);
  await page.waitForTimeout(500);
  await page.keyboard.type("q");
  const s = await waitForScreen(exited, `${cmd} to exit`);
  console.log(`ok  ${cmd}`);
  return s;
}

try {
  await page.goto(url);
  await waitForScreen((s) => s.includes("guest@ascii-renderer"), "prompt");
  await page.click("#terminal");

  // Names inherited from Object.prototype must not dispatch as programs.
  await page.keyboard.type("constructor");
  await page.keyboard.press("Enter");
  await waitForScreen((s) => s.includes("constructor: command not found"), "unknown-command error");
  console.log("ok  constructor (rejected)");

  await run("border", {
    running: (s) => s.includes("ascii-renderer / phase 0"),
    exited: (s) => s.trimEnd().endsWith("guest@ascii-renderer:~$"),
  });

  await run("snake", {
    running: (s) => s.includes("snake  score"),
    exited: (s) => s.includes("snake: final score"),
  });

  const plasma = await run("plasma", {
    running: (s) => (s.match(/▀/g) || []).length > 500,
    exited: (s) => /rendered \d+ frames/.test(s),
  });
  // CI has no GPU, so this only checks that plasma animates (more than one
  // frame); the fps it prints is a software-rendering floor, not a target.
  const [, frames, fps] = plasma.match(/rendered (\d+) frames in [\d.]+s = ([\d.]+) fps/);
  if (Number(frames) < 2) throw new Error(`plasma did not animate (${frames} frame)`);
  const renderer = await page.evaluate(() => document.documentElement.dataset.renderer);
  console.log(`    plasma: ${frames} frames, ${fps} fps, ${renderer} renderer`);

  await run("./ascii-renderer --animate", {
    running: (s) => /frame \d+ +[\d.]+ fps/.test(s),
    exited: (s) => /rendered \d+ frames[\s\S]*rendered \d+ frames/.test(s),
  });

  // Doom is its own module and can't be driven by run(): 'q' is not a Doom
  // key, Ctrl-C is the hard quit. The engine logs on the normal screen, then
  // the first frame enters the alt screen; the title screen is art, not text,
  // so assert on the volume of half-block cells. Exit is detected by the
  // prompt's return, not frame stats — earlier demos' stats are back on the
  // restored main screen and would match instantly.
  await page.keyboard.type("doom");
  await page.keyboard.press("Enter");
  await waitForScreen((s) => (s.match(/▀/g) || []).length > 300, "doom to draw", 30000);
  await page.waitForTimeout(1000);
  await page.keyboard.press("Control+C");
  await waitForScreen((s) => s.trimEnd().endsWith("guest@ascii-renderer:~$"), "doom to exit");
  console.log("ok  doom");

  await page.screenshot({ path: "web/smoke.png" });
  if (errors.length) throw new Error("page errors:\n" + errors.join("\n"));
  console.log("smoke test passed");
} catch (e) {
  console.error(e.message);
  await page.screenshot({ path: "web/smoke.png" }).catch(() => {});
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
}

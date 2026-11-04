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
  try {
    const body = await readFile(join(root, path || "index.html"));
    res.writeHead(200, { "content-type": types[extname(path || ".html")] || "application/octet-stream" });
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
  const frames = Number(plasma.match(/rendered (\d+) frames/)[1]);
  if (frames < 10) throw new Error(`plasma only rendered ${frames} frames`);

  await run("./ascii-renderer --animate", {
    running: (s) => /frame \d+ +[\d.]+ fps/.test(s),
    exited: (s) => /rendered \d+ frames[\s\S]*rendered \d+ frames/.test(s),
  });

  await page.screenshot({ path: "web/smoke.png" });
  if (errors.length) throw new Error("page errors:\n" + errors.join("\n"));
  console.log(`smoke test passed (plasma: ${frames} frames)`);
} catch (e) {
  console.error(e.message);
  await page.screenshot({ path: "web/smoke.png" }).catch(() => {});
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
}

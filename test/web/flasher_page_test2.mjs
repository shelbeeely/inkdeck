import { chromium } from "/opt/node22/lib/node_modules/playwright/index.mjs";
const b = await chromium.launch({ executablePath: "/opt/pw-browsers/chromium" });
const ctx = await b.newContext({ timezoneId: "Europe/Berlin", viewport: { width: 1000, height: 900 } });
// A fake InkDeck on a fake serial port: answers each 0x80 packet with "ok" and records the lines.
await ctx.addInitScript(() => {
  window.__received = [];
  const frame = (type, data) => {
    const bytes = [73, 77, 80, 82, 79, 86, 1, type, data.length, ...data];
    const sum = bytes.reduce((a, b) => a + b, 0) & 255;
    return new Uint8Array([10, ...bytes, sum, 10]);
  };
  let controller;
  const port = {
    readable: null,
    writable: null,
    async open() {
      if (this.readable) { const e = new Error("open"); e.name = "InvalidStateError"; throw e; }
      this.readable = new ReadableStream({ start(c) { controller = c; c.enqueue(new TextEncoder().encode("boot log noise")); } });
      this.writable = new WritableStream({
        write(chunk) {
          const d = [...chunk];
          const data = d.slice(9, 9 + d[8]);
          if (data[0] === 0x80) {
            window.__received.push(new TextDecoder().decode(new Uint8Array(data.slice(2, 2 + data[1]))));
            controller.enqueue(frame(4, [0x80, 3, 2, 111, 107]));
          }
        },
      });
    },
    async close() { this.readable = null; this.writable = null; },
  };
  Object.defineProperty(navigator, "serial", { value: { getPorts: async () => [port], requestPort: async () => port } });
});
const p = await ctx.newPage();
const errs = [];
p.on("pageerror", (e) => errs.push(String(e)));
await p.goto("http://localhost:8767/", { waitUntil: "networkidle" });
const tz = await p.inputValue("#timezone");
console.log((tz === "CET-1CEST,M3.5.0,M10.5.0/3" ? "PASS" : "FAIL") + " browser time zone preselected: " + tz);

await p.click("summary >> text=More");
const label = await p.evaluate(() => document.getElementById("tzsel").selectedOptions[0].textContent);
console.log((label === "Europe/Berlin" ? "PASS" : "FAIL") + " time zone label: " + label);

await p.fill("#repo_url", "https://github.com/you/journal");
await p.fill("#git_token", "github_pat_123");
await p.click("summary >> text=Location and writing times"); await p.selectOption("#location", "phone"); await p.fill("#location_every", "10");
await p.click("#sendnow");
await p.waitForFunction(() => /\b(ok|err)\b/.test(document.getElementById("status").className), null, { timeout: 10000 });
const rec = await p.evaluate(() => window.__received.join(""));
const okSend = rec.includes("repo_url = https://github.com/you/journal") && rec.includes("git_token = github_pat_123") && rec.includes("timezone = CET-1CEST");
console.log((okSend ? "PASS" : "FAIL") + " button sends settings: " + JSON.stringify(rec));
console.log("  status: " + (await p.textContent("#status")));

await p.evaluate(() => { window.__received = []; document.body.appendChild(document.createElement("ewt-install-dialog")); });
await p.evaluate(() => document.querySelector("ewt-install-dialog").remove());
await p.waitForFunction(() => window.__received.length > 0, null, { timeout: 10000 });
console.log("PASS auto-send after installer closes");
await p.screenshot({ path: process.argv[2], fullPage: true });
console.log(errs.length ? "FAIL page errors: " + errs : "PASS no page errors");
await b.close();

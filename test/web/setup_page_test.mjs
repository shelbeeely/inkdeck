import { chromium } from "/opt/node22/lib/node_modules/playwright/index.mjs";
const file = "file://" + process.argv[2];
const b = await chromium.launch({ executablePath: "/opt/pw-browsers/chromium" });
for (const [tzId, expect] of [["America/Chicago","CST6CDT,M3.2.0,M11.1.0"],["Europe/Berlin","CET-1CEST,M3.5.0,M10.5.0/3"],["Asia/Kolkata","IST-5:30"],["Antarctica/Troll","UTC0"]]) {
  const ctx = await b.newContext({ timezoneId: tzId, viewport: { width: 375, height: 900 } });
  const p = await ctx.newPage();
  const errs = []; p.on("pageerror", e => errs.push(String(e)));
  await p.goto(file);
  const r = await p.evaluate(() => ({ sel: document.forms.cfg.tzsel.value, tz: document.forms.cfg.timezone.value, hidden: document.forms.cfg.timezone.hidden, tok: document.getElementById("toklink").hidden }));
  console.log(`${r.sel === expect && r.tz === expect ? "PASS" : "FAIL"} ${tzId} -> ${r.tz} hidden=${r.hidden} errs=${errs.length}`);
  if (tzId === "America/Chicago") {
    await p.fill("#r", "git@github.com:you/journal.git");
    const tokVisible = await p.evaluate(() => !document.getElementById("toklink").hidden);
    await p.selectOption("#z", "");
    await p.fill("input[name=timezone]", "XYZ-3");
    const fd = await p.evaluate(() => Object.fromEntries(new FormData(document.forms.cfg)));
    console.log(`${tokVisible ? "PASS" : "FAIL"} token link shown for github URL`);
    console.log(`${fd.timezone === "XYZ-3" && fd.repo_url.includes("github") ? "PASS" : "FAIL"} custom TZ submitted: ${JSON.stringify(fd)}`);
    await p.screenshot({ path: process.argv[3], fullPage: true });
  }
  await ctx.close();
}
await b.close();

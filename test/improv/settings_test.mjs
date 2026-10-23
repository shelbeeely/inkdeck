import { spawn } from "node:child_process";
import { sendSettings, packLines, SettingsError } from "/home/user/inkdeck/web/settings.js";

function device() {
  const dev = spawn("./harness", [], { stdio: ["pipe", "pipe", "inherit"] });
  const port = {
    readable: new ReadableStream({ start(c) { dev.stdout.on("data", (d) => c.enqueue(new Uint8Array(d))); dev.stdout.on("end", () => { try { c.close(); } catch {} }); } }),
    writable: new WritableStream({ write(chunk) { dev.stdin.write(Buffer.from(chunk)); } }),
  };
  return { dev, port };
}
let fails = 0;
const check = (n, ok, x = "") => { console.log(`${ok ? "PASS" : "FAIL"} ${n} ${x}`); if (!ok) fails++; };

// 1. Full settings, long enough to need several packets.
{
  const { dev, port } = device();
  const lines = [
    "repo_url = git@github.com:you/journal.git",
    "git_token = github_pat_" + "A".repeat(82),
    "branch = main",
    "timezone = CST6CDT,M3.2.0,M11.1.0",
    "wifi_ssid = Café \"Net\"",
    "wifi_password = p#ss wörd",
    "notes_folder = /Journal/" + "Deep".repeat(10),
    "journal_subfolder = Daily",
  ];
  const packets = packLines(lines);
  let progress = [];
  await sendSettings(port, lines, { onProgress: (i, n) => progress.push(`${i}/${n}`) });
  check("multi-packet send acknowledged", progress.length === packets.length && packets.length > 1, `packets=${packets.length} progress=${progress}`);
  // the port must be reusable afterwards (locks released)
  check("locks released", !port.readable.locked && !port.writable.locked);
  dev.kill();
}
// 2. Rejected key surfaces a readable error.
{
  const { dev, port } = device();
  let msg = "";
  try { await sendSettings(port, ["bad_key = 1"], { bootWaitMs: 3000 }); } catch (e) { msg = e instanceof SettingsError ? e.message : "other:" + e; }
  check("rejected settings -> error", msg === "InkDeck rejected the settings.", msg);
  dev.kill();
}
// 3. Over-long single line is refused before sending.
{
  let msg = "";
  try { packLines(["git_token = " + "x".repeat(300)]); } catch (e) { msg = e.message; }
  check("too-long line refused", msg.includes("git_token"), msg);
}
// 4. Device that never answers -> timeout message.
{
  const silent = { readable: new ReadableStream({ start() {} }), writable: new WritableStream({ write() {} }) };
  let msg = "";
  try { await sendSettings(silent, ["branch = main"], { bootWaitMs: 1500 }); } catch (e) { msg = e.message; }
  check("silent device -> timeout", msg.includes("didn't answer"), msg);
}
console.log(fails ? `${fails} FAILED` : "ALL PASSED");
process.exit(fails ? 1 : 0);

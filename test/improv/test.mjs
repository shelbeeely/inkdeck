import { spawn } from "node:child_process";
import { ImprovSerial } from "./sdk/serial.js";
import { ImprovSerialCurrentState } from "./sdk/const.js";

const dev = spawn(process.argv[2], [], { stdio: ["pipe", "pipe", "inherit"] });
const port = {
  readable: new ReadableStream({
    start(c) { dev.stdout.on("data", (d) => c.enqueue(new Uint8Array(d))); dev.stdout.on("end", () => c.close()); },
  }),
  writable: new WritableStream({ write(chunk) { dev.stdin.write(Buffer.from(chunk)); } }),
};
const logger = { log() {}, debug() {}, error: (...a) => console.log("[sdk error]", ...a) };
let failures = 0;
const check = (name, ok, extra = "") => { console.log(`${ok ? "PASS" : "FAIL"} ${name} ${extra}`); if (!ok) failures++; };

const c = new ImprovSerial(port, logger);
const info = await c.initialize(3000);
check("initialize + info", info.firmware === "InkDeck" && info.version === "test-1" && info.chipFamily === "ESP32-C3" && info.name === "InkDeck", JSON.stringify(info));
check("initial state READY", c.state === ImprovSerialCurrentState.READY, `state=${c.state}`);

let scan;
try { scan = await c.scan(); } catch (e) { scan = "rejected:" + e; }
check("scan unsupported -> manual entry", scan === null || String(scan).includes("UNKNOWN_RPC_COMMAND"), String(scan));

let err = "";
try { await c.provision("Bad Net", "x", 5000); } catch (e) { err = String(e); }
check("bad credentials -> UNABLE_TO_CONNECT", err.includes("UNABLE_TO_CONNECT"), err);
check("state back to READY", c.state === ImprovSerialCurrentState.READY, `state=${c.state}`);

await c.provision("Good Net", "p#ss wörd", 5000);
check("good credentials -> PROVISIONED", c.state === ImprovSerialCurrentState.PROVISIONED, `state=${c.state}`);
check("next URL is the setup page", c.nextUrl === "http://192.168.1.42/", String(c.nextUrl));

await c.requestCurrentState(3000);
check("re-query keeps URL", c.state === ImprovSerialCurrentState.PROVISIONED && c.nextUrl === "http://192.168.1.42/", String(c.nextUrl));

console.log(failures ? `${failures} FAILED` : "ALL PASSED");
dev.kill();
process.exit(failures ? 1 : 0);

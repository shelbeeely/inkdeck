// Sends InkDeck settings ("key = value" lines) to the device over Web Serial,
// using Improv's serial framing with InkDeck's extension command 0x80
// (see src/Improv.h). The device stores them on itself; no Wi-Fi needed.

const HEADER = [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56, 0x01]; // "IMPROV", version 1
const TYPE_ERROR_STATE = 0x02;
const TYPE_RPC = 0x03;
const TYPE_RPC_RESULT = 0x04;
const SETTINGS_COMMAND = 0x80;
const MAX_PAYLOAD = 253; // one-byte packet length, minus command + length bytes

export class SettingsError extends Error {}

const ERRORS = {
  0x01: "InkDeck rejected the settings.",
  0x02: "This InkDeck firmware is too old to receive settings. Install the latest version first.",
};

function frame(command, payload) {
  const data = [command, payload.length, ...payload];
  const bytes = [...HEADER, TYPE_RPC, data.length, ...data];
  const sum = bytes.reduce((a, b) => a + b, 0) & 0xff;
  return new Uint8Array([...bytes, sum, 0x0a]);
}

// Groups lines into packets of at most MAX_PAYLOAD bytes, never splitting a line.
export function packLines(lines) {
  const enc = new TextEncoder();
  const packets = [];
  let current = [];
  for (const line of lines) {
    const bytes = [...enc.encode(line + "\n")];
    if (bytes.length > MAX_PAYLOAD) throw new SettingsError(`Setting too long: ${line.split("=")[0].trim()}`);
    if (current.length + bytes.length > MAX_PAYLOAD) {
      packets.push(current);
      current = [];
    }
    current.push(...bytes);
  }
  if (current.length) packets.push(current);
  return packets;
}

// Reads Improv packets from the device, skipping its log output.
function packetReader(reader) {
  const waiting = [];
  const queued = [];
  let line = [];
  let need = 0;
  let done = false;
  (async () => {
    try {
      for (;;) {
        const { value, done: end } = await reader.read();
        if (end) break;
        for (const byte of value) {
          if (need === 0) {
            if (byte === 0x0a) { line = []; continue; }
            line.push(byte);
            if (line.length === 9) {
              const isImprov = String.fromCharCode(...line.slice(0, 6)) === "IMPROV";
              if (isImprov) need = 9 + line[8] + 1;
              else line = [];
            }
            continue;
          }
          line.push(byte);
          if (line.length === need) {
            const sum = line.slice(0, -1).reduce((a, b) => a + b, 0) & 0xff;
            if (sum === line[need - 1]) {
              const packet = { type: line[7], data: line.slice(9, need - 1) };
              const w = waiting.shift();
              if (w) w(packet); else queued.push(packet);
            }
            line = [];
            need = 0;
          }
        }
      }
    } catch (e) {
      // Port closed or device unplugged.
    } finally {
      done = true;
      for (const w of waiting.splice(0)) w(null);
    }
  })();
  return {
    next(timeoutMs) {
      if (queued.length) return Promise.resolve(queued.shift());
      if (done) return Promise.resolve(null);
      return new Promise((resolve) => {
        const w = (p) => { clearTimeout(t); resolve(p); };
        const t = setTimeout(() => {
          const i = waiting.indexOf(w);
          if (i >= 0) waiting.splice(i, 1);
          resolve(undefined); // timed out
        }, timeoutMs);
        waiting.push(w);
      });
    },
  };
}

/**
 * Sends `lines` (strings like "repo_url = ...") to an open Web Serial port.
 * The first packet is re-sent every second for up to `bootWaitMs`, since the
 * device may still be restarting after an install.
 */
export async function sendSettings(port, lines, { bootWaitMs = 20000, onProgress = () => {} } = {}) {
  const packets = packLines(lines);
  const reader = port.readable.getReader();
  const writer = port.writable.getWriter();
  const incoming = packetReader(reader);
  try {
    for (let i = 0; i < packets.length; i++) {
      const deadline = Date.now() + (i === 0 ? bootWaitMs : 3000);
      let answered = false;
      while (!answered) {
        if (Date.now() > deadline) throw new SettingsError("InkDeck didn't answer. Is it awake and plugged in?");
        await writer.write(frame(SETTINGS_COMMAND, packets[i]));
        const until = Date.now() + 1000;
        while (Date.now() < until) {
          const p = await incoming.next(until - Date.now());
          if (p === undefined) break; // no answer this second: send again
          if (p === null) throw new SettingsError("The connection to InkDeck closed.");
          if (p.type === TYPE_ERROR_STATE && p.data[0] !== 0) {
            throw new SettingsError(ERRORS[p.data[0]] || `InkDeck reported error ${p.data[0]}.`);
          }
          if (p.type === TYPE_RPC_RESULT && p.data[0] === SETTINGS_COMMAND) {
            answered = true;
            break;
          }
        }
      }
      onProgress(i + 1, packets.length);
    }
  } finally {
    writer.releaseLock();
    await reader.cancel().catch(() => {});
    reader.releaseLock();
  }
}

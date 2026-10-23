"""InkDeck server: stats dashboard, and (optionally) the journal's companion.

Keeps a clone of the journal repository up to date and serves a dashboard
built from InkDeck's session records and usage logs (stats.py), with levels,
achievements and the journaling pet (companion.py).

With WRITE_BACK=1 it also commits to the repository, as "InkDeck Companion":
  * _inkdeck/companion.md - pet, level, streaks, achievements; the X4 syncs it
    and shows it (the device only displays what the server works out);
  * weather, sun and air quality in finished journal entries (enrich.py);
  * _inkdeck/prompts-suggested.txt - today's journaling prompts picked from
    your journal (prompts.py); the X4 lists them under "For you".

Environment:
  REPO_URL            git URL of the journal repo (https or ssh). Without it,
                      NOTES_PATH is read as is (e.g. a folder you sync yourself).
  GIT_TOKEN           access token for https URLs (read access is enough).
  GIT_USERNAME        username sent with the token (default x-access-token,
                      right for GitHub; GitLab uses oauth2).
  NOTES_PATH          where the clone lives (default /data/notes).
  NOTES_SUBFOLDER     folder inside the repo that InkDeck's notes folder mirrors
                      (default: the repo root).
  PULL_INTERVAL       seconds between fetches (default 300).
  DASHBOARD_PASSWORD  require HTTP basic auth with this password (any user
                      name). Set it whenever the port is reachable by others.
  PORT                default 8080.

  WRITE_BACK          1 to commit and push (needs a token with write access).
  BRANCH              branch to follow and push to (default: the repo's default).
  WEATHER             off to skip weather enrichment (default on with WRITE_BACK).
  HOME_LOCATION       "lat,lon" for sessions without a phone location.
  CACHE_PATH          Open-Meteo response cache (default /data/cache).
  DAILY_GOAL          words per day (default 250).
  REST_DAYS           missed days per week that keep a streak (default 1).
  PET_NAME            default Inkling.
  PET_MODE            gentle (a neglected pet hibernates) or classic (it's lost
                      and a new egg comes). Default gentle.
  TZ                  your time zone, for "today" (e.g. America/Los_Angeles).
"""

from __future__ import annotations

import base64
import datetime as dt
import hmac
import json
import os
import subprocess
import sys
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import companion
import enrich
import prompts
import stats

HERE = Path(__file__).resolve().parent
DASHBOARD = HERE / "dashboard"

REPO_URL = os.environ.get("REPO_URL", "").strip()
GIT_TOKEN = os.environ.get("GIT_TOKEN", "").strip()
GIT_USERNAME = os.environ.get("GIT_USERNAME", "x-access-token").strip()
NOTES_PATH = Path(os.environ.get("NOTES_PATH", "/data/notes"))
NOTES_SUBFOLDER = os.environ.get("NOTES_SUBFOLDER", "").strip("/")
PULL_INTERVAL = int(os.environ.get("PULL_INTERVAL", "300"))
PASSWORD = os.environ.get("DASHBOARD_PASSWORD", "")
PORT = int(os.environ.get("PORT", "8080"))
WRITE_BACK = os.environ.get("WRITE_BACK", "").strip().lower() in ("1", "on", "true", "yes")
BRANCH = os.environ.get("BRANCH", "").strip()
WEATHER = os.environ.get("WEATHER", "on").strip().lower() not in ("0", "off", "false", "no")
HOME = enrich.parse_home(os.environ.get("HOME_LOCATION", ""))
CACHE_PATH = Path(os.environ.get("CACHE_PATH", "/data/cache"))
SETTINGS = companion.Settings(
    daily_goal=int(os.environ.get("DAILY_GOAL", "250")),
    rest_days=int(os.environ.get("REST_DAYS", "1")),
    pet_name=os.environ.get("PET_NAME", "Inkling").strip() or "Inkling",
    pet_mode="classic" if os.environ.get("PET_MODE", "").strip().lower() == "classic" else "gentle",
)
AUTHOR = ["-c", "user.name=InkDeck Companion", "-c", "user.email=companion@inkdeck.invalid"]

_lock = threading.Lock()
_cache: dict = {"json": None, "at": 0.0, "error": None, "pulled": None}


def log(msg: str) -> None:
    print(f"[inkdeck-stats] {msg}", file=sys.stderr, flush=True)


def git(*args: str) -> str:
    cmd = ["git"]
    if GIT_TOKEN and REPO_URL.startswith("http"):
        # Sent per command, never written into the clone's config.
        cred = base64.b64encode(f"{GIT_USERNAME}:{GIT_TOKEN}".encode()).decode()
        cmd += ["-c", f"http.extraHeader=Authorization: Basic {cred}"]
    return subprocess.run(cmd + list(args), check=True, capture_output=True, text=True, timeout=300).stdout


def pull() -> None:
    """Clone or fast-forward the copy (local changes are discarded: anything
    the server writes is pushed straight away, or redone next time)."""
    if not REPO_URL:
        return
    if not (NOTES_PATH / ".git").is_dir():
        NOTES_PATH.parent.mkdir(parents=True, exist_ok=True)
        log(f"cloning {REPO_URL.split('@')[-1]}")
        git("clone", "--depth", "1", *(["--branch", BRANCH] if BRANCH else []), REPO_URL, str(NOTES_PATH))
    else:
        git("-C", str(NOTES_PATH), "fetch", "--depth", "1", "origin", branch())
        git("-C", str(NOTES_PATH), "reset", "--hard", "FETCH_HEAD")
    _cache["pulled"] = time.time()


def branch() -> str:
    return BRANCH or git("-C", str(NOTES_PATH), "rev-parse", "--abbrev-ref", "HEAD").strip()


def notes_root() -> Path:
    return NOTES_PATH / NOTES_SUBFOLDER if NOTES_SUBFOLDER else NOTES_PATH


def _without_timestamp(text: str) -> str:
    return "\n".join(line for line in text.splitlines() if not line.startswith("updated: "))


def write_if_changed(root: Path, rel: str, text: str) -> list[str]:
    path = root / rel
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return []
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return [rel]


def write_companion(root: Path, comp: dict) -> list[str]:
    """Updates companion.md if anything but its timestamp changed."""
    path = root / companion.COMPANION_PATH
    text = companion.companion_markdown(comp, dt.datetime.now().astimezone())
    old = path.read_text(encoding="utf-8") if path.exists() else ""
    if _without_timestamp(old) == _without_timestamp(text):
        return []  # not worth a commit (or a sync on the X4)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return [companion.COMPANION_PATH]


def commit_and_push(changed: list[str]) -> None:
    if not REPO_URL or not git("-C", str(NOTES_PATH), "status", "--porcelain").strip():
        return
    git("-C", str(NOTES_PATH), "add", "-A")
    own = {companion.COMPANION_PATH, prompts.SUGGESTED_PATH}
    entries = [c for c in changed if c not in own]
    what = f"add weather to {len(entries)} entries" if entries else "update companion and prompts"
    git("-C", str(NOTES_PATH), *AUTHOR, "commit", "-q", "-m", f"InkDeck: {what}")
    try:
        git("-C", str(NOTES_PATH), "push", "-q", "origin", f"HEAD:{branch()}")
        log(f"pushed: {what}")
    except subprocess.CalledProcessError as e:
        # Usually the X4 pushed meanwhile; the next refresh starts from its commit and redoes this.
        log(f"push failed, will retry: {(e.stderr or '').strip()[-200:]}")


def refresh() -> None:
    with _lock:
        try:
            pull()
            root = notes_root()
            data = stats.compute(root)
            changed = []
            if WRITE_BACK and WEATHER:
                changed = enrich.run(root, HOME, dt.datetime.now().astimezone(), CACHE_PATH)
                if changed:
                    data = stats.compute(root)  # now with the weather
            pr = prompts.compute(root, data)
            data["prompt_categories"] = pr.pop("categories")
            data["prompt_insights"] = pr["insights"]
            comp = companion.compute(data, SETTINGS)
            if WRITE_BACK:
                changed += write_companion(root, comp)
                changed += write_if_changed(root, prompts.SUGGESTED_PATH, prompts.suggestions_file(pr["suggestions"]))
                commit_and_push(changed)
            data["companion"] = comp
            data["prompts"] = pr
            del data["prompt_categories"]
            data["pulled"] = _cache["pulled"]
            _cache.update(json=json.dumps(data, default=str).encode(), at=time.time(), error=None)
        except subprocess.CalledProcessError as e:
            _cache["error"] = f"git failed: {(e.stderr or '').strip()[-300:]}"
            log(_cache["error"])
        except Exception as e:  # keep serving the last good stats
            _cache["error"] = f"{type(e).__name__}: {e}"
            log(_cache["error"])


def refresher() -> None:
    while True:
        refresh()
        time.sleep(PULL_INTERVAL)


class Handler(BaseHTTPRequestHandler):
    server_version = "InkDeckStats"

    def log_message(self, fmt, *args):  # quieter than the default per-request log
        pass

    def authorized(self) -> bool:
        if not PASSWORD:
            return True
        header = self.headers.get("Authorization", "")
        if header.startswith("Basic "):
            try:
                _, _, given = base64.b64decode(header[6:]).decode().partition(":")
            except (ValueError, UnicodeDecodeError):
                return False
            return hmac.compare_digest(given, PASSWORD)
        return False

    def send(self, status: int, body: bytes, ctype: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/healthz":
            return self.send(200, b"ok", "text/plain")
        if not self.authorized():
            self.send_response(HTTPStatus.UNAUTHORIZED)
            self.send_header("WWW-Authenticate", 'Basic realm="InkDeck stats"')
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        path = self.path.split("?")[0]
        if path == "/api/stats":
            if _cache["json"] is None:
                body = json.dumps({"error": _cache["error"] or "Still loading, try again shortly."}).encode()
                return self.send(503, body, "application/json")
            return self.send(200, _cache["json"], "application/json")
        if path in ("/", "/index.html"):
            return self.send(200, (DASHBOARD / "index.html").read_bytes(), "text/html; charset=utf-8")
        self.send(404, b"not found", "text/plain")

    def do_POST(self):
        if not self.authorized():
            return self.send(401, b"", "text/plain")
        if self.path == "/api/refresh":
            refresh()
            return self.send(200, b"{}", "application/json")
        self.send(404, b"not found", "text/plain")


def main() -> None:
    if WRITE_BACK:
        log("write-back on: companion.md and weather are committed to the repo")
    if not PASSWORD:
        log("DASHBOARD_PASSWORD is not set: anyone who can reach this port can read your stats")
    threading.Thread(target=refresher, daemon=True).start()
    log(f"serving on :{PORT}")
    ThreadingHTTPServer(("", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()

"""Reads an InkDeck notes folder (a clone of the GitJournal repo) into stats.

Sources:
  * journal entries: the `sessions:` records InkDeck writes into front matter
    (times, typing details, parts, location);
  * usage logs: `_inkdeck/usage/YYYY-MM.csv` (sessions in other notes, syncs,
    battery, keyboard).

Journal sessions appear in both; the front matter record is used (it has
parts and location), and usage-log rows marked `journal` are only used for
entries without one.
"""

from __future__ import annotations

import csv
import datetime as dt
import os
from collections import defaultdict
from pathlib import Path

import yaml

USAGE_DIR = Path("_inkdeck") / "usage"
SESSION_FIELDS = ("active_s", "keys", "words", "wpm", "typed", "deleted", "pauses",
                  "longest_pause_s", "longest_burst_s", "first_key_s", "revisions")


def parse_time(value) -> dt.datetime | None:
    """ISO 8601 with offset (as InkDeck writes it); YAML may hand back a datetime."""
    if isinstance(value, dt.datetime):
        return value
    if not value:
        return None
    try:
        return dt.datetime.fromisoformat(str(value).replace("Z", "+00:00"))
    except ValueError:
        return None


def front_matter(text: str) -> dict:
    if not text.startswith("---\n"):
        return {}
    end = text.find("\n---", 4)
    if end < 0:
        return {}
    try:
        data = yaml.safe_load(text[4:end])
    except yaml.YAMLError:
        return {}
    return data if isinstance(data, dict) else {}


def _int(value) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def journal_sessions(root: Path) -> list[dict]:
    sessions = []
    for path in sorted(root.rglob("*.md")):
        rel = path.relative_to(root)
        if any(part.startswith(".") for part in rel.parts):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for s in front_matter(text).get("sessions") or []:
            if not isinstance(s, dict):
                continue
            start = parse_time(s.get("start"))
            if not start:
                continue
            item = {
                "start": start,
                "end": parse_time(s.get("end")) or start,
                "file": rel.as_posix(),
                "kind": "journal",
                **{k: _int(s.get(k)) for k in SESSION_FIELDS},
                "parts": [],
                "place": None,
                "lat": None,
                "lon": None,
                "place_tag": s.get("place_tag") or None,
                "weather": s.get("weather") if isinstance(s.get("weather"), dict) else None,
                "prompts": [str(x) for x in s.get("prompts") or [] if x] if isinstance(s.get("prompts"), list) else [],
            }
            loc = s.get("location")
            if isinstance(loc, dict) and "lat" in loc and "lon" in loc:
                item.update(lat=float(loc["lat"]), lon=float(loc["lon"]), place=loc.get("place") or None)
            for p in s.get("parts") or []:
                if isinstance(p, dict):
                    item["parts"].append({"start": str(p.get("start", "")), "end": str(p.get("end", "")),
                                          "active_s": _int(p.get("active_s")), "words": _int(p.get("words"))})
            sessions.append(item)
    return sessions


def journal_extras(root: Path) -> tuple[list[dict], dict]:
    """Check-ins (front matter `checkins:` items) and per-day weather (`day:` blocks)."""
    checkins, days = [], {}
    for path in sorted(root.rglob("*.md")):
        rel = path.relative_to(root)
        if any(part.startswith(".") for part in rel.parts):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        if "checkins:" not in text and "\nday:" not in text:
            continue
        fm = front_matter(text)
        for c in fm.get("checkins") or []:
            if isinstance(c, dict) and parse_time(c.get("time")):
                checkins.append({**{k: v for k, v in c.items() if k != "time"},
                                 "time": parse_time(c.get("time")), "file": rel.as_posix()})
        day = fm.get("day")
        created = parse_time(fm.get("created"))
        if isinstance(day, dict) and created:
            days[created.date().isoformat()] = day
    checkins.sort(key=lambda c: c["time"])
    return checkins, days


def usage_events(root: Path) -> list[dict]:
    events = []
    folder = root / USAGE_DIR
    if not folder.is_dir():
        return events
    for path in sorted(folder.glob("*.csv")):
        with path.open(encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                when = parse_time(row.get("time"))
                if not when or not row.get("event"):
                    continue
                row = dict(row)
                row["time"] = when
                for k in ("duration_s", "active_s", "keys", "typed", "deleted", "words", "wpm"):
                    row[k] = _int(row.get(k))
                row["battery"] = _int(row["battery"]) if row.get("battery") not in (None, "") else None
                events.append(row)
    events.sort(key=lambda e: e["time"])
    return events


def _streaks(days: set[dt.date], today: dt.date) -> tuple[int, int]:
    best = run = 0
    prev = None
    for d in sorted(days):
        run = run + 1 if prev and d == prev + dt.timedelta(days=1) else 1
        best = max(best, run)
        prev = d
    current = 0
    d = today if today in days else today - dt.timedelta(days=1)
    while d in days:
        current += 1
        d -= dt.timedelta(days=1)
    return current, best


def compute(root: Path, today: dt.date | None = None) -> dict:
    root = Path(root)
    journal = journal_sessions(root)
    events = usage_events(root)
    checkins, weather_days = journal_extras(root)

    # Sessions: journal records from front matter, other notes from the log,
    # plus logged journal sessions for entries that have no record.
    journal_files = {s["file"] for s in journal}
    sessions = list(journal)
    for e in events:
        if e["event"] != "session":
            continue
        if e.get("detail") == "journal" and e.get("path") in journal_files:
            continue
        sessions.append({
            "start": e["time"] - dt.timedelta(seconds=e["duration_s"]),
            "end": e["time"],
            "file": e.get("path") or "",
            "kind": e.get("detail") or "note",
            **{k: e.get(k, 0) for k in ("active_s", "keys", "words", "wpm", "typed", "deleted")},
            "pauses": 0, "longest_pause_s": 0, "longest_burst_s": 0, "first_key_s": 0, "revisions": 0,
            "parts": [], "place": None, "lat": None, "lon": None, "place_tag": None, "weather": None, "prompts": [],
        })
    sessions.sort(key=lambda s: s["start"])

    if today is None:
        today = (sessions[-1]["start"].date() if sessions else dt.date.today())
        today = max(today, dt.date.today())

    by_day = defaultdict(lambda: {"words": 0, "active_s": 0, "sessions": 0})
    hour_weekday = [[0] * 24 for _ in range(7)]
    files = defaultdict(lambda: {"active_s": 0, "words": 0, "sessions": 0})
    places = {}
    for s in sessions:
        day = s["start"].date()
        d = by_day[day]
        d["words"] += s["words"]
        d["active_s"] += s["active_s"]
        d["sessions"] += 1
        hour_weekday[s["start"].weekday()][s["start"].hour] += s["active_s"]
        f = files[s["file"]]
        f["active_s"] += s["active_s"]
        f["words"] += s["words"]
        f["sessions"] += 1
        if s["lat"] is not None:
            key = s["place"] or f"{s['lat']:.3f},{s['lon']:.3f}"
            p = places.setdefault(key, {"place": key, "lat": s["lat"], "lon": s["lon"], "sessions": 0, "words": 0})
            p["sessions"] += 1
            p["words"] += s["words"]

    written = {d for d, v in by_day.items() if v["active_s"] > 0 or v["words"] > 0}
    streak, best_streak = _streaks(written, today)
    month_start = today.replace(day=1)
    week_start = today - dt.timedelta(days=today.weekday())

    def window(start: dt.date) -> dict:
        out = {"words": 0, "active_s": 0, "sessions": 0}
        for day, v in by_day.items():
            if start <= day <= today:
                for k in out:
                    out[k] += v[k]
        return out

    total_active = sum(s["active_s"] for s in sessions)
    total_words = sum(s["words"] for s in sessions)
    total_typed = sum(s["typed"] for s in sessions)
    total_deleted = sum(s["deleted"] for s in sessions)
    best_day = max(by_day.items(), key=lambda kv: kv[1]["words"], default=(None, None))

    def iso(t: dt.datetime) -> str:
        return t.isoformat()

    syncs = [{"time": iso(e["time"]), "duration_s": e["duration_s"], "ok": e.get("detail", "").endswith(" ok"),
              "detail": e.get("detail", "")} for e in events if e["event"] == "sync"]
    battery = [{"time": iso(e["time"]), "battery": e["battery"], "event": e["event"]}
               for e in events if e["battery"] is not None]
    keyboard_s = sum(e["duration_s"] for e in events
                     if e["event"] == "keyboard" and (e.get("detail") or "").startswith("disconnected"))

    return {
        "generated": dt.datetime.now(dt.timezone.utc).isoformat(),
        "today": today.isoformat(),
        "totals": {
            "words": total_words,
            "active_s": total_active,
            "sessions": len(sessions),
            "days_written": len(written),
            "streak": streak,
            "best_streak": best_streak,
            "avg_wpm": round(total_words * 60 / total_active) if total_active >= 30 and total_words > 0 else 0,
            "deleted_pct": round(total_deleted * 100 / total_typed) if total_typed else 0,
            "best_day": best_day[0].isoformat() if best_day[0] else None,
            "best_day_words": best_day[1]["words"] if best_day[1] else 0,
            "today": window(today),
            "week": window(week_start),
            "month": window(month_start),
        },
        "days": [{"date": d.isoformat(), **v} for d, v in sorted(by_day.items())],
        "hour_weekday": hour_weekday,
        "sessions": [{**{k: v for k, v in s.items() if k not in ("start", "end")},
                      "start": iso(s["start"]), "end": iso(s["end"])} for s in sessions],
        "checkins": [{**c, "time": iso(c["time"])} for c in checkins],
        "weather_days": weather_days,
        "places": sorted(places.values(), key=lambda p: -p["sessions"]),
        "files": sorted(({"path": k, **v} for k, v in files.items()), key=lambda f: -f["active_s"])[:20],
        "device": {
            "syncs": syncs,
            "battery": battery,
            "keyboard_s": keyboard_s,
            "boots": sum(1 for e in events if e["event"] == "boot"),
            "events": {name: sum(1 for e in events if e["event"] == name)
                       for name in ("create", "rename", "delete")},
        },
    }


if __name__ == "__main__":
    import json
    import sys

    print(json.dumps(compute(Path(sys.argv[1] if len(sys.argv) > 1 else os.getcwd())), indent=2))

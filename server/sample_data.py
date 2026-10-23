"""Writes a fake InkDeck notes folder: ~4 months of journal entries with
session records, notes, and usage logs. For trying the dashboard and tests.

    python sample_data.py /tmp/sample-notes
"""

from __future__ import annotations

import csv
import datetime as dt
import random
import sys
from pathlib import Path

TZ = dt.timezone(dt.timedelta(hours=-7))
PLACES = [("Home", 45.5231, -122.6765), ("Café on Alberta", 45.5590, -122.6450), ("Library", 45.5193, -122.6829)]
HEADER = "time,event,path,duration_s,active_s,keys,typed,deleted,words,wpm,battery,detail".split(",")


def write(root: Path, days: int = 120, end: dt.date | None = None, seed: int = 7) -> None:
    rnd = random.Random(seed)
    end = end or dt.date.today()
    (root / "_inkdeck" / "usage").mkdir(parents=True, exist_ok=True)
    (root / "Ideas").mkdir(parents=True, exist_ok=True)
    rows: dict[str, list[list]] = {}
    battery = 100

    def log(t: dt.datetime, event: str, path="", duration=0, active=0, keys=0, typed=0, deleted=0, words=0, wpm=0,
            detail=""):
        nonlocal battery
        rows.setdefault(t.strftime("%Y-%m"), []).append(
            [t.isoformat(), event, path, duration, active, keys, typed, deleted, words, wpm, battery, detail])

    for i in range(days):
        day = end - dt.timedelta(days=days - 1 - i)
        if rnd.random() < 0.18:
            continue  # a day off
        sessions = []
        for _ in range(rnd.choice([1, 1, 1, 2, 2, 3])):
            hour = rnd.choice([6, 7, 7, 8, 12, 21, 22]) if rnd.random() < 0.8 else rnd.randint(9, 20)
            start = dt.datetime(day.year, day.month, day.day, hour, rnd.randint(0, 59), tzinfo=TZ)
            active = rnd.randint(240, 1800)
            wpm = rnd.randint(18, 42)
            words = max(20, int(active / 60 * wpm))
            typed = words * 6 + rnd.randint(0, 200)
            deleted = int(typed * rnd.uniform(0.04, 0.2))
            pauses = rnd.randint(0, 4)
            parts, t = [], start + dt.timedelta(seconds=rnd.randint(3, 40))
            for _ in range(rnd.randint(1, 6)):
                p_active = rnd.randint(30, 400)
                p_end = t + dt.timedelta(seconds=p_active + rnd.randint(0, 120))
                parts.append({"start": t.strftime("%H:%M:%S"), "end": p_end.strftime("%H:%M:%S"),
                              "active_s": p_active, "words": rnd.randint(15, 160)})
                t = p_end + dt.timedelta(seconds=rnd.randint(5, 300))
            place = rnd.choice(PLACES) if rnd.random() < 0.85 else None
            sessions.append((start, active, wpm, words, typed, deleted, pauses, parts, place))
            battery = max(5, battery - rnd.randint(1, 4))
            log(start + dt.timedelta(seconds=active + 200), "session", f"{day.isoformat()}.md", active + 200, active,
                typed + deleted, typed, deleted, words, wpm, "journal")
        lines = ["---", f"created: {sessions[0][0].isoformat()}", f"modified: {sessions[-1][0].isoformat()}",
                 "type: Journal", "sessions:"]
        body = []
        for start, active, wpm, words, typed, deleted, pauses, parts, place in sessions:
            end_t = start + dt.timedelta(seconds=active + 200)
            lines += [f"  - start: {start.isoformat()}", f"    end: {end_t.isoformat()}", f"    active_s: {active}",
                      f"    keys: {typed + deleted}", f"    words: {words}", f"    wpm: {wpm}", f"    typed: {typed}",
                      f"    deleted: {deleted}", f"    pauses: {pauses}",
                      f"    longest_pause_s: {rnd.randint(61, 900) if pauses else 0}",
                      f"    longest_burst_s: {rnd.randint(20, 400)}", f"    first_key_s: {rnd.randint(1, 60)}",
                      f"    revisions: {rnd.randint(0, 15)}"]
            if place:
                lines.append(f"    location: {{lat: {place[1]:.6f}, lon: {place[2]:.6f}, accuracy_m: "
                             f"{rnd.randint(8, 40)}, time: {start.isoformat()}, source: phone, place: \"{place[0]}\"}}")
            lines.append("    parts:")
            lines += [f"      - {{start: \"{p['start']}\", end: \"{p['end']}\", active_s: {p['active_s']}, "
                      f"words: {p['words']}}}" for p in parts]
            body.append(f"## {start.strftime('%H:%M')}\n\nWrote {words} words.\n")
        (root / f"{day.isoformat()}.md").write_text("\n".join(lines) + "\n---\n\n" + "\n".join(body), encoding="utf-8")

        if rnd.random() < 0.3:
            t = dt.datetime(day.year, day.month, day.day, 21, 30, tzinfo=TZ)
            log(t, "session", "Ideas/Book outline.md", 900, 600, 1500, 1300, 200, 180, 18, "note")
        if rnd.random() < 0.5:
            t = dt.datetime(day.year, day.month, day.day, 9, 0, tzinfo=TZ)
            ok = rnd.random() > 0.08
            log(t, "sync", "", rnd.randint(6, 30), detail=f"down={rnd.randint(0, 3)} up={rnd.randint(1, 4)} "
                f"deleted=0 conflicts=0 {'ok' if ok else 'failed'}")
        if rnd.random() < 0.15:
            battery = 100
            log(dt.datetime(day.year, day.month, day.day, 23, 0, tzinfo=TZ), "boot", detail="cold")
        log(dt.datetime(day.year, day.month, day.day, 23, 30, tzinfo=TZ), "keyboard",
            duration=rnd.randint(1200, 7200), detail="disconnected")

    (root / "Ideas" / "Book outline.md").write_text("# Book outline\n\nSee [[2026-10-01]].\n", encoding="utf-8")
    for month, month_rows in rows.items():
        with (root / "_inkdeck" / "usage" / f"{month}.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(HEADER)
            w.writerows(month_rows)


if __name__ == "__main__":
    write(Path(sys.argv[1]))
    print(f"Sample notes written to {sys.argv[1]}")

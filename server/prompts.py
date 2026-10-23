"""Journaling prompts: suggestions picked from your journal, and evidence that
journaling is doing something for you.

Two common reasons people stop journaling are not knowing where to start and
not feeling any benefit. The X4 answers the first with a prompt picker (built-in
library, prompts/library.txt). This module helps with both, from your own data:

  * suggestions   up to five prompts written to `_inkdeck/prompts-suggested.txt`
                  (the X4 lists them under "For you"): a look back at what you
                  wrote on this day a week, month or year ago; one matched to
                  your latest check-in (stress, energy, sleep, people); one from
                  the kind of prompt you write most with; one you haven't tried.
  * insights      plain statements from your own numbers, e.g. "With a prompt
                  you write 40% more" or "On days you journal, your evening
                  stress averages 2.1 vs 3.0". Only shown with enough data, and
                  only as what they are: what your journal shows, not a promise.
  * usage         which prompts and categories you've used, and how much you
                  wrote after each.

Pure functions of stats.compute() output and the library text; no state.
"""

from __future__ import annotations

import datetime as dt
import hashlib
import os
import re
from collections import defaultdict
from pathlib import Path

from stats import front_matter, parse_time

LIBRARY = Path(os.environ.get("PROMPTS_LIBRARY") or Path(__file__).resolve().parent.parent / "prompts" / "library.txt")
USER_PATH = "_inkdeck/prompts.txt"
SUGGESTED_PATH = "_inkdeck/prompts-suggested.txt"


# --- Library ----------------------------------------------------------------


def parse_library(text: str) -> tuple[list[dict], list[dict]]:
    """(categories, prompts) from the library format (see prompts/library.txt)."""
    categories, items = [], []
    current = {"id": "", "name": "", "why": ""}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("@"):
            parts = [p.strip() for p in line[1:].split("|", 2)]
            current = {"id": parts[0], "name": parts[1] if len(parts) > 1 else parts[0],
                       "why": parts[2] if len(parts) > 2 else ""}
            categories.append(current)
            continue
        parts = [p.strip() for p in line.split("|", 2)]
        if len(parts) >= 2 and parts[0] and parts[1]:
            items.append({"id": parts[0], "text": parts[1], "why": parts[2] if len(parts) > 2 and parts[2] else current["why"],
                          "category": current["id"], "category_name": current["name"]})
    return categories, items


def load_library(root: Path | None = None, library: Path = LIBRARY) -> tuple[list[dict], list[dict]]:
    """The built-in library plus the user's own prompts (if any)."""
    text = library.read_text(encoding="utf-8") if library.exists() else ""
    if root is not None and (Path(root) / USER_PATH).exists():
        text += "\n" + (Path(root) / USER_PATH).read_text(encoding="utf-8")
    return parse_library(text)


def daily_index(day: int, count: int) -> int:
    """Same as prompts::dailyIndex on the X4 (days since 1970)."""
    if not count:
        return 0
    stride = 37
    while _gcd(stride, count) != 1:
        stride += 1
    return (abs(day) * stride) % count


def _gcd(a: int, b: int) -> int:
    while b:
        a, b = b, a % b
    return a


def _seed(*parts) -> int:
    return int(hashlib.sha1("|".join(map(str, parts)).encode()).hexdigest(), 16)


# --- Usage ------------------------------------------------------------------


def usage(data: dict, items: list[dict]) -> dict:
    by_id = {p["id"]: p for p in items}
    per_prompt: dict[str, dict] = {}
    per_cat: dict[str, dict] = defaultdict(lambda: {"sessions": 0, "words": 0, "name": ""})
    prompted, plain = [], []
    last_used: dict[str, str] = {}
    for s in data.get("sessions", []):
        if s.get("kind") != "journal":
            continue
        ids = s.get("prompts") or []
        words = max(0, s.get("words", 0))
        (prompted if ids else plain).append(words)
        for pid in ids:
            p = by_id.get(pid, {})
            cat = p.get("category") or pid.split("-")[0]
            e = per_prompt.setdefault(pid, {"id": pid, "text": p.get("text", pid), "category": cat, "sessions": 0, "words": 0})
            e["sessions"] += 1
            e["words"] += words
            per_cat[cat]["name"] = p.get("category_name") or ("Look back" if cat == "revisit" else cat)
            per_cat[cat]["sessions"] += 1
            per_cat[cat]["words"] += words
            last_used[pid] = s["start"][:10]
    avg = lambda xs: round(sum(xs) / len(xs)) if xs else 0  # noqa: E731
    return {
        "prompted_sessions": len(prompted),
        "plain_sessions": len(plain),
        "avg_words_prompted": avg(prompted),
        "avg_words_plain": avg(plain),
        "prompts": sorted(per_prompt.values(), key=lambda e: (-e["sessions"], -e["words"])),
        "categories": {k: {**v, "avg_words": round(v["words"] / v["sessions"])} for k, v in per_cat.items()},
        "last_used": last_used,
    }


# --- Insights ---------------------------------------------------------------

CHECKIN_SIGNALS = (
    # key, label, higher is better
    ("energy", "evening energy", True),
    ("stress", "evening stress", False),
    ("mood", "evening mood", True),
)


def insights(data: dict, use: dict, today: dt.date) -> list[str]:
    """What your own data shows, most telling first. Empty without enough data."""
    out = []
    days = {d["date"]: d for d in data.get("days", [])}
    wrote = {d for d, v in days.items() if v.get("words", 0) > 0 or v.get("active_s", 0) > 0}

    # Check-ins on journaling days vs other days (descriptive, not causal).
    for key, label, higher_better in CHECKIN_SIGNALS:
        on, off = [], []
        for c in data.get("checkins", []):
            if c.get("kind") != "evening" or not isinstance(c.get(key), (int, float)):
                continue
            (on if c["time"][:10] in wrote else off).append(float(c[key]))
        if len(on) >= 4 and len(off) >= 4:
            a, b = sum(on) / len(on), sum(off) / len(off)
            if (a - b >= 0.3) if higher_better else (b - a >= 0.3):
                out.append(f"On days you journal, your {label} averages {a:.1f}, vs {b:.1f} on days you don't "
                           f"({len(on)} and {len(off)} evenings).")

    # Prompts and how much you write.
    a, b = use["avg_words_prompted"], use["avg_words_plain"]
    if use["prompted_sessions"] >= 3 and use["plain_sessions"] >= 3 and b and a >= b * 1.1:
        out.append(f"With a prompt you write {round((a - b) * 100 / b)}% more: {a} words a session vs {b}.")
    used = [(k, v) for k, v in use["categories"].items() if v["sessions"] >= 2]
    if len(used) >= 2:
        _, v = max(used, key=lambda kv: kv[1]["avg_words"])
        out.append(f"Your longest sessions follow \"{v['name']}\" prompts: {v['avg_words']} words on average.")

    # Consistency and the total.
    recent = sum(1 for i in range(14) if (today - dt.timedelta(days=i)).isoformat() in wrote)
    if recent >= 3:
        out.append(f"You've journaled on {recent} of the last 14 days.")
    total = data.get("totals", {}).get("words", 0)
    if total >= 2000:
        pages = total // 250
        out.append(f"You've written {total:,} words so far, about {pages:,} printed pages.")
    return out


# --- Suggestions ------------------------------------------------------------


def _first_sentence(text: str) -> str | None:
    """The first real line of an entry's body: no headings, quotes or prompts."""
    end = text.find("\n---", 4) if text.startswith("---\n") else -1
    body = text[end + 4:] if end >= 0 else text
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith(("#", ">", "|", "```", "- [", "![")):
            continue
        line = re.sub(r"[*_`\[\]]", "", line)
        sentence = re.split(r"(?<=[.!?])\s", line, maxsplit=1)[0]
        if len(sentence.split()) < 3:
            continue
        return sentence if len(sentence) <= 90 else sentence[:87].rsplit(" ", 1)[0] + "..."
    return None


def _entry_for(root: Path, day: dt.date) -> Path | None:
    name = f"{day.isoformat()}.md"
    for path in root.rglob(name):
        if not any(part.startswith(".") for part in path.relative_to(root).parts):
            return path
    return None


LOOK_BACKS = ((365, "A year ago today"), (182, "Six months ago"), (30, "A month ago"), (7, "A week ago"))

# Latest check-in -> the kind of prompt that fits.
CHECKIN_RULES = (
    (lambda c: _num(c.get("stress")) >= 4, "stress", "Your last check-in had high stress."),
    (lambda c: 0 < _num(c.get("sleep_h")) < 6, "body-sleep", "You slept under 6 hours last time you checked in."),
    (lambda c: 0 < _num(c.get("energy")) <= 2, "body", "Your energy was low at your last check-in."),
    (lambda c: c.get("social") == 0, "people", "You hadn't seen anyone at your last check-in."),
)


def _num(v) -> float:
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else 0.0


def suggestions(root: Path | None, data: dict, items: list[dict], use: dict, today: dt.date, limit: int = 5) -> list[dict]:
    """Prompts for today, each with the reason it was picked (shown as its "why")."""
    out: list[dict] = []
    recent = {pid for pid, d in use["last_used"].items() if (today - dt.date.fromisoformat(d)).days < 14}
    taken = set(recent)

    def add(p: dict, why: str) -> None:
        if len(out) < limit and p["id"] not in taken:
            out.append({**p, "why": why})
            taken.add(p["id"])

    # 1. Look back at your own words: the clearest way to see journaling pay off.
    if root is not None:
        for days_ago, when in LOOK_BACKS:
            day = today - dt.timedelta(days=days_ago)
            path = _entry_for(Path(root), day)
            sentence = _first_sentence(path.read_text(encoding="utf-8")) if path else None
            if sentence:
                add({"id": f"revisit-{day.isoformat()}", "category": "foryou",
                     "text": f'{when} you wrote: "{sentence}" What has changed since?'},
                    "Looking back at your own words shows you how far you've come.")
                break

    # 2. Matched to your latest check-in (within the last two days).
    checkins = [c for c in data.get("checkins", []) if parse_time(c.get("time"))]
    if checkins:
        last = checkins[-1]
        if (today - parse_time(last["time"]).date()).days <= 2:
            for test, target, reason in CHECKIN_RULES:
                if test(last):
                    pool = [p for p in items if p["id"] == target or p["category"] == target]
                    pool = [p for p in pool if p["id"] not in taken] or pool
                    if pool:
                        p = pool[_seed(today, target) % len(pool)]
                        add(p, f"{reason} {p['why']}")
                    break

    # 3. More of what works for you.
    cats = use["categories"]
    best = max(((k, v) for k, v in cats.items() if v["sessions"] >= 2 and k != "foryou"),
               key=lambda kv: kv[1]["avg_words"], default=None)
    if best:
        pool = [p for p in items if p["category"] == best[0] and p["id"] not in taken]
        if pool:
            p = pool[_seed(today, "best") % len(pool)]
            add(p, f"You write the most after \"{p['category_name'] or best[0]}\" prompts "
                   f"({best[1]['avg_words']} words on average).")

    # 4. Something new.
    tried = set(cats)
    fresh = [p for p in items if p["category"] not in tried and p["category"] != "quick"]
    if fresh:
        p = fresh[_seed(today, "new") % len(fresh)]
        add(p, f"Something you haven't tried yet: {p['category_name']}. {p['why']}")

    # 5. At least three, topped up with quick ones: there's always an easy start.
    quick = [p for p in items if p["category"] in ("start", "quick")]
    first = _seed(today, "quick") % len(quick) if quick else 0
    for p in quick[first:] + quick[:first]:
        if len(out) >= min(limit, 3):
            break
        add(p, p["why"])
    return out


def suggestions_file(items: list[dict]) -> str:
    """`_inkdeck/prompts-suggested.txt` in the library format the X4 reads."""
    lines = ["# Picked for today by your InkDeck server. Rewritten each day; edits are overwritten.",
             "@ foryou | For you | Picked from your own journal."]
    for p in items:
        text = p["text"].replace("|", "/").replace("\n", " ")
        why = p["why"].replace("\n", " ")
        lines.append(f"{p['id']} | {text} | {why}")
    return "\n".join(lines) + "\n"


def compute(root: Path | None, data: dict, today: dt.date | None = None, library: Path = LIBRARY) -> dict:
    today = today or dt.date.fromisoformat(data["today"])
    _, items = load_library(root, library)
    use = usage(data, items)
    return {
        "library_size": len(items),
        "usage": use,
        "insights": insights(data, use, today),
        "suggestions": suggestions(root, data, items, use, today),
        "categories": {p["id"]: p["category"] for p in items},
        "daily": items[daily_index((today - dt.date(1970, 1, 1)).days, len(items))] if items else None,
    }

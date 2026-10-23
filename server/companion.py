"""Gamification and the journaling pet, derived from the journal history.

Everything here is a pure function of the stats (stats.compute) and a few
settings, replayed day by day from the first entry. There is no hidden state:
the same repository always gives the same level, achievements and pet, so the
server can recompute it at any time and nothing is lost if it restarts.

  XP        words/10 + 10 per session of 5+ active minutes + 15 per check-in
            + 5 per prompt answered (30+ words) + 25 per day the daily goal
            was met + 50 per achievement
  Level L   needs 50*L*(L-1) XP: level 2 at 100, 3 at 300, 4 at 600, ...
  Streak    consecutive writing days; up to `rest_days` missed days per
            (Monday-based) week are rest days that keep the streak alive.
  Pet       hatches from an egg after 3 writing days, then grows up (baby,
            child, teen, adult). Writing feeds it, check-ins, streaks and
            achievements cheer it up, days without writing make it hungry and
            then ill. Its adult form depends on how you write.
"""

from __future__ import annotations

import datetime as dt
import hashlib
from collections import defaultdict
from dataclasses import dataclass, field

from stats import parse_time

# --- Settings ---------------------------------------------------------------


@dataclass
class Settings:
    daily_goal: int = 250        # words
    rest_days: int = 1           # per week that don't break a streak
    pet_name: str = "Inkling"
    pet_mode: str = "gentle"     # gentle: a neglected pet hibernates until you write again
                                 # classic: it dies, and a new egg arrives with the next entry


# --- Achievements -----------------------------------------------------------

# id: (title, description). Order is display order.
ACHIEVEMENTS = {
    "first_words": ("First words", "Write your first journal entry."),
    "streak_3": ("Warming up", "Write 3 days in a row."),
    "streak_7": ("One week", "Write 7 days in a row."),
    "streak_30": ("One month", "Write 30 days in a row."),
    "streak_100": ("Centurion", "Write 100 days in a row."),
    "words_1k": ("1,000 words", "Write 1,000 words in total."),
    "words_10k": ("10,000 words", "Write 10,000 words in total."),
    "words_50k": ("A novel's worth", "Write 50,000 words in total."),
    "words_100k": ("100,000 words", "Write 100,000 words in total."),
    "goal_7": ("On target", "Meet your daily goal 7 days in a row."),
    "early_bird": ("Early bird", "Write 50+ words in a session before 7:00."),
    "night_owl": ("Night owl", "Write 50+ words in a session after 23:00."),
    "marathon": ("Marathon", "Type for 30 active minutes in one session."),
    "sprinter": ("Sprinter", "Write 100+ words at 40+ words per minute."),
    "weekend": ("Weekend writer", "Write on both days of a weekend."),
    "checkins_7": ("Checking in", "Complete 7 check-ins."),
    "checkin_week": ("Steady", "Check in 7 days in a row."),
    "reflector": ("Reflector", "Complete 7 evening reflections."),
    "rain_writer": ("Rainy day", "Write while it rains."),
    "sunrise_writer": ("First light", "Write around sunrise."),
    "explorer": ("Explorer", "Write in 5 different places."),
    "pet_adult": ("All grown up", "Raise your pet to adulthood."),
    "prompted_5": ("Prompted", "Answer 5 journaling prompts."),
    "curious": ("Curious", "Answer prompts from 6 different categories."),
    "time_traveler": ("Time traveler", "Answer a look-back prompt about an old entry."),
}


def level_for(xp: int) -> tuple[int, int, int]:
    """(level, XP at which this level started, XP needed for the next)."""
    level = 1
    while xp >= 50 * (level + 1) * level:
        level += 1
    return level, 50 * level * (level - 1), 50 * (level + 1) * level


# --- Pet --------------------------------------------------------------------

STAGES = (("egg", 0), ("baby", 0), ("child", 7), ("teen", 21), ("adult", 60))  # days since hatching
HATCH_DAYS = 3  # writing days to hatch

FACES = {
    "egg": "( )",
    "happy": "(^_^)",
    "content": "(-_-)",
    "hungry": "(o_o)",
    "sad": "(;_;)",
    "sick": "(x_x)",
    "asleep": "(-.-)zz",
    "gone": "( . )",
}

SAYS = {
    "egg": ["Something is moving inside...", "Keep writing. I'm almost ready!", "Tap, tap..."],
    "happy": ["That was a good one!", "More words, please!", "I love it when you write.", "Best day ever."],
    "content": ["Hi again.", "What's on your mind?", "I'm here when you're ready."],
    "hungry": ["My tummy is rumbling...", "Just a few sentences?", "I could really use some words."],
    "sad": ["I missed you.", "It's quiet without you.", "Tell me about your day?"],
    "sick": ["I don't feel so good...", "Words are the only medicine.", "Please write something."],
    "asleep": ["Zzz... (write on 3 days to wake me)"],
    "gone": ["(A new egg will arrive with your next entry.)"],
}

# Adult forms, picked from how you wrote while it grew up.
FORMS = {
    "scholar": "Scholar - met the daily goal most days",
    "early_bird": "Early bird - mostly morning writing",
    "night_owl": "Night owl - mostly late writing",
    "wanderer": "Wanderer - written in many places",
    "chatterbox": "Chatterbox - long, fast sessions",
    "dreamer": "Dreamer - a little of everything",
}


@dataclass
class Pet:
    name: str
    generation: int = 1
    born: dt.date | None = None      # egg arrived
    hatched: dt.date | None = None
    writing_days: int = 0            # while an egg
    fullness: float = 60
    happiness: float = 60
    health: float = 100
    state: str = "egg"               # egg | alive | asleep | gone
    revive_days: int = 0
    form: str | None = None
    history: list = field(default_factory=list)  # past pets (classic mode)
    # Habits while growing up, for the adult form.
    morning: int = 0
    night: int = 0
    goal_days: int = 0
    days_alive: int = 0
    places: set = field(default_factory=set)
    long_fast: int = 0

    def stage(self, today: dt.date) -> str:
        if self.state in ("egg",) or not self.hatched:
            return "egg"
        age = (today - self.hatched).days
        name = "baby"
        for stage, days in STAGES[1:]:
            if age >= days:
                name = stage
        return name


def _clamp(v: float) -> float:
    return max(0.0, min(100.0, v))


def food_for(words: int, goal: int) -> float:
    """Fullness a day's words add: the daily goal is a full meal (70)."""
    return min(100.0, max(0, words) * 70.0 / max(1, goal))


def _pick(options: list[str], seed: str) -> str:
    return options[int(hashlib.sha1(seed.encode()).hexdigest(), 16) % len(options)]


# --- Replay -----------------------------------------------------------------


def compute(data: dict, settings: Settings | None = None, today: dt.date | None = None) -> dict:
    """Level, XP, achievements, streaks and the pet from stats.compute() output."""
    s = settings or Settings()
    today = today or dt.date.fromisoformat(data["today"])

    sessions = []
    for x in data.get("sessions", []):
        start = parse_time(x["start"])
        if start:
            sessions.append({**x, "start_dt": start})
    checkins = [{**c, "time_dt": parse_time(c["time"])} for c in data.get("checkins", []) if parse_time(c.get("time"))]

    day_sessions: dict[dt.date, list] = defaultdict(list)
    for x in sessions:
        day_sessions[x["start_dt"].date()].append(x)
    day_checkins: dict[dt.date, list] = defaultdict(list)
    for c in checkins:
        day_checkins[c["time_dt"].date()].append(c)

    first = min([*day_sessions, *day_checkins], default=None)
    unlocked: dict[str, str] = {}
    xp = 0
    log = []  # (date, xp gained, why) for the last days
    totals = {"words": 0, "checkins": 0, "evening": 0, "prompts": 0}
    prompt_cats: set = set()
    categories = data.get("prompt_categories") or {}
    streak = best_streak = goal_streak = checkin_streak = 0
    rest_used: dict[tuple, int] = defaultdict(int)
    places: set = set()
    pet = Pet(name=s.pet_name)
    today_words = 0

    def unlock(key: str, day: dt.date) -> int:
        if key in unlocked:
            return 0
        unlocked[key] = day.isoformat()
        return 50

    day = first
    while day is not None and day <= today:
        ss = day_sessions.get(day, [])
        cs = day_checkins.get(day, [])
        words = sum(max(0, x.get("words", 0)) for x in ss)
        wrote = words > 0 or any(x.get("active_s", 0) > 0 for x in ss)
        gained = words // 10
        gained += 10 * sum(1 for x in ss if x.get("active_s", 0) >= 300)
        gained += 15 * len(cs)
        answered = [pid for x in ss if x.get("words", 0) >= 30 for pid in x.get("prompts") or []]
        gained += 5 * len(answered)
        totals["prompts"] += len(answered)
        prompt_cats.update(categories.get(pid) or pid.split("-")[0] for pid in answered)
        goal_met = words >= s.daily_goal
        if goal_met:
            gained += 25
        totals["words"] += words
        totals["checkins"] += len(cs)
        totals["evening"] += sum(1 for c in cs if c.get("kind") == "evening" and c.get("learned"))
        if day == today:
            today_words = words

        # Streaks. Today without writing doesn't break anything yet.
        week = day.isocalendar()[:2]
        if wrote:
            streak += 1
        elif day != today:
            if rest_used[week] < s.rest_days and streak > 0:
                rest_used[week] += 1
            else:
                streak = 0
        best_streak = max(best_streak, streak)
        if goal_met:
            goal_streak += 1
        elif day != today:
            goal_streak = 0
        if cs:
            checkin_streak += 1
        elif day != today:
            checkin_streak = 0

        # Achievements.
        new = 0
        if wrote:
            new += unlock("first_words", day)
        for n in (3, 7, 30, 100):
            if streak >= n:
                new += unlock(f"streak_{n}", day)
        for n, key in ((1000, "words_1k"), (10000, "words_10k"), (50000, "words_50k"), (100000, "words_100k")):
            if totals["words"] >= n:
                new += unlock(key, day)
        if goal_streak >= 7:
            new += unlock("goal_7", day)
        if totals["prompts"] >= 5:
            new += unlock("prompted_5", day)
        if len(prompt_cats - {"revisit", "foryou"}) >= 6:
            new += unlock("curious", day)
        if any(pid.startswith("revisit-") for pid in answered):
            new += unlock("time_traveler", day)
        for x in ss:
            h = x["start_dt"].hour
            w = x.get("words", 0)
            if w >= 50 and 4 <= h < 7:
                new += unlock("early_bird", day)
            if w >= 50 and (h >= 23 or h < 4):
                new += unlock("night_owl", day)
            if x.get("active_s", 0) >= 1800:
                new += unlock("marathon", day)
            if w >= 100 and x.get("wpm", 0) >= 40:
                new += unlock("sprinter", day)
            weather = x.get("weather") or {}
            cond = str(weather.get("condition", "")).lower()
            if w > 0 and any(k in cond for k in ("rain", "drizzle", "shower", "thunder")):
                new += unlock("rain_writer", day)
            if w > 0 and weather.get("light") == "sunrise":
                new += unlock("sunrise_writer", day)
            if x.get("lat") is not None:
                places.add(x.get("place_tag") or x.get("place") or f"{x['lat']:.2f},{x['lon']:.2f}")
        if len(places) >= 5:
            new += unlock("explorer", day)
        if day.weekday() == 6 and wrote and day_sessions.get(day - dt.timedelta(days=1)):
            new += unlock("weekend", day)
        if totals["checkins"] >= 7:
            new += unlock("checkins_7", day)
        if checkin_streak >= 7:
            new += unlock("checkin_week", day)
        if totals["evening"] >= 7:
            new += unlock("reflector", day)

        # The pet. Today's food counts right away; the day's wear only once it's over.
        _pet_day(pet, s, day, today, ss, cs, words, wrote, goal_met, streak, new // 50)
        if pet.stage(day) == "adult" and pet.state == "alive":
            new += unlock("pet_adult", day)

        gained += new
        xp += gained
        if gained and (today - day).days < 14:
            log.append({"date": day.isoformat(), "xp": gained, "words": words})
        day += dt.timedelta(days=1)

    level, level_start, level_next = level_for(xp)
    week = today.isocalendar()[:2]
    stage = pet.stage(today)
    mood = _mood(pet, stage)
    pet_out = {
        "name": pet.name,
        "generation": pet.generation,
        "state": pet.state,
        "stage": stage,
        "form": pet.form,
        "form_label": FORMS.get(pet.form or "", ""),
        "mood": mood,
        "face": FACES[mood] if mood in FACES else FACES["content"],
        "says": _pick(SAYS.get(mood, SAYS["content"]), f"{today}{mood}{pet.generation}"),
        "fullness": round(pet.fullness),
        "happiness": round(pet.happiness),
        "health": round(pet.health),
        "age_days": (today - pet.hatched).days if pet.hatched else 0,
        "born": pet.born.isoformat() if pet.born else None,
        "hatched": pet.hatched.isoformat() if pet.hatched else None,
        "eggs_writing_days": pet.writing_days,
        "hatch_days": HATCH_DAYS,
        # The X4 adds what you write today on top of this (fullness = base + words*per_word).
        "fullness_base": round(_clamp(pet.fullness - food_for(today_words, s.daily_goal)) if pet.state == "alive" else pet.fullness),
        "food_per_100_words": round(food_for(100, s.daily_goal), 1),
        "history": pet.history,
    }
    return {
        "settings": {"daily_goal": s.daily_goal, "rest_days": s.rest_days, "pet_mode": s.pet_mode},
        "xp": xp,
        "level": level,
        "level_start": level_start,
        "level_next": level_next,
        "streak": streak,
        "best_streak": best_streak,
        "goal_streak": goal_streak,
        "rest_days_left": max(0, s.rest_days - rest_used[week]),
        "today_words": today_words,
        "goal": s.daily_goal,
        "achievements": [
            {"id": k, "title": t, "description": d, "unlocked": unlocked.get(k)} for k, (t, d) in ACHIEVEMENTS.items()
        ],
        "unlocked": len(unlocked),
        "latest": max(unlocked.items(), key=lambda kv: kv[1], default=(None, None))[0],
        "xp_log": log[::-1],
        "pet": pet_out,
        "insight": (data.get("prompt_insights") or [""])[0],
        "insights": list(data.get("prompt_insights") or []),
    }


def _pet_day(pet: Pet, s: Settings, day, today, ss, cs, words, wrote, goal_met, streak, achievements) -> None:
    if pet.state == "gone":
        if not wrote:
            return
        # Classic mode: a new egg with the first entry after a pet is lost.
        history = pet.history
        gen = pet.generation + 1
        pet.__init__(name=pet.name)
        pet.history, pet.generation = history, gen
    if pet.born is None:
        if not wrote:
            return
        pet.born = day
    if pet.state == "egg":
        if wrote:
            pet.writing_days += 1
        if pet.writing_days >= HATCH_DAYS:
            pet.state, pet.hatched = "alive", day
            pet.fullness, pet.happiness, pet.health = 70, 80, 100
        return
    if pet.state == "asleep":
        # Gentle mode: three writing days (not necessarily in a row) wake it up.
        if wrote:
            pet.revive_days += 1
        if pet.revive_days >= 3:
            pet.state, pet.revive_days = "alive", 0
            pet.fullness, pet.happiness, pet.health = 50, 60, 40
        return

    # Alive: eat and cheer up.
    pet.fullness = _clamp(pet.fullness + food_for(words, s.daily_goal))
    pet.happiness = _clamp(pet.happiness + min(2, len(cs)) * 10 + (10 if streak >= 3 and wrote else 0)
                           + 15 * achievements + (5 if goal_met else 0))
    if wrote:
        pet.days_alive += 1
        for x in ss:
            h = x["start_dt"].hour
            pet.morning += 4 <= h < 10
            pet.night += h >= 22 or h < 4
            if x.get("lat") is not None:
                pet.places.add(x.get("place_tag") or x.get("place") or f"{x['lat']:.2f},{x['lon']:.2f}")
            pet.long_fast += x.get("active_s", 0) >= 900 and x.get("wpm", 0) >= 35
        pet.goal_days += goal_met
    if pet.form is None and pet.stage(day) == "adult":
        pet.form = _form(pet)
    if day >= today:
        return
    # The day is over: it gets hungrier, lonelier if you didn't write, and its
    # health follows how well fed it is.
    pet.fullness = _clamp(pet.fullness - 40)
    pet.happiness = _clamp(pet.happiness - (5 if wrote else 20))
    if pet.fullness <= 0:
        pet.health = _clamp(pet.health - 25)
    elif pet.fullness >= 40:
        pet.health = _clamp(pet.health + 10)
    if pet.health <= 0:
        if s.pet_mode == "classic":
            pet.history.append({"generation": pet.generation, "born": pet.born.isoformat(),
                                "lost": day.isoformat(), "stage": pet.stage(day), "form": pet.form})
            pet.state = "gone"
        else:
            pet.state, pet.revive_days = "asleep", 0


def _form(pet: Pet) -> str:
    days = max(1, pet.days_alive)
    sessions = max(1, pet.morning + pet.night)
    if pet.goal_days / days >= 0.7:
        return "scholar"
    if len(pet.places) >= 5:
        return "wanderer"
    if pet.long_fast >= 10:
        return "chatterbox"
    if pet.morning / sessions >= 0.6 and pet.morning >= 10:
        return "early_bird"
    if pet.night / sessions >= 0.6 and pet.night >= 10:
        return "night_owl"
    return "dreamer"


def _mood(pet: Pet, stage: str) -> str:
    if pet.state in ("egg", "asleep", "gone"):
        return pet.state if pet.state != "egg" else "egg"
    if pet.health < 40:
        return "sick"
    if pet.fullness < 25:
        return "hungry"
    if pet.happiness < 30:
        return "sad"
    if pet.fullness >= 60 and pet.happiness >= 60:
        return "happy"
    return "content"


# --- The file the X4 syncs --------------------------------------------------

COMPANION_PATH = "_inkdeck/companion.md"


def _q(text: str) -> str:
    return '"' + str(text).replace("\\", "\\\\").replace('"', '\\"') + '"'


def companion_markdown(c: dict, now: dt.datetime) -> str:
    """`_inkdeck/companion.md`: flat front matter for the X4, a readable body for GitJournal.

    The X4 reads only `key: value` lines between the first two `---` lines, so
    keep every value on one line.
    """
    p = c["pet"]
    latest = next((a for a in c["achievements"] if a["id"] == c["latest"]), None)
    fm = [
        "---",
        "type: InkDeckCompanion",
        f"updated: {now.isoformat(timespec='seconds')}",
        f"level: {c['level']}",
        f"xp: {c['xp']}",
        f"xp_level_start: {c['level_start']}",
        f"xp_next: {c['level_next']}",
        f"streak: {c['streak']}",
        f"best_streak: {c['best_streak']}",
        f"rest_days_left: {c['rest_days_left']}",
        f"goal: {c['goal']}",
        f"goal_streak: {c['goal_streak']}",
        f"achievements: {c['unlocked']}",
        f"achievements_total: {len(c['achievements'])}",
        f"latest_achievement: {_q(latest['title'] if latest else '')}",
        f"insight: {_q(c.get('insight') or '')}",
        f"pet_name: {_q(p['name'])}",
        f"pet_state: {p['state']}",
        f"pet_stage: {p['stage']}",
        f"pet_form: {p['form'] or ''}",
        f"pet_mood: {p['mood']}",
        f"pet_face: {_q(p['face'])}",
        f"pet_says: {_q(p['says'])}",
        f"pet_age_days: {p['age_days']}",
        f"pet_generation: {p['generation']}",
        f"pet_fullness: {p['fullness']}",
        f"pet_fullness_base: {p['fullness_base']}",
        f"pet_food_per_100_words: {p['food_per_100_words']}",
        f"pet_happiness: {p['happiness']}",
        f"pet_health: {p['health']}",
        f"pet_hatch_progress: {p['eggs_writing_days']}/{p['hatch_days']}",
        "---",
        "",
    ]

    def bar(v: int) -> str:
        n = round(v / 10)
        return "#" * n + "." * (10 - n)

    body = [f"# {p['name']} {p['face']}", ""]
    if p["state"] == "egg":
        body.append(f"An egg. It hatches after {p['hatch_days']} writing days "
                    f"({p['eggs_writing_days']} so far).")
    else:
        stage = p["stage"] + (f" ({p['form_label']})" if p["form"] else "")
        body += [f"*{p['says']}*", "",
                 f"- Stage: {stage}, {p['age_days']} days old",
                 f"- Fullness  `{bar(p['fullness'])}` {p['fullness']}",
                 f"- Happiness `{bar(p['happiness'])}` {p['happiness']}",
                 f"- Health    `{bar(p['health'])}` {p['health']}"]
    span = max(1, c["level_next"] - c["level_start"])
    body += ["", f"## Level {c['level']}", "",
             f"{c['xp']} XP - {c['level_next'] - c['xp']} to level {c['level'] + 1} "
             f"`{bar(round((c['xp'] - c['level_start']) * 100 / span))}`", "",
             f"- Streak: {c['streak']} days (best {c['best_streak']}); rest days left this week: {c['rest_days_left']}",
             f"- Today: {c['today_words']} / {c['goal']} words; goal streak {c['goal_streak']}"]
    if c.get("insights"):
        body += ["", "## What your journal shows", ""] + [f"- {line}" for line in c["insights"]]
    body += ["", f"## Achievements ({c['unlocked']}/{len(c['achievements'])})", ""]
    for a in c["achievements"]:
        mark = "x" if a["unlocked"] else " "
        when = f" - {a['unlocked']}" if a["unlocked"] else ""
        body.append(f"- [{mark}] **{a['title']}**: {a['description']}{when}")
    if p["history"]:
        body += ["", "## In memory", ""]
        for h in p["history"]:
            body.append(f"- Generation {h['generation']}: {h['born']} to {h['lost']} ({h['stage']})")
    body += ["", "_Written by the InkDeck server. Edits here are overwritten._", ""]
    return "\n".join(fm + body)

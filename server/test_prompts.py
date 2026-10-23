import datetime as dt
import tempfile
import unittest
from pathlib import Path

import companion
import prompts

TODAY = dt.date(2026, 10, 23)
TZ = "-07:00"


def session(day: dt.date, words: int, ids=(), hour: int = 9) -> dict:
    return {"start": f"{day.isoformat()}T{hour:02d}:00:00{TZ}", "end": f"{day.isoformat()}T{hour:02d}:20:00{TZ}",
            "kind": "journal", "words": words, "active_s": 600, "wpm": 25, "prompts": list(ids),
            "lat": None, "lon": None, "place": None, "place_tag": None, "weather": None, "file": f"{day}.md"}


def checkin(day: dt.date, **fields) -> dict:
    return {"time": f"{day.isoformat()}T21:00:00{TZ}", "kind": "evening", **fields}


def data(sessions, checkins=(), total_words=None) -> dict:
    days = {}
    for s in sessions:
        d = days.setdefault(s["start"][:10], {"date": s["start"][:10], "words": 0, "active_s": 0, "sessions": 0})
        d["words"] += s["words"]
        d["active_s"] += s["active_s"]
        d["sessions"] += 1
    return {"today": TODAY.isoformat(), "sessions": list(sessions), "checkins": list(checkins),
            "days": list(days.values()), "totals": {"words": total_words or sum(s["words"] for s in sessions)}}


class Library(unittest.TestCase):
    def test_built_in(self):
        cats, items = prompts.load_library()
        self.assertGreaterEqual(len(items), 80)
        self.assertEqual(len({p["id"] for p in items}), len(items))
        self.assertTrue(all(p["why"] and p["category"] for p in items))
        self.assertIn("gratitude", {c["id"] for c in cats})

    def test_user_prompts_added(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "_inkdeck").mkdir()
            (Path(tmp) / prompts.USER_PATH).write_text("@ work | Work | Because.\nwork-1 | Best meeting? |\n")
            _, items = prompts.load_library(Path(tmp))
            mine = next(p for p in items if p["id"] == "work-1")
            self.assertEqual((mine["category"], mine["why"]), ("work", "Because."))

    def test_daily_matches_the_x4(self):
        # Values from prompts::dailyIndex (src/Prompts.cpp).
        self.assertEqual([prompts.daily_index(d, 84) for d in (0, 1, 20384, 20385, 99999)], [0, 37, 56, 9, 15])
        self.assertEqual(prompts.daily_index(20384, 37), 34)


class Insights(unittest.TestCase):
    def test_prompts_help(self):
        days = [TODAY - dt.timedelta(days=i) for i in range(8)]
        s = [session(d, 300, ["grat-three"]) for d in days[:4]] + [session(d, 150) for d in days[4:]]
        out = prompts.compute(None, data(s), TODAY)
        self.assertIn("With a prompt you write 100% more: 300 words a session vs 150.", out["insights"])
        self.assertIn("You've journaled on 8 of the last 14 days.", out["insights"])

    def test_checkins_on_journal_days(self):
        wrote = [TODAY - dt.timedelta(days=i) for i in range(0, 16, 2)]
        skipped = [TODAY - dt.timedelta(days=i) for i in range(1, 16, 2)]
        c = [checkin(d, stress=2, energy=4) for d in wrote] + [checkin(d, stress=4, energy=3) for d in skipped]
        out = prompts.compute(None, data([session(d, 200) for d in wrote], c), TODAY)
        text = " ".join(out["insights"])
        self.assertIn("your evening stress averages 2.0, vs 4.0 on days you don't", text)
        self.assertIn("your evening energy averages 4.0, vs 3.0", text)

    def test_no_claims_without_data(self):
        out = prompts.compute(None, data([session(TODAY, 100, ["grat-three"])]), TODAY)
        self.assertEqual(out["insights"], [])

    def test_no_claim_when_prompts_dont_help(self):
        days = [TODAY - dt.timedelta(days=i) for i in range(8)]
        s = [session(d, 100, ["grat-three"]) for d in days[:4]] + [session(d, 150) for d in days[4:]]
        self.assertFalse(any("With a prompt" in i for i in prompts.compute(None, data(s), TODAY)["insights"]))


class Suggestions(unittest.TestCase):
    def test_look_back_quotes_your_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Journal").mkdir()
            year_ago = TODAY - dt.timedelta(days=365)
            (root / "Journal" / f"{year_ago}.md").write_text(
                "---\ntype: Journal\n---\n\n## 09:00\n\n> **Prompt:** Something\n\n"
                "Finally finished the garden fence. It took all weekend.\n")
            out = prompts.compute(root, data([]), TODAY)
            first = out["suggestions"][0]
            self.assertEqual(first["id"], f"revisit-{year_ago}")
            self.assertEqual(first["text"], 'A year ago today you wrote: "Finally finished the garden fence." '
                                            "What has changed since?")

    def test_matches_latest_checkin(self):
        out = prompts.compute(None, data([], [checkin(TODAY, stress=5)]), TODAY)
        self.assertTrue(any(s["category"] == "stress" and s["why"].startswith("Your last check-in had high stress.")
                            for s in out["suggestions"]))

    def test_skips_recently_used_and_always_offers_some(self):
        s = [session(TODAY - dt.timedelta(days=1), 200, ["quick-best"])]
        out = prompts.compute(None, data(s), TODAY)
        ids = [x["id"] for x in out["suggestions"]]
        self.assertGreaterEqual(len(ids), 3)
        self.assertNotIn("quick-best", ids)
        self.assertEqual(len(set(ids)), len(ids))

    def test_file_format_reads_back(self):
        out = prompts.compute(None, data([], [checkin(TODAY, stress=5)]), TODAY)
        text = prompts.suggestions_file(out["suggestions"])
        cats, items = prompts.parse_library(text)
        self.assertEqual([c["id"] for c in cats], ["foryou"])
        self.assertEqual([p["id"] for p in items], [s["id"] for s in out["suggestions"]])

    def test_deterministic(self):
        d = data([], [checkin(TODAY, energy=1)])
        self.assertEqual(prompts.compute(None, d, TODAY)["suggestions"], prompts.compute(None, d, TODAY)["suggestions"])


class Gamification(unittest.TestCase):
    def test_prompt_xp_and_badges(self):
        days = [TODAY - dt.timedelta(days=i) for i in range(6)]
        cats = ["grat-three", "stress-dump", "feel-name", "goal-week", "ppl-who", "body-move"]
        s = [session(d, 40, [pid]) for d, pid in zip(days, cats)] + [session(TODAY, 40, [f"revisit-{TODAY.replace(year=2025)}"], 20)]
        d = data(s)
        d["prompt_categories"] = prompts.compute(None, d, TODAY)["categories"]
        c = companion.compute(d, today=TODAY)
        unlocked = {a["id"] for a in c["achievements"] if a["unlocked"]}
        self.assertTrue({"prompted_5", "curious", "time_traveler"} <= unlocked)
        plain = data([session(x["start"] and dt.date.fromisoformat(x["start"][:10]), 40, [], 20) for x in s])
        self.assertGreater(c["xp"], companion.compute(plain, today=TODAY)["xp"])


if __name__ == "__main__":
    unittest.main()

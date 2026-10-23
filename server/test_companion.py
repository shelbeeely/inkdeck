import datetime as dt
import unittest

import companion
from companion import Settings

D0 = dt.date(2026, 10, 1)  # a Thursday


def session(day: dt.date, words: int, hour: int = 9, active_s: int = 600, wpm: int = 25, **extra) -> dict:
    start = dt.datetime.combine(day, dt.time(hour, 0), dt.timezone(dt.timedelta(hours=-7)))
    return {"start": start.isoformat(), "words": words, "active_s": active_s, "wpm": wpm,
            "lat": None, "lon": None, "place": None, "place_tag": None, "weather": None, **extra}


def data(sessions, today: dt.date, checkins=()) -> dict:
    return {"today": today.isoformat(), "sessions": list(sessions), "checkins": list(checkins)}


def days(n: int, start: dt.date = D0, words: int = 300, skip=()) -> list:
    return [session(start + dt.timedelta(days=i), words) for i in range(n) if i not in skip]


class Levels(unittest.TestCase):
    def test_thresholds(self):
        self.assertEqual(companion.level_for(0), (1, 0, 100))
        self.assertEqual(companion.level_for(99), (1, 0, 100))
        self.assertEqual(companion.level_for(100), (2, 100, 300))
        self.assertEqual(companion.level_for(600), (4, 600, 1000))


class Streaks(unittest.TestCase):
    def test_rest_day_keeps_streak(self):
        # Writes Thu..Sun, skips Mon, writes Tue: one rest day per week keeps it.
        c = companion.compute(data(days(6, skip={4}), D0 + dt.timedelta(days=5)))
        self.assertEqual(c["streak"], 5)
        self.assertEqual(c["rest_days_left"], 0)

    def test_two_missed_days_break_it(self):
        c = companion.compute(data(days(7, skip={4, 5}), D0 + dt.timedelta(days=6)))
        self.assertEqual(c["streak"], 1)
        self.assertEqual(c["best_streak"], 4)

    def test_no_rest_days(self):
        c = companion.compute(data(days(6, skip={4}), D0 + dt.timedelta(days=5)), Settings(rest_days=0))
        self.assertEqual(c["streak"], 1)

    def test_today_not_written_yet(self):
        c = companion.compute(data(days(3), D0 + dt.timedelta(days=3)))
        self.assertEqual(c["streak"], 3)
        self.assertEqual(c["rest_days_left"], 1)


class Xp(unittest.TestCase):
    def test_one_day(self):
        # 300 words: 30 + 10 (5+ min session) + 25 (goal) + 50 first words = 115.
        c = companion.compute(data([session(D0, 300)], D0))
        self.assertEqual(c["xp"], 115)
        self.assertEqual(c["level"], 2)
        self.assertEqual(c["today_words"], 300)

    def test_checkins(self):
        ci = [{"time": dt.datetime.combine(D0, dt.time(21), dt.timezone.utc).isoformat(), "kind": "evening"}]
        c = companion.compute(data([session(D0, 0, active_s=0)], D0, ci))
        self.assertEqual(c["xp"], 15)


class Achievements(unittest.TestCase):
    def unlocked(self, c):
        return {a["id"] for a in c["achievements"] if a["unlocked"]}

    def test_session_based(self):
        s = [session(D0, 120, hour=5), session(D0, 80, hour=23, active_s=1900, wpm=45),
             session(D0, 60, weather={"condition": "Light rain", "light": "dark"})]
        u = self.unlocked(companion.compute(data(s, D0)))
        self.assertTrue({"first_words", "early_bird", "night_owl", "marathon", "rain_writer"} <= u)
        self.assertNotIn("sprinter", u)  # fast, but under 100 words

    def test_streaks_and_words(self):
        u = self.unlocked(companion.compute(data(days(7, words=1500), D0 + dt.timedelta(days=6))))
        self.assertTrue({"streak_3", "streak_7", "words_1k", "words_10k", "goal_7", "weekend"} <= u)

    def test_explorer(self):
        s = [session(D0, 10, lat=45 + i, lon=-122.0) for i in range(5)]
        self.assertIn("explorer", self.unlocked(companion.compute(data(s, D0))))


class PetLife(unittest.TestCase):
    def test_hatches_after_three_writing_days(self):
        c = companion.compute(data(days(2), D0 + dt.timedelta(days=1)))
        self.assertEqual(c["pet"]["stage"], "egg")
        self.assertEqual(c["pet"]["eggs_writing_days"], 2)
        c = companion.compute(data(days(3), D0 + dt.timedelta(days=2)))
        self.assertEqual(c["pet"]["state"], "alive")
        self.assertEqual(c["pet"]["stage"], "baby")

    def test_grows_up(self):
        c = companion.compute(data(days(70), D0 + dt.timedelta(days=69)))
        self.assertEqual(c["pet"]["stage"], "adult")
        self.assertEqual(c["pet"]["form"], "scholar")
        self.assertEqual(c["pet"]["mood"], "happy")

    def test_neglect_then_hibernate_then_revive(self):
        s = days(5)
        c = companion.compute(data(s, D0 + dt.timedelta(days=8)))
        self.assertIn(c["pet"]["mood"], ("hungry", "sad", "sick"))
        c = companion.compute(data(s, D0 + dt.timedelta(days=20)))
        self.assertEqual(c["pet"]["state"], "asleep")
        back = s + days(3, start=D0 + dt.timedelta(days=21))
        c = companion.compute(data(back, D0 + dt.timedelta(days=23)))
        self.assertEqual(c["pet"]["state"], "alive")

    def test_classic_mode_loses_the_pet(self):
        s = days(5) + [session(D0 + dt.timedelta(days=30), 300)]
        c = companion.compute(data(s, D0 + dt.timedelta(days=30)), Settings(pet_mode="classic"))
        self.assertEqual(c["pet"]["generation"], 2)
        self.assertEqual(c["pet"]["stage"], "egg")
        self.assertEqual(len(c["pet"]["history"]), 1)

    def test_feeding_today_shows_live(self):
        c = companion.compute(data(days(5), D0 + dt.timedelta(days=4)))
        p = c["pet"]
        self.assertGreater(p["fullness"], p["fullness_base"])
        self.assertEqual(p["food_per_100_words"], 28.0)


class Markdown(unittest.TestCase):
    def test_front_matter_is_flat_and_valid(self):
        import yaml
        c = companion.compute(data(days(10), D0 + dt.timedelta(days=9)))
        text = companion.companion_markdown(c, dt.datetime(2026, 10, 10, 9, tzinfo=dt.timezone.utc))
        fm = text.split("---\n")[1]
        parsed = yaml.safe_load(fm)
        self.assertEqual(parsed["type"], "InkDeckCompanion")
        self.assertEqual(parsed["pet_face"], c["pet"]["face"])
        for line in fm.strip().splitlines():
            self.assertRegex(line, r"^[a-z0-9_]+: ")


if __name__ == "__main__":
    unittest.main()

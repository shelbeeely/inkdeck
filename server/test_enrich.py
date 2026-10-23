import datetime as dt
import json
import tempfile
import unittest
from pathlib import Path

import yaml

import enrich

FIXTURES = Path(__file__).resolve().parent.parent / "test" / "host" / "fixtures"
DATA = {"weather": json.loads((FIXTURES / "wx.json").read_text()),
        "air": json.loads((FIXTURES / "aq.json").read_text())}
DAY = dt.date(2026, 10, 20)

ENTRY = """---
created: 2026-10-20T08:40:00-07:00
modified: 2026-10-20T21:05:00-07:00
type: Journal
sessions:
  - start: 2026-10-20T08:40:00-07:00
    end: 2026-10-20T09:00:00-07:00
    words: 400
    location: {lat: 45.523100, lon: -122.676500, accuracy_m: 12, place: "Home"}
    parts:
      - {start: "08:40:05", end: "08:50:00", active_s: 500, words: 220}
  - start: 2026-10-20T21:00:00-07:00
    end: 2026-10-20T21:05:00-07:00
    words: 50
---

## 08:40

Morning text.
"""


class Enrich(unittest.TestCase):
    def setUp(self):
        self.calls = []

    def fetch(self, lat, lon, day):
        self.calls.append((round(lat, 2), round(lon, 2), day))
        return DATA

    def test_adds_weather_and_day(self):
        out = enrich.enrich_text(ENTRY, self.fetch, (45.5, -122.6), DAY)
        fm = yaml.safe_load(out.split("---\n")[1])
        s1, s2 = fm["sessions"]
        self.assertEqual(s1["weather"]["aqi"], 43)
        self.assertEqual(s1["weather"]["light"], "1 h 06 min after sunrise")
        self.assertEqual(s1["weather"]["condition"], "Clear")  # hour 8; the day overall was overcast
        self.assertEqual(s1["weather"]["humidity"], DATA["weather"]["hourly"]["relative_humidity_2m"][8])
        self.assertEqual(s2["weather"]["light"], "dark")
        self.assertEqual(fm["day"]["sunrise"], "07:34")
        self.assertEqual(fm["day"]["sunset"], "18:16")
        self.assertEqual(fm["day"]["weather"], "Overcast")
        self.assertIs(fm["day"]["final"], True)
        self.assertEqual(len(s1["parts"]), 1)
        # Weather goes before parts:, and the body is untouched.
        self.assertLess(out.index("    weather:"), out.index("    parts:"))
        self.assertTrue(out.endswith("## 08:40\n\nMorning text.\n"))
        # Session 1 at its own location, session 2 and the day... day uses the first located session.
        self.assertIn((45.52, -122.68, DAY), self.calls)
        self.assertIn((45.5, -122.6, DAY), self.calls)

    def test_idempotent(self):
        once = enrich.enrich_text(ENTRY, self.fetch, None, DAY)
        self.assertIsNone(enrich.enrich_text(once, self.fetch, None, DAY))

    def test_without_location_or_home(self):
        out = enrich.enrich_text(ENTRY, self.fetch, None, DAY)
        fm = yaml.safe_load(out.split("---\n")[1])
        self.assertIn("weather", fm["sessions"][0])
        self.assertNotIn("weather", fm["sessions"][1])  # no location, no home

    def test_replaces_non_final_day_block(self):
        text = ENTRY.replace("---\n\n## 08:40", "day:\n  sunrise: \"07:00\"\n  final: false\n---\n\n## 08:40")
        out = enrich.enrich_text(text, self.fetch, None, DAY)
        fm = yaml.safe_load(out.split("---\n")[1])
        self.assertEqual(fm["day"]["sunrise"], "07:34")
        self.assertEqual(out.count("day:"), 1)

    def test_gitjournal_column_zero_list(self):
        flat = ENTRY.replace("\n  - start", "\n- start").replace("\n    ", "\n  ").replace("\n      -", "\n    -")
        out = enrich.enrich_text(flat, self.fetch, None, DAY)
        fm = yaml.safe_load(out.split("---\n")[1])
        self.assertEqual(fm["sessions"][0]["weather"]["aqi"], 43)

    def test_run_skips_today_and_recent(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Journal").mkdir()
            (root / "Journal" / "2026-10-20.md").write_text(ENTRY)
            (root / "Journal" / "2026-10-23.md").write_text(ENTRY.replace("2026-10-20", "2026-10-23"))
            now = dt.datetime(2026, 10, 20, 22, 0, tzinfo=dt.timezone(dt.timedelta(hours=-7)))
            self.assertEqual(enrich.run(root, None, now, fetcher=self.fetch), [])  # changed under 2 h ago
            now = dt.datetime(2026, 10, 23, 10, 0, tzinfo=dt.timezone(dt.timedelta(hours=-7)))
            self.assertEqual(enrich.run(root, None, now, fetcher=self.fetch), ["Journal/2026-10-20.md"])
            self.assertEqual(enrich.run(root, None, now, fetcher=self.fetch), [])

    def test_helpers(self):
        self.assertEqual(enrich.moon_phase(dt.date(2024, 1, 11)), "New moon")
        self.assertEqual(enrich.moon_phase(dt.date(2024, 1, 25)), "Full moon")
        self.assertEqual(enrich.sun_label(454, 1096, 15 * 60), "3 h 16 min before sunset")
        self.assertEqual(enrich.parse_home("45.5, -122.6"), (45.5, -122.6))
        self.assertIsNone(enrich.parse_home("nowhere"))


if __name__ == "__main__":
    unittest.main()

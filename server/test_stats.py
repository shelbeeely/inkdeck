"""Tests for stats.py:  python -m unittest test_stats"""

import csv
import datetime as dt
import tempfile
import unittest
from pathlib import Path

import sample_data
import stats

ENTRY = """---
created: 2026-10-23T09:00:00-07:00
type: Journal
sessions:
  - start: 2026-10-23T07:00:00-07:00
    end: 2026-10-23T07:20:00-07:00
    active_s: 900
    keys: 2000
    words: 400
    wpm: 27
    typed: 1900
    deleted: 100
    pauses: 1
    longest_pause_s: 120
    longest_burst_s: 300
    first_key_s: 5
    revisions: 2
    location: {lat: 45.523100, lon: -122.676500, accuracy_m: 12, time: 2026-10-23T06:59:50-07:00, source: phone, place: "Home"}
    parts:
      - {start: "07:00:05", end: "07:10:00", active_s: 500, words: 220}
  - start: 2026-10-23T21:00:00-07:00
    end: 2026-10-23T21:05:00-07:00
    active_s: 200
    words: 50
---

## 07:00
"""

USAGE = [
    ["time", "event", "path", "duration_s", "active_s", "keys", "typed", "deleted", "words", "wpm",
                  "battery", "detail"],
    # Duplicate of the front matter session: must not be counted twice.
    ["2026-10-23T07:20:00-07:00", "session", "2026-10-23.md", "1200", "900", "2000", "1900", "100", "400", "27", "80",
     "journal"],
    ["2026-10-22T21:00:00-07:00", "session", "Ideas/x.md", "600", "300", "500", "450", "50", "60", "12", "75", "note"],
    ["2026-10-22T09:00:00-07:00", "sync", "", "12", "0", "0", "0", "0", "0", "0", "82",
     "down=1 up=2 deleted=0 conflicts=0 ok"],
    ["2026-10-23T23:00:00-07:00", "keyboard", "", "3600", "0", "0", "0", "0", "0", "0", "", "disconnected"],
]


class StatsTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def write_fixture(self):
        (self.root / "2026-10-23.md").write_text(ENTRY, encoding="utf-8")
        usage = self.root / "_inkdeck" / "usage"
        usage.mkdir(parents=True)
        with (usage / "2026-10.csv").open("w", newline="") as f:
            csv.writer(f).writerows(USAGE)

    def test_merges_sources_without_double_counting(self):
        self.write_fixture()
        s = stats.compute(self.root, today=dt.date(2026, 10, 23))
        t = s["totals"]
        self.assertEqual(t["sessions"], 3)  # 2 journal (front matter) + 1 note (log)
        self.assertEqual(t["words"], 400 + 50 + 60)
        self.assertEqual(t["today"]["words"], 450)
        self.assertEqual(t["streak"], 2)  # the 22nd and 23rd
        self.assertEqual(s["places"][0]["place"], "Home")
        journal = [x for x in s["sessions"] if x["kind"] == "journal"]
        self.assertEqual(journal[0]["parts"][0]["words"], 220)
        self.assertEqual(s["device"]["keyboard_s"], 3600)
        self.assertTrue(s["device"]["syncs"][0]["ok"])
        self.assertEqual(s["hour_weekday"][dt.date(2026, 10, 23).weekday()][7], 900)

    def test_sample_data(self):
        sample_data.write(self.root, days=60, end=dt.date(2026, 10, 23))
        s = stats.compute(self.root, today=dt.date(2026, 10, 23))
        self.assertGreater(s["totals"]["sessions"], 40)
        self.assertGreater(len(s["days"]), 30)
        self.assertTrue(s["places"])
        self.assertTrue(s["device"]["battery"])
        # Journal sessions from the log duplicate front matter ones: none counted twice.
        self.assertEqual(len({(x["start"], x["file"]) for x in s["sessions"]}), len(s["sessions"]))

    def test_empty_folder(self):
        s = stats.compute(self.root, today=dt.date(2026, 10, 23))
        self.assertEqual(s["totals"]["sessions"], 0)
        self.assertEqual(s["totals"]["streak"], 0)


if __name__ == "__main__":
    unittest.main()

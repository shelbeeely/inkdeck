# InkDeck server

A small server for your GitJournal repository. It shows a dashboard of how
you write on InkDeck, and runs the **companion**: a journaling pet you keep
alive by writing, levels and achievements, and weather, sun and air quality
added to your entries. The X4 records the raw data; the server does the
thinking and writes the results back to the repo, where the X4 picks them up
on its next sync.

## Dashboard

It shows charts of how
you write on InkDeck: a calendar of writing days, words and active time per
day, typing speed, when in the week you write, habits (pauses, bursts, time to
first keystroke, revisions, paragraphs), places, a per-day timeline of
sessions and paragraphs, battery, syncs and the files you spend time in.

It keeps a shallow clone of the repo up to date (every `PULL_INTERVAL`
seconds). Data comes from what InkDeck syncs:

- the `sessions:` records (and `checkins:`, `day:` weather) in journal entries' front matter, and
- the usage logs in `_inkdeck/usage/*.csv`.

By default it only **reads**. Set `WRITE_BACK=1` for the companion to commit.

## Companion

All of it is worked out from your journal history every refresh
(`companion.py`), so nothing is lost if the server restarts and it can be
recomputed any time.

**The pet** (`PET_NAME`, default Inkling). An egg arrives with your first
entry and hatches after 3 writing days. Then it grows: baby, child (7 days),
teen (21), adult (60). Its adult form depends on how you wrote while it grew
up: scholar (met the goal most days), wanderer (many places), chatterbox
(long fast sessions), early bird, night owl or dreamer.

- **Fullness**: the daily goal in words is a full meal; it drops every night.
- **Happiness**: check-ins, streaks, goals and achievements; drops on days you don't write.
- **Health**: falls while it's starving, recovers while it's fed.
- At zero health, in `PET_MODE=gentle` (default) it **hibernates** until you
  write on 3 days; in `classic` it's **lost** (remembered in "In memory") and
  a new egg comes with your next entry.

**Levels and XP**: words/10, +10 per session of 5+ active minutes, +15 per
check-in, +25 per day the goal (`DAILY_GOAL`, 250 words) is met, +50 per
achievement. Level L needs 50·L·(L−1) XP.

**Streaks**: up to `REST_DAYS` (default 1) missed days a week keep a streak going.

**Achievements**: 22, from "First words" to "Centurion" (100-day streak),
including weather ones ("Rainy day", "First light") and "Explorer" (5 places).

**`_inkdeck/companion.md`**: written to the repo with `WRITE_BACK=1`. Its front
matter is flat `key: value` lines that the X4 reads; the body is a readable
page in GitJournal. It's only committed when something other than its
timestamp changed.

**Prompts** (`prompts.py`): every refresh it writes
`_inkdeck/prompts-suggested.txt`, which the X4 lists under "For you". It holds up
to five prompts:

- a look back at your entry from a week, month, six months or a year ago;
- one matched to your latest check-in;
- one from the kind of prompt you write most with;
- a kind you haven't tried;
- quick ones as a fallback.

It also works out insights from your own data (prompted vs. plain sessions, and
evening check-ins on journaling vs. non-journaling days), shown on the dashboard.
The top insight goes into `companion.md` for the Pet screen. A finding is only
stated when there's enough data and it holds, and only as what your journal
shows. Answering a prompt (30+ words) earns 5 XP, and there are three prompt
achievements: Prompted, Curious and Time traveler. The library is
`prompts/library.txt` plus your `_inkdeck/prompts.txt`; set `PROMPTS_LIBRARY` to
use another file.

**Weather** (`WEATHER=on`): for each journal entry whose day is over and that
hasn't changed for 2 hours, it asks [Open-Meteo](https://open-meteo.com) (free,
no key) about that day where it was written (the phone location, else
`HOME_LOCATION`) and adds a `weather:` line to each session (temperature,
feels like, humidity, cloud, rain, wind, condition, US AQI, PM2.5, and where the
sun was: "dark", "sunrise", "golden hour", "2 h 10 min after sunrise") and a
`day:` block (sunrise, sunset, daylight, high/low, rain, UV, weather, moon
phase). Days older than ~3 months use the historical archive. Responses are
cached in `CACHE_PATH`. Only finished days are touched, so it never edits an
entry you might be writing on the X4. If Open-Meteo rate-limits it, it stops
and continues at the next refresh.

## Run it

```sh
cp .env.example .env    # set REPO_URL, GIT_TOKEN, DASHBOARD_PASSWORD (and WRITE_BACK=1, TZ, ...)
docker compose up -d    # http://localhost:8080
```

Without Docker (Python 3.10+ and git):

```sh
pip install -r requirements.txt
REPO_URL=https://github.com/you/journal.git GIT_TOKEN=... DASHBOARD_PASSWORD=... python app.py
```

Or point it at a folder you already have, with no cloning:

```sh
NOTES_PATH=~/journal python app.py
```

To preview with made-up data:

```sh
python sample_data.py /tmp/sample-notes && NOTES_PATH=/tmp/sample-notes python app.py
```

## Notes

- **Privacy.** The dashboard shows everything about when and where you wrote.
  Set `DASHBOARD_PASSWORD`, and put it behind HTTPS (e.g. Caddy or your NAS's
  reverse proxy) if it's reachable from outside your network. The page loads
  Chart.js and Leaflet from public CDNs and map tiles from OpenStreetMap.
- **Token.** Use a token separate from the one on the X4: read-only for the
  dashboard alone, read and write for `WRITE_BACK=1`. It's sent as a header on
  each git call and never written to disk. Commits are by "InkDeck Companion".
- **Endpoints.** `/` dashboard, `/api/stats` the JSON behind it,
  `POST /api/refresh` fetch now, `/healthz`.
- **Tests.** `python -m unittest` (stats, companion, enrichment, prompts).
- **Docker.** `compose.yaml` builds from the repository root, so the image includes `prompts/library.txt`.
- **Settings.** All in `.env.example` and at the top of `app.py`.

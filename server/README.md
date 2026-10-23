# InkDeck stats dashboard

A small server that reads your GitJournal repository and shows charts of how
you write on InkDeck: a calendar of writing days, words and active time per
day, typing speed, when in the week you write, habits (pauses, bursts, time to
first keystroke, revisions, paragraphs), places, a per-day timeline of
sessions and paragraphs, battery, syncs and the files you spend time in.

It only **reads**: it keeps a shallow clone of the repo up to date (every
`PULL_INTERVAL` seconds) and never pushes. Data comes from what InkDeck syncs:

- the `sessions:` records in journal entries' front matter, and
- the usage logs in `_inkdeck/usage/*.csv`.

## Run it

```sh
cp .env.example .env    # set REPO_URL, GIT_TOKEN, DASHBOARD_PASSWORD
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
- **Token.** Use a read-only token for the dashboard, separate from the one on
  the X4. It's sent as a header on each git call and never written to disk.
- **Endpoints.** `/` dashboard, `/api/stats` the JSON behind it,
  `POST /api/refresh` fetch now, `/healthz`.
- **Tests.** `python -m unittest test_stats`.

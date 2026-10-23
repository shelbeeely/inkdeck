"""Adds weather, sun times and air quality to finished journal entries.

For each journal entry whose day is over (and that hasn't been touched for a
couple of hours, so it won't collide with the X4 syncing), this asks
Open-Meteo (free, no key) about that day at the place it was written and adds:

  * to every session record:
      weather: {temp_c: 11.2, feels_c: 9.8, humidity: 78, cloud: 99, precip_mm: 0.0,
                wind_kmh: 7.4, condition: "Overcast", aqi: 43, pm25: 8.7,
                light: "1 h 06 min after sunrise"}
  * one block for the day:
      day:
        sunrise: "07:34"
        sunset: "18:16"
        daylight_h: 10.7
        temp_max_c: 20.4
        ...
        moon: "Waxing gibbous"
        final: true
        source: open-meteo

The place is the session's phone location, else HOME_LOCATION. The rest of the
file is left byte for byte as it was, so the change is a small, readable diff.
The same output is produced on-device by src/OpenMeteo.cpp (kept as a
fallback for running without a server).
"""

from __future__ import annotations

import datetime as dt
import json
import math
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Callable

import yaml

from stats import front_matter, parse_time

WEATHER_URL = "https://api.open-meteo.com/v1/forecast"
ARCHIVE_URL = "https://archive-api.open-meteo.com/v1/archive"
AIR_URL = "https://air-quality-api.open-meteo.com/v1/air-quality"
FORECAST_DAYS_BACK = 85   # the forecast API serves about three months of the past
QUIET_HOURS = 2           # leave entries alone this long after they last changed

HOURLY = "temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,cloud_cover,weather_code,wind_speed_10m"
DAILY = "temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_sum,weather_code,daylight_duration"

WMO = {0: "Clear", 1: "Mostly clear", 2: "Partly cloudy", 3: "Overcast", 45: "Fog", 48: "Fog",
       51: "Drizzle", 53: "Drizzle", 55: "Drizzle", 56: "Freezing drizzle", 57: "Freezing drizzle",
       61: "Light rain", 63: "Rain", 65: "Heavy rain", 66: "Freezing rain", 67: "Freezing rain",
       71: "Light snow", 73: "Snow", 75: "Heavy snow", 77: "Snow grains", 80: "Rain showers",
       81: "Rain showers", 82: "Heavy showers", 85: "Snow showers", 86: "Snow showers",
       95: "Thunderstorm", 96: "Thunderstorm with hail", 99: "Thunderstorm with hail"}

MOON = ["New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
        "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"]


def describe(code) -> str:
    try:
        return WMO.get(int(code), "Unknown")
    except (TypeError, ValueError):
        return "Unknown"


def moon_phase(day: dt.date) -> str:
    # Days since a known new moon (2000-01-06 18:14 UTC), modulo the synodic month.
    since = (day - dt.date(2000, 1, 6)).days + 0.5 - 0.76
    age = math.fmod(since, 29.530588853)
    if age < 0:
        age += 29.530588853
    return MOON[int(age / 29.530588853 * 8 + 0.5) % 8]


def _span(mins: int) -> str:
    return f"{mins // 60} h {mins % 60:02d} min" if mins >= 60 else f"{mins} min"


def sun_label(rise: int | None, set_: int | None, minute: int) -> str:
    """Where the sun was at `minute` (local minutes after midnight)."""
    if rise is None or set_ is None:
        return ""
    if abs(minute - rise) <= 30:
        return "sunrise"
    if abs(minute - set_) <= 30:
        return "sunset"
    if minute < rise or minute > set_:
        return "dark"
    if set_ - minute <= 60:
        return "golden hour"
    if minute - rise <= (set_ - rise) // 2:
        return f"{_span(minute - rise)} after sunrise"
    return f"{_span(set_ - minute)} before sunset"


def _minutes(iso: str | None) -> int | None:
    if not iso or len(iso) < 16:
        return None
    return int(iso[11:13]) * 60 + int(iso[14:16])


# --- Fetching ---------------------------------------------------------------


class RateLimited(Exception):
    """Open-Meteo said 429: stop for now, the next refresh carries on."""


_last_call = [0.0]
PACE_S = 1.0  # between requests; Open-Meteo rate-limits bursts


def _get(url: str, params: dict) -> dict:
    req = urllib.request.Request(url + "?" + urllib.parse.urlencode(params),
                                 headers={"User-Agent": "InkDeck-server (journal enrichment)"})
    for attempt in range(2):
        wait = _last_call[0] + PACE_S - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        _last_call[0] = time.monotonic()
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            if e.code == 429:
                raise RateLimited() from e
            raise
        except (urllib.error.URLError, TimeoutError):
            if attempt:
                raise
    raise AssertionError("unreachable")


def fetch_day(lat: float, lon: float, day: dt.date, today: dt.date, cache: Path | None = None) -> dict | None:
    """Open-Meteo weather (+ air quality when available) for one local day, or None."""
    key = f"{lat:.2f},{lon:.2f},{day.isoformat()}"
    path = cache / (key.replace(",", "_") + ".json") if cache else None
    if path and path.exists():
        return json.loads(path.read_text())
    params = {"latitude": f"{lat:.4f}", "longitude": f"{lon:.4f}", "timezone": "auto",
              "start_date": day.isoformat(), "end_date": day.isoformat(), "hourly": HOURLY, "daily": DAILY}
    recent = (today - day).days <= FORECAST_DAYS_BACK
    try:
        if recent:
            wx = _get(WEATHER_URL, {**params, "daily": DAILY + ",uv_index_max"})
        else:
            wx = _get(ARCHIVE_URL, params)
    except (urllib.error.URLError, TimeoutError, ValueError):
        return None
    try:
        aq = _get(AIR_URL, {k: params[k] for k in ("latitude", "longitude", "timezone", "start_date", "end_date")}
                  | {"hourly": "us_aqi,pm2_5"})
    except (urllib.error.URLError, TimeoutError, ValueError):
        aq = {}
    data = {"weather": wx, "air": aq}
    if path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data))
    return data


# --- Formatting -------------------------------------------------------------


def _at(block: dict, key: str, i: int):
    values = (block or {}).get(key) or []
    return values[i] if 0 <= i < len(values) else None


def _num(v, fmt: str = "{:.1f}") -> str:
    return fmt.format(v) if isinstance(v, (int, float)) else "null"


def session_weather(data: dict, minute: int) -> str | None:
    """The session's `weather:` flow map for a local time of day, or None."""
    wx, aq = data.get("weather") or {}, data.get("air") or {}
    hourly, daily = wx.get("hourly") or {}, wx.get("daily") or {}
    h = max(0, min(23, minute // 60))
    temp = _at(hourly, "temperature_2m", h)
    if temp is None:
        return None
    parts = [f"temp_c: {_num(temp)}", f"feels_c: {_num(_at(hourly, 'apparent_temperature', h))}",
             f"humidity: {_num(_at(hourly, 'relative_humidity_2m', h), '{:.0f}')}",
             f"cloud: {_num(_at(hourly, 'cloud_cover', h), '{:.0f}')}",
             f"precip_mm: {_num(_at(hourly, 'precipitation', h))}",
             f"wind_kmh: {_num(_at(hourly, 'wind_speed_10m', h))}",
             f'condition: "{describe(_at(hourly, "weather_code", h))}"']
    aqi = _at(aq.get("hourly") or {}, "us_aqi", h)
    if aqi is not None:
        parts += [f"aqi: {aqi:.0f}", f"pm25: {_num(_at(aq.get('hourly') or {}, 'pm2_5', h))}"]
    light = sun_label(_minutes(_at(daily, "sunrise", 0)), _minutes(_at(daily, "sunset", 0)), minute)
    if light:
        parts.append(f'light: "{light}"')
    return "{" + ", ".join(parts) + "}"


def day_block(data: dict, day: dt.date) -> str | None:
    daily = (data.get("weather") or {}).get("daily") or {}
    if _at(daily, "temperature_2m_max", 0) is None:
        return None

    def hhmm(v):
        m = _minutes(v)
        return f"{m // 60:02d}:{m % 60:02d}" if m is not None else ""

    daylight = _at(daily, "daylight_duration", 0)
    lines = ["day:",
             f'  sunrise: "{hhmm(_at(daily, "sunrise", 0))}"',
             f'  sunset: "{hhmm(_at(daily, "sunset", 0))}"',
             f"  daylight_h: {_num(daylight / 3600 if daylight is not None else None)}",
             f"  temp_max_c: {_num(_at(daily, 'temperature_2m_max', 0))}",
             f"  temp_min_c: {_num(_at(daily, 'temperature_2m_min', 0))}",
             f"  precip_mm: {_num(_at(daily, 'precipitation_sum', 0))}"]
    uv = _at(daily, "uv_index_max", 0)
    if uv is not None:
        lines.append(f"  uv_max: {_num(uv)}")
    lines += [f'  weather: "{describe(_at(daily, "weather_code", 0))}"',
              f'  moon: "{moon_phase(day)}"',
              "  final: true",
              "  source: open-meteo"]
    return "\n".join(lines) + "\n"


# --- Editing the entry ------------------------------------------------------


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def enrich_text(text: str, fetch: Callable[[float, float, dt.date], dict | None],
                home: tuple[float, float] | None, day: dt.date) -> str | None:
    """The entry with weather added, or None if there's nothing to add."""
    if not text.startswith("---\n"):
        return None
    end = text.find("\n---", 4)
    if end < 0:
        return None
    fm_text = text[4:end + 1]
    fm = front_matter(text)
    sessions = [s for s in fm.get("sessions") or [] if isinstance(s, dict) and parse_time(s.get("start"))]
    day_done = isinstance(fm.get("day"), dict) and fm["day"].get("final") is True
    todo = [s for s in sessions if not isinstance(s.get("weather"), dict)]
    if day_done and not todo:
        return None

    def place(s: dict) -> tuple[float, float] | None:
        loc = s.get("location")
        if isinstance(loc, dict) and "lat" in loc and "lon" in loc:
            return float(loc["lat"]), float(loc["lon"])
        return home

    lines = fm_text.splitlines(keepends=True)
    inserts: list[tuple[int, str]] = []  # (line index to insert before, text)

    # Session items: "- start: <iso>" under "sessions:".
    try:
        key = next(i for i, l in enumerate(lines) if l.rstrip("\n") == "sessions:")
    except StopIteration:
        key = None
    if key is not None and todo:
        wanted = {str(s["start"]) if not isinstance(s["start"], dt.datetime) else s["start"].isoformat(): s
                  for s in todo}
        i = key + 1
        while i < len(lines):
            line = lines[i]
            if line.strip() and _indent(line) == 0 and not line.lstrip().startswith("-"):
                break  # next top-level key
            m = re.match(r"^( *)- start: (.+?)\s*$", line)
            if not m:
                i += 1
                continue
            ind = len(m.group(1))
            j = i + 1
            parts_at = None
            while j < len(lines):
                l = lines[j]
                if l.strip() and (_indent(l) < ind + 2 or (_indent(l) == ind and l.lstrip().startswith("- "))):
                    break
                if parts_at is None and _indent(l) == ind + 2 and l.lstrip().startswith("parts:"):
                    parts_at = j
                j += 1
            raw = m.group(2).strip().strip('"')
            s = wanted.get(raw) or next((v for k, v in wanted.items() if parse_time(k) == parse_time(raw)), None)
            if s is not None:
                start = parse_time(s["start"])
                where = place(s)
                data = fetch(where[0], where[1], start.date()) if where else None
                flow = session_weather(data, start.hour * 60 + start.minute) if data else None
                if flow:
                    inserts.append((parts_at if parts_at is not None else j, " " * (ind + 2) + f"weather: {flow}\n"))
            i = j

    # Day block: replace a non-final one, or add before the closing ---.
    new_lines = list(lines)
    for at, piece in sorted(inserts, reverse=True):
        new_lines.insert(at, piece)
    if not day_done:
        where = next((place(s) for s in sessions if place(s)), home)
        data = fetch(where[0], where[1], day) if where else None
        block = day_block(data, day) if data else None
        if block:
            start = next((i for i, l in enumerate(new_lines) if l.rstrip("\n") == "day:"), None)
            if start is not None:
                stop = start + 1
                while stop < len(new_lines) and (not new_lines[stop].strip() or _indent(new_lines[stop]) > 0):
                    stop += 1
                new_lines[start:stop] = [block]
            else:
                if new_lines and not new_lines[-1].endswith("\n"):
                    new_lines[-1] += "\n"
                new_lines.append(block)
    new_fm = "".join(new_lines)
    if new_fm == fm_text:
        return None
    out = "---\n" + new_fm + text[end + 1:]
    yaml.safe_load(new_fm)  # never write front matter that doesn't parse
    return out


def entry_day(path: Path, fm: dict) -> dt.date | None:
    m = re.match(r"(\d{4}-\d{2}-\d{2})", path.name)
    if m:
        try:
            return dt.date.fromisoformat(m.group(1))
        except ValueError:
            pass
    created = parse_time(fm.get("created"))
    return created.date() if created else None


def run(root: Path, home: tuple[float, float] | None, now: dt.datetime, cache: Path | None = None,
        limit: int = 20, fetcher=None) -> list[str]:
    """Enriches up to `limit` finished entries under `root`. Returns the paths changed."""
    changed = []
    today = now.date()
    for path in sorted(root.rglob("*.md"), reverse=True):  # newest first
        if len(changed) >= limit:
            break
        rel = path.relative_to(root)
        if any(part.startswith(".") or part == "_inkdeck" for part in rel.parts):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        if "sessions:" not in text and "type: Journal" not in text:
            continue
        fm = front_matter(text)
        day = entry_day(path, fm)
        if not day or day >= today:
            continue
        modified = parse_time(fm.get("modified"))
        if modified and modified.tzinfo and now - modified < dt.timedelta(hours=QUIET_HOURS):
            continue
        fetch = fetcher or (lambda la, lo, d: fetch_day(la, lo, d, today, cache))
        try:
            out = enrich_text(text, fetch, home, day)
        except yaml.YAMLError:
            out = None
        except RateLimited:
            break
        if out:
            path.write_text(out, encoding="utf-8")
            changed.append(rel.as_posix())
    return changed


def parse_home(value: str) -> tuple[float, float] | None:
    try:
        lat, lon = (float(x) for x in value.split(","))
        return lat, lon
    except ValueError:
        return None

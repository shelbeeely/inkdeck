#include "OpenMeteo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace meteo {

namespace {

int minutesOf(const char* iso, size_t n) {
  // "YYYY-MM-DDTHH:MM"
  if (n < 16 || iso[10] != 'T') return -1;
  return (iso[11] - '0') * 600 + (iso[12] - '0') * 60 + (iso[14] - '0') * 10 + (iso[15] - '0');
}

}  // namespace

bool weatherPath(double lat, double lon, const char* date, char* out, size_t outsz) {
  const int n = snprintf(out, outsz,
                         "/v1/forecast?latitude=%.4f&longitude=%.4f"
                         "&hourly=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,"
                         "cloud_cover,weather_code,wind_speed_10m"
                         "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_sum,"
                         "weather_code,uv_index_max,daylight_duration"
                         "&timezone=auto&start_date=%s&end_date=%s",
                         lat, lon, date, date);
  return n > 0 && static_cast<size_t>(n) < outsz;
}

bool airPath(double lat, double lon, const char* date, char* out, size_t outsz) {
  const int n = snprintf(out, outsz,
                         "/v1/air-quality?latitude=%.4f&longitude=%.4f&hourly=us_aqi,pm2_5"
                         "&timezone=auto&start_date=%s&end_date=%s",
                         lat, lon, date, date);
  return n > 0 && static_cast<size_t>(n) < outsz;
}

Parser::Parser(Day& day)
    : day_(day),
      json_(JsonCallbacks{this, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArrayStart,
                          onArrayEnd}) {}

void Parser::onKey(void* c, const char* k, size_t n) {
  auto* p = static_cast<Parser*>(c);
  const size_t m = n < sizeof(p->key_) - 1 ? n : 0;
  memcpy(p->key_, k, m);
  p->key_[m] = '\0';
  if (p->depth_ == 1) {
    const size_t s = n < sizeof(p->section_) - 1 ? n : 0;
    memcpy(p->section_, k, s);
    p->section_[s] = '\0';
  }
}
void Parser::onObjectStart(void* c) { ++static_cast<Parser*>(c)->depth_; }
void Parser::onObjectEnd(void* c) {
  auto* p = static_cast<Parser*>(c);
  if (p->depth_) --p->depth_;
}
void Parser::onArrayStart(void* c) {
  auto* p = static_cast<Parser*>(c);
  p->inArray_ = p->depth_ == 2;
  p->index_ = 0;
}
void Parser::onArrayEnd(void* c) { static_cast<Parser*>(c)->inArray_ = false; }
void Parser::onString(void* c, const char* v, size_t n) { static_cast<Parser*>(c)->value(v, n, false); }
void Parser::onNumber(void* c, const char* v, size_t n) { static_cast<Parser*>(c)->value(v, n, false); }
void Parser::onNull(void* c) { static_cast<Parser*>(c)->value("", 0, true); }

void Parser::value(const char* v, size_t n, bool isNull) {
  if (!inArray_) return;
  const int i = index_++;
  if (isNull) return;
  char buf[32];
  const size_t m = n < sizeof(buf) - 1 ? n : sizeof(buf) - 1;
  memcpy(buf, v, m);
  buf[m] = '\0';
  const float f = static_cast<float>(atof(buf));
  const char* k = key_;

  if (strcmp(section_, "hourly") == 0) {
    if (i < 0 || i >= 24) return;
    Hour& h = day_.hours[i];
    if (strcmp(k, "temperature_2m") == 0) {
      h.temp = f;
      h.valid = true;
    } else if (strcmp(k, "apparent_temperature") == 0) h.feels = f;
    else if (strcmp(k, "relative_humidity_2m") == 0) h.humidity = static_cast<int>(f);
    else if (strcmp(k, "precipitation") == 0) h.precip = f;
    else if (strcmp(k, "cloud_cover") == 0) h.cloud = static_cast<int>(f);
    else if (strcmp(k, "weather_code") == 0) h.code = static_cast<int>(f);
    else if (strcmp(k, "wind_speed_10m") == 0) h.wind = f;
    else if (strcmp(k, "us_aqi") == 0) h.aqi = static_cast<int>(f);
    else if (strcmp(k, "pm2_5") == 0) h.pm25 = f;
  } else if (strcmp(section_, "daily") == 0 && i == 0) {
    if (strcmp(k, "temperature_2m_max") == 0) {
      day_.tMax = f;
      day_.valid = true;
    } else if (strcmp(k, "temperature_2m_min") == 0) day_.tMin = f;
    else if (strcmp(k, "precipitation_sum") == 0) day_.precipSum = f;
    else if (strcmp(k, "uv_index_max") == 0) day_.uvMax = f;
    else if (strcmp(k, "daylight_duration") == 0) day_.daylightS = f;
    else if (strcmp(k, "weather_code") == 0) day_.code = static_cast<int>(f);
    else if (strcmp(k, "sunrise") == 0) day_.sunriseMin = minutesOf(v, n);
    else if (strcmp(k, "sunset") == 0) day_.sunsetMin = minutesOf(v, n);
  }
}

const char* describe(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Rain showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm with hail";
    default: return "Unknown";
  }
}

const char* moonPhase(int y, int m, int d) {
  // Days since a known new moon (2000-01-06 18:14 UTC), modulo the synodic month.
  auto days = [](int yy, int mm, int dd) {
    yy -= mm <= 2;
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy = (153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    return era * 146097 + static_cast<long>(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
  };
  const double since = static_cast<double>(days(y, m, d) - days(2000, 1, 6)) + 0.5 - 0.76;
  double age = fmod(since, 29.530588853);
  if (age < 0) age += 29.530588853;
  static const char* kNames[] = {"New moon",      "Waxing crescent", "First quarter", "Waxing gibbous",
                                 "Full moon",     "Waning gibbous",  "Last quarter",  "Waning crescent"};
  return kNames[static_cast<int>(age / 29.530588853 * 8 + 0.5) % 8];
}

void sunLabel(const Day& day, int minute, char* out, size_t outsz) {
  const int rise = day.sunriseMin, set = day.sunsetMin;
  if (rise < 0 || set < 0) {
    out[0] = '\0';
    return;
  }
  auto span = [](int mins, char* o, size_t n) {
    if (mins >= 60) snprintf(o, n, "%d h %02d min", mins / 60, mins % 60);
    else snprintf(o, n, "%d min", mins);
  };
  char s[24];
  if (abs(minute - rise) <= 30) {
    snprintf(out, outsz, "sunrise");
  } else if (abs(minute - set) <= 30) {
    snprintf(out, outsz, "sunset");
  } else if (minute < rise || minute > set) {
    snprintf(out, outsz, "dark");
  } else if (set - minute <= 60) {
    snprintf(out, outsz, "golden hour");
  } else if (minute - rise <= (set - rise) / 2) {
    span(minute - rise, s, sizeof(s));
    snprintf(out, outsz, "%s after sunrise", s);
  } else {
    span(set - minute, s, sizeof(s));
    snprintf(out, outsz, "%s before sunset", s);
  }
}

size_t dayYaml(const Day& day, int y, int m, int d, bool final, char* out, size_t outsz) {
  char rise[12] = "", set[12] = "";
  if (day.sunriseMin >= 0) snprintf(rise, sizeof(rise), "%02d:%02d", day.sunriseMin / 60, day.sunriseMin % 60);
  if (day.sunsetMin >= 0) snprintf(set, sizeof(set), "%02d:%02d", day.sunsetMin / 60, day.sunsetMin % 60);
  const int n = snprintf(out, outsz,
                         "day:\n"
                         "  sunrise: \"%s\"\n"
                         "  sunset: \"%s\"\n"
                         "  daylight_h: %.1f\n"
                         "  temp_max_c: %.1f\n"
                         "  temp_min_c: %.1f\n"
                         "  precip_mm: %.1f\n"
                         "  uv_max: %.1f\n"
                         "  weather: \"%s\"\n"
                         "  moon: \"%s\"\n"
                         "  final: %s\n"
                         "  source: open-meteo\n",
                         rise, set, day.daylightS / 3600.0, day.tMax, day.tMin, day.precipSum, day.uvMax,
                         describe(day.code), moonPhase(y, m, d), final ? "true" : "false");
  return n > 0 && static_cast<size_t>(n) < outsz ? static_cast<size_t>(n) : 0;
}

size_t sessionYaml(const Day& day, int minute, char* out, size_t outsz) {
  const int hour = minute >= 0 && minute < 24 * 60 ? minute / 60 : 0;
  const Hour& h = day.hours[hour];
  if (!h.valid) return 0;
  char light[40];
  sunLabel(day, minute, light, sizeof(light));
  int n = snprintf(out, outsz,
                   "{temp_c: %.1f, feels_c: %.1f, humidity: %d, cloud: %d, precip_mm: %.1f, wind_kmh: %.1f, "
                   "condition: \"%s\"",
                   h.temp, h.feels, h.humidity, h.cloud, h.precip, h.wind, describe(h.code));
  if (n < 0 || static_cast<size_t>(n) >= outsz) return 0;
  size_t len = static_cast<size_t>(n);
  if (h.aqi >= 0) {
    n = snprintf(out + len, outsz - len, ", aqi: %d, pm25: %.1f", h.aqi, h.pm25);
    if (n < 0 || static_cast<size_t>(n) >= outsz - len) return 0;
    len += static_cast<size_t>(n);
  }
  n = snprintf(out + len, outsz - len, "%s%s%s}", light[0] ? ", light: \"" : "", light, light[0] ? "\"" : "");
  if (n < 0 || static_cast<size_t>(n) >= outsz - len) return 0;
  return len + static_cast<size_t>(n);
}

}  // namespace meteo

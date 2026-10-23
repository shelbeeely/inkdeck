#pragma once

// Weather, air quality and sun times for a journal day, from Open-Meteo
// (https://open-meteo.com: free, no account or key). Pure (no Arduino/network
// dependencies): builds request paths, parses the JSON responses as they
// stream in, and formats what goes into the entry. Host-testable.
//
//   api.open-meteo.com/v1/forecast              hourly + daily weather, sun times
//   air-quality-api.open-meteo.com/v1/air-quality  hourly US AQI and PM2.5
//
// Both are asked for one local day (timezone=auto), which the forecast API
// serves for roughly the last three months.

#include <StreamingJsonParser.h>
#include <stddef.h>
#include <stdint.h>

namespace meteo {

constexpr const char* kWeatherHost = "api.open-meteo.com";
constexpr const char* kAirHost = "air-quality-api.open-meteo.com";
constexpr int kMaxAgeDays = 90;

struct Hour {
  bool valid = false;
  float temp = 0, feels = 0, precip = 0, wind = 0;
  int humidity = 0, cloud = 0, code = -1;
  int aqi = -1;
  float pm25 = -1;
};

struct Day {
  bool valid = false;
  float tMax = 0, tMin = 0, precipSum = 0, uvMax = 0, daylightS = 0;
  int code = -1;
  int sunriseMin = -1, sunsetMin = -1;  // minutes after local midnight
  Hour hours[24];
};

// "/v1/forecast?..." for `date` ("YYYY-MM-DD"). False if it doesn't fit.
bool weatherPath(double lat, double lon, const char* date, char* out, size_t outsz);
bool airPath(double lat, double lon, const char* date, char* out, size_t outsz);

// Streaming parser for either response; fills `day` as data arrives.
class Parser {
 public:
  explicit Parser(Day& day);
  void feed(const char* data, size_t len) { json_.feed(data, len); }
  bool hasError() const { return json_.hasError(); }

 private:
  static void onKey(void* c, const char* k, size_t n);
  static void onString(void* c, const char* v, size_t n);
  static void onNumber(void* c, const char* v, size_t n);
  static void onNull(void* c);
  static void onObjectStart(void* c);
  static void onObjectEnd(void* c);
  static void onArrayStart(void* c);
  static void onArrayEnd(void* c);
  static void onBool(void*, bool) {}
  void value(const char* v, size_t n, bool isNull);

  Day& day_;
  uint8_t depth_ = 0;
  char section_[8] = {0};  // "hourly" / "daily"
  char key_[28] = {0};
  bool inArray_ = false;
  int index_ = 0;
  StreamingJsonParser json_;
};

// WMO weather code -> "Light rain" etc.
const char* describe(int code);
// Moon phase name for a civil date.
const char* moonPhase(int y, int m, int d);
// Where the sun was at `minute` (local minutes after midnight): "dark",
// "sunrise", "golden hour", "sunset", or "3 h 10 min after sunrise".
void sunLabel(const Day& day, int minute, char* out, size_t outsz);

// Front matter: the day block ("day:\n  sunrise: ...") and one session's
// weather flow map ("{temp_c: 11.2, ...}"). `final` = the day is over (the
// highs and lows won't change any more).
size_t dayYaml(const Day& day, int y, int m, int d, bool final, char* out, size_t outsz);
size_t sessionYaml(const Day& day, int minute, char* out, size_t outsz);

}  // namespace meteo

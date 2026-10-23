#include "OpenMeteo.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
using namespace meteo;
static int fails = 0;
#define CHECK(n, c) do { bool ok = (c); printf("%s %s\n", ok ? "PASS" : "FAIL", n); if (!ok) fails++; } while (0)
static std::string slurp(const char* p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main(int, char** argv) {
  Day day;
  for (const char* file : {"wx.json", "aq.json"}) {
    std::string body = slurp((std::string(argv[1]) + "/" + file).c_str());
    Parser p(day);
    for (size_t i = 0; i < body.size(); i += 37) p.feed(body.data() + i, std::min<size_t>(37, body.size() - i));
    CHECK(file, !p.hasError());
  }
  CHECK("daily", day.valid && day.tMax > 20.3f && day.tMax < 20.5f && day.code == 3 && day.sunriseMin == 7 * 60 + 34 && day.sunsetMin == 18 * 60 + 16);
  CHECK("hourly weather", day.hours[0].valid && day.hours[0].temp > 11.1f && day.hours[0].humidity == 78 && day.hours[0].cloud == 99);
  CHECK("hourly air", day.hours[8].aqi == 43 && day.hours[8].pm25 > 8.6f);
  char out[512];
  size_t n = dayYaml(day, 2026, 10, 20, true, out, sizeof out); printf("%.*s", (int)n, out);
  n = sessionYaml(day, 8 * 60 + 40, out, sizeof out); printf("  session 08:40: %.*s\n", (int)n, out);
  CHECK("session yaml", n > 0 && strstr(out, "aqi: 43") && strstr(out, "1 h 06 min after sunrise"));
  const char* cases[][2] = {{"07:20", "sunrise"}, {"05:00", "dark"}, {"17:30", "golden hour"}, {"18:30", "sunset"}, {"21:00", "dark"}, {"15:00", "3 h 16 min before sunset"}};
  for (auto& c : cases) { int h, m; sscanf(c[0], "%d:%d", &h, &m); sunLabel(day, h * 60 + m, out, sizeof out);
    char name[64]; snprintf(name, sizeof name, "sun at %s -> %s (got %s)", c[0], c[1], out); CHECK(name, !strcmp(out, c[1])); }
  CHECK("moon: new 2024-01-11", !strcmp(moonPhase(2024, 1, 11), "New moon"));
  CHECK("moon: full 2024-01-25", !strcmp(moonPhase(2024, 1, 25), "Full moon"));
  CHECK("moon: first quarter 2024-01-18", !strcmp(moonPhase(2024, 1, 18), "First quarter"));
  CHECK("describe", !strcmp(describe(61), "Light rain") && !strcmp(describe(3), "Overcast"));
  char path[512]; CHECK("path", weatherPath(45.5231, -122.6765, "2026-10-20", path, sizeof path) && strstr(path, "latitude=45.5231"));
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
}

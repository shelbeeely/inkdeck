#include "screens.h"
#include "UsageLog.h"
#include <FreeInkApp.h>
#include <fstream>
#include <vector>
using namespace freeink::ui;
AppState g_state; DisplayTarget* g_target = nullptr; ThemeTokens g_theme;
void uiDrawHardwareFooter(const char*, const char*, const char*, const char*) {}
// Sample usage: a month of journaling, mostly mornings, plus some evening notes.
bool UsageLog::load(usage::Stats& st, int32_t& today) {
  today = usage::daysFromCivil(2026, 10, 23); st = usage::Stats(); st.today = today;
  char line[256];
  for (int d = 1; d <= 23; ++d) {
    if (d == 9 || d == 10 || d == 16) continue;  // skipped days
    const int words = 180 + (d * 37) % 400, active = words * 2;
    snprintf(line, sizeof line, "2026-10-%02dT07:%02d:00-07:00,session,J/2026-10-%02d.md,%d,%d,%d,%d,%d,%d,30,80,journal",
             d, d % 50, d, active + 120, active, words * 6, words * 6, words / 2, words);
    usage::Event e; usage::parse(line, e); st.add(e);
    if (d % 3 == 0) { snprintf(line, sizeof line, "2026-10-%02dT21:10:00-07:00,session,Ideas/x.md,600,400,900,900,90,150,22,60,note", d);
      usage::parse(line, e); st.add(e); }
  }
  usage::Event e;
  usage::parse("2026-10-20T09:00:00-07:00,sync,,12,0,0,0,0,0,0,80,down=1 up=2 deleted=0 conflicts=0 ok", e); st.add(e);
  usage::parse("2026-10-21T09:00:00-07:00,sync,,12,0,0,0,0,0,0,80,down=0 up=0 deleted=0 conflicts=0 failed", e); st.add(e);
  usage::parse("2026-10-23T10:00:00-07:00,keyboard,,52200,0,0,0,0,0,0,,disconnected", e); st.add(e);
  return true;
}
int main(int argc, char** argv) {
  const int16_t W = 800, H = 480, WB = W / 8;   // X4 panel (landscape native), shown portrait
  std::vector<uint8_t> fb(WB * H, 0xFF);
  DisplayTarget target(fb.data(), W, H, WB, Orientation::Portrait);
  g_target = &target;
  g_theme = defaultThemeTokens(); g_theme.headerHeight = 52; g_theme.footerHeight = 48; g_theme.rowHeight = 46;
  DeviceContext device = target.deviceContext(); device.safeArea = Insets{0, 0, g_theme.footerHeight, 0};
  FreeInkApp<32, 8> app(target, device); app.setTheme(g_theme);
  statsLoad();
  app.setScreen(statsScreen); app.render();
  // Write the logical (portrait) image as PGM by reading back through the target's rotation.
  const int lw = target.logicalWidth(), lh = target.logicalHeight();
  std::ofstream out(argv[1], std::ios::binary); out << "P5\n" << lw << " " << lh << "\n255\n";
  for (int y = 0; y < lh; ++y) for (int x = 0; x < lw; ++x) {
    // Portrait on this panel: logical (x,y) -> physical (y, W-1-x) or similar; probe via a pixel map built below.
    out.put(0);
  }
  // Physical dump as well; we rotate it in Python.
  std::ofstream phys(argv[2], std::ios::binary); phys << "P5\n" << W << " " << H << "\n255\n";
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) phys.put((fb[y * WB + (x >> 3)] & (0x80 >> (x & 7))) ? 255 : 0);
}

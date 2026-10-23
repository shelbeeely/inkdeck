// Renders the Pet screen on the host: pet_harness <notes folder> <today words> <out.pgm>
// The notes folder must contain _inkdeck/companion.md (e.g. from the server).
// Convert: python3 -c "from PIL import Image; Image.open('out.pgm').rotate(-90, expand=True).save('pet.png')"
#include "screens.h"
#include "UsageLog.h"
#include "Config.h"
#include <FreeInkApp.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
using namespace freeink::ui;
Config g_config;
AppState g_state; DisplayTarget* g_target = nullptr; ThemeTokens g_theme;
static int32_t g_words = 0;
void uiDrawHardwareFooter(const char*, const char*, const char*, const char*) {}
bool UsageLog::load(usage::Stats& st, int32_t& today) {
  today = usage::daysFromCivil(2026, 10, 23); st = usage::Stats(); st.today = today;
  char line[256];
  snprintf(line, sizeof line, "2026-10-23T07:30:00-07:00,session,J/2026-10-23.md,900,700,900,900,80,%d,30,80,journal", (int)g_words);
  usage::Event e; if (g_words > 0 && usage::parse(line, e)) st.add(e);
  return true;
}
int main(int argc, char** argv) {
  strncpy(g_config.notesFolder, argv[1], sizeof g_config.notesFolder - 1);
  g_words = atoi(argv[2]);
  const int16_t W = 800, H = 480, WB = W / 8;
  std::vector<uint8_t> fb(WB * H, 0xFF);
  const bool land = getenv("LANDSCAPE") != nullptr;  // LANDSCAPE=1: landscape, buttons on the right
  DisplayTarget target(fb.data(), W, H, WB, land ? Orientation::LandscapeClockwise : Orientation::Portrait);
  g_target = &target;
  g_theme = defaultThemeTokens(); g_theme.headerHeight = 52; g_theme.footerHeight = 48; g_theme.rowHeight = 46;
  DeviceContext device = target.deviceContext(); device.safeArea = land ? Insets{0, 0, 0, g_theme.footerHeight} : Insets{0, 0, g_theme.footerHeight, 0};
  FreeInkApp<32, 8> app(target, device); app.setTheme(g_theme);
  petLoad();
  app.setScreen(petScreen); app.render();
  std::ofstream phys(argv[3], std::ios::binary); phys << "P5\n" << W << " " << H << "\n255\n";
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) phys.put((fb[y * WB + (x >> 3)] & (0x80 >> (x & 7))) ? 255 : 0);
}

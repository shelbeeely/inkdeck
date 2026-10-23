// Renders the prompt picker on the host:
//   prompt_harness <notes folder> <top.pgm> <on-a-category.pgm> <in-the-category.pgm>
// <notes folder>/_inkdeck/prompts-suggested.txt (optional) stands in for the server.
// Needs the generated PromptLibrary.h: build once with `pio run`, then add
// -I ../../.pio/build/prompts.
#include "screens.h"
#include "Config.h"
#include "Journal.h"
#include "WritingStats.h"
#include <FreeInkApp.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <vector>
using namespace freeink::ui;
Config g_config;
AppState g_state; DisplayTarget* g_target = nullptr; ThemeTokens g_theme;
void uiDrawHardwareFooter(const char*, const char*, const char*, const char*) {}
void uiGotoScreen(AppScreen s) { g_state.screen = s; }
void uiInvalidate(bool) {}
void showToast(const char*) {}
uint32_t Journal::stubHash(const Document&) { return 0; }
void WritingStats::addPrompt(const char*) {}
bool Document::replaceRange(uint32_t, uint32_t, const char*, uint32_t) { return false; }
static void dump(const std::vector<uint8_t>& fb, int W, int H, const char* path) {
  std::ofstream o(path, std::ios::binary); o << "P5\n" << W << " " << H << "\n255\n";
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) o.put((fb[y * (W / 8) + (x >> 3)] & (0x80 >> (x & 7))) ? 255 : 0);
}
int main(int argc, char** argv) {
  strncpy(g_config.notesFolder, argv[1], sizeof g_config.notesFolder - 1);
  const int16_t W = 800, H = 480;
  std::vector<uint8_t> fb(W / 8 * H, 0xFF);
  const bool land = getenv("LANDSCAPE") != nullptr;  // LANDSCAPE=1: landscape, buttons on the right
  DisplayTarget target(fb.data(), W, H, W / 8, land ? Orientation::LandscapeClockwise : Orientation::Portrait);
  g_target = &target;
  g_theme = defaultThemeTokens(); g_theme.headerHeight = 52; g_theme.footerHeight = 48; g_theme.rowHeight = 46;
  DeviceContext device = target.deviceContext(); device.safeArea = land ? Insets{0, 0, 0, g_theme.footerHeight} : Insets{0, 0, g_theme.footerHeight, 0};
  FreeInkApp<32, 8> app(target, device); app.setTheme(g_theme);
  promptsOpen();
  app.setScreen(promptsScreen); app.render(); dump(fb, W, H, argv[2]);
  // Down to the first category, open it, move to its third prompt.
  for (int i = 0; i < 8; ++i) promptsNav(1);
  std::fill(fb.begin(), fb.end(), 0xFF); app.render(); dump(fb, W, H, argv[3]);  // a category selected
  promptsConfirm(); promptsNav(1); promptsNav(1);
  std::fill(fb.begin(), fb.end(), 0xFF); app.render(); dump(fb, W, H, argv[4]);
}

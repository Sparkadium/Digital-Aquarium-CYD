#pragma once
// CYD native panel: 240x320. Habitats expand, animal geometry stays proportional.
static constexpr int FRAME_PIXELS = 240 * 320, MAX_SIDE = 320;
static constexpr int BLOOM_CELLS = 61 * 80, CAUSTIC_CELLS = 160 * 32;
static constexpr int vignetteCapacity(int w, int h) {
  return ((w / 2) * (w / 2) + (h - 1 - h * 150 / 320) * (h - 1 - h * 150 / 320)) / 32 + 1;
}
static constexpr int VIGNETTE_CELLS = vignetteCapacity(240,320) > vignetteCapacity(320,240)
  ? vignetteCapacity(240,320) : vignetteCapacity(320,240);
static bool landscape = false;
static int W = 240, H = 320, BW = 61, BH = 80;
static int VCX = 120, VCY = 150, CAUS_TOP = 236, CGW = 120, CGH = 42;
static constexpr float SURFACE = 9.f;
static float WATER_BOT = 278.f, RAY_BOT = 258.f, depthScale = 1.f;
static inline float layoutX(float x) { return x * (W / 172.f); }
static inline int layoutXi(int x) { return x * W / 172; }
static inline float referenceX(float x) { return x * (172.f / W); }
static inline float layoutY(float y) {
  if (!landscape || y <= SURFACE) return y;
  return y <= 278.f ? SURFACE + (y - SURFACE) * depthScale : H - (320.f - y);
}
static inline int layoutYi(int y) {
  if (!landscape || y <= 9) return y;
  return y <= 278 ? 9 + (y - 9) * 189 / 269 : H - (320 - y);
}
static inline int referenceYi(int y) { return landscape ? 9 + (y - 9) * 269 / 189 : y; }
static void configureLayout(bool horizontal) {
  landscape = horizontal; W = horizontal ? 320 : 240; H = horizontal ? 240 : 320;
  WATER_BOT = H - 42.f; depthScale = (WATER_BOT - SURFACE) / 269.f;
  RAY_BOT = layoutY(258.f); VCX = W / 2; VCY = H * 150 / 320;
  BW = W / 4 + 1; BH = H / 4;
  CAUS_TOP = H * 59 / 80; CGW = W / 2; CGH = (H - CAUS_TOP + 1) / 2;
}

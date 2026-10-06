/* Glowtank — a glowing-particle community aquarium.
   Cheap Yellow Display ESP32-2432S028R: ILI9341 and resistive touch.
   Arduino ESP32 core 3.x, GFX Library for Arduino 1.5.9. No PSRAM, LVGL, SD or Wi-Fi.

   BOOT tap ............ pinch of flakes
   BOOT hold 0.55 s .... sinking algae wafer; normal food in Abyss / Pokemon
   BOOT release 2–5 s .. Realistic -> Fantasy -> Comb Jellies -> Abyss -> Pokemon (remembered)

   BOOT hold 5 s ...... portrait / landscape (remembered; no mode change)

   Serial (115200) prints frame rate and a per-stage timing breakdown every 2 s.
*/
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <SPI.h>
#include "esp_bt.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "board_config.h"
#define GT_BACKGROUND_YIELD() delay(1)
#include "tank.h"
#include "boot_gesture.h"
#include "touch_feed.h"
#include "cyd_panel.h"

#include "lcd_dma.h"
Arduino_DataBus *bus = nullptr;
Arduino_TFT *gfx = nullptr;
static GlowtankDmaBus *dmaBus = nullptr;
Preferences prefs;
static SPIClass lcdSPI(HSPI);
#if CYD_TOUCH_FEED
static SPIClass touchSPI(VSPI);
static TouchFeedGate touchGate;
#endif

#ifndef FRAME_MIN_MS
#define FRAME_MIN_MS 0          // no frame cap; set e.g. 33 to hold ~30 fps
#endif
#ifndef STATS_ON_SCREEN_S
#define STATS_ON_SCREEN_S 0     // optional startup timing overlay; serial timing remains enabled
#endif
#ifndef LCD_DMA
#define LCD_DMA 1               // IDF owns SPI from startup; 0 = original Arduino_HWSPI path
#endif
#ifndef LCD_FAST_PUSH
#define LCD_FAST_PUSH 1         // 0 = the portable writePixels path
#endif


// The CYD has three active-low LED pins, not an addressable RGB LED.
static uint32_t ledOffAt=0;
static bool ledOn=false;
static void ledColor(uint8_t r,uint8_t g,uint8_t b){
  digitalWrite(PIN_LED_R,r?LOW:HIGH);digitalWrite(PIN_LED_G,g?LOW:HIGH);digitalWrite(PIN_LED_B,b?LOW:HIGH);
}
static void ledFlash(uint8_t r,uint8_t g,uint8_t b,uint32_t ms){ledColor(r,g,b);ledOn=true;ledOffAt=millis()+ms;}
static void ledService(uint32_t now){if(ledOn&&(int32_t)(now-ledOffAt)>=0){ledColor(0,0,0);ledOn=false;}}

static void doFeed(float x) { gt::feed(x); ledFlash(40, 22, 0, 220); }
static void doWafer(float x) { gt::dropWafer(x); ledFlash(6, 30, 4, 350); }
#ifndef TANK_LIGHT
#define TANK_LIGHT 0            // 0 day, 1 dusk, 2 night glow, 3 day-night cycle
#endif
static void nextTank() {
  gt::setTheme(gt::theme + 1);
  gt::setLight(gt::themeLight(gt::theme, TANK_LIGHT));
  gt::step(0);  // refresh lighting before the first frame in the new tank
  gt::showLabel(gt::themeName(gt::theme));
  prefs.putUChar("theme", (uint8_t)gt::theme);
  Serial.printf("tank: %s\n", gt::themeName(gt::theme));
}

// DMA setup failure or BOOT held during startup selects the original display driver.
static bool useDma = false;
static Arduino_TFT *makePanel() {
  return new CydIli9341(bus, LCD_RST, LCD_ROTATION, false);
}
static bool beginPanel() {
#if LCD_DMA
  if (digitalRead(PIN_BOOT) != LOW) {
    dmaBus = new GlowtankDmaBus(); bus = dmaBus;
    gfx = makePanel();
    if (gfx->begin(LCD_SPI_HZ) && dmaBus->ok()) { useDma = true; return true; }
    Serial.printf("DMA init failed (%d); using Arduino SPI\n", (int)dmaBus->error());
    delete static_cast<CydIli9341 *>(gfx); gfx = nullptr;
    delete dmaBus; dmaBus = nullptr;
  }
#endif
  bus = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO, &lcdSPI);
  gfx = makePanel();
  return gfx->begin(LCD_SPI_HZ);
}
static void displayFault() {
  Serial.printf("Display transfer error %d; try LCD_DMA=0 in the source for Arduino SPI\n", (int)dmaBus->error());
  while (true) delay(100); // Do not overwrite a buffer whose DMA completion is uncertain.
}

static void startBacklight(){
  pinMode(LCD_BL,OUTPUT);digitalWrite(LCD_BL,HIGH);
#if BACKLIGHT_LEVEL < 255
  if(ledcAttach(LCD_BL,20000,8)&&ledcWrite(LCD_BL,BACKLIGHT_LEVEL)){
    Serial.printf("backlight GPIO%d PWM %d/255\n",LCD_BL,BACKLIGHT_LEVEL);return;
  }
  ledcDetach(LCD_BL);pinMode(LCD_BL,OUTPUT);digitalWrite(LCD_BL,HIGH);
  Serial.println("Backlight PWM failed; using full-on GPIO");
#endif
  Serial.printf("backlight GPIO%d full ON\n",LCD_BL);
}

// Fixed color bars exercise two independent pixel paths. They are not a
// claim that the panel was seen: only the person viewing it can confirm that.
static const uint16_t checkColors[6]={0xf800,0x07e0,0x001f,0xffff,0xffe0,0x07ff};
static void panelCheck(){
  if(!LCD_STARTUP_CHECK_MS)return;
  Serial.println("LCD check A: expect six color bars and CYD 1.3 / PANEL text");
  int w=gfx->width(),h=gfx->height();
  for(int i=0;i<6;i++)gfx->fillRect(i*w/6,0,(i+1)*w/6-i*w/6,h,checkColors[i]);
  gfx->fillRect(0,0,w,48,0);gfx->setTextSize(2);gfx->setTextColor(0xffff);
  gfx->setCursor(8,6);gfx->print("CYD 1.3");gfx->setCursor(8,26);gfx->print("A: PANEL");
  gfx->setTextSize(1);delay(LCD_STARTUP_CHECK_MS);
}
static void frameCheck(){
  if(!LCD_STARTUP_CHECK_MS)return;
  Serial.printf("LCD check B: expect color bars through %s frame transfer\n",useDma?"DMA":"Arduino SPI");
  for(int y=0;y<gt::H;y++)for(int x=0;x<gt::W;x++)gt::frameRow(y)[x]=y<30?0:checkColors[x*6/gt::W];
  gt::drawText(8,8,useDma?"B: DMA FRAME":"B: SPI FRAME",256);
  gt::swapForPanel(true,LCD_SWAP_RB);
  gfx->startWrite();gfx->writeAddrWindow(0,0,gt::W,gt::H);
  if(useDma){
    if(!dmaBus->queueFrame(gt::fbBanks)||!dmaBus->waitFrame())displayFault();
  }else for(auto*bank:gt::fbBanks)bus->writeBytes((uint8_t*)bank,gt::BANK_PIXELS*2);
  gfx->endWrite();delay(LCD_STARTUP_CHECK_MS);
}

// ---------------------------------------------------------------- BOOT button
static BootGesture boot;
static void rotateTank() {
  if (useDma && !dmaBus->waitFrame()) displayFault();
  gt::setOrientation(!gt::landscape);
  // ILI9341 changes its address traversal in hardware; no framebuffer transpose.
  gfx->setRotation(gt::landscape ? 1 : LCD_ROTATION);
  gt::step(0);
  gt::showLabel(gt::landscape ? "LANDSCAPE" : "PORTRAIT");
  prefs.putBool("landscape", gt::landscape);
  ledFlash(0, 24, 30, 350);
  Serial.printf("orientation: %s (%dx%d)\n", gt::landscape ? "landscape" : "portrait", gt::W, gt::H);
}
static void serviceBoot(uint32_t now) {
  switch (boot.update(digitalRead(PIN_BOOT) == LOW, now)) {
    case BootGesture::FEED: doFeed(-1.f); break;
    case BootGesture::WAFER: doWafer(-1.f); break;
    case BootGesture::MODE_READY: ledFlash(24, 0, 24, 180); break;
    case BootGesture::NEXT_MODE: nextTank(); break;
    case BootGesture::ROTATE: rotateTank(); break;
    default: break;
  }
}

// ---------------------------------------------------------------- setup / loop
static const char *resetReasonName(esp_reset_reason_t reason){
  switch(reason){
    case ESP_RST_POWERON:return "POWERON";
    case ESP_RST_EXT:return "EXTERNAL";
    case ESP_RST_SW:return "SOFTWARE";
    case ESP_RST_PANIC:return "PANIC";
    case ESP_RST_INT_WDT:return "INT_WDT";
    case ESP_RST_TASK_WDT:return "TASK_WDT";
    case ESP_RST_WDT:return "WATCHDOG";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:return "BROWNOUT";
    default:return "OTHER";
  }
}
void setup() {
  Serial.begin(115200);
  esp_reset_reason_t reason=esp_reset_reason();
  Serial.printf("\nGlowtank %s | reset: %s (%d) | CPU %lu MHz | IDF %s\n",GLOWTANK_RELEASE,
                resetReasonName(reason),(int)reason,(unsigned long)ESP.getCpuFreqMHz(),ESP.getSdkVersion());
  // Offline aquarium: return the ESP32 controller's reserved RAM to the heap.
  esp_err_t btRelease=esp_bt_mem_release(ESP_BT_MODE_BTDM);
  Serial.printf("Bluetooth memory release: %d; free DRAM %lu\n",(int)btRelease,(unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
  for(int pin: {PIN_LED_R,PIN_LED_G,PIN_LED_B}){pinMode(pin,OUTPUT);digitalWrite(pin,HIGH);}
  pinMode(5,OUTPUT);digitalWrite(5,HIGH); // deselect the unused SD card
  pinMode(PIN_BOOT, INPUT_PULLUP);
  boot.begin(digitalRead(PIN_BOOT) == LOW, millis());
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, LOW);

  if (!beginPanel()) { Serial.println("Display init failed"); while (true) delay(100); }
  gfx->setRotation(LCD_ROTATION);
  gfx->fillScreen(0);

  Serial.printf("panel push: %s, %lu MHz\n", useDma ? "DMA" : "Arduino SPI", (unsigned long)(LCD_SPI_HZ / 1000000));
  startBacklight();
  panelCheck();


#if CYD_TOUCH_FEED
  pinMode(TOUCH_CS,OUTPUT);digitalWrite(TOUCH_CS,HIGH);pinMode(TOUCH_IRQ,INPUT);
  touchSPI.begin(TOUCH_SCK,TOUCH_MISO,TOUCH_MOSI,TOUCH_CS);
  // End an ADC conversion in power-down mode, enabling XPT2046 PENIRQ.
  touchSPI.beginTransaction(SPISettings(2000000,MSBFIRST,SPI_MODE0));
  digitalWrite(TOUCH_CS,LOW);touchSPI.transfer(0x90);touchSPI.transfer16(0);
  digitalWrite(TOUCH_CS,HIGH);touchSPI.endTransaction();
  touchGate.begin(digitalRead(TOUCH_IRQ)==LOW,millis());
#endif

  prefs.begin("glowtank-cyd", false);
  uint32_t t0 = millis();
  if(!gt::init(esp_random(),prefs.getBool("landscape",false))){
    Serial.printf("Framebuffer allocation failed; free heap %lu\n",(unsigned long)ESP.getFreeHeap());
    gfx->setTextColor(0xf800);gfx->setCursor(6,12);gfx->print("Graphics memory unavailable");
    while(true)delay(100);
  }
  gfx->setRotation(gt::landscape ? 1 : LCD_ROTATION);
  frameCheck();
  Serial.printf("cache allocated %lu bytes: IRAM %u x %u, DRAM %u x %u | largest free DRAM %lu\n",
                (unsigned long)(gt::backdrop.capacityWords*4),gt::backdrop.iramBanks,gt::BackdropCache::BANK_BYTES,
                gt::backdrop.dramBanks,gt::BackdropCache::BANK_BYTES,
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  gt::dayLight = gt::lightMode == 0 ? 1.f : gt::lightMode == 1 ? 0.55f : gt::lightMode == 2 ? 0.2f : 0.8f;
  gt::theme = prefs.getUChar("theme", gt::POKEMON) % gt::THEME_COUNT;
  gt::setLight(gt::themeLight(gt::theme, TANK_LIGHT));
  gt::restart();
  gt::step(33); gt::prepBase(); gt::basePrepared = true; gt::prepCaustics();
  Serial.printf("\nGlowtank on %s — init %lu ms, free heap %lu bytes, %d fish, %d plant segments, backdrop %s\n",
                BOARD_NAME, (unsigned long)(millis() - t0), (unsigned long)ESP.getFreeHeap(), gt::population(), gt::segTotal,
                gt::backdrop.valid ? "lossless cache" : "drawn per frame");
  Serial.printf("tank: %s | BOOT on GPIO%d reads %s\n", gt::themeName(gt::theme), PIN_BOOT,
                digitalRead(PIN_BOOT) == LOW ? "LOW (held, or wrong pin)" : "HIGH (released)");
}

static uint32_t lastMs = 0, statAt = 0;
static uint32_t stageUs[9];
static uint32_t frames = 0, blitSum = 0;
static uint32_t worstFrameUs = 0, overBudgetFrames = 0;

void loop() {
  uint32_t now = millis();
  if (!statAt) statAt = now;
  if (FRAME_MIN_MS && now - lastMs < FRAME_MIN_MS) { delay(1); return; }
  uint32_t dt = lastMs ? now - lastMs : 33;
  lastMs = now;

  const uint32_t frameStartUs = micros();
  serviceBoot(now);
#if CYD_TOUCH_FEED
  if(touchGate.update(digitalRead(TOUCH_IRQ)==LOW,now)&&digitalRead(PIN_BOOT)!=LOW)doFeed(-1.f);
#endif
  ledService(now);

  uint32_t a = micros();
  // (the simulation step for this frame already ran while the previous frame was being sent)
  if(!useDma)gt::step(dt);
  uint32_t simBefore=micros()-a,bgStart=micros();
  // The lossless cache can use frame banks as scratch: run it before DMA.
  gt::prepBase();gt::basePrepared=true;
  uint32_t bgUs=micros()-bgStart,causticStart=micros();
  if(!useDma)gt::prepCaustics();
  uint32_t b = micros();
  gt::renderBase();        uint32_t c = micros();
  gt::renderMid();         uint32_t d = micros();
  gt::renderGlow();        uint32_t e = micros();
  gt::renderBloom();       uint32_t f = micros();
  gt::renderFront();       uint32_t g = micros();
  static bool firstFrame=true;
  if(firstFrame){
    uint32_t nonzero=0;uint16_t brightest=0;
    for(int y=0;y<gt::H;y++)for(int x=0;x<gt::W;x++){
      uint16_t pixel=gt::frameRow(y)[x];nonzero+=(pixel!=0);if(pixel>brightest)brightest=pixel;
    }
    Serial.printf("Aquarium frame: %lu/%d nonzero pixels; largest RGB565 0x%04x\n",(unsigned long)nonzero,gt::FRAME_PIXELS,brightest);
    firstFrame=false;
    // Keep the one-time diagnostic outside packing/SPI timing measurements.
    g=micros();
  }
  uint32_t simUs=simBefore+(b-causticStart),waitUs=0,packUs=0;
#if LCD_DMA
  if (useDma) {
    gt::swapForPanel(true, LCD_SWAP_RB);
    packUs=micros()-g;uint32_t pushStart=micros();
    gfx->startWrite();
    gfx->writeAddrWindow(0, 0, gt::W, gt::H);
    if (!dmaBus->queueFrame(gt::fbBanks)) displayFault();
    uint32_t s0 = micros();
    gt::step(dt);                                  // runs while the pixels go out
    gt::prepCaustics();
    uint32_t s1 = micros();
    if (!dmaBus->waitFrame()) displayFault();
    gfx->endWrite();
    simUs += s1-s0; waitUs = (s0-pushStart)+(micros()-s1);
  } else
#endif
  {
    gfx->startWrite();
    gfx->writeAddrWindow(0, 0, gt::W, gt::H);
#if LCD_FAST_PUSH
    uint32_t packStart=micros();
    gt::swapForPanel(true, LCD_SWAP_RB);
    packUs=micros()-packStart;
    for(auto *bank:gt::fbBanks)bus->writeBytes((uint8_t*)bank,gt::BANK_PIXELS*2);
#else
    uint32_t packStart=micros();
    if (LCD_SWAP_RB) gt::swapForPanel(false, true);
    packUs=micros()-packStart;
    for(auto *bank:gt::fbBanks)bus->writePixels(bank,gt::BANK_PIXELS);
#endif
    gfx->endWrite();
    waitUs = micros()-g-packUs;
  }

  // Permit idle/system tasks to run even when simulation consumes the whole DMA window.
  delay(1);
  // Background generation and byte packing are separate from simulation/SPI timings.
  stageUs[0] += simUs; stageUs[1] += c - b; stageUs[2] += d - c; stageUs[3] += e - d;
  stageUs[4] += f - e; stageUs[5] += g - f; stageUs[6] += waitUs;
  stageUs[7] += bgUs; stageUs[8] += packUs;
  const uint32_t frameUs = micros() - frameStartUs;
  if (frameUs > worstFrameUs) worstFrameUs = frameUs;
  if (frameUs > 33333) overBudgetFrames++;
  frames++; blitSum += gt::blitCount;

  uint32_t statsNow = millis();
  if (statsNow - statAt >= 2000) {
    float n = frames ? (float)frames : 1.f;
    float fps = frames * 1000.f / (statsNow - statAt);
    float ms[9]; for (int k = 0; k < 9; k++) ms[k] = stageUs[k] / n / 1000.f;
    Serial.printf("fps %.1f | ms: sim %.1f  bg %.1f  base %.1f  mid %.1f  glow %.1f  bloom %.1f  front %.1f  pack %.1f  spi %.1f | %u sprites\n",
                  (double)fps, (double)ms[0], (double)ms[7], (double)ms[1], (double)ms[2], (double)ms[3], (double)ms[4], (double)ms[5], (double)ms[8], (double)ms[6],
                  (unsigned)(blitSum / (frames ? frames : 1)));
    Serial.printf("%s | %s %dx%d | %d creatures | worst %.1f ms | >33.3ms %lu/%lu | heap %lu\n",
                  gt::themeName(gt::theme), gt::landscape ? "landscape" : "portrait", gt::W, gt::H, gt::population(), (double)(worstFrameUs*.001f),
                  (unsigned long)overBudgetFrames, (unsigned long)frames, (unsigned long)ESP.getFreeHeap());
    Serial.printf("cache %s %lu/%lu bytes | cache builds %lu | up %lus | stack free min %lu\n",gt::backdrop.valid?"on":"fallback",(unsigned long)(gt::backdrop.usedWords*4),(unsigned long)(gt::backdrop.capacityWords*4),(unsigned long)gt::backdrop.fullBakes,(unsigned long)(statsNow/1000),(unsigned long)uxTaskGetStackHighWaterMark(nullptr));
    if (STATS_ON_SCREEN_S && now < (uint32_t)STATS_ON_SCREEN_S * 1000u) {
      char b[32];
      snprintf(b, sizeof b, "FPS %.1f  SPI %.1f", (double)fps, (double)ms[6]); gt::setOverlay(0, b);
      snprintf(b, sizeof b, "SIM %.1f GLOW %.1f", (double)ms[0], (double)ms[3]); gt::setOverlay(1, b);
      snprintf(b, sizeof b, "BG %.1f MID %.1f FRT %.1f", (double)(ms[1]), (double)ms[2], (double)(ms[4] + ms[5])); gt::setOverlay(2, b);
    } else { gt::setOverlay(0, ""); gt::setOverlay(1, ""); gt::setOverlay(2, ""); }
    memset(stageUs, 0, sizeof(stageUs)); frames = 0; blitSum = 0; statAt = statsNow;
    worstFrameUs = 0; overBudgetFrames = 0;
  }
}

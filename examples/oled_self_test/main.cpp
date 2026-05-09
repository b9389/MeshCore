#include <Arduino.h>
#include "target.h"

#ifdef DISPLAY_CLASS
static uint8_t current_page = 0;
static uint32_t last_page_change_ms = 0;
static uint32_t last_status_refresh_ms = 0;
static uint32_t render_count = 0;

static constexpr uint32_t PAGE_INTERVAL_MS = 3000;
static constexpr uint32_t STATUS_REFRESH_MS = 250;
static constexpr uint8_t PAGE_COUNT = 3;

static void renderPage() {
  char line[32];

  display.startFrame();
  display.setTextSize(1);

  switch (current_page) {
    case 0:
      display.setCursor(0, 0);
      display.print("Cinder OLED");
      display.setCursor(0, 10);
      display.print("Heltec V3 test");
      display.setCursor(0, 24);
      display.print("Click: next page");
      display.setCursor(0, 36);
      display.print("Hold: power test");
      display.setCursor(0, 50);
      snprintf(line, sizeof(line), "Frame %lu", (unsigned long)render_count);
      display.print(line);
      break;

    case 1:
      display.drawRect(0, 0, 128, 64);
      display.drawRect(4, 4, 120, 56);
      display.fillRect(10, 12, 18, 18);
      display.fillRect(38, 12, 18, 18);
      display.fillRect(66, 12, 18, 18);
      display.fillRect(94, 12, 18, 18);
      display.setCursor(10, 40);
      display.print("Fill / border");
      display.setCursor(20, 50);
      display.print("pixel sanity");
      break;

    default:
      display.setCursor(0, 0);
      display.print("Power / redraw");
      display.setCursor(0, 12);
      display.print("Vext active-low");
      display.setCursor(0, 24);
      snprintf(line, sizeof(line), "Uptime %lus", (unsigned long)(millis() / 1000));
      display.print(line);
      display.setCursor(0, 36);
      snprintf(line, sizeof(line), "Render %lu", (unsigned long)render_count);
      display.print(line);
      display.setCursor(0, 50);
      display.print("Hold btn to cycle");
      break;
  }

  display.endFrame();
}

static void advancePage() {
  current_page = (uint8_t)((current_page + 1) % PAGE_COUNT);
  last_page_change_ms = millis();
  render_count++;
  renderPage();
}

static void cycleDisplayPower() {
  display.turnOff();
  delay(250);
  display.turnOn();
  render_count++;
  renderPage();
}
#endif

void setup() {
  Serial.begin(115200);
  delay(200);

  board.begin();

#ifdef DISPLAY_CLASS
  user_btn.begin();

  if (!display.begin()) {
    Serial.println("Heltec V3 OLED self-test: display init failed");
    while (1) {
      delay(1000);
    }
  }

  render_count = 1;
  last_page_change_ms = millis();
  last_status_refresh_ms = last_page_change_ms;
  renderPage();
#endif
}

void loop() {
#ifdef DISPLAY_CLASS
  int event = user_btn.check();
  if (event == BUTTON_EVENT_CLICK) {
    advancePage();
  } else if (event == BUTTON_EVENT_LONG_PRESS) {
    cycleDisplayPower();
    last_page_change_ms = millis();
    last_status_refresh_ms = last_page_change_ms;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - last_page_change_ms) >= PAGE_INTERVAL_MS) {
    advancePage();
  } else if (current_page == 2 && (uint32_t)(now - last_status_refresh_ms) >= STATUS_REFRESH_MS) {
    last_status_refresh_ms = now;
    render_count++;
    renderPage();
  }
#else
  delay(1000);
#endif
}

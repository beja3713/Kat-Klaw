#include <Wire.h>
#include <INA226.h>
#include <Adafruit_NeoPixel.h>
// ---------------- MACROS ----------------
#define LED_BRIGHTNESS 255 - 51 * 3

// ---------------- INA226 ----------------
INA226 INA(0x40);

// ---------------- Indicator LEDs ----------------
#define LED_PIN    6
#define LED_COUNT  5

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------------- Test LED Strips ----------------
#define PIN_D9    9
#define PIN_D10   10
#define PIN_D11   11
#define PIN_D12   12
#define PIN_D13   13

#define LEDS_D9   20
#define LEDS_D10  22
#define LEDS_D11  28
#define LEDS_D12  144
#define LEDS_D13  30

Adafruit_NeoPixel stripD9 (LEDS_D9,  PIN_D9,  NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripD10(LEDS_D10, PIN_D10, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripD11(LEDS_D11, PIN_D11, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripD12(LEDS_D12, PIN_D12, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripD13(LEDS_D13, PIN_D13, NEO_GRB + NEO_KHZ800);

// ---------------- Battery thresholds ----------------
const float BATTERY_FULL_V    = 12.6;
const float BATTERY_SHUTOFF_V = 8.4;
const float FLASH_THRESHOLD_V = 8.8;

// ---------------- Timing ----------------
unsigned long lastFlashTime = 0;
bool flashState = false;
const unsigned long flashIntervalMs = 300;

// ----------------------------------------------------
void setStripColor(Adafruit_NeoPixel &s, uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < s.numPixels(); i++) {
    s.setPixelColor(i, s.Color(r, g, b));
  }
  s.show();
}

void setAllLEDs(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

// Smoothly shift from green to red
void showBatteryColor(float batteryVoltage) {

  if (batteryVoltage > BATTERY_FULL_V) batteryVoltage = BATTERY_FULL_V;
  if (batteryVoltage < FLASH_THRESHOLD_V) batteryVoltage = FLASH_THRESHOLD_V;

  float fraction =
      (batteryVoltage - FLASH_THRESHOLD_V) /
      (BATTERY_FULL_V - FLASH_THRESHOLD_V);

  uint8_t red   = (uint8_t)(255.0f * (1.0f - fraction));
  uint8_t green = (uint8_t)(255.0f * fraction);

  setAllLEDs(red, green, 0);
}

void flashRedWarning() {

  unsigned long now = millis();

  if (now - lastFlashTime >= flashIntervalMs) {
    lastFlashTime = now;
    flashState = !flashState;

    if (flashState) {
      setAllLEDs(255, 0, 0);
    } else {
      setAllLEDs(0, 0, 0);
    }
  }
}

void setup() {

  Serial.begin(115200);
  delay(2000);

  Serial.println("INA226 battery monitor with LED warning");

  Wire.begin();

  if (!INA.begin()) {
    Serial.println("INA226 not detected");
    while (1);
  }

  INA.setMaxCurrentShunt(10.0, 0.005);

  // Indicator strip
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();

  // Test strips
  stripD9.begin();
  stripD10.begin();
  stripD11.begin();
  stripD12.begin();
  stripD13.begin();

  stripD9.setBrightness(LED_BRIGHTNESS);
  stripD10.setBrightness(LED_BRIGHTNESS);
  stripD11.setBrightness(LED_BRIGHTNESS);
  stripD12.setBrightness(LED_BRIGHTNESS);
  stripD13.setBrightness(LED_BRIGHTNESS);

  Serial.println("Battery_V\tCurrent_A\tPower_W");
}

void loop() {

  float batteryVoltage = INA.getBusVoltage();
  float currentA = INA.getCurrent();
  float powerW = INA.getPower();

  Serial.print(batteryVoltage, 3);
  Serial.print("\t\t");
  Serial.print(currentA, 3);
  Serial.print("\t\t");
  Serial.println(powerW, 3);

  // Battery indicator strip
  if (batteryVoltage <= FLASH_THRESHOLD_V) {
    flashRedWarning();
  } else {
    showBatteryColor(batteryVoltage);
  }

  // Keep test strips ON to draw load
  setStripColor(stripD9, 255, 255, 255);
  setStripColor(stripD10, 255, 255, 255);
  setStripColor(stripD11, 255, 255, 255);
  setStripColor(stripD12, 255, 255, 255);
  setStripColor(stripD13, 255, 255, 255);

  delay(100);
}

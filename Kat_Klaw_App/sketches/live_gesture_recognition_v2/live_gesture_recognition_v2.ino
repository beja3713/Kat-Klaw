/* ================= INCLUDES ================= */
#include <Arduino_LSM6DSOX.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <INA226.h>
#include "templates/templates_index.h"

/* ================= CONFIG ================= */
#define BUTTON_PIN 5

#define LED_PIN 11
#define LED_BRIGHTNESS 255 - 51 * 3

#define SYNC_PIN 12
#define N_SAMPLES 64 * 2
#define AXES 6
#define SAMPLE_DELAY_MS (1000 / N_SAMPLES)

#define DTW_INF 1e30f
#define SIM_THRESHOLD 0.35f

#define LED_COUNT 4

// ---------------- INA226 ----------------
INA226 INA(0x40);


// ---------------- Indicator LEDs ----------------
// #define LED_PIN    6
#define LED_COUNT 67

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------------- Test LED Strips ----------------
#define PIN_D9 9
#define PIN_D10 10
#define PIN_D11 11
#define PIN_D12 12
#define PIN_D13 13

#define LEDS_D9 20
#define LEDS_D10 22
#define LEDS_D11 28
#define LEDS_D12 144
#define LEDS_D13 30

Adafruit_NeoPixel stripD9(LEDS_D9, PIN_D9, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripD10(LEDS_D10, PIN_D10, NEO_GRB + NEO_KHZ800);
// Adafruit_NeoPixel stripD11(LEDS_D11, PIN_D11, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripD12(LEDS_D12, PIN_D12, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripD13(LEDS_D13, PIN_D13, NEO_GRB + NEO_KHZ800);

// ---------------- Battery thresholds ----------------
const float BATTERY_FULL_V = 12.6;
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
    (batteryVoltage - FLASH_THRESHOLD_V) / (BATTERY_FULL_V - FLASH_THRESHOLD_V);

  uint8_t red = (uint8_t)(255.0f * (1.0f - fraction));
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

/* ================= STRUCTS ================= */
typedef struct struct_message {
  char gesture[32];
  // unsigned long sendMillis;
  // unsigned long syncMillis;
} struct_message;

typedef struct struct_message_in {
  float band1, band2, band3, band4, band5, band6;
} struct_message_in;

struct GestureTemplate {
  const char *name;
  const float (*data)[AXES];
};

/* ================= GLOBALS ================= */
float live[N_SAMPLES][AXES];
int sampleCount = 0;

// uint8_t broadcastAddress[] = {0xB8, 0xF8, 0x62, 0xD5, 0xCD, 0x24};
uint8_t broadcastAddress[] = { 0x1C, 0xDB, 0xD4, 0x40, 0x63, 0x0C };


struct_message myData;
struct_message_in inBands;
esp_now_peer_info_t peerInfo;

GestureTemplate gestures[NUM_GESTURES];
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

unsigned long startOfUnpackaging, startOfLED, ledsOn, frame1, frame2;
bool ifKickOn = false;
bool ifKickOff = true;

/* ================= FORWARD DECLARATIONS ================= */
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len);


void InitWiFi();
void setupTemplates();
void CollectIMU();
void classifyGesture();

float cosineDTW(const float tmpl[N_SAMPLES][AXES]);
static inline float cosineDistance(const float a[AXES], const float b[AXES]);

/* ================= WIFI ================= */
void InitWiFi() {
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&inBands, incomingData, sizeof(inBands));
  // startOfUnpackaging = micros();

  memcpy(&inBands, incomingData, sizeof(inBands));

  // Serial.print(inBands.band1); Serial.print(", ");
  // Serial.print(inBands.band2); Serial.print(", ");
  // Serial.print(inBands.band3); Serial.print(", ");
  // Serial.print(inBands.band4); Serial.print(", ");
  // Serial.print(inBands.band5); Serial.print(", ");
  // Serial.print(inBands.band6); Serial.println("; ");

  if (inBands.band1 > 0 && ifKickOff == true) {
    ifKickOn = true;
    ifKickOff = false;
    frame2 = micros() - frame1;
  }
  frame1 = micros();

  if (inBands.band1 == 0) {
    ifKickOff = true;
  }

  startOfLED = micros();

  //band1 is the greatest: make it red
  if ((inBands.band1 > inBands.band2) && (inBands.band1 > inBands.band3)
      && (inBands.band1 > inBands.band4) && (inBands.band1 > inBands.band5)
      && (inBands.band1 > inBands.band6)) {
    pixels.fill(pixels.Color(255 * inBands.band1, 0, 0), 0, LED_COUNT);
  }
  //band2 is the greatest: make it arenge
  else if ((inBands.band2 > inBands.band1) && (inBands.band2 > inBands.band3)
           && (inBands.band2 > inBands.band4) && (inBands.band2 > inBands.band5)
           && (inBands.band2 > inBands.band6)) {
    pixels.fill(pixels.Color(255 * inBands.band2, 100 * inBands.band2, 0), 0, LED_COUNT);
  }
  //band 3 is the greatest: make it yellow
  else if ((inBands.band3 > inBands.band1) && (inBands.band3 > inBands.band2)
           && (inBands.band3 > inBands.band4) && (inBands.band3 > inBands.band5)
           && (inBands.band3 > inBands.band6)) {
    pixels.fill(pixels.Color(255 * inBands.band3, 255 * inBands.band3, 0), 0, LED_COUNT);
  }
  //band 4 is the greatest: make it green
  else if ((inBands.band4 > inBands.band1) && (inBands.band4 > inBands.band2)
           && (inBands.band4 > inBands.band3) && (inBands.band4 > inBands.band5)
           && (inBands.band4 > inBands.band6)) {
    pixels.fill(pixels.Color(0, 255 * inBands.band4, 0), 0, LED_COUNT);
  }

  //band 5 is the greatest: make it blue
  else if ((inBands.band5 > inBands.band1) && (inBands.band5 > inBands.band2)
           && (inBands.band5 > inBands.band3) && (inBands.band5 > inBands.band4)
           && (inBands.band5 > inBands.band6)) {
    pixels.fill(pixels.Color(0, 0, 255 * inBands.band5), 0, LED_COUNT);
  }

  //band 6 is the greatest: make it violet
  else if ((inBands.band6 > inBands.band1) && (inBands.band6 > inBands.band2)
           && (inBands.band6 > inBands.band3) && (inBands.band6 > inBands.band4)
           && (inBands.band6 > inBands.band5)) {
    pixels.fill(pixels.Color(255 * inBands.band6, 0, 255 * inBands.band6), 0, LED_COUNT);
  }
  pixels.setBrightness(100);
  pixels.show();

  ledsOn = micros();

  if (ifKickOn == true) {

    Serial.println((String) "UNPACKAGING: " + (startOfLED - startOfUnpackaging) + " LEDS: " + (ledsOn - startOfLED) + " FRAME: " + frame2);
    ifKickOn = false;
  }
}


/* ================= TEMPLATE SETUP ================= */
void setupTemplates() {
  for (int i = 0; i < NUM_GESTURES; i++) {
    gestures[i].name = gesture_names[i];
    gestures[i].data = gesture_data[i];
  }
}

/* ================= DTW ================= */
static inline float cosineDistance(
  const float a[AXES],
  const float b[AXES]) {
  float dot = 0, na = 0, nb = 0;
  for (int j = 0; j < AXES; j++) {
    dot += a[j] * b[j];
    na += a[j] * a[j];
    nb += b[j] * b[j];
  }
  if (na <= 0 || nb <= 0) return 1.0f;
  return 1.0f - (dot / (sqrtf(na) * sqrtf(nb)));
}

float cosineDTW(const float tmpl[N_SAMPLES][AXES]) {
  static float dtw[N_SAMPLES + 1][N_SAMPLES + 1];
  int n = sampleCount;
  int m = sampleCount;

  for (int i = 0; i <= n; i++)
    for (int j = 0; j <= m; j++)
      dtw[i][j] = DTW_INF;

  dtw[0][0] = 0;

  for (int i = 1; i <= n; i++) {
    for (int j = 1; j <= m; j++) {
      float cost = cosineDistance(live[i - 1], tmpl[j - 1]);
      float minPrev = min(dtw[i - 1][j], min(dtw[i][j - 1], dtw[i - 1][j - 1]));
      dtw[i][j] = cost + minPrev;
    }
  }
  return dtw[n][m] / (n + m);
}

/* ================= CLASSIFICATION ================= */
void classifyGesture() {
  int bestIdx = -1;
  float bestScore = SIM_THRESHOLD;

  for (int i = 0; i < NUM_GESTURES; i++) {
    float score = cosineDTW(gestures[i].data);
    Serial.print(gestures[i].name);
    Serial.print(" DTW score: ");
    Serial.println(score);

    if (score < bestScore) {
      bestScore = score;
      bestIdx = i;
    }
  }

  if (bestIdx >= 0) {
    strncpy(myData.gesture, gestures[bestIdx].name, sizeof(myData.gesture));
    // myData.sendMillis = millis();
    esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
    Serial.println(gestures[bestIdx].name);
  } else {
    Serial.println("Unknown gesture");
  }
}

/* ================= IMU ================= */
void CollectIMU() {
  static bool buttonWasHigh = false;
  static unsigned long pressStart = 0;

  int buttonState = digitalRead(BUTTON_PIN);

  if (buttonState == LOW && !buttonWasHigh) {
    buttonWasHigh = true;
    sampleCount = 0;
  }

  if (buttonState == LOW && sampleCount < N_SAMPLES) {
    float ax, ay, az, gx, gy, gz;
    while (!IMU.accelerationAvailable() || !IMU.gyroscopeAvailable()) {}
    IMU.readAcceleration(ax, ay, az);
    IMU.readGyroscope(gx, gy, gz);

    live[sampleCount][0] = ax;
    live[sampleCount][1] = ay;
    live[sampleCount][2] = az;
    live[sampleCount][3] = gx * 0.02f;
    live[sampleCount][4] = gy * 0.02f;
    live[sampleCount][5] = gz * 0.02f;

    sampleCount++;
    delay(SAMPLE_DELAY_MS);
  }

  if (buttonState == HIGH && buttonWasHigh) {
    buttonWasHigh = false;
    if (sampleCount > 0) classifyGesture();
  }
}

/* ================= SETUP / LOOP ================= */
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();

  if (!INA.begin()) {
    Serial.println("INA226 not detected");
  } else {
    Serial.println("INA226 OK");
  }

  INA.setMaxCurrentShunt(10.0, 0.005);

  pixels.begin();
  pixels.clear();
  pixels.show();

  pinMode(BUTTON_PIN, INPUT);
  pinMode(SYNC_PIN, OUTPUT);

  // Wire.begin();




  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();

  // Test strips
  stripD9.begin();
  stripD10.begin();
  // stripD11.begin();
  stripD12.begin();
  stripD13.begin();

  stripD9.setBrightness(LED_BRIGHTNESS);
  stripD10.setBrightness(LED_BRIGHTNESS);
  // stripD11.setBrightness(LED_BRIGHTNESS);
  stripD12.setBrightness(LED_BRIGHTNESS);
  stripD13.setBrightness(LED_BRIGHTNESS);

  setupTemplates();

  if (!IMU.begin()) {
    Serial.println("IMU failed");
  } else {
    Serial.println("IMU OK");
  }

  InitWiFi();
  Serial.println("ESP32 Gesture Classifier Ready");
}

void loop() {
  float batteryVoltage = INA.getBusVoltage();
  float currentA = INA.getCurrent();
  float powerW = INA.getPower();

  // float batteryVoltage = INA.getBusVoltage();
  // // Battery indicator strip
  // if (batteryVoltage <= FLASH_THRESHOLD_V) {
  //   flashRedWarning();
  // } else {
  //   showBatteryColor(batteryVoltage);
  // }

  // Keep test strips ON to draw load
  setStripColor(stripD9, 255, 255, 255);
  setStripColor(stripD10, 255, 255, 255);
  // setStripColor(stripD11, 255, 255, 255);
  setStripColor(stripD12, 255, 255, 255);
  setStripColor(stripD13, 255, 255, 255);

  CollectIMU();
}

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
#define LED_PIN_1 9
#define LED_PIN_2 10
#define LED_PIN_3 11
#define LED_PIN_4 12
#define LED_PIN_5 13
#define SYNC_PIN 12
#define N_SAMPLES 64 * 2
#define AXES 6
#define SAMPLE_DELAY_MS (1000 / N_SAMPLES)

#define DTW_INF 1e30f
// #define SIM_THRESHOLD 0.35f
#define SIM_THRESHOLD 0.4f

// ---- LEDs ----
#define LED_COUNT 40
// ---- Battery -----
#define MONITOR_LED_PIN 6
#define MONITOR_LED_COUNT 5
#define BATTERY_FULL_V 12.6
#define FLASH_THRESHOLD_V 9.2
#define FLASH_INTERVAL_MS 300

/* ================= STRUCTS ================= */
typedef struct struct_message {
  char gesture[32];
} struct_message;

typedef struct struct_message_in {
  float band1, band2, band3, band4, band5, band6;
} struct_message_in;

struct GestureTemplate {
  const char* name;
  const float (*data)[AXES];
};

/* ================= GLOBALS ================= */
float live[N_SAMPLES][AXES];
int sampleCount = 0;

uint8_t broadcastAddress[] = {0x1c, 0xdb, 0xd4, 0x40, 0x63, 0x0c};
//1c:db:d4:40:63:0c
struct_message myData;
struct_message_in inBands;
esp_now_peer_info_t peerInfo;

INA226 INA(0x40); // INA

GestureTemplate gestures[NUM_GESTURES];

Adafruit_NeoPixel strip1(LED_COUNT, LED_PIN_1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LED_COUNT, LED_PIN_2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip3(LED_COUNT, LED_PIN_3, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip4(LED_COUNT, LED_PIN_4, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip5(LED_COUNT, LED_PIN_5, NEO_GRB + NEO_KHZ800);

Adafruit_NeoPixel monitor_pixels(MONITOR_LED_COUNT, MONITOR_LED_PIN, NEO_GRB + NEO_KHZ800);

bool newBands = false;

// ---- Battery ----
unsigned long lastFlashTime = 0;
bool flashState = false;
const unsigned long flashIntervalMs = FLASH_INTERVAL_MS;
unsigned long lastCheck = 0, timeCheck = 3000;
float batteryVoltage = 12;

/* ================= FORWARD DECLARATIONS ================= */
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len);


void InitWiFi();
void setupTemplates();
void CollectIMU();
void classifyGesture();

void Scanner(); // DEBUG: Finds I2C addresses

void setAllLEDs(uint8_t, uint8_t g, uint8_t b);
void flashRedWarning();
void showBatteryColor(float batteryVoltage);
void BatteryIndicate();

float cosineDTW(const float tmpl[N_SAMPLES][AXES]);
static inline float cosineDistance(const float a[AXES], const float b[AXES]);

/* ========== BATTERY INDICATOR ============ */

void setAllLEDs(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < LED_COUNT; i++) {
    monitor_pixels.setPixelColor(i, monitor_pixels.Color(r, g, b));
  }
  monitor_pixels.show();
}

void flashRedWarning(){
  unsigned long now = millis();
  if (now - lastFlashTime >= flashIntervalMs){
    lastFlashTime = now;
    flashState = !flashState;

    if (flashState) {
      setAllLEDs(255, 0, 0);
    } else {
      setAllLEDs(0, 0, 0);
    }
  }
}

void showBatteryColor(float batteryVoltage){

if (batteryVoltage > BATTERY_FULL_V) batteryVoltage = BATTERY_FULL_V;
if (batteryVoltage < FLASH_THRESHOLD_V) batteryVoltage = FLASH_THRESHOLD_V;

float fraction =
  (batteryVoltage - FLASH_THRESHOLD_V) / (BATTERY_FULL_V - FLASH_THRESHOLD_V);

uint8_t red = (uint8_t)(255.0f * (1.0f - fraction));
uint8_t green = (uint8_t)(255.0f * fraction);

setAllLEDs(red, green, 0);
}

void BatteryIndicate(){
  if((millis() - lastCheck) > timeCheck)
  {
    batteryVoltage = INA.getBusVoltage();
    lastCheck = millis();
  }

  if(batteryVoltage <= FLASH_THRESHOLD_V){
    flashRedWarning();
    //Serial.println("Battery too low, please unplug");
  } else showBatteryColor(batteryVoltage);
}

/* ================= AUDIO LED CODE ================= */

void audioFlashLED(int r, int g, int b)
{
  strip1.fill(strip1.Color(r,g,b), 0, LED_COUNT);
  strip2.fill(strip1.Color(r,g,b), 0, LED_COUNT);
  strip3.fill(strip1.Color(r,g,b), 0, LED_COUNT);
  strip4.fill(strip1.Color(r,g,b), 0, LED_COUNT);
  strip5.fill(strip1.Color(r,g,b), 0, LED_COUNT);
}

void audioColorLED()
{
  //band1 is the greatest: make it deep blue
  if((inBands.band1 > inBands.band2) && (inBands.band1 > inBands.band3)
  && (inBands.band1 > inBands.band4) && (inBands.band1 > inBands.band5)
  && (inBands.band1 > inBands.band6))
  {
    audioFlashLED(0, 0, 255 * inBands.band1);
  }
  //band2 is the greatest: make it yellow
  else if((inBands.band2 > inBands.band1) && (inBands.band2 > inBands.band3)
  && (inBands.band2 > inBands.band4) && (inBands.band2 > inBands.band5)
  && (inBands.band2 > inBands.band6)) 
  {
    audioFlashLED(200 * inBands.band2, 78 * inBands.band2, 0);
  }
  //band 3 is the greatest: make it red
  else if((inBands.band3 > inBands.band1) && (inBands.band3 > inBands.band2)
  && (inBands.band3 > inBands.band4) && (inBands.band3 > inBands.band5)
  && (inBands.band3 > inBands.band6)) 
  {
    audioFlashLED(200 * inBands.band3, 0, 0);
  }
  //band 4 is the greatest: make it light blue
  else if((inBands.band4 > inBands.band1) && (inBands.band4 > inBands.band2)
  && (inBands.band4 > inBands.band3) && (inBands.band4 > inBands.band5)
  && (inBands.band4 > inBands.band6)) 
  {
    audioFlashLED(117 * inBands.band4, 117 * inBands.band4, 200 * inBands.band4);
  }
  
  //band 5 is the greatest: make it green
  else if((inBands.band5 > inBands.band1) && (inBands.band5 > inBands.band2)
  && (inBands.band5 > inBands.band3) && (inBands.band5 > inBands.band4)
  && (inBands.band5 > inBands.band6)) 
  {
    audioFlashLED(0, 78 * inBands.band5, 0);
  }

  //band 6 is the greatest: make it violet 
  else if((inBands.band6 > inBands.band1) && (inBands.band6 > inBands.band2)
  && (inBands.band6 > inBands.band3) && (inBands.band6 > inBands.band4)
  && (inBands.band6 > inBands.band5)) 
  {
    audioFlashLED(200 * inBands.band6, 0, 200 * inBands.band6);
  }
  strip1.show(); strip2.show(); strip3.show(); strip4.show(); strip5.show();
}

/* ================= WIFI ================= */
void InitWiFi(){
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
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
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
  
  // Serial.print(inBands.band1); Serial.print(", "); 
  // Serial.print(inBands.band2); Serial.print(", ");
  // Serial.print(inBands.band3); Serial.print(", ");
  // Serial.print(inBands.band4); Serial.print(", ");
  // Serial.print(inBands.band5); Serial.print(", "); 
  // Serial.print(inBands.band6); Serial.println("; ");

  newBands = true;
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
  const float b[AXES]
) {
  float dot = 0, na = 0, nb = 0;
  for (int j = 0; j < AXES; j++) {
    dot += a[j] * b[j];
    na  += a[j] * a[j];
    nb  += b[j] * b[j];
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
    esp_now_send(broadcastAddress, (uint8_t*)&myData, sizeof(myData));
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

  if (buttonState == HIGH && !buttonWasHigh) {
    buttonWasHigh = true;
    sampleCount = 0;
  }

  if (buttonState == HIGH && sampleCount < N_SAMPLES) {
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

  if (buttonState == LOW && buttonWasHigh) {
    buttonWasHigh = false;
    if (sampleCount > 0) classifyGesture();
  }
}

/* ================= SETUP / LOOP ================= */
void setup() {
  Serial.begin(115200);
  delay(1000); // Delay for Serial to wake up

  strip1.begin(); strip2.begin(); strip3.begin(); strip4.begin(); strip5.begin();
  strip1.clear(); strip2.clear(); strip3.clear(); strip4.clear(); strip5.clear();
  strip1.show(); strip2.show(); strip3.show(); strip4.show(); strip5.show();

  pinMode(BUTTON_PIN, INPUT);
  pinMode(SYNC_PIN, OUTPUT);

  setupTemplates();

  if (!IMU.begin()) while (1) Serial.println("IMU not connected");
  Wire.begin();
  if (!INA.begin()) while (1) Serial.println("INA funking out");

  InitWiFi();
  Serial.println("ESP32 Gesture Classifier Ready");

  xTaskCreatePinnedToCore(LED, "LED Task", 36000, NULL, 1, NULL, 1);
}

void LED(void * parameter) {
  for(;;) 
  {
    if(newBands == true)
    {
      audioColorLED();
      newBands = false;
    }
    BatteryIndicate();
    CollectIMU();

    vTaskDelay(1);
  }
}

void loop() {
}

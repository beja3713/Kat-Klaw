/* ================= INCLUDES ================= */
#include <Arduino_LSM6DSOX.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_NeoPixel.h>
#include "templates/templates_index.h"

/* ================= CONFIG ================= */
#define BUTTON_PIN 13
#define LED_PIN 11
#define SYNC_PIN 12
#define N_SAMPLES 64 * 2
#define AXES 6
#define SAMPLE_DELAY_MS (1000 / N_SAMPLES)

#define DTW_INF 1e30f
#define SIM_THRESHOLD 0.35f

#define LED_COUNT 4

/* ================= STRUCTS ================= */
typedef struct struct_message {
  char gesture[32];
  unsigned long sendMillis;
  unsigned long syncMillis;
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

uint8_t broadcastAddress[] = {0xB8, 0xF8, 0x62, 0xD5, 0xCD, 0x24};

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
  startOfUnpackaging = micros();

  memcpy(&inBands, incomingData, sizeof(inBands));
  
  // Serial.print(inBands.band1); Serial.print(", "); 
  // Serial.print(inBands.band2); Serial.print(", ");
  // Serial.print(inBands.band3); Serial.print(", ");
  // Serial.print(inBands.band4); Serial.print(", ");
  // Serial.print(inBands.band5); Serial.print(", "); 
  // Serial.print(inBands.band6); Serial.println("; ");

  if(inBands.band1 > 0 && ifKickOff == true)
  {
    ifKickOn = true;
    ifKickOff = false;
    frame2 = micros() - frame1;
  }
  frame1 = micros();

  if(inBands.band1 == 0)
  {
    ifKickOff = true;
  }

  startOfLED = micros();

  //band1 is the greatest: make it red
  if((inBands.band1 > inBands.band2) && (inBands.band1 > inBands.band3)
     && (inBands.band1 > inBands.band4) && (inBands.band1 > inBands.band5)
     && (inBands.band1 > inBands.band6)) {
      pixels.fill(pixels.Color(255 * inBands.band1, 0, 0), 0, 4);
    }
  //band2 is the greatest: make it arenge
  else if((inBands.band2 > inBands.band1) && (inBands.band2 > inBands.band3)
     && (inBands.band2 > inBands.band4) && (inBands.band2 > inBands.band5)
     && (inBands.band2 > inBands.band6)) {
      pixels.fill(pixels.Color(255 * inBands.band2, 100 * inBands.band2, 0), 0, 4);
    }
  //band 3 is the greatest: make it yellow
  else if((inBands.band3 > inBands.band1) && (inBands.band3 > inBands.band2)
     && (inBands.band3 > inBands.band4) && (inBands.band3 > inBands.band5)
     && (inBands.band3 > inBands.band6)) {
      pixels.fill(pixels.Color(255 * inBands.band3, 255 * inBands.band3, 0), 0, 4);
    }
  //band 4 is the greatest: make it green
  else if((inBands.band4 > inBands.band1) && (inBands.band4 > inBands.band2)
     && (inBands.band4 > inBands.band3) && (inBands.band4 > inBands.band5)
     && (inBands.band4 > inBands.band6)) {
      pixels.fill(pixels.Color(0, 255 * inBands.band4, 0), 0, 4);
    }
  
  //band 5 is the greatest: make it blue
  else if((inBands.band5 > inBands.band1) && (inBands.band5 > inBands.band2)
     && (inBands.band5 > inBands.band3) && (inBands.band5 > inBands.band4)
     && (inBands.band5 > inBands.band6)) {
      pixels.fill(pixels.Color(0, 0, 255 * inBands.band5), 0, 4);
    }

  //band 6 is the greatest: make it violet 
  else if((inBands.band6 > inBands.band1) && (inBands.band6 > inBands.band2)
     && (inBands.band6 > inBands.band3) && (inBands.band6 > inBands.band4)
     && (inBands.band6 > inBands.band5)) {
      pixels.fill(pixels.Color(255 * inBands.band6, 0, 255 * inBands.band6), 0, 4);
    }
  pixels.setBrightness(100);
  pixels.show();

  ledsOn = micros();

  if(ifKickOn == true)
  {

    Serial.println((String) "UNPACKAGING: " +(startOfLED - startOfUnpackaging)+ 
                            " LEDS: " +(ledsOn - startOfLED)+
                            " FRAME: " +frame2);
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
    myData.sendMillis = millis();
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
    pressStart = millis();
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
  pixels.begin();

  pinMode(BUTTON_PIN, INPUT);
  pinMode(SYNC_PIN, OUTPUT);

  setupTemplates();

  if (!IMU.begin()) while (1);

  InitWiFi();
  Serial.println("ESP32 Gesture Classifier Ready");
}

void loop() {
  CollectIMU();
}

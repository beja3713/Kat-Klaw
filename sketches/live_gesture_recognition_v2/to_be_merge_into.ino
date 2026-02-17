#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

//Pins
#define LED_PIN 22
#define BUTTON_PIN 21

bool buttonRelease = false; //only for glove standin

//Constants
#define LED_COUNT 4

Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

//Reciever mac adress
uint8_t pedalAddress[] = {0xb8, 0xf8, 0x62, 0xd5, 0xcd, 0x24}; 

//For integration testing
unsigned long startOfUnpackaging, startOfLED, ledsOn, frame1, frame2;
bool ifKickOn = false; bool ifKickOff = true;

//FFT analysis input
typedef struct struct_message_in {
  float band1, band2, band3, band4, band5, band6;
} struct_message_in;

//MIDI controll output
typedef struct struct_message_out {
  bool toggle1;
} struct_message_out;

struct_message_in inBands;
struct_message_out outMidi;

//Esp32 WiFi
esp_now_peer_info_t peerInfo;

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) 
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

//When data is had
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) 
{

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

void setup() 
{
  Serial.begin(115200);

  #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
  #endif
  
  //Setup gesture control stand in
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  //INITIALIZE NeoPixel strip object (REQUIRED)
  pixels.begin();

  //Zero midi out
  outMidi.toggle1 = false;

  //Set device as a WiFi station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
  }

  // Regester to get transmited package status
  esp_now_register_send_cb(esp_now_send_cb_t(OnDataSent));

  // Register to get recieved package status
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // Register peer
  memcpy(peerInfo.peer_addr, pedalAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  
  // Add peer    
  while(esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
  }    
}

void loop() 
{
  if(digitalRead(BUTTON_PIN) == LOW)
  {
    buttonRelease = true;
  }

  if(digitalRead(BUTTON_PIN) == HIGH && buttonRelease == true)
  {
    outMidi.toggle1 = !outMidi.toggle1;

    esp_err_t result = esp_now_send(pedalAddress, (uint8_t *) &outMidi, sizeof(outMidi));
    if (result != ESP_OK) Serial.println("Error sending the data");
    buttonRelease = false;
  }
}
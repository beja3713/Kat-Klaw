#include <arduinoFFT.h>
#include <queue.h>
#include <WiFi.h>
#include <esp_now.h>
#include "esp_adc/adc_continuous.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

#include <Preferences.h>

Preferences prefs;

//Core differentiaiton
TaskHandle_t Task1;
TaskHandle_t Task2;

Adafruit_USBD_MIDI usb_midi;

// Create a new instance of the Arduino MIDI Library,
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

//Pins
#define AUDIO_PIN 13
#define WIFI_LED 12
#define PEAK_LED 10

//Thresholds
#define PEAK 4094

//FFT parameters
#define SAMPLES         1024
#define SAMPLING_FREQ   40000  
#define NOISE           10 
#define BAND_COUNT       6

//Peak deteciton variables
int peakBrightness;

//FFT variables
int16_t adcBuffer[SAMPLES];
size_t byteCount;

unsigned long newTime;
float vReal[SAMPLES];
float vImag[SAMPLES];
float peakValues[] = {1,1,1,1,1,1};
float bandValues[] = {0,0,0,0,0,0};
float smoothValues[] = {0,0,0,0,0,0};

//For Integraiton testing
unsigned long startO

unsigned long startOfBuffer, startOfFFT, startOfPackaging, ledDataSent;
bool ifKick = false;
bool send = false;

ArduinoFFT<float> FFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

//Reciever mac adress
uint8_t gloveAddress[] = {0x98, 0xa3, 0x16, 0x85, 0x18, 0xb0}; //XIAO glove standin

QueueHandle_t gestureQueue;

uint8_t nextAvailableNote;
bool noteActive[128] = {false};

struct GestureMap {
  String name;
  uint8_t note;
};


//FFT analysis output 
typedef struct struct_message_out {
  float band1, band2, band3, band4, band5, band6;
} struct_message_out;

typedef struct struct_message_in {
  char gesture[32];
  // unsigned long sendMillis;
  // unsigned long syncMillis;
} struct_message_in;

struct_message_out outBands;
struct_message_in inMidi;

// Lookup Function

uint8_t getNoteForGesture(const char* gesture)
{
    uint8_t storedNote = prefs.getUChar(gesture, 255);

    if (storedNote != 255)
        return storedNote;

    if (nextAvailableNote > 127)
        nextAvailableNote = 60;   // prevent overflow

    storedNote = nextAvailableNote++;
    prefs.putUChar(gesture, storedNote);
    prefs.putUChar("nextNote", nextAvailableNote);

    return storedNote;
}

/*-------------------------------------------------*/

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) 
{
  if(status == ESP_NOW_SEND_SUCCESS)
  {
    //Serial.println("Delivery Success");
    analogWrite(WIFI_LED, 255);

    ledDataSent = micros();

  if(ifKick == true)
    {
      Serial.println((String) "BUFFER: " +(startOfFFT - startOfBuffer)+ 
                          " FFT: " +(startOfPackaging - startOfFFT)+
                          " PACKAGING: " +(ledDataSent - startOfPackaging)+
                          " WIFI: " +(ledDataSent - startOfPackaging));
      ifKick = false;
    }
  }
  else
  {
    //Serial.println("Delivery Fail");
    analogWrite(WIFI_LED, 0); 
  }
}

//When data is had
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) 
{
    struct_message_in temp;
    memcpy(&temp, incomingData, sizeof(temp));

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gestureQueue, &temp, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

//Setup funciton for I2S based buffer
adc_continuous_handle_t adc_cont_handle = NULL;

void sendGestureNote(int note)
{
  MIDI.sendNoteOn(note, 127, 1);
  delay(5);
  MIDI.sendNoteOff(note, 0, 1);
}

void MIDIcode(void* pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_now_peer_info_t peerInfo = {};
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    memcpy(peerInfo.peer_addr, gloveAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;

    while (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        delay(1000);
    }

    analogWrite(WIFI_LED, 255);

    struct_message_in receivedGesture;

    while (true)
    {
        if (xQueueReceive(gestureQueue, &receivedGesture, portMAX_DELAY) == pdTRUE)
        {
            uint8_t note = getNoteForGesture(receivedGesture.gesture);

            // If not already active, send NoteOn
            if (!noteActive[note])
            {
                MIDI.sendNoteOn(note, 127, 1);
                noteActive[note] = true;
            }

            // Optional: auto note off after short time
            vTaskDelay(pdMS_TO_TICKS(20));

            MIDI.sendNoteOff(note, 0, 1);
            noteActive[note] = false;
        }
    }
}

void setupADC()
{
  esp_err_t err;

  //Handle config
  adc_continuous_handle_cfg_t adc_config = 
  {
    .max_store_buf_size = SAMPLES * 4,
    .conv_frame_size = SAMPLES * 4,
  };

  err = adc_continuous_new_handle(&adc_config, &adc_cont_handle);
  if (err != ESP_OK) {
    Serial.println("new_handle failed");
    return;
  }

  //Digital controller config
  adc_continuous_config_t dig_cfg = {};
  dig_cfg.sample_freq_hz = SAMPLING_FREQ;
  dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;

  //Pattern must be static!
  static adc_digi_pattern_config_t pattern;

  pattern.atten = ADC_ATTEN_DB_11;
  pattern.channel = ADC_CHANNEL_4;   // GPIO13
  pattern.unit = ADC_UNIT_1;
  pattern.bit_width = ADC_BITWIDTH_12;

  dig_cfg.pattern_num = 1;
  dig_cfg.adc_pattern = &pattern;

  err = adc_continuous_config(adc_cont_handle, &dig_cfg);
  if (err != ESP_OK) {
    Serial.println("config failed");
    return;
  }

  err = adc_continuous_start(adc_cont_handle);
  if (err != ESP_OK) {
    Serial.println("start failed");
    return;
  }
}

void DSPcode(void * pvParameters)
{   
  uint8_t result[SAMPLES * 4];   // buffer for raw ADC data, 4 bytes per sample
  uint32_t ret_num = 0;          // number of bytes returned

  while(true)
  {
    if(ifKick == false) startOfBuffer = micros();

    for(int i = 0; i < BAND_COUNT; i++)
    {
      bandValues[i] = 0;
    }

    // Read samples
    esp_err_t r = adc_continuous_read(adc_cont_handle, result, sizeof(result), &ret_num, portMAX_DELAY);
    if (r != ESP_OK) continue;

    int sampleCount = ret_num / sizeof(adc_digi_output_data_t);

    for(int i = 0; i < sampleCount; i++)
    {
      // TYPE1 format: 12-bit ADC result is in lower 12 bits of first two bytes
      uint16_t raw = ((uint16_t)result[i*4] | ((uint16_t)result[i*4 + 1] << 8)) & 0x0FFF;

      if(raw > PEAK) peakBrightness = 255.0;
      vReal[i] = raw - 2048;  // center for FFT
      vImag[i] = 0;
    }

    analogWrite(PEAK_LED, peakBrightness);
    if(peakBrightness > 0) peakBrightness -=10;

    startOfFFT = micros();

    FFT.dcRemoval();
    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

    for (int i = 2; i < (SAMPLES/2); i++)
    {
      vReal[i] /= (SAMPLES / 2);
      if (vReal[i] > NOISE)
      {                   
        if (i <= 2)             bandValues[0] += (int)vReal[i];
        else if (i <= 6)        bandValues[1] += (int)vReal[i];
        else if (i <= 12)       bandValues[2] += (int)vReal[i];
        else if (i <= 40)       bandValues[3] += (int)vReal[i];
        else if (i <= 100)      bandValues[4] += (int)vReal[i];
        else                    bandValues[5] += (int)vReal[i];
      }
    }

    if(smoothValues[0] == 0 && bandValues[0] > 0)
    {
      ifKick = true;
    }

    for(int i = 0; i < BAND_COUNT; i++)
    {
      if(bandValues[i] > peakValues[i])
      {
        peakValues[i] = bandValues[i];
      }

      bandValues[i] = bandValues[i]/peakValues[i];
      smoothValues[i] = 0.3 * smoothValues[i] + 0.7 * bandValues[i];
    }

    startOfPackaging = micros();

    // Serial.print(smoothValues[0]); Serial.print(", "); 
    // Serial.print(smoothValues[1]); Serial.print(", ");
    // Serial.print(smoothValues[2]); Serial.print(", ");
    // Serial.print(smoothValues[3]); Serial.print(", ");
    // Serial.print(smoothValues[4]); Serial.print(", "); 
    // Serial.print(smoothValues[5]); Serial.println("; ");

    outBands.band1 = smoothValues[0];
    outBands.band2 = smoothValues[1];
    outBands.band3 = smoothValues[2];
    outBands.band4 = smoothValues[3];
    outBands.band5 = smoothValues[4];
    outBands.band6 = smoothValues[5];

    send = true;

    vTaskDelay(pdMS_TO_TICKS(1)); //Yeild CPU
  }
}

/*-------------------------------------------------*/

void setup() {
  Serial.begin(115200);

  //Fill delay with toggle


  //Pins
  pinMode(AUDIO_PIN, INPUT);
  pinMode(PEAK_LED, OUTPUT);

  //Start MIDI
  MIDI.begin();

  //Set device as a WiFi station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  setupADC();
  prefs.begin("gestureMap", false);
  nextAvailableNote = prefs.getUChar("nextNote", 60);

  gestureQueue = xQueueCreate(10, sizeof(struct_message_in));

  //Core designation
  xTaskCreatePinnedToCore(MIDIcode, "Task2", 20000, NULL, 1, &Task2, 0); //Pin task to core 0
  delay(100); 

  xTaskCreatePinnedToCore(DSPcode, "Task1", 20000, NULL, 1, &Task1, 1); //pin task to core 1                  
  delay(500); 
}

/*-------------------------------------------------*/

void loop() {
}

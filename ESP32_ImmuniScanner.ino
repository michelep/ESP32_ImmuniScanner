/*
 * BLE Sniffer
 * 
 * Burn using TTGO-LoRa32-OLED V1 profile
 */

#define __DEBUG__

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>

#include "logo.h"

#include "time.h"
// Firmware data
const char BUILD[] = __DATE__ " " __TIME__;
#define FW_NAME         "immuniscanner"
#define FW_VERSION      "0.0.1"


// ************************************
// DEBUG_PRINT() and DEBUG_PRINTLN()
//
// send message via RSyslog (if enabled) or fallback on Serial 
// ************************************
void DEBUG_PRINT(String message) {
#ifdef __DEBUG__
  Serial.print(message);
#endif
}

void DEBUG_PRINTLN(String message) {
#ifdef __DEBUG__
  Serial.println(message);
#endif
}

// OLED LCD display
#define I2C_SCL 4
#define I2C_SDA 5
#include <Wire.h>  
#include "SSD1306.h"

SSD1306 display(0x3c, I2C_SDA, I2C_SCL);

//
// BLE
//

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAddress.h>

int scanTime = 30; //In seconds

uint8_t immuniFound=0;

bool bleIsScanning=false;

typedef struct {
  char address[18];
  int rssi;
} _BTDevice;

_BTDevice btDevice[20];
uint8_t btIdx=0;

bool addDevice(char *address, int rssi) {
  Serial.print("[!] ");
  Serial.println(address);
  for(uint8_t i;i<btIdx;i++) {
    Serial.print("[=] ");
    Serial.println(btDevice[i].address);
    if(strcmp(btDevice[i].address,address)==0) {
      return false;
    }
  }
  strncpy(btDevice[btIdx].address,address,18);
  btDevice[btIdx].rssi=rssi;
  btIdx++;
  if(btIdx>20) {
    btIdx=0;
  }
  immuniFound++;
  return true;
}

void printDevices() {

}

// https://blog.google/documents/58/Contact_Tracing_-_Bluetooth_Specification_v1.1_RYGZbKW.pdf
const char* uuid = "0000fd6f-0000-1000-8000-00805f9b34fb";

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
      void onResult(BLEAdvertisedDevice advertisedDevice) {
        if(advertisedDevice.haveServiceUUID()){
            if(strncmp(advertisedDevice.getServiceUUID().toString().c_str(),uuid, 36) == 0){
                int rssi = advertisedDevice.getRSSI();
                char address[18];             
                DEBUG_PRINTLN(F("---------------------------"));
                DEBUG_PRINT(F("RSSI: "));
                DEBUG_PRINTLN(String(rssi));
                DEBUG_PRINT(F("ADDR: "));
                strncpy(address,advertisedDevice.getAddress().toString().c_str(),18);
                DEBUG_PRINTLN(String(address));
                // Immuni found. Check and add if is a new device.
                addDevice(address,rssi);
            }
        }
    }
};

//
// high priority bleTask on CORE0
//
void bleTask(void *pvParameters) {
  BLEDevice::init("");
  DEBUG_PRINTLN(F("BLEDevice::init() DONE !"));
  
  while(1) {
    if(BLEDevice::getInitialized()) {
      /* SCAN... */
      bleIsScanning=true;
      DEBUG_PRINTLN("Start scanning ("+String(scanTime)+"secs)...");
      BLEScan* pBLEScan = BLEDevice::getScan(); //create new scan
      pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
      pBLEScan->setActiveScan(true);
      BLEScanResults foundDevices = pBLEScan->start(scanTime);
      bleIsScanning=false;
    } else {
      delay(5000);
      BLEDevice::init("");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println();
  Serial.println();
  Serial.print(FW_NAME);
  Serial.print(" ");
  Serial.print(FW_VERSION);
  Serial.print(" ");
  Serial.println(BUILD);
  delay(1000);

  display.init();

  display.drawXbm(0, 0, logo_width, logo_height, logo_bits);

  // write the buffer to the display
  display.display();

  // Wait 2 secs
  delay(2000);

  // Launch bleTask on CORE0 with high priority
  xTaskCreatePinnedToCore(
                  bleTask,   /* Function to implement the task */
                  "bleTask", /* Name of the task */
                  10000,      /* Stack size in words */
                  NULL,       /* Task input parameter */
                  15,          /* Priority of the task */
                  NULL,       /* Task handle. */
                  0);  /* Core where the task should run */
  display.resetDisplay();                
}

unsigned long last;
uint8_t progress=0, found=0;

void loop() {
  if((millis() - last) > 1100) {  
    display.clear();  
    display.setFont(ArialMT_Plain_10);
    display.drawHorizontalLine(0, 0, DISPLAY_WIDTH);
    display.drawProgressBar(10, DISPLAY_HEIGHT-12, DISPLAY_WIDTH-20, 10, progress);

    if(bleIsScanning) {
      progress+=10;
      if(progress > 100) { progress=0; }
    }
    display.drawString(26,25,"Found       Immuni");

    display.setFont(ArialMT_Plain_24);
    display.drawString(61,17, String(immuniFound));
    
    display.display();
    last = millis();    
  }
  delay(10);
}

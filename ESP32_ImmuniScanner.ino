/*
 * ** Immuni Scanner **
 * 
 * Far to be perfect, just listen for Immuni beacons...
 * 
 * I have been used a TTGO ESP32 board with 18650 LiPo battery slot and OLED 0,96" display - https://it.aliexpress.com/item/1000007779002.html
 * 
 * Burn using TTGO-LoRa32-OLED V1 profile - 
 *
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
#define FW_VERSION      "0.0.2"

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

//
#define TOUCH_UP 33
#define TOUCH_UP_TRIGGER 25
#define TOUCH_DOWN 27
#define TOUCH_DOWN_TRIGGER 50

// *******************************
// OLED LCD display
//
// https://github.com/LilyGO/ESP32-OLED0.96-ssd1306
// *******************************
#define I2C_SCL 4
#define I2C_SDA 5
#include <Wire.h>  
#include "SSD1306.h"

SSD1306 display(0x3c, I2C_SDA, I2C_SCL);

// *****************************
// BLE
//
// https://github.com/nkolban/esp32-snippets/tree/master/BLE/scanner
// *****************************
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAddress.h>

int scanTime = 30; //In seconds

uint8_t immuniFound=0;

bool bleIsScanning=false;

// ****************************
// LinkedList
//
// https://github.com/ivanseidel/LinkedList
// ****************************
#include <LinkedList.h>

#define MAX_AGE 2 // How many search cycles wait before discard a device?

class BTDevice {
  public:
    char address[18];
    int rssi;
    uint8_t age;
};

LinkedList<BTDevice *> devicesList = LinkedList<BTDevice *>();

bool addDevice(char *address, int rssi) {
  for(uint8_t i;i<devicesList.size();i++) {
    BTDevice *tmpDev = devicesList.get(i);
    if(strncmp(tmpDev->address,address,18)==0) {
      // Device is already present, but update rssi and age
      tmpDev->rssi=rssi;
      tmpDev->age=MAX_AGE;
      DEBUG_PRINTLN(F("[!] UPDATED"));
      return false;
    }
  }
  BTDevice *tmpDev = new BTDevice();
  strncpy(tmpDev->address,address,18);
  tmpDev->rssi = rssi;
  tmpDev->age = MAX_AGE;
  devicesList.add(tmpDev);
  DEBUG_PRINTLN(F("[+] ADDED!"));
  immuniFound++;
  return true;
}

void cycleDevices() {
  for(uint8_t i;i<devicesList.size();i++) {
    BTDevice *tmpDev = devicesList.get(i);
    tmpDev->age--;
    if(1 > tmpDev->age) {
      // Device is out of age: discard
      devicesList.remove(i);
      delete(tmpDev);
      DEBUG_PRINTLN(F("[#] DELETED"));
    }
  }
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
      cycleDevices();
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
uint8_t progress=0, display_page=0;

void loop() {
  int ts_val;
  uint8_t dy=4;
  char buff[16];
  
  if((millis() - last) > 1100) {  
    display.clear();  
    display.setFont(ArialMT_Plain_10);
    display.drawHorizontalLine(0, 1, (DISPLAY_WIDTH/3)*(display_page+1));
    display.drawVerticalLine((DISPLAY_WIDTH/3)*(display_page+1), 0, 3);

    // Display page contents
    switch(display_page) {
      case 0: // Main page
        // Just show how many "Immuni" found
        display.drawString(26,25,"Found       Immuni");

        display.setFont(ArialMT_Plain_24);
        display.drawString(61,17, String(immuniFound));
        break;
      case 1:
        // Last devices found
        for(uint8_t i;i<devicesList.size();i++) {
          BTDevice *tmpDev = devicesList.get(i);
          display.drawString(1,dy,tmpDev->address);
          dy+=11;
        }
        break;
      case 2:
        // System info
        sprintf(buff,"FreeHEAP: %d",ESP.getFreeHeap());
        display.drawString(1,4,buff);
        sprintf(buff,"Devices: %d",devicesList.size());
        display.drawString(1,15,buff);
        sprintf(buff,"Uptime: %d",millis()/1000);
        display.drawString(1,26,buff);
        break;
    }
    
    display.drawProgressBar(10, DISPLAY_HEIGHT-12, DISPLAY_WIDTH-20, 10, progress);

    if(bleIsScanning) {
      progress+=10;
      if(progress > 100) { progress=0; }
    }
    
    display.display();    

    // <--- Touch button 
    ts_val = touchRead(TOUCH_UP);
    if(ts_val < TOUCH_UP_TRIGGER) {
      display_page++;
      if(display_page > 2) display_page=0;
    }
    ts_val = touchRead(TOUCH_DOWN);
    if(ts_val < TOUCH_DOWN_TRIGGER) {
      if(display_page > 0) {
        display_page--;
      } else {
        display_page=2;
      }
    }

    // --->
    last = millis();    
  }
  delay(10);
}

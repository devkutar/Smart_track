#pragma once
#include <Arduino.h>
#include "ConfigManager.h"

bool LCD_begin();
void LCD_showTemp(float tempC, const char* reason);  // reason: "BOOT" / "MOTION" vs.
void LCD_showStatus(float tempC, const PowerStatus& power, const Cfg& cfg, 
                    int wifiRssi, bool is4G, uint8_t alarmState, const char* macAddr,
                    uint32_t bootCount = 0, float latitude = 0.0, float longitude = 0.0,
                    float gpsSpeed = 0.0, float gpsCourse = 0.0,
                    int gpsSats = 0, float gpsHdop = 99.0, bool gpsFix = false,
                    int bleCount = 0, bool mqttConnected = false, const char* version = "1.0.0");
void LCD_sleep();

// BLE Sensör gösterimi
void LCD_showBLESensor(const char* sensorName, const char* sensorMac, float temp, 
                       int battPct, int rssi, const PowerStatus& power, 
                       int wifiRssi, bool is4G, const char* gatewayMac,
                       float tempHigh, float tempLow, uint8_t alarmState);

// OTA Update ekranları
void LCD_showUpdateProgress(int currentBytes, int totalBytes, const char* status = nullptr);
void LCD_showUpdateSuccess();
void LCD_showUpdateFailed(const char* reason);
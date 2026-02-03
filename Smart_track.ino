/*
 * Smart_track - Smart Tracking Device
 * 
 * Description: GPS tracking + BLE Eddystone tag scanner
 * - 4G only (no WiFi)
 * - No sleep mode (always powered)
 * - GPS coordinates transmission
 * - BLE Eddystone tag scanning (prefix: 8C696B)
 * - Data transmission every 2 minutes
 * - OTA firmware update over 4G
 * - FIFO-based offline data storage
 * 
 * Version: 1.0.0
 * Date: 2025-01-15
 */

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <ArduinoJson.h>
#include <string.h>
#include <sys/time.h>

// Proje Modülleri
#include "hardware.h"
#include "ExternalSensor.h"  // Harici sensör (T117 veya AHT20)
#include "LCD_ST7789.h"
#include "ConfigManager.h"
#include "NetworkManager.h"
#include "C16QS4GManager.h"
#include "MQTTManager.h"
#include "PowerManager.h"
#include "BuzzerManager.h"
#include "AlarmManager.h"
#include "OTAUpdate.h"
#include "GPSManager.h"  // GPS modülü (oluşturulacak)
#include "BLEManager.h"  // BLE Eddystone tarayıcı (oluşturulacak)

// ESP32 Sistem Kütüphaneleri
#include "esp_mac.h"

// ===== CONFIGURATION =====
static const char* FW_VERSION = "1.0.0";
static const uint32_t DATA_TX_INTERVAL_MS = 120000; // 2 dakika

// ===== GLOBAL NESNELER =====
ExternalSensor extSensor;  // Harici sensör (T117 veya AHT20 - otomatik algılama)
ConfigManager configMgr;
SmartTrackNetworkManager netMgr;  // 4G only
MQTTManager mqttMgr;
PowerManager powerMgr;
BuzzerManager buzzerMgr;
AlarmManager alarmMgr;
GPSManager gpsMgr;  // GPS modülü
BLEManager bleMgr;  // BLE Eddystone tarayıcı

Cfg cfg;
PowerStatus gPower;
String macAddr;

// Timing
unsigned long lastDataTxTime = 0;
unsigned long lastBLEDisplayChange = 0;
uint8_t currentBLEDisplayIndex = 0;

// 2 dakikalık periyot yönetimi
unsigned long cycleStartTime = 0;
bool cycleActive = false;
bool cycleDataReady = false;

// ===== MQTT CALLBACK =====
void mqttCallback(char* topic, byte* payload, unsigned int len) {
    Serial.println("\n========== MQTT MESSAGE RECEIVED ==========");
    Serial.printf("[MQTT] Topic: %s, Length: %d\n", topic, len);
    
    // Payload'ı yazdır (debug için)
    if (len > 0) {
        char payloadStr[len + 1];
        memcpy(payloadStr, payload, len);
        payloadStr[len] = '\0';
        Serial.printf("[MQTT] Payload: %s\n", payloadStr);
    }
    Serial.println("===========================================\n");

    String tpc(topic);
    bool isCfgTopic = (tpc == mqttMgr.getConfigTopic(macAddr.c_str()));
    bool isUpdateTopic = (tpc == mqttMgr.getUpdateTopic());

    if (!isCfgTopic && !isUpdateTopic) {
        Serial.printf("[MQTT] Topic ignored. Expected: %s or %s\n",
                     mqttMgr.getConfigTopic(macAddr.c_str()).c_str(),
                     mqttMgr.getUpdateTopic().c_str());
        return;
    }

    char payloadStr[len + 1];
    memcpy(payloadStr, payload, len);
    payloadStr[len] = '\0';
    Serial.printf("[CFG] Received JSON (original, %d bytes): %s\n", len, payloadStr);

    // JSON'u minify et (gereksiz boşlukları kaldır)
    String minifiedJson = "";
    bool inString = false;
    bool escapeNext = false;
    for (unsigned int i = 0; i < len; i++) {
        char c = payloadStr[i];
        
        if (escapeNext) {
            minifiedJson += c;
            escapeNext = false;
            continue;
        }
        
        if (c == '\\') {
            escapeNext = true;
            minifiedJson += c;
            continue;
        }
        
        if (c == '"') {
            inString = !inString;
            minifiedJson += c;
            continue;
        }
        
        if (inString) {
            // String içindeyse tüm karakterleri koru
            minifiedJson += c;
        } else {
            // String dışındayken gereksiz boşlukları kaldır
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                minifiedJson += c;
            }
        }
    }
    
    Serial.printf("[CFG] Minified JSON (%d bytes): %s\n", minifiedJson.length(), minifiedJson.c_str());

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, minifiedJson.c_str(), minifiedJson.length());
    if (err) {
        Serial.printf("[CFG] JSON parse error: %s\n", err.c_str());
        return;
    }

    const char* cmd = doc["cmd"] | "";
    Serial.printf("[CFG] Command: %s\n", cmd);

    // UPDATE command (OTA)
    if (!strcmp(cmd, "update")) {
        Serial.println("[OTA] UPDATE command received");
        const char* url = doc["url"] | cfg.otaUrl;
        // OTA update logic here
        // ...
        return;
    }

    // Config topic only from here
    if (!isCfgTopic) {
        return;
    }

    // RESET
    if (!strcmp(cmd, "reset")) {
        ESP.restart();
        return;
    }

    // FACTORY DEFAULT
    if (!strcmp(cmd, "factoryDefault")) {
        configMgr.resetToFactory(cfg);
        ESP.restart();
        return;
    }

    // BUZZER SILENCE
    if (!strcmp(cmd, "silence")) {
        buzzerMgr.alarmStop();
        return;
    }

    // SET command
    if (!strcmp(cmd, "set")) {
        Serial.println("[CFG] SET command received");
        
        // MQTT Broker ayarları
        if (doc.containsKey("mqttHost")) {
            strlcpy(cfg.mqttHost, doc["mqttHost"], sizeof(cfg.mqttHost));
            Serial.printf("[CFG] MQTT Host: %s\n", cfg.mqttHost);
        }
        if (doc.containsKey("mqttPort")) {
            cfg.mqttPort = doc["mqttPort"];
            Serial.printf("[CFG] MQTT Port: %d\n", cfg.mqttPort);
        }
        if (doc.containsKey("mqttUser")) {
            strlcpy(cfg.mqttUser, doc["mqttUser"], sizeof(cfg.mqttUser));
            Serial.printf("[CFG] MQTT User: %s\n", cfg.mqttUser);
        }
        if (doc.containsKey("mqttPass")) {
            strlcpy(cfg.mqttPass, doc["mqttPass"], sizeof(cfg.mqttPass));
            Serial.println("[CFG] MQTT Pass: ****");
        }
        
        // WiFi ayarları
        if (doc.containsKey("wifiSsid")) {
            strlcpy(cfg.wifiSsid, doc["wifiSsid"], sizeof(cfg.wifiSsid));
            Serial.printf("[CFG] WiFi SSID: %s\n", cfg.wifiSsid);
        }
        if (doc.containsKey("wifiPass")) {
            strlcpy(cfg.wifiPass, doc["wifiPass"], sizeof(cfg.wifiPass));
            Serial.println("[CFG] WiFi Pass: ****");
        }
        
        // Zaman ayarları
        if (doc.containsKey("dataPeriod")) {
            cfg.dataPeriod = doc["dataPeriod"];
            Serial.printf("[CFG] Data Period: %lu ms\n", cfg.dataPeriod);
        }
        if (doc.containsKey("infoPeriod")) {
            cfg.infoPeriod = doc["infoPeriod"];
            Serial.printf("[CFG] Info Period: %lu ms\n", cfg.infoPeriod);
        }
        
        // Alarm eşikleri
        if (doc.containsKey("tempHigh")) {
            cfg.tempHigh = doc["tempHigh"];
            Serial.printf("[CFG] Temp High: %.1f\n", cfg.tempHigh);
        }
        if (doc.containsKey("tempLow")) {
            cfg.tempLow = doc["tempLow"];
            Serial.printf("[CFG] Temp Low: %.1f\n", cfg.tempLow);
        }
        
        // Buzzer ayarları
        if (doc.containsKey("buzzerEnabled")) {
            cfg.buzzerEnabled = doc["buzzerEnabled"];
            Serial.printf("[CFG] Buzzer: %s\n", cfg.buzzerEnabled ? "ON" : "OFF");
        }
        
        // Dahili sensör bilgileri
        if (doc.containsKey("sensorName")) {
            strlcpy(cfg.internalSensorName, doc["sensorName"], sizeof(cfg.internalSensorName));
            Serial.printf("[CFG] Sensor Name: %s\n", cfg.internalSensorName);
        }
        if (doc.containsKey("mahalId")) {
            strlcpy(cfg.internalMahalId, doc["mahalId"], sizeof(cfg.internalMahalId));
            Serial.printf("[CFG] Mahal ID: %s\n", cfg.internalMahalId);
        }
        
        configMgr.saveConfiguration(cfg);
        Serial.println("[CFG] Configuration saved. Restart recommended for MQTT changes.");
        return;
    }

    // SET EDDYSTONE CONFIGS command
    if (!strcmp(cmd, "setEddystoneConfigs")) {
        Serial.println("[CFG] setEddystoneConfigs command received");
        
        if (!doc.containsKey("eddystoneSensors")) {
            Serial.println("[CFG] 'eddystoneSensors' array not found in payload.");
            return;
        }
        
        JsonArray sensorsArray = doc["eddystoneSensors"];
        int count = sensorsArray.size();
        if (count > 32) {
            count = 32; // Max limit
        }
        
        cfg.activeSensorCount = 0;
        for (int i = 0; i < count; i++) {
            JsonObject sensor = sensorsArray[i];
            
            if (sensor.containsKey("macAddress")) {
                strlcpy(cfg.eddystoneSensors[cfg.activeSensorCount].macAddress, 
                       sensor["macAddress"], 
                       sizeof(cfg.eddystoneSensors[cfg.activeSensorCount].macAddress));
            }
            
            if (sensor.containsKey("mahalId")) {
                strlcpy(cfg.eddystoneSensors[cfg.activeSensorCount].mahalId, 
                       sensor["mahalId"], 
                       sizeof(cfg.eddystoneSensors[cfg.activeSensorCount].mahalId));
            }
            
            if (sensor.containsKey("sensorName")) {
                strlcpy(cfg.eddystoneSensors[cfg.activeSensorCount].sensorName, 
                       sensor["sensorName"], 
                       sizeof(cfg.eddystoneSensors[cfg.activeSensorCount].sensorName));
            }
            
            if (sensor.containsKey("tempHigh")) {
                cfg.eddystoneSensors[cfg.activeSensorCount].tempHigh = sensor["tempHigh"];
            }
            
            if (sensor.containsKey("tempLow")) {
                cfg.eddystoneSensors[cfg.activeSensorCount].tempLow = sensor["tempLow"];
            }
            
            if (sensor.containsKey("buzzerEnabled")) {
                cfg.eddystoneSensors[cfg.activeSensorCount].buzzerEnabled = sensor["buzzerEnabled"];
            }
            
            if (sensor.containsKey("enabled")) {
                cfg.eddystoneSensors[cfg.activeSensorCount].enabled = sensor["enabled"];
            }
            
            cfg.activeSensorCount++;
        }
        
        configMgr.saveConfiguration(cfg);
        
        // BLE Manager'a yeni config'leri yükle
        bleMgr.loadConfigFromCfg(cfg);
        
        Serial.printf("[CFG] Loaded %d Eddystone sensor configurations\n", cfg.activeSensorCount);
        
        // Config değişti, cihazı yeniden başlat
        Serial.println("[CFG] Configuration saved. Restarting in 3 seconds...");
        delay(3000);
        ESP.restart();
        return;
    }

    Serial.println("[CFG] Unknown command");
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(100);

    // Türkiye saat dilimi ayarı (UTC+3)
    setenv("TZ", "TUR-3", 1);
    tzset();
    Serial.println("[TIME] Timezone set to TUR-3 (UTC+3)");

    // 1) Konfigürasyon
    configMgr.begin();
    configMgr.loadConfiguration(cfg);
    
    // Config'i serial porttan yazdır
    Serial.println("\n========== CURRENT CONFIG ==========");
    Serial.printf("Internal Sensor Name: %s\n", cfg.internalSensorName);
    Serial.printf("Internal Mahal ID: %s\n", cfg.internalMahalId);
    Serial.printf("Active Eddystone Sensors: %d\n", cfg.activeSensorCount);
    for (int i = 0; i < cfg.activeSensorCount && i < 4; i++) {
        Serial.printf("  Sensor %d: MAC=%s, Name=%s, Mahal=%s, Enabled=%d\n", 
                     i+1, 
                     cfg.eddystoneSensors[i].macAddress,
                     cfg.eddystoneSensors[i].sensorName,
                     cfg.eddystoneSensors[i].mahalId,
                     cfg.eddystoneSensors[i].enabled);
    }
    Serial.println("====================================\n");

    // MAC adresini al
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char hexStr[3];
        macAddr = "";
        for (int i = 0; i < 6; i++) {
            snprintf(hexStr, sizeof(hexStr), "%02X", mac[i]);
            macAddr += hexStr;
        }
    } else {
        macAddr = "000000000000";
    }

    Serial.println("\n========================================");
    Serial.println("Smart_track - Starting...");
    Serial.printf("Firmware Version: %s\n", FW_VERSION);
    Serial.printf("MAC Address: %s\n", macAddr.c_str());
    Serial.println("========================================\n");

    // 2) Donanım Başlatma
    HW_beginI2C();
    LCD_begin();
    powerMgr.begin();
    buzzerMgr.begin();
    powerMgr.update();
    gPower = powerMgr.getStatus();

    // ===== 3) SENSÖR OKUMA VE LCD GÖSTERME =====
    Serial.println("\n[STEP 3] Reading external sensor...");
    bool sensorOK = false;
    float tempC = -99.0;
    float humidity = -1.0;
    
    if (extSensor.begin()) {
        Serial.printf("[SENSOR] External sensor type: %s\n", extSensor.getTypeName());
        if (extSensor.readTemperature(tempC)) {
            if (tempC > -55.0f && tempC < 125.0f) {
                sensorOK = true;
                Serial.printf("[SENSOR] Temp: %.2f C (Sensor OK)\n", tempC);
                
                // AHT20 ise nem de oku
                if (extSensor.getType() == SENSOR_AHT20) {
                    if (extSensor.readHumidity(humidity)) {
                        Serial.printf("[SENSOR] Humidity: %.1f %%\n", humidity);
                    }
                }
            }
        }
    } else {
        Serial.println("[SENSOR] No external sensor detected");
    }
    
    // LCD'de sensör bilgisini göster
    LCD_showStatus(tempC, gPower, cfg, 0, true, 0, macAddr.c_str(), 0, 0.0, 0.0, 0.0, 0.0, 0, 99.0, false,
                   0, false, FW_VERSION);
    Serial.println("[LCD] Sensor data displayed");

    // ===== 4) 4G NETWORK BAŞLATMA =====
    Serial.println("\n[STEP 4] Starting 4G network (GPS disabled)...");
    netMgr.begin();
    
    // ===== 5) NETWORK BAĞLANTISI =====
    Serial.println("\n[STEP 5] Connecting to 4G network...");
    bool networkConnected = false;
    if (netMgr.connect(cfg)) {
        Serial.println("[NET] 4G network connected!");
        networkConnected = true;
        
        // ===== 6) TIME SYNC =====
        Serial.println("\n[STEP 6] Syncing time from GSM...");
        netMgr.syncTimeGSM();
        
        // ===== 7) MQTT BAĞLANTISI =====
        Serial.println("\n[STEP 7] Connecting to MQTT broker...");
        mqttMgr.begin(macAddr.c_str(), nullptr, netMgr.get4GModem(), true, &cfg);
        mqttMgr.setCallback(mqttCallback);
        
        if (mqttMgr.connect()) {
            Serial.println("[MQTT] Connected!");
            
            // LCD güncelle - MQTT bağlandı
            LCD_showStatus(tempC, gPower, cfg, 0, true, 0, macAddr.c_str(), 0, 0.0, 0.0, 0.0, 0.0, 0, 99.0, false,
                           0, true, FW_VERSION);
            
            // ===== 8) INFO MESAJI GÖNDER =====
            Serial.println("\n[STEP 8] Publishing info message...");
            time_t now_t = time(nullptr);
            mqttMgr.publishInfo(macAddr.c_str(), FW_VERSION, millis()/1000, 
                               ESP.getFreeHeap(), (uint32_t)now_t, netMgr.getRSSI(), 
                               cfg, gPower, sensorOK);
            Serial.println("[MQTT] Info message sent");
            delay(200);
            
            // ===== 9) DATA MESAJI GÖNDER =====
            if (sensorOK) {
                Serial.println("\n[STEP 9] Publishing sensor data...");
                mqttMgr.publishData(macAddr.c_str(), tempC, gPower.battPct, netMgr.getRSSI(),
                                   (uint32_t)now_t, cfg.internalSensorName, cfg.internalMahalId);
                Serial.println("[MQTT] Sensor data sent");
                delay(200);
            } else {
                Serial.println("\n[STEP 9] Skipping data publish - no sensor");
            }
        } else {
            Serial.println("[MQTT] Connection failed!");
        }
    } else {
        Serial.println("[NET] Network connection failed!");
    }

    // ===== 10) BLE SCAN BAŞLAT =====
    Serial.println("\n[STEP 10] Initializing BLE scanner...");
    bleMgr.begin();
    bleMgr.loadConfigFromCfg(cfg);
    Serial.printf("[BLE] Loaded %d Eddystone sensor configs\n", cfg.activeSensorCount);
    
    // Kısa bir BLE scan yap (varsa sensör dataları gönder)
    if (cfg.activeSensorCount > 0 && mqttMgr.isConnected()) {
        Serial.println("[BLE] Starting initial scan for configured sensors...");
        bleMgr.startScanCycle();  // Tarama döngüsü başlat
        
        // 10 saniye boyunca scan et
        unsigned long scanStart = millis();
        while (millis() - scanStart < 10000) {
            bleMgr.scan();  // Sürekli çağır
            delay(100);
        }
        
        Serial.printf("[BLE] Initial scan complete - found %d tags\n", bleMgr.getScannedTagCount());
    }

    // ===== 11) GPS BAŞLAT (EN SON) =====
    Serial.println("\n[STEP 11] Starting GPS (last step)...");
    if (networkConnected && netMgr.startGPS()) {
        Serial.println("[GPS] GPS started successfully");
        
        // GPS fix için kısa bir süre bekle (opsiyonel)
        Serial.println("[GPS] Waiting for initial fix (max 30 sec)...");
        unsigned long gpsWaitStart = millis();
        bool gotFix = false;
        
        while (millis() - gpsWaitStart < 30000) {
            netMgr.updateGPS();
            
            float lat, lon;
            if (netMgr.getGPSLocation(&lat, &lon)) {
                gotFix = true;
                Serial.printf("[GPS] Fix acquired! Lat: %.6f, Lon: %.6f\n", lat, lon);
                
                // ===== 12) GPS DATA GÖNDER =====
                if (mqttMgr.isConnected()) {
                    Serial.println("\n[STEP 12] Publishing GPS data...");
                    time_t now_t = time(nullptr);
                    mqttMgr.publishDataWithGPS(macAddr.c_str(), tempC, gPower.battPct, 
                                               netMgr.getRSSI(), (uint32_t)now_t, sensorOK, lat, lon);
                    Serial.println("[MQTT] GPS data sent");
                }
                break;
            }
            
            // Her 5 saniyede bir durum göster
            if ((millis() - gpsWaitStart) % 5000 < 100) {
                Serial.printf("[GPS] Waiting for fix... (%lu sec)\n", (millis() - gpsWaitStart) / 1000);
            }
            delay(100);
        }
        
        if (!gotFix) {
            Serial.println("[GPS] No fix yet - will continue in loop");
        }
    } else {
        Serial.println("[GPS] GPS start failed or network not connected");
    }

    // ===== SETUP TAMAMLANDI =====
    Serial.println("\n========================================");
    Serial.println("[INIT] Setup complete!");
    Serial.printf("[STATUS] Sensor: %s, Network: %s, MQTT: %s\n",
                  sensorOK ? "OK" : "FAIL",
                  networkConnected ? "OK" : "FAIL",
                  mqttMgr.isConnected() ? "OK" : "FAIL");
    Serial.println("========================================\n");
    
    lastDataTxTime = millis();
}

// ===== LOOP =====
void loop() {
    unsigned long now = millis();

    // 1) Power status update
    powerMgr.update();
    gPower = powerMgr.getStatus();

    // 2) GPS update - NMEA stream oku (sürekli çağrılmalı)
    netMgr.updateGPS();

    // 3) Dahili sensör okuma
    float tempC = -99.0;
    bool sensorOK = false;
    if (extSensor.isAvailable()) {
        if (extSensor.readTemperature(tempC)) {
            if (tempC > -55.0f && tempC < 125.0f) {
                sensorOK = true;
            }
        }
    }

    // 4) Alarm kontrolü
    uint8_t alarmState = 0;
    if (sensorOK) {
        alarmState = alarmMgr.checkTemperature(tempC, cfg.tempHigh, cfg.tempLow);
    }

    // 5) Buzzer kontrolü
    if (cfg.buzzerEnabled && alarmState != 0) {
        if (!buzzerMgr.isAlarming()) {
            buzzerMgr.alarmStart(10000);
        }
    } else {
        buzzerMgr.alarmStop();
    }
    buzzerMgr.update();

    // ===== 2 DAKİKALIK PERİYOT YÖNETİMİ =====
    // Periyot başında: buffer temizle, scan başlat
    // Periyot süresince: tüm kayıtlı sensörler bulunana kadar scan et
    // Periyot sonunda: GPS + tüm sensörler tek JSON'da publish et
    
    // Yeni periyot başlat
    if (!cycleActive && (now - lastDataTxTime >= DATA_TX_INTERVAL_MS || lastDataTxTime == 0)) {
        cycleActive = true;
        cycleDataReady = false;
        cycleStartTime = now;
        
        bleMgr.startScanCycle();
        Serial.println("\n========== NEW 2-MINUTE CYCLE STARTED ==========");
        Serial.printf("[CYCLE] Looking for %d configured Eddystone sensors\n", bleMgr.getConfiguredTagCount());
        Serial.println("=================================================\n");
    }
    
    // Aktif periyot - BLE scan devam ediyor
    if (cycleActive && !cycleDataReady) {
        // BLE scan (tüm sensörler bulunana kadar devam et)
        bleMgr.scan();
        
        // Progress log (her 30 saniyede bir)
        static unsigned long lastProgressLog = 0;
        if (now - lastProgressLog >= 30000) {
            lastProgressLog = now;
            uint8_t found = bleMgr.getFoundConfiguredTagCount();
            uint8_t total = bleMgr.getConfiguredTagCount();
            unsigned long elapsed = (now - cycleStartTime) / 1000;
            Serial.printf("[CYCLE] Progress: %d/%d sensors found (%lu sec elapsed)\n", found, total, elapsed);
        }
        
        // Tüm sensörler bulundu mu veya 2 dakika doldu mu?
        bool allFound = bleMgr.allConfiguredTagsFound();
        bool timeExpired = (now - cycleStartTime >= DATA_TX_INTERVAL_MS);
        
        if (allFound || timeExpired) {
            cycleDataReady = true;
            
            if (allFound) {
                Serial.println("\n[CYCLE] All configured sensors found!");
            } else {
                uint8_t found = bleMgr.getFoundConfiguredTagCount();
                uint8_t total = bleMgr.getConfiguredTagCount();
                Serial.printf("\n[CYCLE] Time expired - found %d/%d sensors\n", found, total);
            }
        }
    }
    
    // Veri hazır - publish et
    if (cycleActive && cycleDataReady) {
        lastDataTxTime = now;
        cycleActive = false;
        cycleDataReady = false;
        
        Serial.println("\n========== PUBLISHING CYCLE DATA ==========");
        
        // Time sync
        netMgr.syncTimeGSM();
        
        // MQTT bağlantısını kontrol et
        if (!mqttMgr.isConnected()) {
            if (netMgr.connect(cfg)) {
                mqttMgr.connect();
            }
        }

        if (mqttMgr.isConnected()) {
            time_t now_time;
            time(&now_time);
            uint32_t epoch = (uint32_t)now_time;

            // GPS koordinatları (4G modem üzerinden)
            float lat = 0.0, lon = 0.0;
            if (netMgr.getGPSLocation(&lat, &lon)) {
                Serial.printf("[GPS] Location: Lat=%.6f, Lon=%.6f\n", lat, lon);
            } else {
                Serial.println("[GPS] No GPS fix available");
            }

            // ===== TEK JSON'DA TÜM VERİLERİ PUBLISH ET =====
            DynamicJsonDocument doc(4096);
            doc["msg"] = "advData";
            doc["gmac"] = macAddr;
            doc["stat"] = "online";
            doc["conn"] = "4G";
            
            // GPS objesi (konum + hız + yön + zaman)
            JsonObject gps = doc.createNestedObject("gps");
            gps["lat"] = lat;
            gps["lon"] = lon;
            gps["speed"] = netMgr.getGPSSpeed();      // km/h
            gps["course"] = netMgr.getGPSCourse();    // derece (0-360)
            gps["time"] = netMgr.getGPSTime();        // UTC saat (HH:MM:SS)
            gps["date"] = netMgr.getGPSDate();        // Tarih (DD/MM/YY)
            gps["sats"] = netMgr.getGPSSatellites();  // Uydu sayısı
            gps["hdop"] = netMgr.getGPSHDOP();        // HDOP
            
            // Sensör dizisi
            JsonArray obj = doc.createNestedArray("obj");
            
            // 1) Dahili sensör
            JsonObject internalSensor = obj.createNestedObject();
            internalSensor["type"] = 1;
            internalSensor["dmac"] = macAddr;
            internalSensor["name"] = cfg.internalSensorName;
            internalSensor["location"] = cfg.internalMahalId;
            internalSensor["temp"] = sensorOK ? tempC : 0;
            internalSensor["batt"] = gPower.battPct;
            internalSensor["rssi"] = netMgr.getRSSI();
            internalSensor["time"] = epoch;
            
            // 2) BLE Eddystone sensörler
            uint8_t bleCount = bleMgr.getScannedTagCount();
            for (uint8_t i = 0; i < bleCount; i++) {
                BLETagData* tag = bleMgr.getScannedTag(i);
                if (!tag || !tag->valid) continue;
                
                BLETagConfig* config = bleMgr.getTagConfig(tag->macAddress);
                
                // MAC'i normalize et
                String normalizedMac = String(tag->macAddress);
                normalizedMac.toUpperCase();
                normalizedMac.replace(":", "");
                
                JsonObject bleSensor = obj.createNestedObject();
                bleSensor["type"] = 2; // BLE sensor type
                bleSensor["dmac"] = normalizedMac;
                bleSensor["name"] = config ? config->name : "Unknown";
                bleSensor["location"] = config ? config->mahalId : "";
                bleSensor["temp"] = tag->temperature;
                bleSensor["batt"] = tag->batteryPct;
                bleSensor["rssi"] = tag->rssi;
                bleSensor["time"] = epoch;
                bleSensor["advCount"] = tag->advCount;
            }
            
            // JSON'u publish et
            String payload;
            serializeJson(doc, payload);
            
            String topic = mqttMgr.getDataTopic(macAddr.c_str());
            mqttMgr.publishRaw(topic.c_str(), payload.c_str());
            
            Serial.printf("[TX] Published combined data: %d sensors (1 internal + %d BLE)\n", 
                         1 + bleCount, bleCount);
            Serial.printf("[TX] GPS: lat=%.6f, lon=%.6f, speed=%.1f km/h, course=%.1f°\n", 
                         lat, lon, netMgr.getGPSSpeed(), netMgr.getGPSCourse());
            Serial.printf("[TX] GPS Time: %s %s UTC | Sats: %d | HDOP: %.1f\n",
                         netMgr.getGPSDate().c_str(), netMgr.getGPSTime().c_str(),
                         netMgr.getGPSSatellites(), netMgr.getGPSHDOP());
            Serial.printf("[TX] Payload size: %d bytes\n", payload.length());

            // Info message
            mqttMgr.publishInfo(macAddr.c_str(), FW_VERSION, millis() / 1000,
                              ESP.getFreeHeap(), epoch, netMgr.getRSSI(), cfg, gPower, sensorOK);

            // FIFO'dan bekleyen kayıtları gönder
            uint16_t pendingCount = configMgr.getPendingCount();
            if (pendingCount > 0) {
                OfflineDataRecord* records = new OfflineDataRecord[pendingCount];
                int actualCount = 0;
                OfflineDataRecord record;
                while (configMgr.popRecord(record) && actualCount < pendingCount) {
                    records[actualCount] = record;
                    actualCount++;
                }
                if (actualCount > 0) {
                    mqttMgr.publishDataArray(macAddr.c_str(), records, actualCount,
                                            cfg.internalSensorName, cfg.internalMahalId);
                }
                delete[] records;
                configMgr.clearAllRecords();
            }
        } else {
            // Bağlantı yoksa FIFO'ya kaydet
            Serial.println("[TX] Connection failed - saving to FIFO");
            time_t now_time;
            time(&now_time);
            uint32_t epochForRecord = (uint32_t)now_time;
            OfflineDataRecord record;
            memset(&record, 0, sizeof(OfflineDataRecord));
            record.timestamp = epochForRecord;
            record.temperature = sensorOK ? tempC : -99.0f;
            record.batteryPct = gPower.battPct;
            record.rssi = netMgr.getRSSI();
            record.alarmState = alarmState;
            strlcpy(record.sensorId, "INTERNAL", sizeof(record.sensorId));
            configMgr.pushRecord(record);
        }
        
        Serial.println("============================================\n");
    }

    // 6) LCD güncelleme - Sıralı gösterim (dahili sensör + BLE sensörler)
    static unsigned long lastLCDUpdate = 0;
    static uint8_t lcdDisplayIndex = 0;
    
    // GPS koordinatlarını al (NMEA stream'den güncel değerler)
    float currentLat = 0.0, currentLon = 0.0;
    netMgr.getGPSLocation(&currentLat, &currentLon);
    
    // GPS durumu logu (her 30 saniyede bir)
    static unsigned long lastGPSLog = 0;
    if (now - lastGPSLog >= 30000) {
        lastGPSLog = now;
        if (netMgr.isGPSFixValid()) {
            Serial.println("\n---------- GPS STATUS ----------");
            Serial.printf("[GPS] Konum: %.6f, %.6f\n", currentLat, currentLon);
            Serial.printf("[GPS] Hız: %.1f km/h | Yön: %.1f°\n", netMgr.getGPSSpeed(), netMgr.getGPSCourse());
            Serial.printf("[GPS] Zaman: %s %s UTC\n", netMgr.getGPSDate().c_str(), netMgr.getGPSTime().c_str());
            Serial.printf("[GPS] Sats: %d | HDOP: %.1f\n", netMgr.getGPSSatellites(), netMgr.getGPSHDOP());
            Serial.println("--------------------------------\n");
        } else {
            Serial.printf("[GPS] Status: Waiting for fix... (Sats: %d)\n", netMgr.getGPSSatellites());
        }
    }
    
    if (now - lastLCDUpdate >= 10000) {
        lastLCDUpdate = now;
        
        uint8_t totalDisplays = 1 + bleMgr.getScannedTagCount();
        
        if (lcdDisplayIndex == 0) {
            LCD_showStatus(tempC, gPower, cfg, netMgr.getRSSI(), true, alarmState, 
                          macAddr.c_str(), 0, currentLat, currentLon,
                          netMgr.getGPSSpeed(), netMgr.getGPSCourse(),
                          netMgr.getGPSSatellites(), netMgr.getGPSHDOP(), netMgr.isGPSFixValid(),
                          bleMgr.getScannedTagCount(), mqttMgr.isConnected(), FW_VERSION);
        } else {
            uint8_t bleIndex = lcdDisplayIndex - 1;
            BLETagData* tag = bleMgr.getScannedTag(bleIndex);
            
            if (tag && tag->valid) {
                BLETagConfig* config = bleMgr.getTagConfig(tag->macAddress);
                
                uint8_t bleAlarmState = 0;
                float bleHighLimit = config ? config->tempHigh : cfg.tempHigh;
                float bleLowLimit = config ? config->tempLow : cfg.tempLow;
                
                if (tag->temperature > bleHighLimit) bleAlarmState = 1;
                else if (tag->temperature < bleLowLimit) bleAlarmState = 2;
                
                LCD_showBLESensor(
                    config ? config->name : "Unknown",
                    tag->macAddress,
                    tag->temperature,
                    tag->batteryPct,
                    tag->rssi,
                    gPower,
                    netMgr.getRSSI(),
                    true,
                    macAddr.c_str(),
                    bleHighLimit,
                    bleLowLimit,
                    bleAlarmState
                );
            } else {
                lcdDisplayIndex = 0;
                LCD_showStatus(tempC, gPower, cfg, netMgr.getRSSI(), true, alarmState, 
                              macAddr.c_str(), 0, currentLat, currentLon,
                              netMgr.getGPSSpeed(), netMgr.getGPSCourse(),
                              netMgr.getGPSSatellites(), netMgr.getGPSHDOP(), netMgr.isGPSFixValid(),
                              bleMgr.getScannedTagCount(), mqttMgr.isConnected(), FW_VERSION);
            }
        }
        
        lcdDisplayIndex++;
        if (lcdDisplayIndex >= totalDisplays) {
            lcdDisplayIndex = 0;
        }
    }

    // 7) MQTT loop
    mqttMgr.loop();

    delay(10);
}

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "ConfigManager.h"

// Forward declaration
class C16QS4GManager;

class MQTTManager {
public:
    MQTTManager();
    bool begin(const char* macAddr, WiFiClient* wifiClient, C16QS4GManager* modem4G = nullptr, bool is4G = false, const Cfg* config = nullptr);
    void setCallback(void (*callback)(char*, byte*, unsigned int));
    bool connect();
    bool isConnected();
    void loop();
    void disconnect();
    
    // Publish functions
    bool publishData(const char* macAddr, float temp, int battPct, int rssi, uint32_t epoch, 
                     const char* sensorName, const char* mahalId);
    bool publishDataWithGPS(const char* macAddr, float temp, int battPct, int rssi, uint32_t epoch, 
                            bool sensorOK, float latitude, float longitude);
    bool publishRaw(const char* topic, const char* payload); // For BLE tags
    bool publishDataArray(const char* macAddr, const OfflineDataRecord* records, int count,
                         const char* sensorName, const char* mahalId);
    bool publishInfo(const char* macAddr, const char* fwVersion, uint32_t uptime, 
                     uint32_t heap, uint32_t epoch, int wifiRssi, const Cfg& cfg, 
                     const PowerStatus& power, bool sensorOK = true);
    bool publishAlarm(const char* macAddr, uint8_t alarmState, const char* reason, 
                      float temp, int battPct);
    bool publishError(const char* macAddr, const char* errorMsg);
    
    // Topic getters
    String getDataTopic(const char* macAddr);
    String getConfigTopic(const char* macAddr);
    String getInfoTopic(const char* macAddr);
    String getAlarmTopic(const char* macAddr);
    String getErrorTopic(const char* macAddr);
    String getUpdateTopic() { return "KUTARIoT/update"; }
    
private:
    PubSubClient* _client;  // WiFi için
    C16QS4GManager* _modem4G;  // 4G için
    bool _is4GMode;
    String _macAddr;
    
    // Broker bilgileri (Cfg'den veya default)
    char _mqttHost[64];
    uint16_t _mqttPort;
    char _mqttUser[32];
    char _mqttPass[64];
    
    // Default değerler (Cfg verilmezse kullanılır)
    static const char* DEFAULT_MQTT_HOST;
    static const int DEFAULT_MQTT_PORT;
    static const char* DEFAULT_MQTT_USER;
    static const char* DEFAULT_MQTT_PASS;
};

#endif

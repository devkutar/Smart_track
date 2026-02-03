#include "MQTTManager.h"
#include "C16QS4GManager.h"
#include <ArduinoJson.h>

// Default broker bilgileri (Cfg verilmezse kullanılır)
const char* MQTTManager::DEFAULT_MQTT_HOST = "broker.smarttech.tr";
const int MQTTManager::DEFAULT_MQTT_PORT = 1883;
const char* MQTTManager::DEFAULT_MQTT_USER = "anymqtt";
const char* MQTTManager::DEFAULT_MQTT_PASS = "Anv2023*!";

MQTTManager::MQTTManager() : _client(nullptr), _modem4G(nullptr), _is4GMode(false), _mqttPort(1883) {
    // Default değerleri ayarla
    strlcpy(_mqttHost, DEFAULT_MQTT_HOST, sizeof(_mqttHost));
    strlcpy(_mqttUser, DEFAULT_MQTT_USER, sizeof(_mqttUser));
    strlcpy(_mqttPass, DEFAULT_MQTT_PASS, sizeof(_mqttPass));
}

bool MQTTManager::begin(const char* macAddr, WiFiClient* wifiClient, C16QS4GManager* modem4G, bool is4G, const Cfg* config) {
    _macAddr = String(macAddr);
    _is4GMode = is4G;
    
    // Cfg'den broker bilgilerini al (varsa)
    if (config != nullptr && strlen(config->mqttHost) > 0) {
        strlcpy(_mqttHost, config->mqttHost, sizeof(_mqttHost));
        _mqttPort = config->mqttPort;
        strlcpy(_mqttUser, config->mqttUser, sizeof(_mqttUser));
        strlcpy(_mqttPass, config->mqttPass, sizeof(_mqttPass));
        Serial.printf("[MQTT] Using config broker: %s:%d\n", _mqttHost, _mqttPort);
    } else {
        // Default değerleri kullan
        strlcpy(_mqttHost, DEFAULT_MQTT_HOST, sizeof(_mqttHost));
        _mqttPort = DEFAULT_MQTT_PORT;
        strlcpy(_mqttUser, DEFAULT_MQTT_USER, sizeof(_mqttUser));
        strlcpy(_mqttPass, DEFAULT_MQTT_PASS, sizeof(_mqttPass));
        Serial.printf("[MQTT] Using default broker: %s:%d\n", _mqttHost, _mqttPort);
    }
    
    if (_is4GMode) {
        // 4G mode - C16QS modülü kullan
        _modem4G = modem4G;
        _client = nullptr;
    } else {
        // WiFi mode - PubSubClient kullan
        _client = new PubSubClient(*wifiClient);
        _client->setServer(_mqttHost, _mqttPort);
        _client->setBufferSize(8192); // Large buffer for JSON
        _modem4G = nullptr;
    }
    return true;
}

void MQTTManager::setCallback(void (*callback)(char*, byte*, unsigned int)) {
    if (_is4GMode) {
        // 4G için callback'i modem'e set et (wrapper ile)
        if (_modem4G) {
            _modem4G->setMqttCallback(callback);
        }
    } else {
        if (_client) {
            _client->setCallback(callback);
        }
    }
}

bool MQTTManager::connect() {
    if (_is4GMode) {
        // 4G MQTT bağlantısı
        if (!_modem4G) return false;
        
        String clientId = "STC_" + _macAddr;
        Serial.printf("[MQTT-4G] Connecting to %s:%d as %s...\n", _mqttHost, _mqttPort, clientId.c_str());
        
        if (_modem4G->connectMQTT(_mqttHost, _mqttPort, clientId.c_str(), _mqttUser, _mqttPass)) {
            Serial.println("[MQTT-4G] Connected!");
            
            // Subscribe to config topic
            String configTopic = getConfigTopic(_macAddr.c_str());
            if (_modem4G->subscribeMQTT(configTopic.c_str())) {
                Serial.printf("[MQTT-4G] Subscribed to %s\n", configTopic.c_str());
            }
            
            // Subscribe to update topic
            String updateTopic = getUpdateTopic();
            if (_modem4G->subscribeMQTT(updateTopic.c_str())) {
                Serial.printf("[MQTT-4G] Subscribed to %s\n", updateTopic.c_str());
            }
            
            return true;
        } else {
            Serial.println("[MQTT-4G] Connection failed");
            return false;
        }
    } else {
        // WiFi MQTT bağlantısı
        if (!_client) return false;
        if (_client->connected()) return true;
        
        String clientId = "STC_" + _macAddr;
        Serial.printf("[MQTT-WiFi] Connecting to %s:%d as %s...\n", _mqttHost, _mqttPort, clientId.c_str());
        
        if (_client->connect(clientId.c_str(), _mqttUser, _mqttPass)) {
            Serial.println("[MQTT-WiFi] Connected!");
            
            // Subscribe to config topic
            String configTopic = getConfigTopic(_macAddr.c_str());
            if (_client->subscribe(configTopic.c_str())) {
                Serial.printf("[MQTT-WiFi] Subscribed to %s\n", configTopic.c_str());
            }
            
            // Subscribe to update topic
            String updateTopic = getUpdateTopic();
            if (_client->subscribe(updateTopic.c_str())) {
                Serial.printf("[MQTT-WiFi] Subscribed to %s\n", updateTopic.c_str());
            }
            
            return true;
        } else {
            Serial.printf("[MQTT-WiFi] Connection failed, rc=%d\n", _client->state());
            return false;
        }
    }
}

bool MQTTManager::isConnected() {
    if (_is4GMode) {
        return (_modem4G && _modem4G->isMQTTConnected());
    } else {
        return (_client && _client->connected());
    }
}

void MQTTManager::loop() {
    if (_is4GMode) {
        if (_modem4G) {
            _modem4G->loop(); // URC işleme
        }
    } else {
        if (_client) {
            _client->loop();
        }
    }
}

void MQTTManager::disconnect() {
    if (_is4GMode) {
        if (_modem4G) {
            _modem4G->disconnectMQTT();
        }
    } else {
        if (_client) {
            _client->disconnect();
        }
    }
}

String MQTTManager::getDataTopic(const char* macAddr) {
    return String("KUTARIoT/data/") + String(macAddr);
}

String MQTTManager::getConfigTopic(const char* macAddr) {
    return String("KUTARIoT/config/") + String(macAddr);
}

String MQTTManager::getInfoTopic(const char* macAddr) {
    return String("KUTARIoT/info/") + String(macAddr);
}

String MQTTManager::getAlarmTopic(const char* macAddr) {
    return String("KUTARIoT/alarm/") + String(macAddr);
}

String MQTTManager::getErrorTopic(const char* macAddr) {
    return String("KUTARIoT/errors/") + String(macAddr);
}

bool MQTTManager::publishData(const char* macAddr, float temp, int battPct, int rssi, 
                               uint32_t epoch, const char* sensorName, const char* mahalId) {
    DynamicJsonDocument doc(1024);
    doc["msg"] = "advData";
    doc["gmac"] = macAddr;
    doc["stat"] = "online";
    
    // Bağlantı tipini ekle: WiFi veya 4G
    doc["conn"] = _is4GMode ? "4G" : "wifi";
    
    JsonArray obj = doc.createNestedArray("obj");
    JsonObject sensor = obj.createNestedObject();
    sensor["type"] = 1;
    sensor["dmac"] = macAddr;
    sensor["name"] = sensorName;
    sensor["location"] = mahalId;
    sensor["temp"] = temp;
    sensor["batt"] = battPct;
    // RSSI değeri zaten doğru geliyor (4G modunda CSQ, WiFi modunda WiFi RSSI)
    sensor["rssi"] = rssi;
    sensor["time"] = epoch;
    
    String topic = getDataTopic(macAddr);
    String payload;
    serializeJson(doc, payload);
    
    Serial.printf("[MQTT] Publishing to %s: %s\n", topic.c_str(), payload.c_str());
    
    if (_is4GMode) {
        if (_modem4G) {
            return _modem4G->publishMQTT(topic.c_str(), payload.c_str());
        }
        return false;
    } else {
        if (_client) {
            return _client->publish(topic.c_str(), payload.c_str());
        }
        return false;
    }
}

bool MQTTManager::publishDataArray(const char* macAddr, const OfflineDataRecord* records, int count,
                                   const char* sensorName, const char* mahalId) {
    if (count == 0) return false;
    
    // Dinamik boyut hesapla (her kayıt için ~150 byte)
    DynamicJsonDocument doc(2048 + (count * 200));
    doc["msg"] = "advData";
    doc["gmac"] = macAddr;
    doc["stat"] = "online";
    
    // Bağlantı tipini ekle: WiFi veya 4G
    doc["conn"] = _is4GMode ? "4G" : "wifi";
    
    JsonArray obj = doc.createNestedArray("obj");
    
    for (int i = 0; i < count; i++) {
        JsonObject sensor = obj.createNestedObject();
        sensor["type"] = 1;
        sensor["dmac"] = macAddr;
        sensor["name"] = sensorName;
        sensor["location"] = mahalId;
        sensor["temp"] = records[i].temperature;
        sensor["batt"] = records[i].batteryPct;
        sensor["rssi"] = records[i].rssi;
        sensor["time"] = records[i].timestamp;
    }
    
    String topic = getDataTopic(macAddr);
    String payload;
    serializeJson(doc, payload);
    
    Serial.printf("[MQTT] Publishing array (%d records) to %s\n", count, topic.c_str());
    
    if (_is4GMode) {
        if (_modem4G) {
            return _modem4G->publishMQTT(topic.c_str(), payload.c_str());
        }
        return false;
    } else {
        if (_client) {
            return _client->publish(topic.c_str(), payload.c_str());
        }
        return false;
    }
}

bool MQTTManager::publishInfo(const char* macAddr, const char* fwVersion, uint32_t uptime, 
                               uint32_t heap, uint32_t epoch, int wifiRssi, const Cfg& cfg, 
                               const PowerStatus& power, bool sensorOK) {
    DynamicJsonDocument doc(4096);
    doc["msg"] = "info";
    doc["fw"] = fwVersion;
    doc["uptime"] = uptime;
    doc["heap"] = heap;
    doc["epoch"] = epoch;
    
    if (_is4GMode) {
        // 4G modunda 4G bilgileri
        JsonObject fourg = doc.createNestedObject("4g");
        if (_modem4G) {
            fourg["rssi"] = _modem4G->getSignalStrength(); // RSSI in dBm
            fourg["mac"] = macAddr;
            fourg["ip"] = _modem4G->getIPAddress();
            String imei = _modem4G->getIMEI();
            if (imei.length() > 0) {
                fourg["imei"] = imei;
            }
            fourg["csq"] = _modem4G->getCSQString(); // CSQ string formatı: "CSQ,BER" (örn: "24,0")
        } else {
            fourg["rssi"] = -100;
            fourg["mac"] = macAddr;
            fourg["ip"] = "0.0.0.0";
            fourg["imei"] = "";
            fourg["csq"] = "99,0"; // CSQ 99 = no signal
        }
        
        // WiFi bilgileri (4G modunda da varsa göster)
        JsonObject wifi = doc.createNestedObject("wifi");
        wifi["rssi"] = 0;
        wifi["mac"] = macAddr;
        wifi["ip"] = "0.0.0.0";
        wifi["ssid"] = "";
    } else {
        // WiFi modunda WiFi bilgileri
        JsonObject wifi = doc.createNestedObject("wifi");
        wifi["rssi"] = wifiRssi;
        wifi["mac"] = macAddr;
        wifi["ip"] = WiFi.localIP().toString();
        wifi["ssid"] = cfg.wifiSsid;
        
        // 4G bilgileri yok (WiFi modunda)
        JsonObject fourg = doc.createNestedObject("4g");
        fourg["rssi"] = -100;
        fourg["mac"] = macAddr;
        fourg["ip"] = "0.0.0.0";
        fourg["imei"] = "";
        fourg["csq"] = -100;
    }
    
    JsonObject sensors = doc.createNestedObject("sensors");
    sensors["t117"] = sensorOK; // Sensör durumu (true/false)
    sensors["internalName"] = cfg.internalSensorName;
    sensors["internalMahal"] = cfg.internalMahalId;
    
    JsonObject config = doc.createNestedObject("config");
    config["wifiSsid"] = cfg.wifiSsid;
    config["dataPeriod"] = cfg.dataPeriod;
    config["infoPeriod"] = cfg.infoPeriod;
    config["tempHigh"] = cfg.tempHigh;
    config["tempLow"] = cfg.tempLow;
    config["buzzerEnabled"] = cfg.buzzerEnabled;
    config["internalName"] = cfg.internalSensorName;
    config["internalMahal"] = cfg.internalMahalId;
    
    JsonObject beacon = config.createNestedObject("beacon");
    beacon["enabled"] = cfg.beacon.enabled;
    beacon["scanInterval"] = cfg.beacon.scanInterval;
    beacon["rssiThreshold"] = cfg.beacon.rssiThreshold;
    beacon["targetPrefix"] = cfg.beacon.targetPrefix;
    beacon["maxBeacons"] = cfg.beacon.maxBeacons;
    beacon["timeoutMs"] = cfg.beacon.timeoutMs;
    
    JsonObject pwr = doc.createNestedObject("power");
    pwr["mains"] = power.mainsPresent;
    pwr["charging"] = power.charging;
    pwr["vbat"] = power.vbat;
    pwr["battPct"] = power.battPct;
    pwr["powerCut"] = false; // TODO: implement power cut detection
    
    String topic = getInfoTopic(macAddr);
    String payload;
    serializeJson(doc, payload);
    
    Serial.printf("[MQTT] Publishing info to %s\n", topic.c_str());
    
    if (_is4GMode) {
        if (_modem4G) {
            return _modem4G->publishMQTT(topic.c_str(), payload.c_str());
        }
        return false;
    } else {
        if (_client) {
            return _client->publish(topic.c_str(), payload.c_str());
        }
        return false;
    }
}

bool MQTTManager::publishAlarm(const char* macAddr, uint8_t alarmState, const char* reason, 
                                float temp, int battPct) {
    DynamicJsonDocument doc(512);
    doc["msg"] = "alarm";
    doc["gmac"] = macAddr;
    doc["state"] = alarmState;
    doc["reason"] = reason;
    doc["temp"] = temp;
    doc["batt"] = battPct;
    doc["time"] = time(nullptr);
    
    String topic = getAlarmTopic(macAddr);
    String payload;
    serializeJson(doc, payload);
    
    Serial.printf("[MQTT] Publishing alarm to %s: %s\n", topic.c_str(), payload.c_str());
    
    if (_is4GMode) {
        if (_modem4G) {
            return _modem4G->publishMQTT(topic.c_str(), payload.c_str());
        }
        return false;
    } else {
        if (_client) {
            return _client->publish(topic.c_str(), payload.c_str());
        }
        return false;
    }
}

bool MQTTManager::publishError(const char* macAddr, const char* errorMsg) {
    DynamicJsonDocument doc(512);
    doc["msg"] = "error";
    doc["gmac"] = macAddr;
    doc["error"] = errorMsg;
    doc["time"] = time(nullptr);
    
    String topic = getErrorTopic(macAddr);
    String payload;
    serializeJson(doc, payload);
    
    Serial.printf("[MQTT] Publishing error to %s: %s\n", topic.c_str(), payload.c_str());
    
    if (_is4GMode) {
        if (_modem4G) {
            return _modem4G->publishMQTT(topic.c_str(), payload.c_str());
        }
        return false;
    } else {
        if (_client) {
            return _client->publish(topic.c_str(), payload.c_str());
        }
        return false;
    }
}

bool MQTTManager::publishDataWithGPS(const char* macAddr, float temp, int battPct, int rssi, 
                                     uint32_t epoch, bool sensorOK, float latitude, float longitude) {
    DynamicJsonDocument doc(1024);
    doc["msg"] = "advData";
    doc["gmac"] = macAddr;
    doc["stat"] = "online";
    doc["conn"] = "4G"; // Smart_track is 4G only
    
    JsonArray obj = doc.createNestedArray("obj");
    JsonObject sensor = obj.createNestedObject();
    sensor["type"] = 1;
    sensor["dmac"] = macAddr;
    sensor["name"] = "Internal"; // Default name
    sensor["location"] = ""; // Default location
    if (sensorOK) {
        sensor["temp"] = temp;
    } else {
        sensor["temp"] = (float)0.0;
    }
    sensor["batt"] = battPct;
    sensor["rssi"] = rssi;
    sensor["time"] = epoch;
    
    // GPS coordinates
    JsonObject gps = doc.createNestedObject("gps");
    gps["lat"] = latitude;
    gps["lon"] = longitude;
    
    String topic = getDataTopic(macAddr);
    String payload;
    serializeJson(doc, payload);
    
    Serial.printf("[MQTT] Publishing GPS data to %s: %s\n", topic.c_str(), payload.c_str());
    
    if (_is4GMode) {
        if (_modem4G) {
            return _modem4G->publishMQTT(topic.c_str(), payload.c_str());
        }
        return false;
    } else {
        if (_client) {
            return _client->publish(topic.c_str(), payload.c_str());
        }
        return false;
    }
}

bool MQTTManager::publishRaw(const char* topic, const char* payload) {
    Serial.printf("[MQTT] Publishing raw to %s: %s\n", topic, payload);
    
    if (_is4GMode) {
        if (_modem4G) {
            return _modem4G->publishMQTT(topic, payload);
        }
        return false;
    } else {
        if (_client) {
            return _client->publish(topic, payload);
        }
        return false;
    }
}
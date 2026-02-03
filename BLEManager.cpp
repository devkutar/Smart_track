#include "BLEManager.h"
#include "MQTTManager.h"
#include "ConfigManager.h"
#include "hardware.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ArduinoJson.h>

static BLEScan* pBLEScan = nullptr;

// BLE Scan Callback Class
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    BLEManager* _bleMgr;
    
public:
    MyAdvertisedDeviceCallbacks(BLEManager* mgr) : _bleMgr(mgr) {}
    
    void onResult(BLEAdvertisedDevice advertisedDevice);
};

// Callback implementation (must be defined after BLEManager class declaration)
void MyAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
    if (_bleMgr) {
        _bleMgr->onBLEScanResult(advertisedDevice);
    }
}

BLEManager::BLEManager() 
    : _initialized(false), _tagCount(0), _configCount(0) {
    memset(_scannedTags, 0, sizeof(_scannedTags));
    memset(_tagConfigs, 0, sizeof(_tagConfigs));
}

bool BLEManager::begin() {
    if (_initialized) return true;
    
    Serial.println("[BLE] Initializing BLE scanner...");
    
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(this));
    pBLEScan->setActiveScan(true); // Active scan (higher power, better results)
    pBLEScan->setInterval(100); // Scan interval in ms
    pBLEScan->setWindow(99); // Scan window in ms
    
    _initialized = true;
    Serial.println("[BLE] BLE scanner initialized");
    return true;
}

void BLEManager::scan() {
    if (!_initialized || !pBLEScan) return;
    
    // Start scan (non-blocking)
    if (!pBLEScan->isScanning()) {
        static unsigned long lastScanLog = 0;
        unsigned long now = millis();
        // Her 30 saniyede bir log bas (sürekli spam önlemek için)
        if (now - lastScanLog > 30000) {
            Serial.printf("[BLE] Starting scan... (looking for %d configured tags)\n", _configCount);
            lastScanLog = now;
        }
        pBLEScan->start(5, false); // 5 seconds scan, don't delete results
    }
}

void BLEManager::onBLEScanResult(BLEAdvertisedDevice advertisedDevice) {
    // Get MAC address
    String macStr = advertisedDevice.getAddress().toString().c_str();
    macStr.toUpperCase();
    String macStrNoColon = macStr;
    macStrNoColon.replace(":", "");
    
    // Check if MAC address matches target prefix
    if (!macStrNoColon.startsWith(BLE_TARGET_PREFIX)) {
        return; // Not our target prefix
    }
    
    // Prefix'e uyan tüm cihazları logla (aynı MAC'i tekrar loglamayı önle)
    static String lastLoggedMacs[32];
    static uint8_t lastLoggedCount = 0;
    static unsigned long lastLogReset = 0;
    
    // 60 saniyede bir log listesini sıfırla (yeni tarama)
    if (millis() - lastLogReset > 60000) {
        lastLoggedCount = 0;
        lastLogReset = millis();
    }
    
    // Bu MAC daha önce loglanmış mı kontrol et
    bool alreadyLogged = false;
    for (uint8_t i = 0; i < lastLoggedCount; i++) {
        if (lastLoggedMacs[i] == macStrNoColon) {
            alreadyLogged = true;
            break;
        }
    }
    
    if (!alreadyLogged && lastLoggedCount < 32) {
        lastLoggedMacs[lastLoggedCount++] = macStrNoColon;
        Serial.printf("[BLE-SCAN] Found device with prefix %s: MAC=%s, RSSI=%d dBm\n", 
                     BLE_TARGET_PREFIX, macStr.c_str(), advertisedDevice.getRSSI());
    }
    
    // Check if this MAC is in config (only accept configured tags for processing)
    bool isConfigured = false;
    int matchedConfigIndex = -1;
    
    for (uint8_t i = 0; i < _configCount; i++) {
        String configMac = String(_tagConfigs[i].macAddress);
        configMac.toUpperCase();
        configMac.replace(":", "");
        
        if (configMac == macStrNoColon && _tagConfigs[i].enabled) {
            isConfigured = true;
            matchedConfigIndex = i;
            break;
        }
    }
    
    if (!isConfigured) {
        return; // Not in config list, don't process further
    }
    
    // Parse Eddystone data from advertised data
    BLETagData tagData;
    memset(&tagData, 0, sizeof(BLETagData));
    strlcpy(tagData.macAddress, advertisedDevice.getAddress().toString().c_str(), sizeof(tagData.macAddress));
    tagData.rssi = advertisedDevice.getRSSI();
    tagData.lastSeenTime = time(nullptr); // Current epoch time
    
    // Parse Eddystone TLM frame
    bool foundEddystone = false;
    
    // Method 1: getServiceData() ile dene
    if (advertisedDevice.haveServiceData()) {
        String serviceDataStr = advertisedDevice.getServiceData();
        int sdLen = serviceDataStr.length();
        
        if (sdLen >= 12) {
            const uint8_t* data = (const uint8_t*)serviceDataStr.c_str();
            uint8_t frameType = data[0];
            
            if (frameType == 0x20) { // TLM frame
                if (_parseEddystoneTLM(data, sdLen, tagData)) {
                    foundEddystone = true;
                }
            }
        }
    }
    
    // Method 2: Raw payload'dan parse et (Method 1 başarısız olduysa)
    if (!foundEddystone) {
        uint8_t* payload = advertisedDevice.getPayload();
        size_t payloadLen = advertisedDevice.getPayloadLength();
        
        for (size_t i = 0; i < payloadLen; ) {
            uint8_t fieldLen = payload[i];
            if (fieldLen == 0 || i + fieldLen >= payloadLen) break;
            
            uint8_t fieldType = payload[i + 1];
            
            // Service Data - 16-bit UUID (type 0x16)
            if (fieldType == 0x16 && fieldLen >= 4) {
                uint16_t serviceUUID = payload[i + 2] | (payload[i + 3] << 8);
                
                if (serviceUUID == 0xFEAA) {
                    const uint8_t* eddystoneData = &payload[i + 4];
                    int eddystoneLen = fieldLen - 3;
                    
                    if (eddystoneLen > 0 && eddystoneData[0] == 0x20) {
                        if (_parseEddystoneTLM(eddystoneData, eddystoneLen, tagData)) {
                            foundEddystone = true;
                        }
                    }
                }
            }
            
            i += fieldLen + 1;
        }
    }
    
    if (!foundEddystone) {
        // TLM frame gelmediyse sadece RSSI ve lastSeenTime güncelle (eğer tag zaten varsa)
        for (uint8_t i = 0; i < _tagCount; i++) {
            if (strcmp(_scannedTags[i].macAddress, tagData.macAddress) == 0) {
                _scannedTags[i].rssi = tagData.rssi;
                _scannedTags[i].lastSeenTime = tagData.lastSeenTime;
                return;
            }
        }
        return; // Tag henüz yoksa, TLM frame bekliyoruz
    }
    
    // TLM frame bulundu - tag'ı kaydet
    tagData.valid = true;
    
    // Bu tag daha önce kaydedilmiş mi kontrol et
    bool isNewTag = true;
    for (uint8_t i = 0; i < _tagCount; i++) {
        if (strcmp(_scannedTags[i].macAddress, tagData.macAddress) == 0) {
            isNewTag = false;
            break;
        }
    }
    
    _updateTagData(tagData.macAddress, tagData);
    
    // Sadece yeni bulunan sensörleri logla
    if (isNewTag) {
        BLETagConfig* config = getTagConfig(tagData.macAddress);
        Serial.println("\n========== BLE SENSOR FOUND ==========");
        Serial.printf("[BLE] Name: %s\n", config ? config->name : "Unknown");
        Serial.printf("[BLE] MAC: %s\n", tagData.macAddress);
        Serial.printf("[BLE] Temp: %.2f C | Batt: %d%% | RSSI: %d dBm\n", 
                     tagData.temperature, tagData.batteryPct, tagData.rssi);
        Serial.printf("[BLE] Found %d/%d configured sensors\n", getFoundConfiguredTagCount(), _configCount);
        Serial.println("======================================\n");
    }
}

bool BLEManager::_parseEddystoneTLM(const uint8_t* data, int len, BLETagData& tag) {
    // Eddystone-TLM format (after service UUID):
    // [0] Frame Type (0x20)
    // [1] Version (0x00 for unencrypted)
    // [2-3] Battery voltage (mV, big-endian)
    // [4-5] Temperature (8.8 fixed point, big-endian)
    // [6-9] Advertisement count (big-endian)
    // [10-13] Time since power-on (big-endian)
    
    if (len < 12) return false;
    
    // Battery voltage: 2 bytes, big-endian, 1 mV resolution
    uint16_t battRaw = (data[2] << 8) | data[3];
    float battVoltage = battRaw / 1000.0f;
    
    // Estimate battery percentage (2.0V = 0%, 3.0V = 100% for CR2032)
    if (battVoltage < 2.0) tag.batteryPct = 0;
    else if (battVoltage > 3.0) tag.batteryPct = 100;
    else tag.batteryPct = (int)((battVoltage - 2.0) / 1.0 * 100.0);
    
    // Temperature: 2 bytes, big-endian, 8.8 fixed point signed
    int8_t tempInt = (int8_t)data[4];
    uint8_t tempFrac = data[5];
    tag.temperature = tempInt + (tempFrac / 256.0f);
    
    // Advertisement count: 4 bytes, big-endian
    tag.advCount = ((uint32_t)data[6] << 24) | ((uint32_t)data[7] << 16) | 
                   ((uint32_t)data[8] << 8) | data[9];
    
    return true;
}

bool BLEManager::_parseEddystoneUID(const uint8_t* data, int len, BLETagData& tag) {
    // Eddystone-UID format: [Frame Type (0x00)] [TX Power] [Namespace (10 bytes)] [Instance (6 bytes)]
    // UID frame doesn't contain temperature/battery data
    // This would need to be combined with TLM frame
    return false;
}

bool BLEManager::_parseEddystoneURL(const uint8_t* data, int len, BLETagData& tag) {
    // Eddystone-URL format: [Frame Type (0x10)] [TX Power] [URL Scheme] [Encoded URL]
    // URL frame doesn't contain temperature/battery data
    return false;
}

bool BLEManager::_matchesTargetPrefix(const char* macAddress) {
    String macStr = String(macAddress);
    macStr.toUpperCase();
    macStr.replace(":", "");
    return macStr.startsWith(BLE_TARGET_PREFIX);
}

void BLEManager::_updateTagData(const char* macAddress, const BLETagData& newData) {
    uint8_t index = _findTagIndex(macAddress);
    
    if (index == 255) {
        // New tag - add it
        if (_tagCount < 32) {
            _scannedTags[_tagCount] = newData;
            _tagCount++;
        }
    } else {
        // Existing tag - update it
        _scannedTags[index] = newData;
    }
}

uint8_t BLEManager::_findTagIndex(const char* macAddress) {
    for (uint8_t i = 0; i < _tagCount; i++) {
        if (strcmp(_scannedTags[i].macAddress, macAddress) == 0) {
            return i;
        }
    }
    return 255; // Not found
}

uint8_t BLEManager::getScannedTagCount() {
    return _tagCount;
}

BLETagData* BLEManager::getScannedTag(uint8_t index) {
    if (index >= _tagCount) return nullptr;
    return &_scannedTags[index];
}

BLETagData* BLEManager::getTagByMac(const char* macAddress) {
    uint8_t index = _findTagIndex(macAddress);
    if (index == 255) return nullptr;
    return &_scannedTags[index];
}

void BLEManager::updateTagConfig(const BLETagConfig& config) {
    // Find existing config or add new one
    for (uint8_t i = 0; i < _configCount; i++) {
        if (strcmp(_tagConfigs[i].macAddress, config.macAddress) == 0) {
            _tagConfigs[i] = config;
            return;
        }
    }
    
    // Add new config
    if (_configCount < 32) {
        _tagConfigs[_configCount] = config;
        _configCount++;
    }
}

BLETagConfig* BLEManager::getTagConfig(const char* macAddress) {
    // MAC adresini normalize et (uppercase, no colons)
    String searchMac = String(macAddress);
    searchMac.toUpperCase();
    searchMac.replace(":", "");
    
    for (uint8_t i = 0; i < _configCount; i++) {
        String configMac = String(_tagConfigs[i].macAddress);
        configMac.toUpperCase();
        configMac.replace(":", "");
        
        if (configMac == searchMac) {
            return &_tagConfigs[i];
        }
    }
    return nullptr;
}

void BLEManager::loadConfigFromCfg(const Cfg& cfg) {
    // Config'deki eddystone sensörlerini yükle (max 4 tane)
    _configCount = 0;
    for (uint8_t i = 0; i < 32 && i < cfg.activeSensorCount && _configCount < 4; i++) {
        if (cfg.eddystoneSensors[i].enabled && strlen(cfg.eddystoneSensors[i].macAddress) > 0) {
            BLETagConfig tagConfig;
            strlcpy(tagConfig.macAddress, cfg.eddystoneSensors[i].macAddress, sizeof(tagConfig.macAddress));
            strlcpy(tagConfig.name, cfg.eddystoneSensors[i].sensorName, sizeof(tagConfig.name));
            strlcpy(tagConfig.mahalId, cfg.eddystoneSensors[i].mahalId, sizeof(tagConfig.mahalId));
            tagConfig.tempHigh = cfg.eddystoneSensors[i].tempHigh;
            tagConfig.tempLow = cfg.eddystoneSensors[i].tempLow;
            tagConfig.buzzerEnabled = cfg.eddystoneSensors[i].buzzerEnabled;
            tagConfig.enabled = true;
            _tagConfigs[_configCount] = tagConfig;
            _configCount++;
        }
    }
    Serial.printf("[BLE] Loaded %d eddystone configs from Cfg\n", _configCount);
}

void BLEManager::publishScannedTags(MQTTManager* mqtt, const char* gatewayMac, uint32_t epoch) {
    if (!mqtt || _tagCount == 0) return;
    
    for (uint8_t i = 0; i < _tagCount; i++) {
        if (!_scannedTags[i].valid) continue;
        
        BLETagConfig* config = getTagConfig(_scannedTags[i].macAddress);
        
        // MAC adresini normalize et (uppercase, no colons)
        String normalizedMac = String(_scannedTags[i].macAddress);
        normalizedMac.toUpperCase();
        normalizedMac.replace(":", "");
        
        DynamicJsonDocument doc(1024);
        doc["msg"] = "advData";
        doc["gmac"] = gatewayMac;
        doc["stat"] = "online";
        doc["conn"] = "4G";
        
        JsonArray obj = doc.createNestedArray("obj");
        JsonObject sensor = obj.createNestedObject();
        sensor["type"] = 1; // BLE sensor type
        sensor["dmac"] = normalizedMac; // Büyük harf, : olmadan
        sensor["name"] = config ? config->name : "Unknown";
        sensor["location"] = config ? config->mahalId : "";
        sensor["temp"] = _scannedTags[i].temperature;
        sensor["batt"] = _scannedTags[i].batteryPct;
        sensor["rssi"] = _scannedTags[i].rssi;
        sensor["time"] = epoch;
        sensor["advCount"] = _scannedTags[i].advCount;
        
        String topic = mqtt->getDataTopic(gatewayMac);
        String payload;
        serializeJson(doc, payload);
        
        mqtt->publishRaw(topic.c_str(), payload.c_str());
    }
}

// ===== 2 Dakikalık Periyot Yönetimi =====

uint8_t BLEManager::getConfiguredTagCount() {
    return _configCount;
}

uint8_t BLEManager::getFoundConfiguredTagCount() {
    uint8_t foundCount = 0;
    
    for (uint8_t i = 0; i < _configCount; i++) {
        // Config'deki MAC'i normalize et
        String configMac = String(_tagConfigs[i].macAddress);
        configMac.toUpperCase();
        configMac.replace(":", "");
        
        // Bu MAC taranan tag'larda var mı ve valid mi?
        for (uint8_t j = 0; j < _tagCount; j++) {
            String scannedMac = String(_scannedTags[j].macAddress);
            scannedMac.toUpperCase();
            scannedMac.replace(":", "");
            
            if (configMac == scannedMac && _scannedTags[j].valid) {
                foundCount++;
                break;
            }
        }
    }
    
    return foundCount;
}

bool BLEManager::allConfiguredTagsFound() {
    if (_configCount == 0) return true; // Hiç config yoksa tamamdır
    return getFoundConfiguredTagCount() >= _configCount;
}

void BLEManager::clearScannedTags() {
    memset(_scannedTags, 0, sizeof(_scannedTags));
    _tagCount = 0;
    Serial.println("[BLE] Cleared scanned tags buffer for new cycle");
}

void BLEManager::startScanCycle() {
    clearScannedTags();
    Serial.printf("[BLE] Starting new scan cycle - looking for %d configured sensors\n", _configCount);
}


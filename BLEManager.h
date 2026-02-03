#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include "ConfigManager.h"

// Forward declaration
class MQTTManager;

// BLE Eddystone Tag Data Structure
struct BLETagData {
    char macAddress[18];      // MAC address (e.g., "8C:69:6B:XX:XX:XX")
    float temperature;        // Temperature in Celsius
    int batteryPct;           // Battery percentage (0-100)
    int rssi;                 // RSSI in dBm
    uint16_t advCount;        // Advertisement count
    uint32_t lastSeenTime;    // Last seen timestamp (epoch)
    bool valid;               // Is this tag data valid
};

// BLE Eddystone Tag Configuration (from broker)
struct BLETagConfig {
    char macAddress[18];
    char name[32];
    char mahalId[25];
    float tempHigh;
    float tempLow;
    bool buzzerEnabled;
    bool enabled;
};

class BLEManager {
public:
    BLEManager();
    bool begin();
    void loadConfigFromCfg(const Cfg& cfg); // Load eddystone config from Cfg
    void scan(); // Call in loop to scan for BLE tags (only scans when GPS fix valid)
    uint8_t getScannedTagCount();
    BLETagData* getScannedTag(uint8_t index);
    BLETagData* getTagByMac(const char* macAddress);
    void updateTagConfig(const BLETagConfig& config);
    BLETagConfig* getTagConfig(const char* macAddress);
    void publishScannedTags(MQTTManager* mqtt, const char* gatewayMac, uint32_t epoch);
    
    // 2 dakikalık periyot yönetimi
    uint8_t getConfiguredTagCount();         // Config'deki kayıtlı sensör sayısı
    uint8_t getFoundConfiguredTagCount();    // Bulunan kayıtlı sensör sayısı (TLM alınmış)
    bool allConfiguredTagsFound();           // Tüm kayıtlı sensörler bulundu mu?
    void clearScannedTags();                 // Buffer'ı temizle (yeni periyot için)
    void startScanCycle();                   // Yeni tarama döngüsü başlat
    
    // Callback function for BLE scan results (called from callback class)
    void onBLEScanResult(class BLEAdvertisedDevice advertisedDevice);
    
private:
    bool _initialized;
    BLETagData _scannedTags[32]; // Max 32 tags
    BLETagConfig _tagConfigs[32]; // Max 32 configs
    uint8_t _tagCount;
    uint8_t _configCount;
    
    bool _parseEddystoneTLM(const uint8_t* data, int len, BLETagData& tag);
    bool _parseEddystoneUID(const uint8_t* data, int len, BLETagData& tag);
    bool _parseEddystoneURL(const uint8_t* data, int len, BLETagData& tag);
    bool _matchesTargetPrefix(const char* macAddress);
    void _updateTagData(const char* macAddress, const BLETagData& newData);
    uint8_t _findTagIndex(const char* macAddress);
};

#endif


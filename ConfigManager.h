#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

// Çevrimdışı Kayıt Yapısı
struct OfflineDataRecord {
  uint32_t timestamp;
  float temperature;
  float humidity;
  int batteryPct;
  int rssi;
  uint8_t alarmState;
  uint8_t recordType; 
  char alarmReason[16];
  char sensorId[16];    
  uint32_t crc32;
};

// Tampon Bellek Yönetimi (FIFO)
struct OfflineBuffer {
  uint16_t writeIndex;
  uint16_t readIndex;
  uint16_t recordCount;
  uint32_t totalRecords;
  uint32_t lastSyncTime;
  bool bufferFull;
  uint8_t reserved[16];
};

// Buzzer Ayarları
struct AlarmSoundSettings {
  bool enabled;
  uint32_t buzPeriodMs;
  uint32_t buzPulseMs;
};

// Beacon Ayarları (gelecekte kullanım için)
struct BeaconSettings {
  bool enabled;
  uint32_t scanInterval;
  int rssiThreshold;
  char targetPrefix[16];
  uint8_t maxBeacons;
  uint32_t timeoutMs;
};

// Eddystone Sensör Konfigürasyonu (BLE sensörler için - şu an kullanılmıyor)
struct EddystoneSensorConfig {
  char macAddress[18];
  char mahalId[25];
  char sensorName[32];
  float tempHigh;
  float tempLow;
  bool buzzerEnabled;
  bool enabled;
};

// Ana Konfigürasyon Yapısı
struct Cfg {
  // WiFi
  char wifiSsid[32];
  char wifiPass[64];

  // MQTT Broker
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[32];
  char mqttPass[64];

  // MQTT & Zaman
  uint32_t dataPeriod;   // 2 dk
  uint32_t infoPeriod;   // 10 saat

  // OTA
  char otaUrl[128];
  bool otaInsecureTLS;
  char otaUser[32];
  char otaPass[32];
  char otaBasicB64[96];

  // Dahili sensör alarm eşikleri
  float tempHigh;
  float tempLow;

  // Buzzer
  bool buzzerEnabled;
  uint32_t buzPeriodMs;
  uint32_t buzPulseMs;

  // Dahili sensör bilgileri
  char internalSensorName[32];
  char internalMahalId[25];

  // Eddystone sensör konfigürasyonları (şu an kullanılmıyor)
  EddystoneSensorConfig eddystoneSensors[32];
  uint8_t activeSensorCount;

  // Yeni ayarlar
  AlarmSoundSettings alarmSound;
  BeaconSettings beacon;
  
  // Bağlantı modu (0: WiFi, 1: 4G fallback)
  uint8_t connectionMode;
};

// Güç Durumu
struct PowerStatus {
  bool mainsPresent;   // cihaz elektriğe takılı mı (AC/USB)
  bool charging;       // BQ24072 CHG üzerinden
  float vbat;          // batarya voltajı
  uint8_t battPct;     // yüzde
  uint16_t rawPwrAdc;  // AC sense için ham ADC (debug için)
};

class ConfigManager {
public:
    ConfigManager();
    bool begin();
    void loadConfiguration(Cfg &config);
    void saveConfiguration(const Cfg &config);
    void resetToFactory(Cfg &config);
    
    // FIFO İşlemleri
    bool pushRecord(OfflineDataRecord record);
    bool popRecord(OfflineDataRecord &record);
    uint16_t getPendingCount() { return _buffer.recordCount; }
    void clearAllRecords();
    
    // CRC32 hesaplama
    static uint32_t calculateCRC32(const OfflineDataRecord &record);

private:
    Preferences _prefs;
    OfflineBuffer _buffer;
    void _saveBufferState();
    void _loadBufferState();
};

#endif
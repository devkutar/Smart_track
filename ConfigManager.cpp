#include "ConfigManager.h"
#include <cstddef>

ConfigManager::ConfigManager() {}

bool ConfigManager::begin() {
    _loadBufferState();
    return true;
}

void ConfigManager::loadConfiguration(Cfg &config) {
    _prefs.begin("cfg", true);
    if (_prefs.getBytesLength("config") == sizeof(Cfg)) {
        _prefs.getBytes("config", &config, sizeof(Cfg));
    } else {
        resetToFactory(config);
    }
    _prefs.end();
}

void ConfigManager::saveConfiguration(const Cfg &config) {
    _prefs.begin("cfg", false);
    _prefs.putBytes("config", &config, sizeof(Cfg));
    _prefs.end();
}

void ConfigManager::resetToFactory(Cfg &config) {
    // WiFi
    strlcpy(config.wifiSsid, "WINDESK", sizeof(config.wifiSsid));
    strlcpy(config.wifiPass, "", sizeof(config.wifiPass));
    
    // MQTT Broker
    strlcpy(config.mqttHost, "broker.smarttech.tr", sizeof(config.mqttHost));
    config.mqttPort = 1883;
    strlcpy(config.mqttUser, "anymqtt", sizeof(config.mqttUser));
    strlcpy(config.mqttPass, "Anv2023.!", sizeof(config.mqttPass));
    
    // MQTT & Zaman
    config.dataPeriod = 120000;   // 2 dk
    config.infoPeriod = 960000;   // 16 dk (960 saniye)
    
    // OTA
    strlcpy(config.otaUrl, "http://update.kutar.com.tr:8780/stcFix.bin", sizeof(config.otaUrl));
    config.otaInsecureTLS = true;
    strlcpy(config.otaUser, "kadmin", sizeof(config.otaUser));
    strlcpy(config.otaPass, "b0%41A4p", sizeof(config.otaPass));
    strlcpy(config.otaBasicB64, "", sizeof(config.otaBasicB64));
    
    // Dahili sensör alarm eşikleri
    config.tempHigh = 40.0f;
    config.tempLow = 5.0f;
    
    // Buzzer
    config.buzzerEnabled = false;
    config.buzPeriodMs = 60000;
    config.buzPulseMs = 3000;
    
    // Dahili sensör bilgileri
    strlcpy(config.internalSensorName, "Dahili Sensor", sizeof(config.internalSensorName));
    strlcpy(config.internalMahalId, "KTR-K1-RD-2300010", sizeof(config.internalMahalId));
    
    // Eddystone sensörler (şu an kullanılmıyor)
    config.activeSensorCount = 0;
    for (int i = 0; i < 32; i++) {
        config.eddystoneSensors[i].enabled = false;
        config.eddystoneSensors[i].macAddress[0] = '\0';
    }
    
    // Alarm ve Beacon ayarları
    config.alarmSound.enabled = false;
    config.alarmSound.buzPeriodMs = 60000;
    config.alarmSound.buzPulseMs = 3000;
    
    config.beacon.enabled = true;
    config.beacon.scanInterval = 30000;
    config.beacon.rssiThreshold = -120;
    strlcpy(config.beacon.targetPrefix, "8C696B", sizeof(config.beacon.targetPrefix));
    config.beacon.maxBeacons = 30;
    config.beacon.timeoutMs = 120000;
    
    // Bağlantı modu
    config.connectionMode = 1; // 0: WiFi only, 1: WiFi+4G (fallback), 2: 4G only
    
    saveConfiguration(config);
}

void ConfigManager::_loadBufferState() {
    _prefs.begin("buf", true);
    if (_prefs.getBytesLength("state") == sizeof(OfflineBuffer)) {
        _prefs.getBytes("state", &_buffer, sizeof(OfflineBuffer));
    } else {
        memset(&_buffer, 0, sizeof(OfflineBuffer));
    }
    _prefs.end();
}

void ConfigManager::_saveBufferState() {
    _prefs.begin("buf", false);
    _prefs.putBytes("state", &_buffer, sizeof(OfflineBuffer));
    _prefs.end();
}

uint32_t ConfigManager::calculateCRC32(const OfflineDataRecord &record) {
    // Basit CRC32 hesaplama (crc32 field hariç)
    const uint8_t *data = (const uint8_t *)&record;
    size_t len = offsetof(OfflineDataRecord, crc32);
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

bool ConfigManager::pushRecord(OfflineDataRecord record) {
    // CRC32 hesapla ve kaydet
    record.crc32 = calculateCRC32(record);
    
    char key[10];
    sprintf(key, "r%d", _buffer.writeIndex);
    
    _prefs.begin("records", false);
    _prefs.putBytes(key, &record, sizeof(OfflineDataRecord));
    _prefs.end();

    _buffer.totalRecords++;
    _buffer.writeIndex = (_buffer.writeIndex + 1) % 100; // 100 kayıtlık limit
    if (_buffer.recordCount < 100) {
        _buffer.recordCount++;
        _buffer.bufferFull = false;
    } else {
        _buffer.readIndex = (_buffer.readIndex + 1) % 100; // Üzerine yaz
        _buffer.bufferFull = true;
    }

    _saveBufferState();
    return true;
}

bool ConfigManager::popRecord(OfflineDataRecord &record) {
    if (_buffer.recordCount == 0) return false;

    char key[10];
    sprintf(key, "r%d", _buffer.readIndex);

    _prefs.begin("records", true);
    _prefs.getBytes(key, &record, sizeof(OfflineDataRecord));
    _prefs.end();

    // CRC32 kontrolü
    uint32_t calculatedCRC = calculateCRC32(record);
    if (record.crc32 != calculatedCRC) {
        // CRC hatası - kayıt bozulmuş olabilir, devam et ama logla
        Serial.printf("[STORAGE] CRC mismatch for record %d\n", _buffer.readIndex);
    }

    _buffer.readIndex = (_buffer.readIndex + 1) % 100;
    _buffer.recordCount--;
    _buffer.bufferFull = false;

    _saveBufferState();
    return true;
}

void ConfigManager::clearAllRecords() {
    _prefs.begin("records", false);
    for (int i = 0; i < 100; i++) {
        char key[10];
        sprintf(key, "r%d", i);
        _prefs.remove(key);
    }
    _prefs.end();
    
    memset(&_buffer, 0, sizeof(OfflineBuffer));
    _saveBufferState();
}
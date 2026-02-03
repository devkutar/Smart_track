#include "ExternalSensor.h"

ExternalSensor::ExternalSensor() 
    : _sensorType(SENSOR_NONE), _lastTemp(-99.0f), _lastHumidity(-1.0f) {
}

bool ExternalSensor::probeI2C(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

uint8_t ExternalSensor::crc8_t117(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x01) crc = (crc >> 1) ^ 0x8C;
            else crc >>= 1;
        }
    }
    return crc;
}

uint8_t ExternalSensor::crc8_aht20(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return crc;
}

bool ExternalSensor::begin() {
    // Sensör gücünü aç
    HW_sensorPower(true);
    delay(50);  // Sensörlerin başlaması için bekle
    
    _sensorType = SENSOR_NONE;
    
    // Önce T117'yi dene (0x41)
    Serial.println("[SENSOR] Probing T117 (0x41)...");
    if (probeI2C(T117_ADDR)) {
        _sensorType = SENSOR_T117;
        Serial.println("[SENSOR] T117 detected!");
        return true;
    }
    
    // T117 yoksa AHT20'yi dene (0x38)
    Serial.println("[SENSOR] Probing AHT20 (0x38)...");
    if (probeI2C(AHT20_ADDR)) {
        if (aht20_init()) {
            _sensorType = SENSOR_AHT20;
            Serial.println("[SENSOR] AHT20 detected and initialized!");
            return true;
        }
    }
    
    Serial.println("[SENSOR] No external sensor found!");
    return false;
}

void ExternalSensor::end() {
    HW_sensorPower(false);
    _sensorType = SENSOR_NONE;
}

const char* ExternalSensor::getTypeName() {
    switch (_sensorType) {
        case SENSOR_T117: return "T117";
        case SENSOR_AHT20: return "AHT20";
        default: return "None";
    }
}

bool ExternalSensor::readTemperature(float &tempC) {
    switch (_sensorType) {
        case SENSOR_T117:
            t117_startConvert();
            delay(15);
            return t117_readTemp(tempC);
            
        case SENSOR_AHT20: {
            float humidity;
            bool ok = aht20_readTempHumidity(tempC, humidity);
            if (ok) _lastHumidity = humidity;
            return ok;
        }
            
        default:
            return false;
    }
}

bool ExternalSensor::readHumidity(float &humidity) {
    if (_sensorType == SENSOR_AHT20) {
        // Son okunan nem değerini döndür
        if (_lastHumidity >= 0) {
            humidity = _lastHumidity;
            return true;
        }
        // Yoksa yeni okuma yap
        float tempC;
        if (aht20_readTempHumidity(tempC, humidity)) {
            _lastHumidity = humidity;
            return true;
        }
    }
    return false;
}

// ===== T117 Functions =====

void ExternalSensor::t117_startConvert() {
    Wire.beginTransmission(T117_ADDR);
    Wire.write(T117_REG_TEMP_CMD);
    Wire.write(T117_SINGLE_CONVERT);
    Wire.endTransmission();
}

bool ExternalSensor::t117_readTemp(float &tempC) {
    uint8_t data[3];
    
    Wire.beginTransmission(T117_ADDR);
    Wire.write(T117_REG_TEMP_LSB);
    if (Wire.endTransmission(false) != 0) return false;
    
    if (Wire.requestFrom(T117_ADDR, (uint8_t)3) != 3) return false;
    
    data[0] = Wire.read();
    data[1] = Wire.read();
    data[2] = Wire.read();
    
    uint8_t calc = crc8_t117(data, 2);
    if (data[2] != calc) return false;
    
    int16_t raw = (int16_t)((data[1] << 8) | data[0]);
    tempC = (float)raw / 256.0f + 25.0f;
    return true;
}

// ===== AHT20 Functions =====

bool ExternalSensor::aht20_init() {
    // Soft reset
    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(AHT20_CMD_SOFTRESET);
    Wire.endTransmission();
    delay(20);
    
    // Calibration check
    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(AHT20_CMD_INIT);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(10);
    
    // Status check
    Wire.requestFrom(AHT20_ADDR, (uint8_t)1);
    if (Wire.available()) {
        uint8_t status = Wire.read();
        // Bit 3 should be 1 (calibrated)
        if (status & 0x08) {
            return true;
        }
    }
    
    // Calibration komutu tekrar gönder
    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(AHT20_CMD_INIT);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(10);
    
    return true;  // Devam et, belki çalışır
}

bool ExternalSensor::aht20_readTempHumidity(float &tempC, float &humidity) {
    // Trigger measurement
    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(AHT20_CMD_TRIGGER);
    Wire.write(0x33);
    Wire.write(0x00);
    Wire.endTransmission();
    
    delay(80);  // Ölçüm süresi
    
    // Read data (6 bytes: status + 5 data bytes)
    uint8_t data[7];
    if (Wire.requestFrom(AHT20_ADDR, (uint8_t)7) != 7) {
        return false;
    }
    
    for (int i = 0; i < 7; i++) {
        data[i] = Wire.read();
    }
    
    // Status check (bit 7 should be 0 = not busy)
    if (data[0] & 0x80) {
        return false;  // Still busy
    }
    
    // CRC check (optional - data[6])
    uint8_t crc = crc8_aht20(data, 6);
    if (crc != data[6]) {
        Serial.println("[AHT20] CRC mismatch, continuing anyway");
        // CRC hatası olsa bile devam et
    }
    
    // Parse humidity (20-bit)
    uint32_t rawHumidity = ((uint32_t)data[1] << 12) | 
                          ((uint32_t)data[2] << 4) | 
                          ((uint32_t)data[3] >> 4);
    humidity = (float)rawHumidity * 100.0f / 1048576.0f;
    
    // Parse temperature (20-bit)
    uint32_t rawTemp = (((uint32_t)data[3] & 0x0F) << 16) | 
                       ((uint32_t)data[4] << 8) | 
                       (uint32_t)data[5];
    tempC = (float)rawTemp * 200.0f / 1048576.0f - 50.0f;
    
    _lastTemp = tempC;
    _lastHumidity = humidity;
    
    return true;
}
